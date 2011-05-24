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

#include "v8.h"

#include "char-predicates-inl.h"
#include "conversions.h"
#include "json-parser.h"
#include "messages.h"
#include "spaces.h"

namespace v8 {
namespace internal {


Handle<Object> JsonParser::ParseJson(Handle<String> source) {
  isolate_ = source->map()->isolate();
  source_ = Handle<String>(source->TryFlattenGetString());
  source_length_ = source_->length() - 1;

  // Optimized fast case where we only have ascii characters.
  if (source_->IsSeqAsciiString()) {
      is_sequential_ascii_ = true;
      seq_source_ = Handle<SeqAsciiString>::cast(source_);
  } else {
    is_sequential_ascii_ = false;
  }

  // Set initial position right before the string.
  position_ = -1;
  // Advance to the first character (posibly EOS)
  Advance();
  Next();
  Handle<Object> result = ParseJsonValue();
  if (result.is_null() || Next() != Token::EOS) {
    // Parse failed. Scanner's current token is the unexpected token.
    Token::Value token = current_.token;

    const char* message;
    const char* name_opt = NULL;

    switch (token) {
      case Token::EOS:
        message = "unexpected_eos";
        break;
      case Token::NUMBER:
        message = "unexpected_token_number";
        break;
      case Token::STRING:
        message = "unexpected_token_string";
        break;
      case Token::IDENTIFIER:
      case Token::FUTURE_RESERVED_WORD:
        message = "unexpected_token_identifier";
        break;
      default:
        message = "unexpected_token";
        name_opt = Token::String(token);
        ASSERT(name_opt != NULL);
        break;
    }

    Factory* factory = isolate()->factory();
    MessageLocation location(factory->NewScript(source),
                             current_.beg_pos,
                             current_.end_pos);
    Handle<JSArray> array;
    if (name_opt == NULL) {
      array = factory->NewJSArray(0);
    } else {
      Handle<String> name = factory->NewStringFromUtf8(CStrVector(name_opt));
      Handle<FixedArray> element = factory->NewFixedArray(1);
      element->set(0, *name);
      array = factory->NewJSArrayWithElements(element);
    }
    Handle<Object> result = factory->NewSyntaxError(message, array);
    isolate()->Throw(*result, &location);
    return Handle<Object>::null();
  }
  return result;
}


// Parse any JSON value.
Handle<Object> JsonParser::ParseJsonValue() {
  Token::Value token = Next();
  switch (token) {
    case Token::STRING:
      return GetString(false);
    case Token::NUMBER:
      return isolate()->factory()->NewNumber(number_);
    case Token::FALSE_LITERAL:
      return isolate()->factory()->false_value();
    case Token::TRUE_LITERAL:
      return isolate()->factory()->true_value();
    case Token::NULL_LITERAL:
      return isolate()->factory()->null_value();
    case Token::LBRACE:
      return ParseJsonObject();
    case Token::LBRACK:
      return ParseJsonArray();
    default:
      return ReportUnexpectedToken();
  }
}


// Parse a JSON object. Scanner must be right after '{' token.
Handle<Object> JsonParser::ParseJsonObject() {
  Handle<JSFunction> object_constructor(
      isolate()->global_context()->object_function());
  Handle<JSObject> json_object =
      isolate()->factory()->NewJSObject(object_constructor);

  if (Peek() == Token::RBRACE) {
    Next();
  } else {
    do {
      if (Next() != Token::STRING) {
        return ReportUnexpectedToken();
      }
      Handle<String> key = GetString(true);
      if (Next() != Token::COLON) {
        return ReportUnexpectedToken();
      }

      Handle<Object> value = ParseJsonValue();
      if (value.is_null()) return Handle<Object>::null();

      uint32_t index;
      if (key->AsArrayIndex(&index)) {
        SetOwnElement(json_object, index, value, kNonStrictMode);
      } else if (key->Equals(isolate()->heap()->Proto_symbol())) {
        SetPrototype(json_object, value);
      } else {
        SetLocalPropertyIgnoreAttributes(json_object, key, value, NONE);
      }
    } while (Next() == Token::COMMA);
    if (current_.token != Token::RBRACE) {
      return ReportUnexpectedToken();
    }
  }
  return json_object;
}

// Parse a JSON array. Scanner must be right after '[' token.
Handle<Object> JsonParser::ParseJsonArray() {
  ZoneScope zone_scope(isolate(), DELETE_ON_EXIT);
  ZoneList<Handle<Object> > elements(4);

  Token::Value token = Peek();
  if (token == Token::RBRACK) {
    Next();
  } else {
    do {
      Handle<Object> element = ParseJsonValue();
      if (element.is_null()) return Handle<Object>::null();
      elements.Add(element);
      token = Next();
    } while (token == Token::COMMA);
    if (token != Token::RBRACK) {
      return ReportUnexpectedToken();
    }
  }

  // Allocate a fixed array with all the elements.
  Handle<FixedArray> fast_elements =
      isolate()->factory()->NewFixedArray(elements.length());

  for (int i = 0, n = elements.length(); i < n; i++) {
    fast_elements->set(i, *elements[i]);
  }

  return isolate()->factory()->NewJSArrayWithElements(fast_elements);
}


Token::Value JsonParser::Next() {
  current_ = next_;
  ScanJson();
  return current_.token;
}

void JsonParser::ScanJson() {
  if (source_->IsSeqAsciiString()) {
    is_sequential_ascii_ = true;
  } else {
    is_sequential_ascii_ = false;
  }

  Token::Value token;
  do {
    // Remember the position of the next token
    next_.beg_pos = position_;
    switch (c0_) {
      case '\t':
      case '\r':
      case '\n':
      case ' ':
        Advance();
        token = Token::WHITESPACE;
        break;
      case '{':
        Advance();
        token = Token::LBRACE;
        break;
      case '}':
        Advance();
        token = Token::RBRACE;
        break;
      case '[':
        Advance();
        token = Token::LBRACK;
        break;
      case ']':
        Advance();
        token = Token::RBRACK;
        break;
      case ':':
        Advance();
        token = Token::COLON;
        break;
      case ',':
        Advance();
        token = Token::COMMA;
        break;
      case '"':
        token = ScanJsonString();
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
        token = ScanJsonNumber();
        break;
      case 't':
        token = ScanJsonIdentifier("true", Token::TRUE_LITERAL);
        break;
      case 'f':
        token = ScanJsonIdentifier("false", Token::FALSE_LITERAL);
        break;
      case 'n':
        token = ScanJsonIdentifier("null", Token::NULL_LITERAL);
        break;
      default:
        if (c0_ < 0) {
          Advance();
          token = Token::EOS;
        } else {
          Advance();
          token = Token::ILLEGAL;
        }
    }
  } while (token == Token::WHITESPACE);

  next_.end_pos = position_;
  next_.token = token;
}


Token::Value JsonParser::ScanJsonIdentifier(const char* text,
                                            Token::Value token) {
  while (*text != '\0') {
    if (c0_ != *text) return Token::ILLEGAL;
    Advance();
    text++;
  }
  return token;
}


Token::Value JsonParser::ScanJsonNumber() {
  bool negative = false;

  if (c0_ == '-') {
    Advance();
    negative = true;
  }
  if (c0_ == '0') {
    Advance();
    // Prefix zero is only allowed if it's the only digit before
    // a decimal point or exponent.
    if ('0' <= c0_ && c0_ <= '9') return Token::ILLEGAL;
  } else {
    int i = 0;
    int digits = 0;
    if (c0_ < '1' || c0_ > '9') return Token::ILLEGAL;
    do {
      i = i * 10 + c0_ - '0';
      digits++;
      Advance();
    } while (c0_ >= '0' && c0_ <= '9');
    if (c0_ != '.' && c0_ != 'e' && c0_ != 'E' && digits < 10) {
      number_ = (negative ? -i : i);
      return Token::NUMBER;
    }
  }
  if (c0_ == '.') {
    Advance();
    if (c0_ < '0' || c0_ > '9') return Token::ILLEGAL;
    do {
      Advance();
    } while (c0_ >= '0' && c0_ <= '9');
  }
  if (AsciiAlphaToLower(c0_) == 'e') {
    Advance();
    if (c0_ == '-' || c0_ == '+') Advance();
    if (c0_ < '0' || c0_ > '9') return Token::ILLEGAL;
    do {
      Advance();
    } while (c0_ >= '0' && c0_ <= '9');
  }
  if (is_sequential_ascii_) {
    Vector<const char> chars(seq_source_->GetChars() +  next_.beg_pos,
                             position_ - next_.beg_pos);
    number_ = StringToDouble(isolate()->unicode_cache(),
                             chars,
                             NO_FLAGS,  // Hex, octal or trailing junk.
                             OS::nan_value());
  } else {
    Vector<char> buffer = Vector<char>::New(position_ - next_.beg_pos);
    String::WriteToFlat(*source_, buffer.start(), next_.beg_pos, position_);
    Vector<const char> result =
        Vector<const char>(reinterpret_cast<const char*>(buffer.start()),
        position_ - next_.beg_pos);
    number_ = StringToDouble(isolate()->unicode_cache(),
                             result,
                             NO_FLAGS,  // Hex, octal or trailing junk.
                             0.0);
    buffer.Dispose();
  }
  return Token::NUMBER;
}

Token::Value JsonParser::SlowScanJsonString() {
  // The currently scanned ascii characters.
  Handle<String> ascii(isolate()->factory()->NewSubString(source_,
                                                          next_.beg_pos + 1,
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
      allocation_count++;
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
    if (c0_ < 0x20) return Token::ILLEGAL;
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
              return Token::ILLEGAL;
            }
            value = value * 16 + digit;
          }
          seq_two_byte->SeqTwoByteStringSet(count++, value);
          break;
        }
        default:
          return Token::ILLEGAL;
      }
      Advance();
    }
  }
  // Advance past the last '"'.
  ASSERT_EQ('"', c0_);
  Advance();

  // Shrink the the string to our length.
  isolate()->heap()->
      new_space()->
      ShrinkStringAtAllocationBoundary<SeqTwoByteString>(*seq_two_byte,
                                                         count);
  string_val_ = isolate()->factory()->NewConsString(ascii, seq_two_byte);
  return Token::STRING;
}


