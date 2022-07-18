// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/temporal/temporal-parser.h"

#include "src/base/bounds.h"
#include "src/base/optional.h"
#include "src/objects/string-inl.h"
#include "src/strings/char-predicates-inl.h"

namespace v8 {
namespace internal {

namespace {

// Temporal #prod-TZLeadingChar
inline constexpr bool IsTZLeadingChar(base::uc32 c) {
  return base::IsInRange(AsciiAlphaToLower(c), 'a', 'z') || c == '.' ||
         c == '_';
}

// Temporal #prod-TZChar
inline constexpr bool IsTZChar(base::uc32 c) {
  return IsTZLeadingChar(c) || c == '-';
}

// Temporal #prod-DecimalSeparator
inline constexpr bool IsDecimalSeparator(base::uc32 c) {
  return c == '.' || c == ',';
}

// Temporal #prod-DateTimeSeparator
inline constexpr bool IsDateTimeSeparator(base::uc32 c) {
  return c == ' ' || AsciiAlphaToLower(c) == 't';
}

// Temporal #prod-ASCIISign
inline constexpr bool IsAsciiSign(base::uc32 c) { return c == '-' || c == '+'; }

// Temporal #prod-Sign
inline constexpr bool IsSign(base::uc32 c) {
  return c == 0x2212 || IsAsciiSign(c);
}

// Temporal #prod-TimeZoneUTCOffsetSign
inline constexpr bool IsTimeZoneUTCOffsetSign(base::uc32 c) {
  return IsSign(c);
}

inline constexpr base::uc32 CanonicalSign(base::uc32 c) {
  return c == 0x2212 ? '-' : c;
}

inline constexpr int32_t ToInt(base::uc32 c) { return c - '0'; }

// A helper template to make the scanning of production w/ two digits simpler.
template <typename Char>
bool HasTwoDigits(base::Vector<Char> str, int32_t s, int32_t* out) {
  if (str.length() >= (s + 2) && IsDecimalDigit(str[s]) &&
      IsDecimalDigit(str[s + 1])) {
    *out = ToInt(str[s]) * 10 + ToInt(str[s + 1]);
    return true;
  }
  return false;
}

// A helper template to make the scanning of production w/ a single two digits
// value simpler.
template <typename Char>
int32_t ScanTwoDigitsExpectValue(base::Vector<Char> str, int32_t s,
                                 int32_t expected, int32_t* out) {
  return HasTwoDigits<Char>(str, s, out) && (*out == expected) ? 2 : 0;
}

// A helper template to make the scanning of production w/ two digits value in a
// range simpler.
template <typename Char>
int32_t ScanTwoDigitsExpectRange(base::Vector<Char> str, int32_t s, int32_t min,
                                 int32_t max, int32_t* out) {
  return HasTwoDigits<Char>(str, s, out) && base::IsInRange(*out, min, max) ? 2
                                                                            : 0;
}

// A helper template to make the scanning of production w/ two digits value as 0
// or in a range simpler.
template <typename Char>
int32_t ScanTwoDigitsExpectZeroOrRange(base::Vector<Char> str, int32_t s,
                                       int32_t min, int32_t max, int32_t* out) {
  return HasTwoDigits<Char>(str, s, out) &&
                 (*out == 0 || base::IsInRange(*out, min, max))
             ? 2
             : 0;
}

/**
 * The TemporalParser use two types of internal routine:
 * - Scan routines: Follow the function signature below:
 *   template <typename Char> int32_t Scan$ProductionName(
 *   base::Vector<Char> str, int32_t s, R* out)
 *
 *   These routine scan the next item from position s in str and store the
 *   parsed result into out if the expected string is successfully scanned.
 *   It return the length of matched text from s or 0 to indicate no
 *   expected item matched.
 *
 * - Satisfy routines: Follow the function sigature below:
 *   template <typename Char>
 *   bool Satisfy$ProductionName(base::Vector<Char> str, R* r);
 *   It scan from the beginning of the str by calling Scan routines to put
 *   parsed result into r and return true if the entire str satisfy the
 *   production. It internally use Scan routines.
 *
 * TODO(ftang) investigate refactoring to class before shipping
 * Reference to RegExpParserImpl by encapsulating the cursor position and
 * only manipulating the current character and position with Next(),
 * Advance(), current(), etc
 */

// For Hour Production
// Hour:
//   [0 1] Digit
//   2 [0 1 2 3]
template <typename Char>
int32_t ScanHour(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 0, 23, out);
}

// MinuteSecond:
//   [0 1 2 3 4 5] Digit
template <typename Char>
int32_t ScanMinuteSecond(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 0, 59, out);
}

// For the forward production in the grammar such as
// ProductionB:
//   ProductionT
#define SCAN_FORWARD(B, T, R)                                \
  template <typename Char>                                   \
  int32_t Scan##B(base::Vector<Char> str, int32_t s, R* r) { \
    return Scan##T(str, s, r);                               \
  }

// Same as above but store the result into a particular field in R

// For the forward production in the grammar such as
// ProductionB:
//   ProductionT1
//   ProductionT2
#define SCAN_EITHER_FORWARD(B, T1, T2, R)                    \
  template <typename Char>                                   \
  int32_t Scan##B(base::Vector<Char> str, int32_t s, R* r) { \
    int32_t len;                                             \
    if ((len = Scan##T1(str, s, r)) > 0) return len;         \
    return Scan##T2(str, s, r);                              \
  }

// TimeHour: Hour
SCAN_FORWARD(TimeHour, Hour, int32_t)

// TimeMinute: MinuteSecond
SCAN_FORWARD(TimeMinute, MinuteSecond, int32_t)

// TimeSecond:
//   MinuteSecond
//   60
template <typename Char>
int32_t ScanTimeSecond(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 0, 60, out);
}

constexpr int kPowerOfTen[] = {1,      10,      100,      1000,     10000,
                               100000, 1000000, 10000000, 100000000};

// FractionalPart : Digit{1,9}
template <typename Char>
int32_t ScanFractionalPart(base::Vector<Char> str, int32_t s, int32_t* out) {
  int32_t cur = s;
  if ((str.length() < (cur + 1)) || !IsDecimalDigit(str[cur])) return 0;
  *out = ToInt(str[cur++]);
  while ((cur < str.length()) && ((cur - s) < 9) && IsDecimalDigit(str[cur])) {
    *out = 10 * (*out) + ToInt(str[cur++]);
  }
  *out *= kPowerOfTen[9 - (cur - s)];
  return cur - s;
}

template <typename Char>
int32_t ScanFractionalPart(base::Vector<Char> str, int32_t s, int64_t* out) {
  int32_t out32;
  int32_t len = ScanFractionalPart(str, s, &out32);
  *out = out32;
  return len;
}

// TimeFraction: FractionalPart
SCAN_FORWARD(TimeFractionalPart, FractionalPart, int32_t)

// Fraction: DecimalSeparator FractionalPart
// DecimalSeparator: one of , .
template <typename Char>
int32_t ScanFraction(base::Vector<Char> str, int32_t s, int32_t* out) {
  if ((str.length() < (s + 2)) || (!IsDecimalSeparator(str[s]))) return 0;
  int32_t len;
  if ((len = ScanFractionalPart(str, s + 1, out)) == 0) return 0;
  return len + 1;
}

// TimeFraction: DecimalSeparator TimeFractionalPart
// DecimalSeparator: one of , .
template <typename Char>
int32_t ScanTimeFraction(base::Vector<Char> str, int32_t s, int32_t* out) {
  if ((str.length() < (s + 2)) || (!IsDecimalSeparator(str[s]))) return 0;
  int32_t len;
  if ((len = ScanTimeFractionalPart(str, s + 1, out)) == 0) return 0;
  return len + 1;
}

template <typename Char>
int32_t ScanTimeFraction(base::Vector<Char> str, int32_t s,
                         ParsedISO8601Result* r) {
  return ScanTimeFraction(str, s, &(r->time_nanosecond));
}

