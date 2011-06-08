// Copyright 2011 the V8 project authors. All rights reserved.
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

#ifndef V8_JSON_PARSER_H_
#define V8_JSON_PARSER_H_

#include "v8.h"

#include "char-predicates-inl.h"
#include "conversions.h"
#include "messages.h"
#include "spaces-inl.h"
#include "token.h"

namespace v8 {
namespace internal {

// A simple json parser.
template <bool seq_ascii>
class JsonParser BASE_EMBEDDED {
 public:
  static Handle<Object> Parse(Handle<String> source) {
    return JsonParser().ParseJson(source);
  }

  static const int kEndOfString = -1;

 private:
  // Parse a string containing a single JSON value.
  Handle<Object> ParseJson(Handle<String> source);

  inline void Advance() {
    position_++;
    if (position_ > source_length_) {
      c0_ = kEndOfString;
    } else if (seq_ascii) {
      c0_ = seq_source_->SeqAsciiStringGet(position_);
    } else {
      c0_ = source_->Get(position_);
    }
  }

  // The JSON lexical grammar is specified in the ECMAScript 5 standard,
  // section 15.12.1.1. The only allowed whitespace characters between tokens
  // are tab, carriage-return, newline and space.

  inline void AdvanceSkipWhitespace() {
    do {
      Advance();
    } while (c0_ == '\t' || c0_ == '\r' || c0_ == '\n' || c0_ == ' ');
  }

  inline void SkipWhitespace() {
    while (c0_ == '\t' || c0_ == '\r' || c0_ == '\n' || c0_ == ' ') {
      Advance();
    }
  }

  inline uc32 AdvanceGetChar() {
    Advance();
    return c0_;
  }

  // Checks that current charater is c.
  // If so, then consume c and skip whitespace.
  inline bool MatchSkipWhiteSpace(uc32 c) {
    if (c0_ == c) {
      AdvanceSkipWhitespace();
      return true;
    }
    return false;
  }

  // A JSON string (production JSONString) is subset of valid JavaScript string
  // literals. The string must only be double-quoted (not single-quoted), and
  // the only allowed backslash-escapes are ", /, \, b, f, n, r, t and
  // four-digit hex escapes (uXXXX). Any other use of backslashes is invalid.
  Handle<String> ParseJsonString() {
    return ScanJsonString<false>();
  }
  Handle<String> ParseJsonSymbol() {
    return ScanJsonString<true>();
  }
  template <bool is_symbol>
  Handle<String> ScanJsonString();
  // Slow version for unicode support, uses the first ascii_count characters,
  // as first part of a ConsString
  Handle<String> SlowScanJsonString();

  // A JSON number (production JSONNumber) is a subset of the valid JavaScript
  // decimal number literals.
  // It includes an optional minus sign, must have at least one
  // digit before and after a decimal point, may not have prefixed zeros (unless
  // the integer part is zero), and may include an exponent part (e.g., "e-10").
  // Hexadecimal and octal numbers are not allowed.
  Handle<Object> ParseJsonNumber();

  // Parse a single JSON value from input (grammar production JSONValue).
  // A JSON value is either a (double-quoted) string literal, a number literal,
  // one of "true", "false", or "null", or an object or array literal.
  Handle<Object> ParseJsonValue();

  // Parse a JSON object literal (grammar production JSONObject).
  // An object literal is a squiggly-braced and comma separated sequence
  // (possibly empty) of key/value pairs, where the key is a JSON string
  // literal, the value is a JSON value, and the two are separated by a colon.
  // A JSON array dosn't allow numbers and identifiers as keys, like a
  // JavaScript array.
  Handle<Object> ParseJsonObject();

  // Parses a JSON array literal (grammar production JSONArray). An array
  // literal is a square-bracketed and comma separated sequence (possibly empty)
  // of JSON values.
  // A JSON array doesn't allow leaving out values from the sequence, nor does
  // it allow a terminal comma, like a JavaScript array does.
  Handle<Object> ParseJsonArray();


  // Mark that a parsing error has happened at the current token, and
  // return a null handle. Primarily for readability.
  inline Handle<Object> ReportUnexpectedCharacter() {
    return Handle<Object>::null();
  }

  inline Isolate* isolate() { return isolate_; }

  static const int kInitialSpecialStringSize = 1024;


