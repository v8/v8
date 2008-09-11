// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "dateparser.h"

namespace v8 { namespace internal {


bool DateParser::Parse(String* str, FixedArray* out) {
  ASSERT(out->length() == OUTPUT_SIZE);

  InputReader in(str);
  TimeZoneComposer tz;
  TimeComposer time;
  DayComposer day;

  while (!in.IsEnd()) {
    if (in.IsAsciiDigit()) {
      // Parse a number (possibly with 1 or 2 trailing colons).
      int n = in.ReadUnsignedNumber();
      if (in.Skip(':')) {
        if (in.Skip(':')) {
          // n + "::"
          if (!time.IsEmpty()) return false;
          time.Add(n);
          time.Add(0);
        } else {
          // n + ":"
          if (!time.Add(n)) return false;
        }
      } else if (tz.IsExpecting(n)) {
        tz.SetAbsoluteMinute(n);
      } else if (time.IsExpecting(n)) {
        time.AddFinal(n);
        // Require end or white space immediately after finalizing time.
        if (!in.IsEnd() && !in.SkipWhiteSpace()) return false;
      } else {
        if (!day.Add(n)) return false;
        in.Skip('-');  // Ignore suffix '-' for year, month, or day.
      }
    } else if (in.IsAsciiAlphaOrAbove()) {
      // Parse a "word" (sequence of chars. >= 'A').
      uint32_t pre[KeywordTable::kPrefixLength];
      int len = in.ReadWord(pre, KeywordTable::kPrefixLength);
      int index = KeywordTable::Lookup(pre, len);
      KeywordType type = KeywordTable::GetType(index);

      if (type == AM_PM && !time.IsEmpty()) {
        time.SetHourOffset(KeywordTable::GetValue(index));
      } else if (type == MONTH_NAME) {
        day.SetNamedMonth(KeywordTable::GetValue(index));
        in.Skip('-');  // Ignore suffix '-' for month names
      } else if (type == TIME_ZONE_NAME && in.HasReadNumber()) {
        tz.Set(KeywordTable::GetValue(index));
      } else {
        // Garbage words are illegal if no number read yet.
        if (in.HasReadNumber()) return false;
      }
    } else if (in.IsAsciiSign() && (tz.IsUTC() || !time.IsEmpty())) {
      // Parse UTC offset (only after UTC or time).
      tz.SetSign(in.GetAsciiSignValue());
      in.Next();
      int n = in.ReadUnsignedNumber();
      if (in.Skip(':')) {
        tz.SetAbsoluteHour(n);
        tz.SetAbsoluteMinute(kNone);
      } else {
        tz.SetAbsoluteHour(n / 100);
        tz.SetAbsoluteMinute(n % 100);
      }
    } else if (in.Is('(')) {
      // Ignore anything from '(' to a matching ')' or end of string.
      in.SkipParentheses();
    } else if ((in.IsAsciiSign() || in.Is(')')) && in.HasReadNumber()) {
      // Extra sign or ')' is illegal if no number read yet.
      return false;
    } else {
      // Ignore other characters.
      in.Next();
    }
  }
  return day.Write(out) && time.Write(out) && tz.Write(out);
}


bool DateParser::DayComposer::Write(FixedArray* output) {
  int year = 0;  // Default year is 0 (=> 2000) for KJS compatibility.
  int month = kNone;
  int day = kNone;

  if (named_month_ == kNone) {
    if (index_ < 2) return false;
    if (index_ == 3 && !IsDay(comp_[0])) {
      // YMD
      year = comp_[0];
      month = comp_[1];
      day = comp_[2];
    } else {
      // MD(Y)
      month = comp_[0];
      day = comp_[1];
      if (index_ == 3) year = comp_[2];
    }
  } else {
    month = named_month_;
    if (index_ < 1) return false;
    if (index_ == 1) {
      // MD or DM
      day = comp_[0];
    } else if (!IsDay(comp_[0])) {
      // YMD, MYD, or YDM
      year = comp_[0];
      day = comp_[1];
    } else {
      // DMY, MDY, or DYM
      day = comp_[0];
      year = comp_[1];
    }
  }

  if (Between(year, 0, 49)) year += 2000;
  else if (Between(year, 50, 99)) year += 1900;

  if (!Smi::IsValid(year) || !IsMonth(month) || !IsDay(day)) return false;

  output->set(YEAR, Smi::FromInt(year));
  output->set(MONTH, Smi::FromInt(month - 1));  // 0-based
  output->set(DAY, Smi::FromInt(day));
  return true;
}


bool DateParser::TimeComposer::Write(FixedArray* output) {
  // All time slots default to 0
  while (index_ < kSize) {
    comp_[index_++] = 0;
  }

  int& hour = comp_[0];
  int& minute = comp_[1];
  int& second = comp_[2];

  if (hour_offset_ != kNone) {
    if (!IsHour12(hour)) return false;
    hour %= 12;
    hour += hour_offset_;
  }

  if (!IsHour(hour) || !IsMinute(minute) || !IsSecond(second)) return false;

  output->set(HOUR, Smi::FromInt(hour));
  output->set(MINUTE, Smi::FromInt(minute));
  output->set(SECOND, Smi::FromInt(second));
  return true;
}


bool DateParser::TimeZoneComposer::Write(FixedArray* output) {
  if (sign_ != kNone) {
    if (hour_ == kNone) hour_ = 0;
    if (minute_ == kNone) minute_ = 0;
    int total_seconds = sign_ * (hour_ * 3600 + minute_ * 60);
    if (!Smi::IsValid(total_seconds)) return false;
    output->set(UTC_OFFSET, Smi::FromInt(total_seconds));
  } else {
    output->set(UTC_OFFSET, Heap::null_value());
  }
  return true;
}


const int8_t
DateParser::KeywordTable::array[][DateParser::KeywordTable::kEntrySize] = {
  {'j', 'a', 'n', DateParser::MONTH_NAME, 1},
  {'f', 'e', 'b', DateParser::MONTH_NAME, 2},
  {'m', 'a', 'r', DateParser::MONTH_NAME, 3},
  {'a', 'p', 'r', DateParser::MONTH_NAME, 4},
  {'m', 'a', 'y', DateParser::MONTH_NAME, 5},
  {'j', 'u', 'n', DateParser::MONTH_NAME, 6},
  {'j', 'u', 'l', DateParser::MONTH_NAME, 7},
  {'a', 'u', 'g', DateParser::MONTH_NAME, 8},
  {'s', 'e', 'p', DateParser::MONTH_NAME, 9},
  {'o', 'c', 't', DateParser::MONTH_NAME, 10},
  {'n', 'o', 'v', DateParser::MONTH_NAME, 11},
  {'d', 'e', 'c', DateParser::MONTH_NAME, 12},
  {'a', 'm', '\0', DateParser::AM_PM, 0},
  {'p', 'm', '\0', DateParser::AM_PM, 12},
  {'u', 't', '\0', DateParser::TIME_ZONE_NAME, 0},
  {'u', 't', 'c', DateParser::TIME_ZONE_NAME, 0},
  {'g', 'm', 't', DateParser::TIME_ZONE_NAME, 0},
  {'c', 'd', 't', DateParser::TIME_ZONE_NAME, -5},
  {'c', 's', 't', DateParser::TIME_ZONE_NAME, -6},
  {'e', 'd', 't', DateParser::TIME_ZONE_NAME, -4},
  {'e', 's', 't', DateParser::TIME_ZONE_NAME, -5},
  {'m', 'd', 't', DateParser::TIME_ZONE_NAME, -6},
  {'m', 's', 't', DateParser::TIME_ZONE_NAME, -7},
  {'p', 'd', 't', DateParser::TIME_ZONE_NAME, -7},
  {'p', 's', 't', DateParser::TIME_ZONE_NAME, -8},
  {'\0', '\0', '\0', DateParser::INVALID, 0},
};


// We could use perfect hashing here, but this is not a bottleneck.
int DateParser::KeywordTable::Lookup(const uint32_t* pre, int len) {
  int i;
  for (i = 0; array[i][kTypeOffset] != INVALID; i++) {
    int j = 0;
    while (j < kPrefixLength &&
           pre[j] == static_cast<uint32_t>(array[i][j])) {
      j++;
    }
    // Check if we have a match and the length is legal.
    // Word longer than keyword is only allowed for month names.
    if (j == kPrefixLength &&
        (len <= kPrefixLength || array[i][kTypeOffset] == MONTH_NAME)) {
      return i;
    }
  }
  return i;
}


} }  // namespace v8::internal