// TimeSpec:
//  TimeHour
//  TimeHour : TimeMinute
//  TimeHour : TimeMinute : TimeSecond [TimeFraction]
//  TimeHour TimeMinute
//  TimeHour TimeMinute TimeSecond [TimeFraction]
template <typename Char>
int32_t ScanTimeSpec(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Result* r) {
  int32_t time_hour, time_minute, time_second;
  int32_t len;
  int32_t cur = s;
  if ((len = ScanTimeHour(str, cur, &time_hour)) == 0) return 0;
  cur += len;
  if ((cur + 1) > str.length()) {
    // TimeHour
    r->time_hour = time_hour;
    return cur - s;
  }
  if (str[cur] == ':') {
    cur++;
    if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) return 0;
    cur += len;
    if ((cur + 1) > str.length() || (str[cur] != ':')) {
      // TimeHour : TimeMinute
      r->time_hour = time_hour;
      r->time_minute = time_minute;
      return cur - s;
    }
    cur++;
    if ((len = ScanTimeSecond(str, cur, &time_second)) == 0) return 0;
  } else {
    if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) {
      // TimeHour
      r->time_hour = time_hour;
      return cur - s;
    }
    cur += len;
    if ((len = ScanTimeSecond(str, cur, &time_second)) == 0) {
      // TimeHour TimeMinute
      r->time_hour = time_hour;
      r->time_minute = time_minute;
      return cur - s;
    }
  }
  cur += len;
  len = ScanTimeFraction(str, cur, r);
  r->time_hour = time_hour;
  r->time_minute = time_minute;
  r->time_second = time_second;
  cur += len;
  return cur - s;
}

// TimeSpecSeparator: DateTimeSeparator TimeSpec
// DateTimeSeparator: SPACE, 't', or 'T'
template <typename Char>
int32_t ScanTimeSpecSeparator(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Result* r) {
  if (!(((s + 1) < str.length()) && IsDateTimeSeparator(str[s]))) return 0;
  int32_t len = ScanTimeSpec(str, s + 1, r);
  return (len == 0) ? 0 : len + 1;
}

// DateExtendedYear: Sign Digit Digit Digit Digit Digit Digit
template <typename Char>
int32_t ScanDateExtendedYear(base::Vector<Char> str, int32_t s, int32_t* out) {
  if (str.length() < (s + 7)) return 0;
  if (IsSign(str[s]) && IsDecimalDigit(str[s + 1]) &&
      IsDecimalDigit(str[s + 2]) && IsDecimalDigit(str[s + 3]) &&
      IsDecimalDigit(str[s + 4]) && IsDecimalDigit(str[s + 5]) &&
      IsDecimalDigit(str[s + 6])) {
    int32_t sign = (CanonicalSign(str[s]) == '-') ? -1 : 1;
    *out = sign * (ToInt(str[s + 1]) * 100000 + ToInt(str[s + 2]) * 10000 +
                   ToInt(str[s + 3]) * 1000 + ToInt(str[s + 4]) * 100 +
                   ToInt(str[s + 5]) * 10 + ToInt(str[s + 6]));
    // In the end of #sec-temporal-iso8601grammar
    // It is a Syntax Error if DateExtendedYear is "-000000" or "−000000"
    // (U+2212 MINUS SIGN followed by 000000).
    if (sign == -1 && *out == 0) return 0;
    return 7;
  }
  return 0;
}

// DateFourDigitYear: Digit Digit Digit Digit
template <typename Char>
int32_t ScanDateFourDigitYear(base::Vector<Char> str, int32_t s, int32_t* out) {
  if (str.length() < (s + 4)) return 0;
  if (IsDecimalDigit(str[s]) && IsDecimalDigit(str[s + 1]) &&
      IsDecimalDigit(str[s + 2]) && IsDecimalDigit(str[s + 3])) {
    *out = ToInt(str[s]) * 1000 + ToInt(str[s + 1]) * 100 +
           ToInt(str[s + 2]) * 10 + ToInt(str[s + 3]);
    return 4;
  }
  return 0;
}

// DateYear:
//   DateFourDigitYear
//   DateExtendedYear
// The lookahead is at most 1 char.
SCAN_EITHER_FORWARD(DateYear, DateFourDigitYear, DateExtendedYear, int32_t)

// DateMonth:
//   0 NonzeroDigit
//   10
//   11
//   12
template <typename Char>
int32_t ScanDateMonth(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 1, 12, out);
}

// DateDay:
//   0 NonzeroDigit
//   1 Digit
//   2 Digit
//   30
//   31
template <typename Char>
int32_t ScanDateDay(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 1, 31, out);
}

// Date:
//   DateYear - DateMonth - DateDay
//   DateYear DateMonth DateDay
template <typename Char>
int32_t ScanDate(base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t date_year, date_month, date_day;
  int32_t cur = s;
  int32_t len;
  if ((len = ScanDateYear(str, cur, &date_year)) == 0) return 0;
  if (((cur += len) + 1) > str.length()) return 0;
  if (str[cur] == '-') {
    cur++;
    if ((len = ScanDateMonth(str, cur, &date_month)) == 0) return 0;
    cur += len;
    if (((cur + 1) > str.length()) || (str[cur++] != '-')) return 0;
  } else {
    if ((len = ScanDateMonth(str, cur, &date_month)) == 0) return 0;
    cur += len;
  }
  if ((len = ScanDateDay(str, cur, &date_day)) == 0) return 0;
  r->date_year = date_year;
  r->date_month = date_month;
  r->date_day = date_day;
  return cur + len - s;
}

// TimeHourNotValidMonth : one of
//   `00` `13` `14` `15` `16` `17` `18` `19` `20` `21` `23`
template <typename Char>
int32_t ScanTimeHourNotValidMonth(base::Vector<Char> str, int32_t s,
                                  int32_t* out) {
  return ScanTwoDigitsExpectZeroOrRange<Char>(str, s, 13, 23, out);
}

// TimeHourNotThirtyOneDayMonth : one of
//    `02` `04` `06` `09` `11`
template <typename Char>
int32_t ScanTimeHourNotThirtyOneDayMonth(base::Vector<Char> str, int32_t s,
                                         int32_t* out) {
  return HasTwoDigits<Char>(str, s, out) &&
                 (*out == 2 || *out == 4 || *out == 6 || *out == 9 ||
                  *out == 11)
             ? 2
             : 0;
}

// TimeHourTwoOnly : `02`
template <typename Char>
int32_t ScanTimeHourTwoOnly(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectValue<Char>(str, s, 2, out);
}

// TimeMinuteNotValidDay :
//        `00`
//        `32`
//        `33`
//        `34`
//        `35`
//        `36`
//        `37`
//        `38`
//        `39`
//        `4` DecimalDigit
//        `5` DecimalDigit
//        `60`
template <typename Char>
int32_t ScanTimeMinuteNotValidDay(base::Vector<Char> str, int32_t s,
                                  int32_t* out) {
  return ScanTwoDigitsExpectZeroOrRange<Char>(str, s, 32, 60, out);
}

// TimeMinuteThirtyOnly : `30`
template <typename Char>
int32_t ScanTimeMinuteThirtyOnly(base::Vector<Char> str, int32_t s,
                                 int32_t* out) {
  return ScanTwoDigitsExpectValue<Char>(str, s, 30, out);
}

//    TimeMinuteThirtyOneOnly : `31`
template <typename Char>
int32_t ScanTimeMinuteThirtyOneOnly(base::Vector<Char> str, int32_t s,
                                    int32_t* out) {
  return ScanTwoDigitsExpectValue<Char>(str, s, 31, out);
}

//    TimeSecondNotValidMonth :
//        `00`
//        `13`
//        `14`
//        `15`
//        `16`
//        `17`
//        `18`
//        `19`
//        `2` DecimalDigit
//        `3` DecimalDigit
//        `4` DecimalDigit
//        `5` DecimalDigit
//        `60`
template <typename Char>
int32_t ScanTimeSecondNotValidMonth(base::Vector<Char> str, int32_t s,
                                    int32_t* out) {
  return ScanTwoDigitsExpectZeroOrRange<Char>(str, s, 13, 60, out);
}

// TimeZoneUTCOffsetHour: Hour
SCAN_FORWARD(TimeZoneUTCOffsetHour, Hour, int32_t)

