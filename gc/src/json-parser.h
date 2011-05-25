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

#include "token.h"

namespace v8 {
namespace internal {

// A simple json parser.
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
    if (position_ >= source_length_) {
      position_++;
      c0_ = kEndOfString;
    } else if (is_sequential_ascii_) {
      position_++;
      c0_ = seq_source_->SeqAsciiStringGet(position_);
    } else {
      position_++;
      c0_ = source_->Get(position_);
    }
  }

  inline Isolate* isolate() { return isolate_; }

  // Get the string for the current string token.
  Handle<String> GetString(bool hint_symbol);
  Handle<String> GetString();
  Handle<String> GetSymbol();

  // Scan a single JSON token. The JSON lexical grammar is specified in the
  // ECMAScript 5 standard, section 15.12.1.1.
  // Recognizes all of the single-character tokens directly, or calls a function
  // to scan a number, string or identifier literal.
  // The only allowed whitespace characters between tokens are tab,
  // carriage-return, newline and space.
  void ScanJson();

  // A JSON string (production JSONString) is subset of valid JavaScript string
  // literals. The string must only be double-quoted (not single-quoted), and
  // the only allowed backslash-escapes are ", /, \, b, f, n, r, t and
  // four-digit hex escapes (uXXXX). Any other use of backslashes is invalid.
  Token::Value ScanJsonString();
  // Slow version for unicode support, uses the first ascii_count characters,
  // as first part of a ConsString
  Token::Value SlowScanJsonString();

  // A JSON number (production JSONNumber) is a subset of the valid JavaScript
  // decimal number literals.
  // It includes an optional minus sign, must have at least one
  // digit before and after a decimal point, may not have prefixed zeros (unless
  // the integer part is zero), and may include an exponent part (e.g., "e-10").
  // Hexadecimal and octal numbers are not allowed.
  Token::Value ScanJsonNumber();

  // Used to recognizes one of the literals "true", "false", or "null". These
  // are the only valid JSON identifiers (productions JSONBooleanLiteral,
  // JSONNullLiteral).
  Token::Value ScanJsonIdentifier(const char* text, Token::Value token);

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
  Handle<Object> ReportUnexpectedToken() { return Handle<Object>::null(); }

  // Peek at the next token.
  Token::Value Peek() { return next_.token; }
  // Scan the next token and return the token scanned on the last call.
  Token::Value Next();

  struct TokenInfo {
    TokenInfo() : token(Token::ILLEGAL),
                  beg_pos(0),
                  end_pos(0) { }
    Token::Value token;
    int beg_pos;
    int end_pos;
  };

  static const int kInitialSpecialStringSize = 100;


 private:
  Handle<String> source_;
  int source_length_;
  Handle<SeqAsciiString> seq_source_;

  bool is_sequential_ascii_;
  // Current and next token
  TokenInfo current_;
  TokenInfo next_;
  Isolate* isolate_;
  uc32 c0_;
  int position_;


  Handle<String> string_val_;
  double number_;
};

} }  // namespace v8::internal

#endif  // V8_JSON_PARSER_H_
