// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/uri.h"

#include "src/char-predicates-inl.h"
#include "src/handles.h"
#include "src/isolate-inl.h"
#include "src/list.h"

namespace v8 {
namespace internal {

namespace {  // anonymous namespace for DecodeURI helper functions
bool IsReservedPredicate(uc16 c) {
  switch (c) {
    case '#':
    case '$':
    case '&':
    case '+':
    case ',':
    case '/':
    case ':':
    case ';':
    case '=':
    case '?':
    case '@':
      return true;
    default:
      return false;
  }
}

bool IsReplacementCharacter(const uint8_t* octets, int length) {
  // The replacement character is at codepoint U+FFFD in the Unicode Specials
  // table. Its UTF-8 encoding is 0xEF 0xBF 0xBD.
  if (length != 3 || octets[0] != 0xef || octets[1] != 0xbf ||
      octets[2] != 0xbd) {
    return false;
  }
  return true;
}

bool DecodeOctets(const uint8_t* octets, int length, List<uc16>* buffer) {
  size_t cursor = 0;
  uc32 value = unibrow::Utf8::ValueOf(octets, length, &cursor);
  if (value == unibrow::Utf8::kBadChar &&
      !IsReplacementCharacter(octets, length)) {
    return false;
  }

  if (value <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
    buffer->Add(value);
  } else {
    buffer->Add(unibrow::Utf16::LeadSurrogate(value));
    buffer->Add(unibrow::Utf16::TrailSurrogate(value));
  }
  return true;
}

bool TwoDigitHex(int index, String::FlatContent* uri_content, uc16* decoded) {
  char high = HexValue(uri_content->Get(index + 1));
  char low = HexValue(uri_content->Get(index + 2));
  if (high < 0 || low < 0) {
    return false;
  }
  *decoded = (high << 4) | low;
  return true;
}

template <typename T>
void AddToBuffer(uc16 decoded, String::FlatContent* uri_content, int index,
                 bool is_uri, List<T>* buffer) {
  if (is_uri && IsReservedPredicate(decoded)) {
    buffer->Add('%');
    uc16 first = uri_content->Get(index + 1);
    uc16 second = uri_content->Get(index + 2);
    DCHECK_GT(std::numeric_limits<T>::max(), first);
    DCHECK_GT(std::numeric_limits<T>::max(), second);

    buffer->Add(first);
    buffer->Add(second);
  } else {
    buffer->Add(decoded);
  }
}

bool IntoTwoByte(int index, bool is_uri, int uri_length,
                 String::FlatContent* uri_content, List<uc16>* buffer) {
  for (int k = index; k < uri_length; k++) {
    uc16 code = uri_content->Get(k);
    if (code == '%') {
      uc16 decoded;
      if (k + 2 >= uri_length || !TwoDigitHex(k, uri_content, &decoded)) {
        return false;
      }
      k += 2;
      if (decoded > unibrow::Utf8::kMaxOneByteChar) {
        uint8_t octets[unibrow::Utf8::kMaxEncodedSize];
        octets[0] = decoded;

        int number_of_continuation_bytes = 0;
        while ((decoded << ++number_of_continuation_bytes) & 0x80) {
          if (number_of_continuation_bytes > 3 || k + 3 >= uri_length) {
            return false;
          }

          uc16 continuation_byte;

          if (uri_content->Get(++k) != '%' ||
              !TwoDigitHex(k, uri_content, &continuation_byte)) {
            return false;
          }
          k += 2;
          octets[number_of_continuation_bytes] = continuation_byte;
        }

        if (!DecodeOctets(octets, number_of_continuation_bytes, buffer)) {
          return false;
        }
      } else {
        AddToBuffer(decoded, uri_content, k - 2, is_uri, buffer);
      }
    } else {
      buffer->Add(code);
    }
  }
  return true;
}

bool IntoOneAndTwoByte(Handle<String> uri, bool is_uri,
                       List<uint8_t>* one_byte_buffer,
                       List<uc16>* two_byte_buffer) {
  DisallowHeapAllocation no_gc;
  String::FlatContent uri_content = uri->GetFlatContent();

  int uri_length = uri->length();
  for (int k = 0; k < uri_length; k++) {
    uc16 code = uri_content.Get(k);
    if (code == '%') {
      uc16 decoded;
      if (k + 2 >= uri_length || !TwoDigitHex(k, &uri_content, &decoded)) {
        return false;
      }

      if (decoded > unibrow::Utf8::kMaxOneByteChar) {
        return IntoTwoByte(k, is_uri, uri_length, &uri_content,
                           two_byte_buffer);
      }

      AddToBuffer(decoded, &uri_content, k, is_uri, one_byte_buffer);
      k += 2;
    } else {
      if (code > unibrow::Utf8::kMaxOneByteChar) {
        return IntoTwoByte(k, is_uri, uri_length, &uri_content,
                           two_byte_buffer);
      }
      one_byte_buffer->Add(code);
    }
  }
  return true;
}

}  // anonymous namespace

MaybeHandle<String> Uri::Decode(Isolate* isolate, Handle<String> uri,
                                bool is_uri) {
  uri = String::Flatten(uri);
  List<uint8_t> one_byte_buffer;
  List<uc16> two_byte_buffer;

  if (!IntoOneAndTwoByte(uri, is_uri, &one_byte_buffer, &two_byte_buffer)) {
    THROW_NEW_ERROR(isolate, NewURIError(), String);
  }

  if (two_byte_buffer.is_empty()) {
    return isolate->factory()->NewStringFromOneByte(
        one_byte_buffer.ToConstVector());
  }

  Handle<SeqTwoByteString> result;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, result, isolate->factory()->NewRawTwoByteString(
                           one_byte_buffer.length() + two_byte_buffer.length()),
      String);