// TimeZoneUTCOffsetMinute
SCAN_FORWARD(TimeZoneUTCOffsetMinute, MinuteSecond, int32_t)

// TimeZoneUTCOffsetSecond
SCAN_FORWARD(TimeZoneUTCOffsetSecond, MinuteSecond, int32_t)

// TimeZoneUTCOffsetFractionalPart: FractionalPart
// See PR1796
SCAN_FORWARD(TimeZoneUTCOffsetFractionalPart, FractionalPart, int32_t)

// TimeZoneUTCOffsetFraction: DecimalSeparator TimeZoneUTCOffsetFractionalPart
// See PR1796
template <typename Char>
int32_t ScanTimeZoneUTCOffsetFraction(base::Vector<Char> str, int32_t s,
                                      int32_t* out) {
  if ((str.length() < (s + 2)) || (!IsDecimalSeparator(str[s]))) return 0;
  int32_t len;
  if ((len = ScanTimeZoneUTCOffsetFractionalPart(str, s + 1, out)) > 0) {
    return len + 1;
  }
  return 0;
}

// We found the only difference between TimeZoneNumericUTCOffset and
// TimeZoneNumericUTCOffsetNotAmbiguous is ascii minus ('-') is not allowed in
// the production with only TimeZoneUTCOffsetHour for the case of
// TimeZoneNumericUTCOffsetNotAmbiguous.
// So we use the ScanTimeZoneNumericUTCOffset_Common template with an extra enum
// to implement both.
//
// TimeZoneNumericUTCOffset:
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute :
//   TimeZoneUTCOffsetSecond [TimeZoneUTCOffsetFraction] TimeZoneUTCOffsetSign
//   TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute TimeZoneUTCOffsetSecond
//   [TimeZoneUTCOffsetFraction]
//
// TimeZoneNumericUTCOffsetNotAmbiguous :
//   + TimeZoneUTCOffsetHour
//   U+2212 TimeZoneUTCOffsetHour
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute :
//   TimeZoneUTCOffsetSecond [TimeZoneUTCOffsetFraction] TimeZoneUTCOffsetSign
//   TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute TimeZoneUTCOffsetSecond
//   [TimeZoneUTCOffsetFraction]
enum class Ambiguous { kAmbiguous, kNotAmbiguous };
template <typename Char>
int32_t ScanTimeZoneNumericUTCOffset_Common(base::Vector<Char> str, int32_t s,
                                            ParsedISO8601Result* r,
                                            Ambiguous ambiguous) {
  int32_t len, hour, minute, second, nanosecond;
  int32_t cur = s;
  if ((str.length() < (cur + 1)) || (!IsTimeZoneUTCOffsetSign(str[cur]))) {
    return 0;
  }
  bool sign_is_ascii_minus = str[s] == '-';
  int32_t sign = (CanonicalSign(str[cur++]) == '-') ? -1 : 1;
  if ((len = ScanTimeZoneUTCOffsetHour(str, cur, &hour)) == 0) return 0;
  cur += len;
  if ((cur + 1) > str.length()) {
    if (ambiguous == Ambiguous::kNotAmbiguous && sign_is_ascii_minus) return 0;
    //   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour
    r->tzuo_sign = sign;
    r->tzuo_hour = hour;
    r->offset_string_start = s;
    r->offset_string_length = cur - s;
    return cur - s;
  }
  if (str[cur] == ':') {
    cur++;
    if ((len = ScanTimeZoneUTCOffsetMinute(str, cur, &minute)) == 0) return 0;
    cur += len;
    if ((cur + 1) > str.length() || str[cur] != ':') {
      //   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute
      r->tzuo_sign = sign;
      r->tzuo_hour = hour;
      r->tzuo_minute = minute;
      r->offset_string_start = s;
      r->offset_string_length = cur - s;
      return cur - s;
    }
    cur++;
    if ((len = ScanTimeZoneUTCOffsetSecond(str, cur, &second)) == 0) return 0;
  } else {
    if ((len = ScanTimeZoneUTCOffsetMinute(str, cur, &minute)) == 0) {
      if (ambiguous == Ambiguous::kNotAmbiguous && sign_is_ascii_minus) {
        return 0;
      }
      //   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour
      r->tzuo_sign = sign;
      r->tzuo_hour = hour;
      r->offset_string_start = s;
      r->offset_string_length = cur - s;
      return cur - s;
    }
    cur += len;
    if ((len = ScanTimeZoneUTCOffsetSecond(str, cur, &second)) == 0) {
      //   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
      r->tzuo_sign = sign;
      r->tzuo_hour = hour;
      r->tzuo_minute = minute;
      r->offset_string_start = s;
      r->offset_string_length = cur - s;
      return cur - s;
    }
  }
  cur += len;
  len = ScanTimeZoneUTCOffsetFraction(str, cur, &nanosecond);
  r->tzuo_sign = sign;
  r->tzuo_hour = hour;
  r->tzuo_minute = minute;
  r->tzuo_second = second;
  if (len > 0) r->tzuo_nanosecond = nanosecond;
  r->offset_string_start = s;
  r->offset_string_length = cur + len - s;
  cur += len;
  return cur - s;
}

template <typename Char>
int32_t ScanTimeZoneNumericUTCOffset(base::Vector<Char> str, int32_t s,
                                     ParsedISO8601Result* r) {
  return ScanTimeZoneNumericUTCOffset_Common<Char>(str, s, r,
                                                   Ambiguous::kAmbiguous);
}

template <typename Char>
int32_t ScanTimeZoneNumericUTCOffsetNotAmbiguous(base::Vector<Char> str,
                                                 int32_t s,
                                                 ParsedISO8601Result* r) {
  return ScanTimeZoneNumericUTCOffset_Common<Char>(str, s, r,
                                                   Ambiguous::kNotAmbiguous);
}

// TimeZoneNumericUTCOffsetNotAmbiguousAllowedNegativeHour :
//   TimeZoneNumericUTCOffsetNotAmbiguous
//   `-` TimeHourNotValidMonth
template <typename Char>
int32_t ScanTimeZoneNumericUTCOffsetNotAmbiguousAllowedNegativeHour(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t len;
  if ((len = ScanTimeZoneNumericUTCOffsetNotAmbiguous(str, s, r)) > 0) {
    return len;
  }
  if (str.length() >= (s + 3) && str[s] == '-') {
    int32_t time_hour;
    len = ScanTimeHourNotValidMonth(str, s + 1, &time_hour);
    if (len == 0) return 0;
    r->time_hour = time_hour;
    return 1 + len;
  }
  return 0;
}

// TimeHourMinuteBasicFormatNotAmbiguous :
//   TimeHourNotValidMonth TimeMinute
//   TimeHour TimeMinuteNotValidDay
//   TimeHourNotThirtyOneDayMonth TimeMinuteThirtyOneOnly
//   TimeHourTwoOnly TimeMinuteThirtyOnly
template <typename Char>
int32_t ScanTimeHourMinuteBasicFormatNotAmbiguous(base::Vector<Char> str,
                                                  int32_t s,
                                                  ParsedISO8601Result* r) {
  int32_t time_hour, time_minute;
  int32_t len1, len2;
  if (((len1 = ScanTimeHourNotValidMonth(str, s, &time_hour)) > 0 &&
       (len2 = ScanTimeMinute(str, s + len1, &time_minute)) > 0) ||
      ((len1 = ScanTimeHour(str, s, &time_hour)) > 0 &&
       (len2 = ScanTimeMinuteNotValidDay(str, s + len1, &time_minute)) > 0) ||
      ((len1 = ScanTimeHourNotThirtyOneDayMonth(str, s, &time_hour)) > 0 &&
       (len2 = ScanTimeMinuteThirtyOneOnly(str, s + len1, &time_minute)) > 0) ||
      ((len1 = ScanTimeHourTwoOnly(str, s, &time_hour)) > 0 &&
       (len2 = ScanTimeMinuteThirtyOnly(str, s + len1, &time_minute)) > 0)) {
    // Only set both after we got both
    r->time_hour = time_hour;
    r->time_minute = time_minute;
    return len1 + len2;
  }
  return 0;
}

