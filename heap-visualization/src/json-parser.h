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
#include "v8conversions.h"
#include "messages.h"
#include "spaces-inl.h"
#include "token.h"

namespace v8 {
namespace internal {

// A simple JSON parser.
template <typename StringType>
class JsonParser BASE_EMBEDDED {
 public:
  static Handle<Object> Parse(Handle<String> source) {
    return JsonParser(Handle<StringType>::cast(source)).ParseJson();
  }

  static const int kEndOfString = -1;

 private:
  typedef typename StringType::CharType SourceChar;

  explicit JsonParser(Handle<StringType> source)
      : isolate_(source->GetHeap()->isolate()),
        source_(source),
        characters_(NULL),
        source_length_(source->length()),
        position_(-1) {
    InitializeSource();
  }


  // Parse the source string as containing a single JSON value.
  Handle<Object> ParseJson();

  // Set up the object so GetChar works, in case it needs more than just
  // the constructor.
  void InitializeSource();

  inline uc32 GetChar(int position);
  inline const SourceChar* GetChars();

  inline void Advance() {
    position_++;
    if (position_ >= source_length_) {
      c0_ = kEndOfString;
    } else {
      c0_ = GetChar(position_);
    }
  }

  // The JSON lexical grammar is specified in the ECMAScript 5 standard,
  // section 15.12.1.1. The only allowed whitespace characters between tokens
  // are tab, carriage-return, newline and space.


  static inline bool IsJsonWhitespace(uc32 ch) {
    const char* whitespaces = "\x20\x09\x0a\0\0\x0d\0\0";
    return (static_cast<uc32>(whitespaces[ch & 0x07]) == ch);
  }

  inline void AdvanceSkipWhitespace() {
    do {
      Advance();
    } while (IsJsonWhitespace(c0_));
  }