  CopyChars(result->GetChars(), one_byte_buffer.ToConstVector().start(),
            one_byte_buffer.length());
  CopyChars(result->GetChars() + one_byte_buffer.length(),
            two_byte_buffer.ToConstVector().start(), two_byte_buffer.length());

  return result;
}

namespace {  // anonymous namespace for EncodeURI helper functions
bool IsUnescapePredicateInUriComponent(uc16 c) {
  if (IsAlphaNumeric(c)) {
    return true;
  }

  switch (c) {
    case '!':
    case '\'':
    case '(':
    case ')':
    case '*':
    case '-':
    case '.':
    case '_':
    case '~':
      return true;
    default:
      return false;
  }
}

bool IsUriSeparator(uc16 c) {
  switch (c) {
    case '#':
    case ':':
    case ';':
    case '/':
    case '?':
    case '$':
    case '&':
    case '+':
    case ',':
    case '@':
    case '=':
      return true;
    default:
      return false;
  }
}

void AddHexEncodedToBuffer(uint8_t octet, List<uint8_t>* buffer) {
  buffer->Add('%');
  buffer->Add(HexCharOfValue(octet >> 4));
  buffer->Add(HexCharOfValue(octet & 0x0F));
}

void EncodeSingle(uc16 c, List<uint8_t>* buffer) {
  char s[4] = {};
  int number_of_bytes;
  number_of_bytes =
      unibrow::Utf8::Encode(s, c, unibrow::Utf16::kNoPreviousCharacter, false);
  for (int k = 0; k < number_of_bytes; k++) {
    AddHexEncodedToBuffer(s[k], buffer);
  }
}

void EncodePair(uc16 cc1, uc16 cc2, List<uint8_t>* buffer) {
  char s[4];
  int number_of_bytes =
      unibrow::Utf8::Encode(s, unibrow::Utf16::CombineSurrogatePair(cc1, cc2),
                            unibrow::Utf16::kNoPreviousCharacter, false);
  for (int k = 0; k < number_of_bytes; k++) {
    AddHexEncodedToBuffer(s[k], buffer);
  }
}

}  // anonymous namespace

MaybeHandle<String> Uri::Encode(Isolate* isolate, Handle<String> uri,
                                bool is_uri) {
  uri = String::Flatten(uri);
  int uri_length = uri->length();
  List<uint8_t> buffer(uri_length);

  {
    DisallowHeapAllocation no_gc;
    String::FlatContent uri_content = uri->GetFlatContent();

    for (int k = 0; k < uri_length; k++) {
      uc16 cc1 = uri_content.Get(k);
      if (unibrow::Utf16::IsLeadSurrogate(cc1)) {
        k++;
        if (k < uri_length) {
          uc16 cc2 = uri->Get(k);
          if (unibrow::Utf16::IsTrailSurrogate(cc2)) {
            EncodePair(cc1, cc2, &buffer);
            continue;
          }
        }
      } else if (!unibrow::Utf16::IsTrailSurrogate(cc1)) {
        if (IsUnescapePredicateInUriComponent(cc1) ||
            (is_uri && IsUriSeparator(cc1))) {
          buffer.Add(cc1);
        } else {
          EncodeSingle(cc1, &buffer);
        }
        continue;
      }

      AllowHeapAllocation allocate_error_and_return;
      THROW_NEW_ERROR(isolate, NewURIError(), String);
    }
  }

  return isolate->factory()->NewStringFromOneByte(buffer.ToConstVector());
}

}  // namespace internal
}  // namespace v8