// TimeZoneUTCOffset:
//   TimeZoneNumericUTCOffset
//   UTCDesignator
template <typename Char>
int32_t ScanTimeZoneUTCOffset(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Result* r) {
  if (str.length() < (s + 1)) return 0;
  if (AsciiAlphaToLower(str[s]) == 'z') {
    // UTCDesignator
    r->utc_designator = true;
    return 1;
  }
  // TimeZoneNumericUTCOffset
  return ScanTimeZoneNumericUTCOffset(str, s, r);
}

// TimeZoneIANANameComponent :
//   TZLeadingChar TZChar{0,13} but not one of . or ..
template <typename Char>
int32_t ScanTimeZoneIANANameComponent(base::Vector<Char> str, int32_t s) {
  int32_t cur = s;
  if (str.length() < (cur + 1) || !IsTZLeadingChar(str[cur++])) return 0;
  while (((cur) < str.length()) && ((cur - s) < 14) && IsTZChar(str[cur])) {
    cur++;
  }
  if ((cur - s) == 1 && str[s] == '.') return 0;
  if ((cur - s) == 2 && str[s] == '.' && str[s + 1] == '.') return 0;
  return cur - s;
}

// TimeZoneIANANameTail :
//   TimeZoneIANANameComponent
//   TimeZoneIANANameComponent / TimeZoneIANANameTail
// TimeZoneIANAName :
//   TimeZoneIANANameTail
// The spec text use tail recusion with TimeZoneIANANameComponent and
// TimeZoneIANANameTail. In our implementation, we use an iteration loop
// instead.
template <typename Char>
int32_t ScanTimeZoneIANAName(base::Vector<Char> str, int32_t s) {
  int32_t cur = s;
  int32_t len;
  if ((len = ScanTimeZoneIANANameComponent(str, cur)) == 0) return 0;
  cur += len;
  while ((str.length() > (cur + 1)) && (str[cur] == '/')) {
    cur++;
    if ((len = ScanTimeZoneIANANameComponent(str, cur)) == 0) {
      return 0;
    }
    // TimeZoneIANANameComponent / TimeZoneIANAName
    cur += len;
  }
  return cur - s;
}

template <typename Char>
int32_t ScanTimeZoneIANAName(base::Vector<Char> str, int32_t s,
                             ParsedISO8601Result* r) {
  int32_t len;
  if ((len = ScanTimeZoneIANAName(str, s)) == 0) return 0;
  r->tzi_name_start = s;
  r->tzi_name_length = len;
  return len;
}

// TimeZoneUTCOffsetName
//   Sign Hour
//   Sign Hour : MinuteSecond
//   Sign Hour MinuteSecond
//   Sign Hour : MinuteSecond : MinuteSecond [Fraction]
//   Sign Hour MinuteSecond MinuteSecond [Fraction]
//
template <typename Char>
int32_t ScanTimeZoneUTCOffsetName(base::Vector<Char> str, int32_t s) {
  int32_t cur = s;
  int32_t len;
  if ((str.length() < (s + 3)) || !IsSign(str[cur++])) return 0;
  int32_t hour, minute, second, fraction;
  if ((len = ScanHour(str, cur, &hour)) == 0) return 0;
  cur += len;
  if ((cur + 1) > str.length()) {
    // Sign Hour
    return cur - s;
  }
  if (str[cur] == ':') {
    // Sign Hour :
    cur++;
    if ((len = ScanMinuteSecond(str, cur, &minute)) == 0) return 0;
    cur += len;
    if ((cur + 1) > str.length() || (str[cur] != ':')) {
      // Sign Hour : MinuteSecond
      return cur - s;
    }
    cur++;
    // Sign Hour : MinuteSecond :
    if ((len = ScanMinuteSecond(str, cur, &second)) == 0) return 0;
    cur += len;
    len = ScanFraction(str, cur, &fraction);
    return cur + len - s;
  } else {
    if ((len = ScanMinuteSecond(str, cur, &minute)) == 0) {
      // Sign Hour
      return cur - s;
    }
    cur += len;
    if ((len = ScanMinuteSecond(str, cur, &second)) == 0) {
      // Sign Hour MinuteSecond
      return cur - s;
    }
    cur += len;
    len = ScanFraction(str, cur, &fraction);
    //  Sign Hour MinuteSecond MinuteSecond [Fraction]
    cur += len;
    return cur - s;
  }
}

// TimeZoneBracketedName
//   TimeZoneIANAName
//   "Etc/GMT" ASCIISign Hour
//   TimeZoneUTCOffsetName
// Since "Etc/GMT" also fit TimeZoneIANAName so we need to try
// "Etc/GMT" ASCIISign Hour first.
template <typename Char>
int32_t ScanEtcGMTAsciiSignHour(base::Vector<Char> str, int32_t s) {
  if ((s + 10) > str.length()) return 0;
  int32_t cur = s;
  if ((str[cur++] != 'E') || (str[cur++] != 't') || (str[cur++] != 'c') ||
      (str[cur++] != '/') || (str[cur++] != 'G') || (str[cur++] != 'M') ||
      (str[cur++] != 'T')) {
    return 0;
  }
  Char sign = str[cur++];
  if (!IsAsciiSign(sign)) return 0;
  int32_t hour;
  int32_t len = ScanHour(str, cur, &hour);
  if (len == 0) return 0;
  //   "Etc/GMT" ASCIISign Hour
  return 10;
}

template <typename Char>
int32_t ScanTimeZoneBracketedName(base::Vector<Char> str, int32_t s,
                                  ParsedISO8601Result* r) {
  int32_t len;
  if ((len = ScanEtcGMTAsciiSignHour(str, s)) > 0) return len;
  if ((len = ScanTimeZoneIANAName(str, s)) > 0) {
    r->tzi_name_start = s;
    r->tzi_name_length = len;
    return len;
  } else {
    r->tzi_name_start = 0;
    r->tzi_name_length = 0;
  }
  return ScanTimeZoneUTCOffsetName(str, s);
}

// TimeZoneBracketedAnnotation: '[' TimeZoneBracketedName ']'
template <typename Char>
int32_t ScanTimeZoneBracketedAnnotation(base::Vector<Char> str, int32_t s,
                                        ParsedISO8601Result* r) {
  if ((str.length() < (s + 3)) || (str[s] != '[')) return 0;
  int32_t cur = s + 1;
  cur += ScanTimeZoneBracketedName(str, cur, r);
  if ((cur - s == 1) || str.length() < (cur + 1) || (str[cur++] != ']')) {
    // Reset value setted by ScanTimeZoneBracketedName
    r->tzi_name_start = 0;
    r->tzi_name_length = 0;
    return 0;
  }
  return cur - s;
}

// TimeZoneOffsetRequired:
//   TimeZoneUTCOffset [TimeZoneBracketedAnnotation]
template <typename Char>
int32_t ScanTimeZoneOffsetRequired(base::Vector<Char> str, int32_t s,
                                   ParsedISO8601Result* r) {
  int32_t cur = s;
  cur += ScanTimeZoneUTCOffset(str, cur, r);
  if (cur == s) return 0;
  cur += ScanTimeZoneBracketedAnnotation(str, cur, r);
  return cur - s;
}