 private:
  Handle<String> source_;
  int source_length_;
  Handle<SeqAsciiString> seq_source_;

  // begin and end position of scanned string or number
  int beg_pos_;
  int end_pos_;

  Isolate* isolate_;
  uc32 c0_;
  int position_;

  double number_;
};

template <bool seq_ascii>
Handle<Object> JsonParser<seq_ascii>::ParseJson(Handle<String> source) {
  isolate_ = source->map()->isolate();
  source_ = Handle<String>(source->TryFlattenGetString());
  source_length_ = source_->length() - 1;

  // Optimized fast case where we only have ascii characters.
  if (seq_ascii) {
    seq_source_ = Handle<SeqAsciiString>::cast(source_);
  }

  // Set initial position right before the string.
  position_ = -1;
  // Advance to the first character (posibly EOS)
  AdvanceSkipWhitespace();
  Handle<Object> result = ParseJsonValue();
  if (result.is_null() || c0_ != kEndOfString) {
    // Parse failed. Current character is the unexpected token.

    const char* message;
    Factory* factory = isolate()->factory();
    Handle<JSArray> array;

    switch (c0_) {
      case kEndOfString:
        message = "unexpected_eos";
        array = factory->NewJSArray(0);
        break;
      case '-':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        message = "unexpected_token_number";
        array = factory->NewJSArray(0);
        break;
      case '"':
        message = "unexpected_token_string";
        array = factory->NewJSArray(0);
        break;
      default:
        message = "unexpected_token";
        Handle<Object> name = LookupSingleCharacterStringFromCode(c0_);
        Handle<FixedArray> element = factory->NewFixedArray(1);
        element->set(0, *name);
        array = factory->NewJSArrayWithElements(element);
        break;
    }

    MessageLocation location(factory->NewScript(source),
                             position_,
                             position_ + 1);
    Handle<Object> result = factory->NewSyntaxError(message, array);
    isolate()->Throw(*result, &location);
    return Handle<Object>::null();
  }
  return result;
}


// Parse any JSON value.
template <bool seq_ascii>
Handle<Object> JsonParser<seq_ascii>::ParseJsonValue() {
  switch (c0_) {
    case '"':
      return ParseJsonString();
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return ParseJsonNumber();
    case 'f':
      if (AdvanceGetChar() == 'a' && AdvanceGetChar() == 'l' &&
          AdvanceGetChar() == 's' && AdvanceGetChar() == 'e') {
        AdvanceSkipWhitespace();
        return isolate()->factory()->false_value();
      } else {
        return ReportUnexpectedCharacter();
      }
    case 't':
      if (AdvanceGetChar() == 'r' && AdvanceGetChar() == 'u' &&
          AdvanceGetChar() == 'e') {
        AdvanceSkipWhitespace();
        return isolate()->factory()->true_value();
      } else {
        return ReportUnexpectedCharacter();
      }
    case 'n':
      if (AdvanceGetChar() == 'u' && AdvanceGetChar() == 'l' &&
          AdvanceGetChar() == 'l') {
        AdvanceSkipWhitespace();
        return isolate()->factory()->null_value();
      } else {
        return ReportUnexpectedCharacter();
      }
    case '{':
      return ParseJsonObject();
    case '[':
      return ParseJsonArray();
    default:
      return ReportUnexpectedCharacter();
  }
}


// Parse a JSON object. Position must be right at '{'.
template <bool seq_ascii>
Handle<Object> JsonParser<seq_ascii>::ParseJsonObject() {
  Handle<JSFunction> object_constructor(
      isolate()->global_context()->object_function());
  Handle<JSObject> json_object =
      isolate()->factory()->NewJSObject(object_constructor);
  ASSERT_EQ(c0_, '{');

  AdvanceSkipWhitespace();
  if (c0_ != '}') {
    do {
      if (c0_ != '"') return ReportUnexpectedCharacter();
      Handle<String> key = ParseJsonSymbol();
      if (key.is_null() || c0_ != ':') return ReportUnexpectedCharacter();
      AdvanceSkipWhitespace();
      Handle<Object> value = ParseJsonValue();
      if (value.is_null()) return ReportUnexpectedCharacter();

      uint32_t index;
      if (key->AsArrayIndex(&index)) {
        SetOwnElement(json_object, index, value, kNonStrictMode);
      } else if (key->Equals(isolate()->heap()->Proto_symbol())) {
        SetPrototype(json_object, value);
      } else {
        SetLocalPropertyIgnoreAttributes(json_object, key, value, NONE);
      }
    } while (MatchSkipWhiteSpace(','));
    if (c0_ != '}') {
      return ReportUnexpectedCharacter();
    }
  }
  AdvanceSkipWhitespace();
  return json_object;
}

// Parse a JSON array. Position must be right at '['.
template <bool seq_ascii>
Handle<Object> JsonParser<seq_ascii>::ParseJsonArray() {
  ZoneScope zone_scope(isolate(), DELETE_ON_EXIT);
  ZoneList<Handle<Object> > elements(4);
  ASSERT_EQ(c0_, '[');

  AdvanceSkipWhitespace();
  if (c0_ != ']') {
    do {
      Handle<Object> element = ParseJsonValue();
      if (element.is_null()) return ReportUnexpectedCharacter();
      elements.Add(element);
    } while (MatchSkipWhiteSpace(','));
    if (c0_ != ']') {
      return ReportUnexpectedCharacter();
    }
  }
  AdvanceSkipWhitespace();
  // Allocate a fixed array with all the elements.
  Handle<FixedArray> fast_elements =
      isolate()->factory()->NewFixedArray(elements.length());
  for (int i = 0, n = elements.length(); i < n; i++) {
    fast_elements->set(i, *elements[i]);
  }
  return isolate()->factory()->NewJSArrayWithElements(fast_elements);
}


template <bool seq_ascii>
Handle<Object> JsonParser<seq_ascii>::ParseJsonNumber() {
  bool negative = false;
  beg_pos_ = position_;
  if (c0_ == '-') {
    Advance();
    negative = true;
  }
  if (c0_ == '0') {
    Advance();
    // Prefix zero is only allowed if it's the only digit before
    // a decimal point or exponent.
    if ('0' <= c0_ && c0_ <= '9') return ReportUnexpectedCharacter();
  } else {
    int i = 0;
    int digits = 0;
    if (c0_ < '1' || c0_ > '9') return ReportUnexpectedCharacter();
    do {
      i = i * 10 + c0_ - '0';
      digits++;
      Advance();
    } while (c0_ >= '0' && c0_ <= '9');
    if (c0_ != '.' && c0_ != 'e' && c0_ != 'E' && digits < 10) {
      number_ = (negative ? -i : i);
      SkipWhitespace();
      return isolate()->factory()->NewNumber(number_);
    }
  }
  if (c0_ == '.') {
    Advance();
    if (c0_ < '0' || c0_ > '9') return ReportUnexpectedCharacter();
    do {
      Advance();
    } while (c0_ >= '0' && c0_ <= '9');
  }
  if (AsciiAlphaToLower(c0_) == 'e') {
    Advance();
    if (c0_ == '-' || c0_ == '+') Advance();
    if (c0_ < '0' || c0_ > '9') return ReportUnexpectedCharacter();
    do {
      Advance();
    } while (c0_ >= '0' && c0_ <= '9');
  }
  int length = position_ - beg_pos_;
  if (seq_ascii) {
    Vector<const char> chars(seq_source_->GetChars() +  beg_pos_, length);
    number_ = StringToDouble(isolate()->unicode_cache(),
                             chars,
                             NO_FLAGS,  // Hex, octal or trailing junk.
                             OS::nan_value());
  } else {
    Vector<char> buffer = Vector<char>::New(length);
    String::WriteToFlat(*source_, buffer.start(), beg_pos_, position_);
    Vector<const char> result =
        Vector<const char>(reinterpret_cast<const char*>(buffer.start()),
        length);
    number_ = StringToDouble(isolate()->unicode_cache(),
                             result,
                             NO_FLAGS,  // Hex, octal or trailing junk.
                             0.0);
    buffer.Dispose();
  }
  SkipWhitespace();
  return isolate()->factory()->NewNumber(number_);
}

template <bool seq_ascii>
Handle<String> JsonParser<seq_ascii>::SlowScanJsonString() {
  // The currently scanned ascii characters.
  Handle<String> ascii(isolate()->factory()->NewSubString(source_,
                                                          beg_pos_,
                                                          position_));
  Handle<String> two_byte =
      isolate()->factory()->NewRawTwoByteString(kInitialSpecialStringSize,
                                                NOT_TENURED);
  Handle<SeqTwoByteString> seq_two_byte =
      Handle<SeqTwoByteString>::cast(two_byte);

  int allocation_count = 1;
  int count = 0;

  while (c0_ != '"') {
    // Create new seq string
    if (count >= kInitialSpecialStringSize * allocation_count) {
      allocation_count = allocation_count * 2;
      int new_size = allocation_count * kInitialSpecialStringSize;
      Handle<String> new_two_byte =
          isolate()->factory()->NewRawTwoByteString(new_size,
                                                    NOT_TENURED);
      uc16* char_start =
          Handle<SeqTwoByteString>::cast(new_two_byte)->GetChars();
      String::WriteToFlat(*seq_two_byte, char_start, 0, count);
      seq_two_byte = Handle<SeqTwoByteString>::cast(new_two_byte);
    }

    // Check for control character (0x00-0x1f) or unterminated string (<0).
    if (c0_ < 0x20) return Handle<String>::null();
    if (c0_ != '\\') {
      seq_two_byte->SeqTwoByteStringSet(count++, c0_);
      Advance();
    } else {
      Advance();
      switch (c0_) {
        case '"':
        case '\\':
        case '/':
          seq_two_byte->SeqTwoByteStringSet(count++, c0_);
          break;
        case 'b':
          seq_two_byte->SeqTwoByteStringSet(count++, '\x08');
          break;
        case 'f':
          seq_two_byte->SeqTwoByteStringSet(count++, '\x0c');
          break;
        case 'n':
          seq_two_byte->SeqTwoByteStringSet(count++, '\x0a');
          break;
        case 'r':
          seq_two_byte->SeqTwoByteStringSet(count++, '\x0d');
          break;
        case 't':
          seq_two_byte->SeqTwoByteStringSet(count++, '\x09');
          break;
        case 'u': {
          uc32 value = 0;
          for (int i = 0; i < 4; i++) {
            Advance();
            int digit = HexValue(c0_);
            if (digit < 0) {
              return Handle<String>::null();
            }
            value = value * 16 + digit;
          }
          seq_two_byte->SeqTwoByteStringSet(count++, value);
          break;
        }
        default:
          return Handle<String>::null();
      }
      Advance();
    }
  }
  // Advance past the last '"'.
  ASSERT_EQ('"', c0_);
  AdvanceSkipWhitespace();

  // Shrink the the string to our length.
  if (isolate()->heap()->InNewSpace(*seq_two_byte)) {
    isolate()->heap()->new_space()->
        template ShrinkStringAtAllocationBoundary<SeqTwoByteString>(
            *seq_two_byte, count);
  } else {
    int string_size = SeqTwoByteString::SizeFor(count);
    int allocated_string_size =
        SeqTwoByteString::SizeFor(kInitialSpecialStringSize * allocation_count);
    int delta = allocated_string_size - string_size;
    Address start_filler_object = seq_two_byte->address() + string_size;
    seq_two_byte->set_length(count);
    isolate()->heap()->CreateFillerObjectAt(start_filler_object, delta);
  }
  return isolate()->factory()->NewConsString(ascii, seq_two_byte);
}

template <bool seq_ascii>
template <bool is_symbol>
Handle<String> JsonParser<seq_ascii>::ScanJsonString() {
  ASSERT_EQ('"', c0_);
  Advance();
  beg_pos_ = position_;
  // Fast case for ascii only without escape characters.
  while (c0_ != '"') {
    // Check for control character (0x00-0x1f) or unterminated string (<0).
    if (c0_ < 0x20) return Handle<String>::null();
    if (c0_ != '\\' && (seq_ascii || c0_ < kMaxAsciiCharCode)) {
      Advance();
    } else {
      return this->SlowScanJsonString();
    }
  }
  ASSERT_EQ('"', c0_);
  end_pos_ = position_;
  // Advance past the last '"'.
  AdvanceSkipWhitespace();
  if (seq_ascii && is_symbol) {
    return isolate()->factory()->LookupAsciiSymbol(seq_source_,
                                                   beg_pos_,
                                                   end_pos_ - beg_pos_);
  } else {
    return isolate()->factory()->NewSubString(source_, beg_pos_, end_pos_);
  }
}

} }  // namespace v8::internal

#endif  // V8_JSON_PARSER_H_