  inline void SkipWhitespace() {
    while (IsJsonWhitespace(c0_)) {
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
  // Creates a new string and copies prefix[start..end] into the beginning
  // of it. Then scans the rest of the string, adding characters after the
  // prefix. Called by ScanJsonString when reaching a '\' or non-ASCII char.
  template <typename SinkStringType>
  Handle<String> SlowScanJsonString(Handle<String> prefix, int start, int end);

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

  static const int kInitialSpecialStringLength = 1024;

  Isolate* isolate_;
  Handle<StringType> source_;
  // Used for external strings, to avoid going through the resource on
  // every access.
  const SourceChar* characters_;
  int source_length_;
  int position_;
  uc32 c0_;
};

template <typename StringType>
Handle<Object> JsonParser<StringType>::ParseJson() {
  // Initial position is right before the string.
  ASSERT(position_ == -1);
  // Advance to the first character (posibly EOS)
  AdvanceSkipWhitespace();
  // ParseJsonValue also consumes following whitespace.
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

    MessageLocation location(factory->NewScript(source_),
                             position_,
                             position_ + 1);
    Handle<Object> result = factory->NewSyntaxError(message, array);
    isolate()->Throw(*result, &location);
    return Handle<Object>::null();
  }
  return result;
}


// Parse any JSON value.
template <typename StringType>
Handle<Object> JsonParser<StringType>::ParseJsonValue() {
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
template <typename StringType>
Handle<Object> JsonParser<StringType>::ParseJsonObject() {
  Handle<JSFunction> object_constructor(
      isolate()->global_context()->object_function());
  Handle<JSObject> json_object =
      isolate()->factory()->NewJSObject(object_constructor);
  ASSERT_EQ('{', c0_);

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
template <typename StringType>
Handle<Object> JsonParser<StringType>::ParseJsonArray() {
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


template <typename StringType>
Handle<Object> JsonParser<StringType>::ParseJsonNumber() {
  bool negative = false;
  int beg_pos = position_;
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
      SkipWhitespace();
      return Handle<Smi>(Smi::FromInt((negative ? -i : i)), isolate());
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
  int length = position_ - beg_pos;
  double number;

  Vector<const SourceChar> chars(GetChars() +  beg_pos, length);
  number = StringToDouble(isolate()->unicode_cache(),
                          chars,
                          NO_FLAGS,  // Hex, octal or trailing junk.
                          OS::nan_value());
  SkipWhitespace();
  return isolate()->factory()->NewNumber(number);
}


template <typename StringType>
inline void SeqStringSet(Handle<StringType> seq_str, int i, uc32 c);

template <>
inline void SeqStringSet(Handle<SeqTwoByteString> seq_str, int i, uc32 c) {
  seq_str->SeqTwoByteStringSet(i, c);
}

template <>
inline void SeqStringSet(Handle<SeqAsciiString> seq_str, int i, uc32 c) {
  seq_str->SeqAsciiStringSet(i, c);
}

template <typename StringType>
inline Handle<StringType> NewRawString(Factory* factory, int length);

template <>
inline Handle<SeqTwoByteString> NewRawString(Factory* factory, int length) {
  return factory->NewRawTwoByteString(length, NOT_TENURED);
}

template <>
inline Handle<SeqAsciiString> NewRawString(Factory* factory, int length) {
  return factory->NewRawAsciiString(length, NOT_TENURED);
}


// Scans the rest of a JSON string starting from position_ and writes
// prefix[start..end] along with the scanned characters into a
// sequential string of type StringType.
template <typename StringType>
template <typename SinkStringType>
Handle<String> JsonParser<StringType>::SlowScanJsonString(
    Handle<String> prefix, int start, int end) {
  typedef typename SinkStringType::CharType SinkChar;
  int count = end - start;
  int max_length = count + source_length_ - position_;
  int length = Min(max_length, Max(kInitialSpecialStringLength, 2 * count));
  Handle<SinkStringType> seq_str =
      NewRawString<SinkStringType>(isolate()->factory(),
                                   length);
  // Copy prefix into seq_str.
  SinkChar* dest = seq_str->GetChars();
  String::WriteToFlat(*prefix, dest, start, end);

  while (c0_ != '"') {
    // Check for control character (0x00-0x1f) or unterminated string (<0).
    if (c0_ < 0x20) return Handle<String>::null();
    if (count >= length) {
      // We need to create a longer sequential string for the result.
      return SlowScanJsonString<SinkStringType>(seq_str, 0, count);
    }
    if (c0_ != '\\') {
      // If the sink can contain UC16 characters, or source_ contains only
      // ASCII characters, there's no need to test whether we can store the
      // character. Otherwise check whether the UC16 source character can fit
      // in the ASCII sink.
      if (sizeof(SinkChar) == kUC16Size ||
          sizeof(SourceChar) == kCharSize ||
          c0_ <= kMaxAsciiCharCode) {
        SeqStringSet(seq_str, count++, c0_);
        Advance();
      } else {
        // SinkStringType is SeqAsciiString and we just read a non-ASCII char.
        return SlowScanJsonString<SeqTwoByteString>(seq_str, 0, count);
      }
    } else {
      Advance();  // Advance past the '\'.
      switch (c0_) {
        case '"':
        case '\\':
        case '/':
          SeqStringSet(seq_str, count++, c0_);
          break;
        case 'b':
          SeqStringSet(seq_str, count++, '\x08');
          break;
        case 'f':
          SeqStringSet(seq_str, count++, '\x0c');
          break;
        case 'n':
          SeqStringSet(seq_str, count++, '\x0a');
          break;
        case 'r':
          SeqStringSet(seq_str, count++, '\x0d');
          break;
        case 't':
          SeqStringSet(seq_str, count++, '\x09');
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
          if (sizeof(SinkChar) == kUC16Size || value <= kMaxAsciiCharCode) {
            SeqStringSet(seq_str, count++, value);
            break;
          } else {
            // StringType is SeqAsciiString and we just read a non-ASCII char.
            position_ -= 6;  // Rewind position_ to \ in \uxxxx.
            Advance();
            return SlowScanJsonString<SeqTwoByteString>(seq_str,
                                                        0,
                                                        count);
          }
        }
        default:
          return Handle<String>::null();
      }
      Advance();
    }
  }
  // Shrink seq_string length to count.
  if (isolate()->heap()->InNewSpace(*seq_str)) {
    isolate()->heap()->new_space()->
        template ShrinkStringAtAllocationBoundary<SinkStringType>(
            *seq_str, count);
  } else {
    int string_size = SinkStringType::SizeFor(count);
    int allocated_string_size = SinkStringType::SizeFor(length);
    int delta = allocated_string_size - string_size;
    Address start_filler_object = seq_str->address() + string_size;
    seq_str->set_length(count);
    isolate()->heap()->CreateFillerObjectAt(start_filler_object, delta);
  }
  ASSERT_EQ('"', c0_);
  // Advance past the last '"'.
  AdvanceSkipWhitespace();
  return seq_str;
}


template <typename StringType>
template <bool is_symbol>
Handle<String> JsonParser<StringType>::ScanJsonString() {
  ASSERT_EQ('"', c0_);
  Advance();
  if (c0_ == '"') {
    AdvanceSkipWhitespace();
    return Handle<String>(isolate()->heap()->empty_string());
  }
  int beg_pos = position_;
  // Fast case for ASCII only without escape characters.
  do {
    // Check for control character (0x00-0x1f) or unterminated string (<0).
    if (c0_ < 0x20) return Handle<String>::null();
    if (c0_ != '\\') {
      if (c0_ <= kMaxAsciiCharCode) {
        Advance();
      } else {
        return SlowScanJsonString<SeqTwoByteString>(source_,
                                                    beg_pos,
                                                    position_);
      }
    } else {
      return SlowScanJsonString<SeqAsciiString>(source_,
                                                beg_pos,
                                                position_);
    }
  } while (c0_ != '"');
  int length = position_ - beg_pos;
  Handle<String> result;
  if (is_symbol && source_->IsSeqAsciiString()) {
    result = isolate()->factory()->LookupAsciiSymbol(
         Handle<SeqAsciiString>::cast(source_), beg_pos, length);
  } else {
    result = isolate()->factory()->NewRawAsciiString(length);
    char* dest = SeqAsciiString::cast(*result)->GetChars();
    String::WriteToFlat(*source_, dest, beg_pos, position_);
  }
  ASSERT_EQ('"', c0_);
  // Advance past the last '"'.
  AdvanceSkipWhitespace();
  return result;
}


template <typename StringType>
void JsonParser<StringType>::InitializeSource() { }


template <>
void JsonParser<ExternalAsciiString>::InitializeSource() {
  characters_ = source_->resource()->data();
}


template <>
void JsonParser<ExternalTwoByteString>::InitializeSource() {
  characters_ = source_->resource()->data();
}


template <>
uc32 JsonParser<SeqAsciiString>::GetChar(int pos) {
  return static_cast<uc32>(source_->SeqAsciiStringGet(pos));
}


template <>
uc32 JsonParser<SeqTwoByteString>::GetChar(int pos) {
  return static_cast<uc32>(source_->SeqTwoByteStringGet(pos));
}


template <>
uc32 JsonParser<ExternalAsciiString>::GetChar(int pos) {
  ASSERT(pos >= 0);
  ASSERT(pos < source_length_);
  return static_cast<uc32>(characters_[pos]);
}


template <>
uc32 JsonParser<ExternalTwoByteString>::GetChar(int pos) {
  ASSERT(pos >= 0);
  ASSERT(pos < source_length_);
  return static_cast<uc32>(characters_[pos]);
}


template <>
const char* JsonParser<SeqAsciiString>::GetChars() {
  return source_->GetChars();
}


template <>
const uc16* JsonParser<SeqTwoByteString>::GetChars() {
  return source_->GetChars();
}


template <>
const char* JsonParser<ExternalAsciiString>::GetChars() {
  return characters_;
}


template <>
const uc16* JsonParser<ExternalTwoByteString>::GetChars() {
  return characters_;
}

} }  // namespace v8::internal

#endif  // V8_JSON_PARSER_H_