//   TimeZoneNameRequired:
//   [TimeZoneUTCOffset] TimeZoneBracketedAnnotation
template <typename Char>
int32_t ScanTimeZoneNameRequired(base::Vector<Char> str, int32_t s,
                                 ParsedISO8601Result* r) {
  int32_t cur = s;
  cur += ScanTimeZoneUTCOffset(str, cur, r);
  int32_t len = ScanTimeZoneBracketedAnnotation(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  return cur - s;
}

// TimeZone:
//   TimeZoneOffsetRequired
//   TimeZoneNameRequired
// The lookahead is at most 1 char.
SCAN_EITHER_FORWARD(TimeZone, TimeZoneOffsetRequired, TimeZoneNameRequired,
                    ParsedISO8601Result)

// The defintion of TimeSpecWithOptionalTimeZoneNotAmbiguous is very complex. We
// break them down into 8 template with _L suffix.
// TimeSpecWithOptionalTimeZoneNotAmbiguous :
//   TimeHour [TimeZoneNumericUTCOffsetNotAmbiguous]
//   [TimeZoneBracketedAnnotation]
//
//   TimeHourNotValidMonth TimeZone
//
//   TimeHour : TimeMinute [TimeZone]
//
//   TimeHourMinuteBasicFormatNotAmbiguous [TimeZoneBracketedAnnotation]
//
//   TimeHour TimeMinute TimeZoneNumericUTCOffsetNotAmbiguousAllowedNegativeHour
//   [TimeZoneBracketedAnnotation]
//
//   TimeHour : TimeMinute : TimeSecond [TimeFraction] [TimeZone]
//
//   TimeHour TimeMinute  TimeSecondNotValidMonth [TimeZone]
//
//   TimeHour TimeMinute TimeSecond TimeFraction [TimeZone]
//
//  TimeSpecWithOptionalTimeZoneNotAmbiguous_L1:
//   TimeHour [TimeZoneNumericUTCOffsetNotAmbiguous]
//   [TimeZoneBracketedAnnotation]
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous_L1(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t time_hour;
  int32_t cur = s;
  int32_t len = ScanTimeHour(str, s, &time_hour);
  if (len == 0) return 0;
  cur += len;
  r->time_hour = time_hour;
  cur += ScanTimeZoneNumericUTCOffsetNotAmbiguous(str, cur, r);
  cur += ScanTimeZoneBracketedAnnotation(str, cur, r);
  return cur - s;
}

//  TimeSpecWithOptionalTimeZoneNotAmbiguous_L2:
//   TimeHourNotValidMonth TimeZone
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous_L2(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t time_hour;
  int32_t cur = s;
  int32_t len = ScanTimeHourNotValidMonth(str, s, &time_hour);
  if (len == 0) return 0;
  cur += len;
  if ((len = ScanTimeZone(str, cur, r)) == 0) return 0;
  // Set time_hour only after we have both.
  r->time_hour = time_hour;
  cur += len;
  return cur - s;
}

//  TimeSpecWithOptionalTimeZoneNotAmbiguous_L3:
//   TimeHour : TimeMinute [TimeZone]
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous_L3(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t time_hour, time_minute;
  // TimeHour
  int32_t cur = s;
  int32_t len = ScanTimeHour(str, s, &time_hour);
  cur += len;
  // :
  if (str.length() < (cur + 3) || str[cur++] != ':') return 0;
  // TimeMinute
  if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) return 0;
  // Set time_hour  and time_minute only after we have both.
  r->time_hour = time_hour;
  r->time_minute = time_minute;
  cur += len;
  // [TimeZone]
  cur += ScanTimeZone(str, cur, r);
  return cur - s;
}

//  TimeSpecWithOptionalTimeZoneNotAmbiguous_L4:
//   TimeHourMinuteBasicFormatNotAmbiguous [TimeZoneBracketedAnnotation]
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous_L4(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len = ScanTimeHourMinuteBasicFormatNotAmbiguous(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  cur += ScanTimeZoneBracketedAnnotation(str, cur, r);
  return cur - s;
}

//  TimeSpecWithOptionalTimeZoneNotAmbiguous_L5:
//   TimeHour TimeMinute TimeZoneNumericUTCOffsetNotAmbiguousAllowedNegativeHour
//   [TimeZoneBracketedAnnotation]
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous_L5(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t time_hour, time_minute;
  // TimeHour
  int32_t cur = s;
  int32_t len = ScanTimeHour(str, s, &time_hour);
  if (len == 0) return 0;
  cur += len;
  // TimeMinute
  if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) return 0;
  cur += len;
  // TimeZoneNumericUTCOffsetNotAmbiguousAllowedNegativeHour
  if ((len = ScanTimeZoneNumericUTCOffsetNotAmbiguousAllowedNegativeHour(
           str, cur, r)) == 0)
    return 0;
  // Set time_hour  and time_minute only after we have both.
  r->time_hour = time_hour;
  r->time_minute = time_minute;
  cur += len;
  // [TimeZoneBracketedAnnotation]
  cur += ScanTimeZoneBracketedAnnotation(str, cur, r);
  return cur - s;
}

//  TimeSpecWithOptionalTimeZoneNotAmbiguous_L6:
//   TimeHour : TimeMinute : TimeSecond [TimeFraction] [TimeZone]
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous_L6(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t time_hour, time_minute, time_second;
  // TimeHour
  int32_t cur = s;
  int32_t len = ScanTimeHour(str, s, &time_hour);
  cur += len;
  // :
  if (str.length() < (cur + 3) || str[cur++] != ':') return 0;
  // TimeMinute
  if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) return 0;
  cur += len;
  // :
  if (str.length() < (cur + 3) || str[cur++] != ':') return 0;
  // TimeSecond
  if ((len = ScanTimeSecond(str, cur, &time_second)) == 0) return 0;
  cur += len;
  // Set time_hour, time_minute, and time_second only after we have them all.
  r->time_hour = time_hour;
  r->time_minute = time_minute;
  r->time_second = time_second;
  // [TimeFraction]
  cur += ScanTimeFraction(str, cur, r);
  // [TimeZone]
  cur += ScanTimeZone(str, cur, r);
  return cur - s;
}

//  TimeSpecWithOptionalTimeZoneNotAmbiguous_L7:
//   TimeHour TimeMinute  TimeSecondNotValidMonth [TimeZone]
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous_L7(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t time_hour, time_minute, time_second;
  // TimeHour
  int32_t cur = s;
  int32_t len = ScanTimeHour(str, s, &time_hour);
  if (len == 0) return 0;
  cur += len;
  // TimeMinute
  if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) return 0;
  cur += len;
  // TimeSecondNotValidMonth
  if ((len = ScanTimeSecondNotValidMonth(str, cur, &time_second)) == 0)
    return 0;
  cur += len;
  // Set time_hour, time_minute, and time_second only after we have them all.
  r->time_hour = time_hour;
  r->time_minute = time_minute;
  r->time_second = time_second;
  // [TimeZone]
  cur += ScanTimeZone(str, cur, r);
  return cur - s;
}

//  TimeSpecWithOptionalTimeZoneNotAmbiguous_L8:
//   TimeHour TimeMinute TimeSecond TimeFraction [TimeZone]
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous_L8(
    base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t time_hour, time_minute, time_second;
  // TimeHour
  int32_t cur = s;
  int32_t len = ScanTimeHour(str, s, &time_hour);
  cur += len;
  // TimeMinute
  if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) return 0;
  cur += len;
  // TimeSecond
  if ((len = ScanTimeSecond(str, cur, &time_second)) == 0) return 0;
  cur += len;
  // TimeFraction
  if ((len = ScanTimeFraction(str, cur, r)) == 0) return 0;
  cur += len;
  // Set time_hour, time_minute, and time_second only after we have them all.
  r->time_hour = time_hour;
  r->time_minute = time_minute;
  r->time_second = time_second;
  // [TimeZone]
  cur += ScanTimeZone(str, cur, r);
  return cur - s;
}

// CalendarNameComponent:
//   CalChar {3,8}
template <typename Char>
int32_t ScanCalendarNameComponent(base::Vector<Char> str, int32_t s) {
  int32_t cur = s;
  while ((cur < str.length()) && IsAlphaNumeric(str[cur])) cur++;
  if ((cur - s) < 3 || (cur - s) > 8) return 0;
  return cur - s;
}

// CalendarNameTail :
//   CalendarNameComponent
//   CalendarNameComponent - CalendarNameTail
// CalendarName :
//   CalendarNameTail
// The spec text use tail recusion with CalendarNameComponent and
// CalendarNameTail. In our implementation, we use an iteration loop instead.
template <typename Char>
int32_t ScanCalendarName(base::Vector<Char> str, int32_t s,
                         ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len;
  if ((len = ScanCalendarNameComponent(str, cur)) == 0) return 0;
  cur += len;
  while ((str.length() > (cur + 1)) && (str[cur++] == '-')) {
    if ((len = ScanCalendarNameComponent(str, cur)) == 0) return 0;
    // CalendarNameComponent - CalendarName
    cur += len;
  }
  r->calendar_name_start = s;
  r->calendar_name_length = cur - s;
  return cur - s;
}