Token::Value JsonParser::ScanJsonString() {
  ASSERT_EQ('"', c0_);
  // Set string_val to null. If string_val is not set we assume an
  // ascii string begining at next_.beg_pos + 1 to next_.end_pos - 1.
  string_val_ = Handle<String>::null();
  Advance();
  // Fast case for ascii only without escape characters.
  while (c0_ != '"') {
    // Check for control character (0x00-0x1f) or unterminated string (<0).
    if (c0_ < 0x20) return Token::ILLEGAL;
    if (c0_ != '\\' && c0_ < kMaxAsciiCharCode) {
      Advance();
    } else {
      return SlowScanJsonString();
    }
  }
  ASSERT_EQ('"', c0_);
  // Advance past the last '"'.
  Advance();
  return Token::STRING;
}

Handle<String> JsonParser::GetString() {
  return GetString(false);
}

Handle<String> JsonParser::GetSymbol() {
  Handle<String> result = GetString(true);
  if (result->IsSymbol()) return result;
  return isolate()->factory()->LookupSymbol(result);
}

Handle<String> JsonParser::GetString(bool hint_symbol) {
  // We have a non ascii string, return that.
  if (!string_val_.is_null()) return string_val_;

  if (is_sequential_ascii_ && hint_symbol) {
    Handle<SeqAsciiString> seq = Handle<SeqAsciiString>::cast(source_);
    // The current token includes the '"' in both ends.
    int length = current_.end_pos - current_.beg_pos - 2;
    return isolate()->factory()->LookupAsciiSymbol(seq_source_,
                                                   current_.beg_pos + 1,
                                                   length);
  }
  // The current token includes the '"' in both ends.
  return  isolate()->factory()->NewSubString(
      source_, current_.beg_pos + 1, current_.end_pos - 1);
}

} }  // namespace v8::internal