// Calendar: '[u-ca=' CalendarName ']'
template <typename Char>
int32_t ScanCalendar(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Result* r) {
  if (str.length() < (s + 7)) return 0;
  int32_t cur = s;
  // "[u-ca="
  if ((str[cur++] != '[') || (str[cur++] != 'u') || (str[cur++] != '-') ||
      (str[cur++] != 'c') || (str[cur++] != 'a') || (str[cur++] != '=')) {
    return 0;
  }
  int32_t len = ScanCalendarName(str, cur, r);
  if (len == 0) return 0;
  if ((str.length() < (cur + len + 1)) || (str[cur + len] != ']')) {
    return 0;
  }
  return 6 + len + 1;
}

// CalendarTime_L1:
//  TimeDesignator TimeSpec [TimeZone] [Calendar]
template <typename Char>
int32_t ScanCalendarTime_L1(base::Vector<Char> str, int32_t s,
                            ParsedISO8601Result* r) {
  int32_t cur = s;
  if (str.length() < (s + 1)) return 0;
  // TimeDesignator
  if (AsciiAlphaToLower(str[cur++]) != 't') return 0;
  int32_t len = ScanTimeSpec(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  // [TimeZone]
  cur += ScanTimeZone(str, cur, r);
  // [Calendar]
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

// CalendarTime_L2 :
//  TimeSpec [TimeZone] Calendar
template <typename Char>
int32_t ScanCalendarTime_L2(base::Vector<Char> str, int32_t s,
                            ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len = ScanTimeSpec(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  // [TimeZone]
  cur += ScanTimeZone(str, cur, r);
  if ((len = ScanCalendar(str, cur, r)) == 0) return 0;
  cur += len;
  return cur - s;
}

// DateTime: Date [TimeSpecSeparator][TimeZone]
template <typename Char>
int32_t ScanDateTime(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  cur += ScanTimeSpecSeparator(str, cur, r);
  cur += ScanTimeZone(str, cur, r);
  return cur - s;
}

// DateSpecYearMonth: DateYear ['-'] DateMonth
template <typename Char>
int32_t ScanDateSpecYearMonth(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Result* r) {
  int32_t date_year, date_month;
  int32_t cur = s;
  int32_t len = ScanDateYear(str, cur, &date_year);
  if (len == 0) return 0;
  cur += len;
  if (str.length() < (cur + 1)) return 0;
  if (str[cur] == '-') cur++;
  len = ScanDateMonth(str, cur, &date_month);
  if (len == 0) return 0;
  r->date_year = date_year;
  r->date_month = date_month;
  cur += len;
  return cur - s;
}

// DateSpecMonthDay:
// TwoDashopt DateMonth -opt DateDay
template <typename Char>
int32_t ScanDateSpecMonthDay(base::Vector<Char> str, int32_t s,
                             ParsedISO8601Result* r) {
  if (str.length() < (s + 4)) return 0;
  int32_t cur = s;
  if (str[cur] == '-') {
    // The first two dash are optional together
    if (str[++cur] != '-') return 0;
    // TwoDash
    cur++;
  }
  int32_t date_month, date_day;
  int32_t len = ScanDateMonth(str, cur, &date_month);
  if (len == 0) return 0;
  cur += len;
  if (str.length() < (cur + 1)) return 0;
  // '-'
  if (str[cur] == '-') cur++;
  len = ScanDateDay(str, cur, &date_day);
  if (len == 0) return 0;
  r->date_month = date_month;
  r->date_day = date_day;
  cur += len;
  return cur - s;
}

// TemporalTimeZoneIdentifier:
//   TimeZoneNumericUTCOffset
//   TimeZoneIANAName
template <typename Char>
int32_t ScanTemporalTimeZoneIdentifier(base::Vector<Char> str, int32_t s,
                                       ParsedISO8601Result* r) {
  int32_t len;
  if ((len = ScanTimeZoneNumericUTCOffset(str, s, r)) > 0) return len;
  if ((len = ScanTimeZoneIANAName(str, s)) == 0) return 0;
  r->tzi_name_start = s;
  r->tzi_name_length = len;
  return len;
}

// CalendarDateTime: DateTime [Calendar]
template <typename Char>
int32_t ScanCalendarDateTime(base::Vector<Char> str, int32_t s,
                             ParsedISO8601Result* r) {
  int32_t len = ScanDateTime(str, s, r);
  if (len == 0) return 0;
  return len + ScanCalendar(str, len, r);
}

// CalendarDateTimeTimeRequired: Date TimeSpecSeparator [TimeZone] [Calendar]
template <typename Char>
int32_t ScanCalendarDateTimeTimeRequired(base::Vector<Char> str, int32_t s,
                                         ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  len = ScanTimeSpecSeparator(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  // [TimeZone]
  cur + ScanTimeZone(str, cur, r);
  // [Calendar]
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

// TemporalZonedDateTimeString:
//   Date [TimeSpecSeparator] TimeZoneNameRequired [Calendar]
template <typename Char>
int32_t ScanTemporalZonedDateTimeString(base::Vector<Char> str, int32_t s,
                                        ParsedISO8601Result* r) {
  // Date
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur += len;

  // TimeSpecSeparator
  cur += ScanTimeSpecSeparator(str, cur, r);

  // TimeZoneNameRequired
  len = ScanTimeZoneNameRequired(str, cur, r);
  if (len == 0) return 0;
  cur += len;

  // Calendar
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

SCAN_FORWARD(TemporalDateTimeString, CalendarDateTime, ParsedISO8601Result)

// TemporalTimeZoneString:
//   TemporalTimeZoneIdentifier
//   Date [TimeSpecSeparator] TimeZone [Calendar]
template <typename Char>
int32_t ScanDate_TimeSpecSeparator_TimeZone_Calendar(base::Vector<Char> str,
                                                     int32_t s,
                                                     ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur = len;
  cur += ScanTimeSpecSeparator(str, cur, r);
  len = ScanTimeZone(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

// The lookahead is at most 8 chars.
SCAN_EITHER_FORWARD(TemporalTimeZoneString, TemporalTimeZoneIdentifier,
                    Date_TimeSpecSeparator_TimeZone_Calendar,
                    ParsedISO8601Result)

// TemporalTimeString
//   CalendarTime
//   CalendarDateTimeTimeRequired
// The lookahead is at most 7 chars.
SCAN_EITHER_FORWARD(TemporalTimeString, CalendarTime,
                    CalendarDateTimeTimeRequired, ParsedISO8601Result)

// TemporalYearMonthString:
//   DateSpecYearMonth
//   CalendarDateTime
// The lookahead is at most 11 chars.
SCAN_EITHER_FORWARD(TemporalYearMonthString, DateSpecYearMonth,
                    CalendarDateTime, ParsedISO8601Result)

// TemporalMonthDayString
//   DateSpecMonthDay
//   CalendarDateTime
// The lookahead is at most 5 chars.
SCAN_EITHER_FORWARD(TemporalMonthDayString, DateSpecMonthDay, CalendarDateTime,
                    ParsedISO8601Result)

// TemporalInstantString
//   Date TimeZoneOffsetRequired
//   Date DateTimeSeparator TimeSpec TimeZoneOffsetRequired
template <typename Char>
int32_t ScanTemporalInstantString(base::Vector<Char> str, int32_t s,
                                  ParsedISO8601Result* r) {
  // Date
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur += len;

  // TimeZoneOffsetRequired
  len = ScanTimeZoneOffsetRequired(str, cur, r);
  if (len > 0) {
    cur += len;
    return cur - s;
  }

  // DateTimeSeparator
  if (!(((cur + 1) < str.length()) && IsDateTimeSeparator(str[cur++]))) {
    return 0;
  }
  // TimeSpec
  len = ScanTimeSpec(str, cur, r);
  if (len == 0) return 0;
  cur += len;

  // TimeZoneOffsetRequired
  len = ScanTimeZoneOffsetRequired(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  return cur - s;
}

// ==============================================================================
#define SATISIFY(T, R)                            \
  template <typename Char>                        \
  bool Satisfy##T(base::Vector<Char> str, R* r) { \
    R ret;                                        \
    int32_t len = Scan##T(str, 0, &ret);          \
    if ((len > 0) && (len == str.length())) {     \
      *r = ret;                                   \
      return true;                                \
    }                                             \
    return false;                                 \
  }

#define IF_SATISFY_RETURN(T)             \
  {                                      \
    if (Satisfy##T(str, r)) return true; \
  }

#define SATISIFY_EITHER(T1, T2, T3, R)             \
  template <typename Char>                         \
  bool Satisfy##T1(base::Vector<Char> str, R* r) { \
    IF_SATISFY_RETURN(T2)                          \
    IF_SATISFY_RETURN(T3)                          \
    return false;                                  \
  }

SATISIFY(TemporalDateTimeString, ParsedISO8601Result)
SATISIFY(DateTime, ParsedISO8601Result)
SATISIFY(DateSpecYearMonth, ParsedISO8601Result)
SATISIFY(DateSpecMonthDay, ParsedISO8601Result)
SATISIFY(Date_TimeSpecSeparator_TimeZone_Calendar, ParsedISO8601Result)
SATISIFY(CalendarDateTime, ParsedISO8601Result)
SATISIFY(CalendarTime_L1, ParsedISO8601Result)
SATISIFY(CalendarTime_L2, ParsedISO8601Result)
SATISIFY(TimeSpecWithOptionalTimeZoneNotAmbiguous_L1, ParsedISO8601Result)
SATISIFY(TimeSpecWithOptionalTimeZoneNotAmbiguous_L2, ParsedISO8601Result)
SATISIFY(TimeSpecWithOptionalTimeZoneNotAmbiguous_L3, ParsedISO8601Result)
SATISIFY(TimeSpecWithOptionalTimeZoneNotAmbiguous_L4, ParsedISO8601Result)
SATISIFY(TimeSpecWithOptionalTimeZoneNotAmbiguous_L5, ParsedISO8601Result)
SATISIFY(TimeSpecWithOptionalTimeZoneNotAmbiguous_L6, ParsedISO8601Result)
SATISIFY(TimeSpecWithOptionalTimeZoneNotAmbiguous_L7, ParsedISO8601Result)
SATISIFY(TimeSpecWithOptionalTimeZoneNotAmbiguous_L8, ParsedISO8601Result)
template <typename Char>
bool SatisfyTimeSpecWithOptionalTimeZoneNotAmbiguous(base::Vector<Char> str,
                                                     ParsedISO8601Result* r) {
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous_L1)
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous_L2)
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous_L3)
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous_L4)
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous_L5)
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous_L6)
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous_L7)
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous_L8)
  return false;
}
template <typename Char>
bool SatisfyCalendarTime(base::Vector<Char> str, ParsedISO8601Result* r) {
  IF_SATISFY_RETURN(CalendarTime_L1)
  IF_SATISFY_RETURN(CalendarTime_L2)
  IF_SATISFY_RETURN(TimeSpecWithOptionalTimeZoneNotAmbiguous)
  return false;
}
SATISIFY_EITHER(TemporalTimeString, CalendarTime, CalendarDateTime,
                ParsedISO8601Result)
SATISIFY_EITHER(TemporalYearMonthString, DateSpecYearMonth, CalendarDateTime,
                ParsedISO8601Result)
SATISIFY_EITHER(TemporalMonthDayString, DateSpecMonthDay, CalendarDateTime,
                ParsedISO8601Result)
SATISIFY(TimeZoneNumericUTCOffset, ParsedISO8601Result)
SATISIFY(TimeZoneIANAName, ParsedISO8601Result)
SATISIFY_EITHER(TemporalTimeZoneIdentifier, TimeZoneNumericUTCOffset,
                TimeZoneIANAName, ParsedISO8601Result)
SATISIFY_EITHER(TemporalTimeZoneString, TemporalTimeZoneIdentifier,
                Date_TimeSpecSeparator_TimeZone_Calendar, ParsedISO8601Result)
SATISIFY(TemporalInstantString, ParsedISO8601Result)
SATISIFY(TemporalZonedDateTimeString, ParsedISO8601Result)

SATISIFY(CalendarName, ParsedISO8601Result)

template <typename Char>
bool SatisfyTemporalCalendarString(base::Vector<Char> str,
                                   ParsedISO8601Result* r) {
  IF_SATISFY_RETURN(CalendarName)
  IF_SATISFY_RETURN(TemporalInstantString)
  IF_SATISFY_RETURN(CalendarDateTime)
  IF_SATISFY_RETURN(CalendarTime)
  IF_SATISFY_RETURN(DateSpecYearMonth)
  IF_SATISFY_RETURN(DateSpecMonthDay)
  return false;
}

// Duration

SCAN_FORWARD(TimeFractionalPart, FractionalPart, int64_t)

template <typename Char>
int32_t ScanFraction(base::Vector<Char> str, int32_t s, int64_t* out) {
  if (str.length() < (s + 2) || !IsDecimalSeparator(str[s])) return 0;
  int32_t len = ScanTimeFractionalPart(str, s + 1, out);
  return (len == 0) ? 0 : len + 1;
}

SCAN_FORWARD(TimeFraction, Fraction, int64_t)

// Digits : Digit [Digits]

template <typename Char>
int32_t ScanDigits(base::Vector<Char> str, int32_t s, int64_t* out) {
  if (str.length() < (s + 1) || !IsDecimalDigit(str[s])) return 0;
  *out = ToInt(str[s]);
  int32_t len = 1;
  while (s + len + 1 <= str.length() && IsDecimalDigit(str[s + len])) {
    *out = 10 * (*out) + ToInt(str[s + len]);
    len++;
  }
  return len;
}

SCAN_FORWARD(DurationYears, Digits, int64_t)
SCAN_FORWARD(DurationMonths, Digits, int64_t)
SCAN_FORWARD(DurationWeeks, Digits, int64_t)
SCAN_FORWARD(DurationDays, Digits, int64_t)

// DurationWholeHours : Digits
SCAN_FORWARD(DurationWholeHours, Digits, int64_t)

// DurationWholeMinutes : Digits
SCAN_FORWARD(DurationWholeMinutes, Digits, int64_t)

// DurationWholeSeconds : Digits
SCAN_FORWARD(DurationWholeSeconds, Digits, int64_t)

// DurationHoursFraction : TimeFraction
SCAN_FORWARD(DurationHoursFraction, TimeFraction, int64_t)

// DurationMinutesFraction : TimeFraction
SCAN_FORWARD(DurationMinutesFraction, TimeFraction, int64_t)

// DurationSecondsFraction : TimeFraction
SCAN_FORWARD(DurationSecondsFraction, TimeFraction, int64_t)

#define DURATION_WHOLE_FRACTION_DESIGNATOR(Name, name, d)                 \
  template <typename Char>                                                \
  int32_t ScanDurationWhole##Name##FractionDesignator(                    \
      base::Vector<Char> str, int32_t s, ParsedISO8601Duration* r) {      \
    int32_t cur = s;                                                      \
    int64_t whole = ParsedISO8601Duration::kEmpty;                        \
    cur += ScanDurationWhole##Name(str, cur, &whole);                     \
    if (cur == s) return 0;                                               \
    int64_t fraction = ParsedISO8601Duration::kEmpty;                     \
    int32_t len = ScanDuration##Name##Fraction(str, cur, &fraction);      \
    cur += len;                                                           \
    if (str.length() < (cur + 1) || AsciiAlphaToLower(str[cur++]) != (d)) \
      return 0;                                                           \
    r->whole_##name = whole;                                              \
    r->name##_fraction = fraction;                                        \
    return cur - s;                                                       \
  }

DURATION_WHOLE_FRACTION_DESIGNATOR(Seconds, seconds, 's')
DURATION_WHOLE_FRACTION_DESIGNATOR(Minutes, minutes, 'm')
DURATION_WHOLE_FRACTION_DESIGNATOR(Hours, hours, 'h')

// DurationSecondsPart :
//   DurationWholeSeconds DurationSecondsFractionopt SecondsDesignator
SCAN_FORWARD(DurationSecondsPart, DurationWholeSecondsFractionDesignator,
             ParsedISO8601Duration)

// DurationMinutesPart :
//   DurationWholeMinutes DurationMinutesFractionopt MinutesDesignator
//   [DurationSecondsPart]
template <typename Char>
int32_t ScanDurationMinutesPart(base::Vector<Char> str, int32_t s,
                                ParsedISO8601Duration* r) {
  int32_t cur = s;
  int32_t len = ScanDurationWholeMinutesFractionDesignator(str, s, r);
  if (len == 0) return 0;
  cur += len;
  cur += ScanDurationSecondsPart(str, cur, r);
  return cur - s;
}

// DurationHoursPart :
//   DurationWholeHours DurationHoursFractionopt HoursDesignator
//   DurationMinutesPart
//
//   DurationWholeHours DurationHoursFractionopt HoursDesignator
//   [DurationSecondsPart]
template <typename Char>
int32_t ScanDurationHoursPart(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Duration* r) {
  int32_t cur = s;
  int32_t len = ScanDurationWholeHoursFractionDesignator(str, s, r);
  if (len == 0) return 0;
  cur += len;
  len = ScanDurationMinutesPart(str, cur, r);
  if (len > 0) {
    cur += len;
  } else {
    cur += ScanDurationSecondsPart(str, cur, r);
  }
  return cur - s;
}

// DurationTime :
//   TimeDesignator DurationHoursPart
//   TimeDesignator DurationMinutesPart
//   TimeDesignator DurationSecondsPart
template <typename Char>
int32_t ScanDurationTime(base::Vector<Char> str, int32_t s,
                         ParsedISO8601Duration* r) {
  int32_t cur = s;
  if (str.length() < (s + 1)) return 0;
  if (AsciiAlphaToLower(str[cur++]) != 't') return 0;
  if ((cur += ScanDurationHoursPart(str, cur, r)) - s > 1) return cur - s;
  if ((cur += ScanDurationMinutesPart(str, cur, r)) - s > 1) return cur - s;
  if ((cur += ScanDurationSecondsPart(str, cur, r)) - s > 1) return cur - s;
  return 0;
}

#define DURATION_AND_DESIGNATOR(Name, name, d)                              \
  template <typename Char>                                                  \
  int32_t ScanDuration##Name##Designator(base::Vector<Char> str, int32_t s, \
                                         ParsedISO8601Duration* r) {        \
    int32_t cur = s;                                                        \
    int64_t name;                                                           \
    if ((cur += ScanDuration##Name(str, cur, &name)) == s) return 0;        \
    if (str.length() < (cur + 1) || AsciiAlphaToLower(str[cur++]) != (d)) { \
      return 0;                                                             \
    }                                                                       \
    r->name = name;                                                         \
    return cur - s;                                                         \
  }

DURATION_AND_DESIGNATOR(Days, days, 'd')
DURATION_AND_DESIGNATOR(Weeks, weeks, 'w')
DURATION_AND_DESIGNATOR(Months, months, 'm')
DURATION_AND_DESIGNATOR(Years, years, 'y')

// DurationDaysPart : DurationDays DaysDesignator
SCAN_FORWARD(DurationDaysPart, DurationDaysDesignator, ParsedISO8601Duration)

// DurationWeeksPart : DurationWeeks WeeksDesignator [DurationDaysPart]
template <typename Char>
int32_t ScanDurationWeeksPart(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Duration* r) {
  int32_t cur = s;
  if ((cur += ScanDurationWeeksDesignator(str, cur, r)) == s) return 0;
  cur += ScanDurationDaysPart(str, cur, r);
  return cur - s;
}

// DurationMonthsPart :
//   DurationMonths MonthsDesignator DurationWeeksPart
//   DurationMonths MonthsDesignator [DurationDaysPart]
template <typename Char>
int32_t ScanDurationMonthsPart(base::Vector<Char> str, int32_t s,
                               ParsedISO8601Duration* r) {
  int32_t cur = s;
  int32_t len = ScanDurationMonthsDesignator(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  if ((len = ScanDurationWeeksPart(str, cur, r)) > 0) {
    cur += len;
  } else {
    cur += ScanDurationDaysPart(str, cur, r);
  }
  return cur - s;
}

// DurationYearsPart :
//   DurationYears YearsDesignator DurationMonthsPart
//   DurationYears YearsDesignator DurationWeeksPart
//   DurationYears YearsDesignator [DurationDaysPart]
template <typename Char>
int32_t ScanDurationYearsPart(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Duration* r) {
  int32_t cur = s;
  int32_t len = ScanDurationYearsDesignator(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  if ((len = ScanDurationMonthsPart(str, cur, r)) > 0) {
    cur += len;
  } else if ((len = ScanDurationWeeksPart(str, cur, r)) > 0) {
    cur += len;
  } else {
    len = ScanDurationDaysPart(str, cur, r);
    cur += len;
  }
  return cur - s;
}

// DurationDate :
//   DurationYearsPart [DurationTime]
//   DurationMonthsPart [DurationTime]
//   DurationWeeksPart [DurationTime]
//   DurationDaysPart [DurationTime]
template <typename Char>
int32_t ScanDurationDate(base::Vector<Char> str, int32_t s,
                         ParsedISO8601Duration* r) {
  int32_t cur = s;
  do {
    if ((cur += ScanDurationYearsPart(str, cur, r)) > s) break;
    if ((cur += ScanDurationMonthsPart(str, cur, r)) > s) break;
    if ((cur += ScanDurationWeeksPart(str, cur, r)) > s) break;
    if ((cur += ScanDurationDaysPart(str, cur, r)) > s) break;
    return 0;
  } while (false);
  cur += ScanDurationTime(str, cur, r);
  return cur - s;
}

// Duration :
//   Signopt DurationDesignator DurationDate
//   Signopt DurationDesignator DurationTime
template <typename Char>
int32_t ScanDuration(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Duration* r) {
  if (str.length() < (s + 2)) return 0;
  int32_t cur = s;
  int32_t sign =
      (IsSign(str[cur]) && CanonicalSign(str[cur++]) == '-') ? -1 : 1;
  if (AsciiAlphaToLower(str[cur++]) != 'p') return 0;
  int32_t len = ScanDurationDate(str, cur, r);
  if (len == 0) len = ScanDurationTime(str, cur, r);
  if (len == 0) return 0;
  r->sign = sign;
  cur += len;
  return cur - s;
}
SCAN_FORWARD(TemporalDurationString, Duration, ParsedISO8601Duration)

SATISIFY(TemporalDurationString, ParsedISO8601Duration)

}  // namespace

#define IMPL_PARSE_METHOD(R, NAME)                                           \
  base::Optional<R> TemporalParser::Parse##NAME(Isolate* isolate,            \
                                                Handle<String> iso_string) { \
    bool valid;                                                              \
    R parsed;                                                                \
    iso_string = String::Flatten(isolate, iso_string);                       \
    {                                                                        \
      DisallowGarbageCollection no_gc;                                       \
      String::FlatContent str_content = iso_string->GetFlatContent(no_gc);   \
      if (str_content.IsOneByte()) {                                         \
        valid = Satisfy##NAME(str_content.ToOneByteVector(), &parsed);       \
      } else {                                                               \
        valid = Satisfy##NAME(str_content.ToUC16Vector(), &parsed);          \
      }                                                                      \
    }                                                                        \
    if (valid) return parsed;                                                \
    return base::nullopt;                                                    \
  }

IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalDateTimeString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalYearMonthString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalMonthDayString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalTimeString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalInstantString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalZonedDateTimeString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalTimeZoneString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalCalendarString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TimeZoneNumericUTCOffset)
IMPL_PARSE_METHOD(ParsedISO8601Duration, TemporalDurationString)

}  // namespace internal
}  // namespace v8
