// Copyright 2013 the V8 project authors. All rights reserved.
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

#ifndef V8_LEXER_EXPERIMENTAL_SCANNER_H
#define V8_LEXER_EXPERIMENTAL_SCANNER_H

#include <set>

#include "compiler.h"
#include "isolate.h"
#include "scanner.h"  // UnicodeCache.
#include "token.h"
#include "utils.h"
#include "v8stdint.h"
#include "char-predicates-inl.h"

namespace v8 {
namespace internal {

class UnicodeCache;

// Base class for scanners for different encodings. The meat is the pure virtual
// Scan() which each of them specializes.
class ScannerBase {
 public:
  struct Location {
    Location(int b, int e) : beg_pos(b), end_pos(e) { }
    Location() : beg_pos(0), end_pos(0) { }

    bool IsValid() const {
      return beg_pos >= 0 && end_pos >= beg_pos;
    }

    static Location invalid() { return Location(-1, -1); }

    int beg_pos;
    int end_pos;
  };

  explicit ScannerBase(Isolate* isolate)
    : isolate_(isolate),
      unicode_cache_(isolate->unicode_cache()),
      has_line_terminator_before_next_(true),
      current_literal_(&literals_[0]),
      next_literal_(&literals_[1]),
      harmony_numeric_literals_(false),
      harmony_modules_(false),
      harmony_scoping_(false) {
    if (!scanners_) {
      scanners_ = new std::set<ScannerBase*>();
      isolate->heap()->AddGCEpilogueCallback(&ScannerBase::UpdateBuffersAfterGC,
                                             kGCTypeAll, false);
    }
    scanners_->insert(this);
  }

  virtual ~ScannerBase() {
    scanners_->erase(this);
    if (scanners_->empty()) {
      isolate_->heap()->RemoveGCEpilogueCallback(
          &ScannerBase::UpdateBuffersAfterGC);
      delete scanners_;
      scanners_ = NULL;
    }
  }

  // Returns the next token and advances input.
  Token::Value Next() {
    has_line_terminator_before_next_ = false;
    current_ = next_;
    std::swap(current_literal_, next_literal_);
    Scan();  // Virtual! Will fill in next_.
    return current_.token;
  }

  // Returns the current token again.
  Token::Value current_token() { return current_.token; }

  // Returns the location information for the current token
  // (the token last returned by Next()).
  Location location() {
    return Location(current_.beg_pos, current_.end_pos);
  }

  // One token look-ahead (past the token returned by Next()).
  Token::Value peek() const { return next_.token; }

  Location peek_location() const {
    return Location(next_.beg_pos, next_.end_pos);
  }

  UnicodeCache* unicode_cache() { return unicode_cache_; }

  bool HarmonyScoping() const {
    return harmony_scoping_;
  }
  void SetHarmonyScoping(bool scoping) {
    harmony_scoping_ = scoping;
  }
  bool HarmonyModules() const {
    return harmony_modules_;
  }
  void SetHarmonyModules(bool modules) {
    harmony_modules_ = modules;
  }
  bool HarmonyNumericLiterals() const {
    return harmony_numeric_literals_;
  }
  void SetHarmonyNumericLiterals(bool numeric_literals) {
    harmony_numeric_literals_ = numeric_literals;
  }

  // Returns true if there was a line terminator before the peek'ed token,
  // possibly inside a multi-line comment.
  bool HasAnyLineTerminatorBeforeNext() const {
    return has_line_terminator_before_next_;
    // FIXME: do we need to distinguish between newlines inside and outside
    // multiline comments? Atm doesn't look like we need to.
  }

  Vector<const char> literal_ascii_string() {
    if (!current_literal_->Valid(current_.beg_pos)) {
      FillLiteral(current_, current_literal_);
    }
    return current_literal_->ascii_string;
  }

  Vector<const uc16> literal_utf16_string() {
    if (!current_literal_->Valid(current_.beg_pos)) {
      FillLiteral(current_, current_literal_);
    }
    return current_literal_->utf16_string;
  }

  int literal_length() {
    if (!current_literal_->Valid(current_.beg_pos)) {
      FillLiteral(current_, current_literal_);
    }
    return current_literal_->length;
  }

  bool is_literal_ascii() {
    if (!current_literal_->Valid(current_.beg_pos)) {
      FillLiteral(current_, current_literal_);
    }
    return current_literal_->is_ascii;
  }

  bool is_literal_contextual_keyword(Vector<const char> keyword) {
    if (!is_literal_ascii()) return false;
    Vector<const char> literal = literal_ascii_string();
    return literal.length() == keyword.length() &&
        (memcmp(literal.start(), keyword.start(), literal.length()) == 0);
  }

  bool literal_contains_escapes() const {
    return current_.has_escapes;
  }

  Vector<const char> next_literal_ascii_string() {
    if (!next_literal_->Valid(next_.beg_pos)) {
      FillLiteral(next_, next_literal_);
    }
    return next_literal_->ascii_string;
  }

  Vector<const uc16> next_literal_utf16_string() {
    if (!next_literal_->Valid(next_.beg_pos)) {
      FillLiteral(next_, next_literal_);
    }
    return next_literal_->utf16_string;
  }

  int next_literal_length() {
    if (!next_literal_->Valid(next_.beg_pos)) {
      FillLiteral(next_, next_literal_);
    }
    return next_literal_->length;
  }

  bool is_next_literal_ascii() {
    if (!next_literal_->Valid(next_.beg_pos)) {
      FillLiteral(next_, next_literal_);
    }
    return next_literal_->is_ascii;
  }

  bool is_next_contextual_keyword(Vector<const char> keyword) {
    if (!is_next_literal_ascii()) return false;
    Vector<const char> literal = next_literal_ascii_string();
    return literal.length() == keyword.length() &&
        (memcmp(literal.start(), keyword.start(), literal.length()) == 0);
  }

  // Seek forward to the given position. This operation works for simple cases
  // such as seeking forward until simple delimiter tokens, which is what it is
  // used for. After this call, we will have the token at the given position as
  // the "next" token. The "current" token will be invalid. FIXME: for utf-8,
  // we need to decide if pos is counted in characters or in bytes.
  virtual void SeekForward(int pos) = 0;
  virtual void SetEnd(int pos) = 0;

  // Scans the input as a regular expression pattern, previous character(s) must
  // be /(=). Returns true if a pattern is scanned. FIXME: this won't work for
  // utf-8 newlines.
  virtual bool ScanRegExpPattern(bool seen_equal) = 0;
  // Returns true if regexp flags are scanned (always since flags can
  // be empty).
  virtual bool ScanRegExpFlags() = 0;

  // Returns the location of the last seen octal literal.
  virtual Location octal_position() const = 0;
  virtual void clear_octal_position() = 0;

 protected:
  struct TokenDesc {
    Token::Value token;
    int beg_pos;
    int end_pos;
    bool has_escapes;
  };

  struct LiteralDesc {
    int beg_pos;
    bool is_ascii;
    int length;
    Vector<const char> ascii_string;
    Vector<const uc16> utf16_string;
    LiteralBuffer buffer;
    bool Valid(int pos) { return beg_pos == pos; }
  };

  virtual void Scan() = 0;
  virtual void SetBufferBasedOnHandle() = 0;

  static void UpdateBuffersAfterGC(v8::Isolate*, GCType, GCCallbackFlags);
  virtual bool FillLiteral(const TokenDesc& token, LiteralDesc* literal) = 0;

  Isolate* isolate_;
  UnicodeCache* unicode_cache_;

  bool has_line_terminator_before_next_;

  TokenDesc current_;  // desc for current token (as returned by Next())
  TokenDesc next_;     // desc for next token (one token look-ahead)

  LiteralDesc* current_literal_;
  LiteralDesc* next_literal_;
  LiteralDesc literals_[2];

  bool harmony_numeric_literals_;
  bool harmony_modules_;
  bool harmony_scoping_;

 private:
  static std::set<ScannerBase*>* scanners_;
};


template<typename Char>
class ExperimentalScanner : public ScannerBase {
 public:
  explicit ExperimentalScanner(
      Handle<String> source,
      Isolate* isolate)
      : ScannerBase(isolate),
        source_handle_(source),
        buffer_(NULL),
        buffer_end_(NULL),
        start_(NULL),
        cursor_(NULL),
        marker_(NULL),
        last_octal_end_(NULL) {
    ASSERT(source->IsFlat());
    SetBufferBasedOnHandle();
    Scan();
  }

  virtual ~ExperimentalScanner() { }

  virtual void SeekForward(int pos);
  virtual void SetEnd(int pos);
  virtual bool ScanRegExpPattern(bool seen_equal);
  virtual bool ScanRegExpFlags();
  virtual Location octal_position() const;
  virtual void clear_octal_position() {
    last_octal_end_ = NULL;
  }

 protected:
  virtual void Scan();

  virtual void SetBufferBasedOnHandle() {
    // We get a raw pointer from the Handle, but we also update it every time
    // there is a GC, so it is safe.
    DisallowHeapAllocation no_gc;
    const Char* new_buffer = GetNewBufferBasedOnHandle();
    if (new_buffer != buffer_) {
      int start_offset = start_ - buffer_;
      int cursor_offset = cursor_ - buffer_;
      int marker_offset = marker_ - buffer_;
      buffer_ = new_buffer;
      buffer_end_ = buffer_ + source_handle_->length();
      start_ = buffer_ + start_offset;
      cursor_ = buffer_ + cursor_offset;
      marker_ = buffer_ + marker_offset;
    }
  }

  const Char* GetNewBufferBasedOnHandle() const;

  virtual bool FillLiteral(const TokenDesc& token, LiteralDesc* literal);

 private:
  bool ValidIdentifierPart() {
      return unicode_cache_->IsIdentifierPart(ScanHexNumber(4));
  }

  bool ValidIdentifierStart() {
    return unicode_cache_->IsIdentifierStart(ScanHexNumber(4));
  }

  uc32 ScanHexNumber(int length);
  bool ScanLiteralUnicodeEscape();

  const Char* ScanHexNumber(const Char* start,
                            const Char* end,
                            uc32* result);
  const Char* ScanOctalEscape(const Char* start,
                              const Char* end,
                              uc32* result);
  const Char* ScanIdentifierUnicodeEscape(const Char* start,
                                          const Char* end,
                                          uc32* result);
  const Char* ScanEscape(const Char* start,
                         const Char* end,
                         LiteralBuffer* literal);

  Handle<String> source_handle_;
  const Char* buffer_;
  const Char* buffer_end_;
  const Char* start_;
  const Char* cursor_;
  const Char* marker_;

  // Where we have seen the last octal number or an octal escape inside a
  // string. Used by octal_position().
  const Char* last_octal_end_;
};


template<typename Char>
void ExperimentalScanner<Char>::SeekForward(int pos) {
  cursor_ = buffer_ + pos;
  start_ = cursor_;
  marker_ = cursor_;
  has_line_terminator_before_next_ = false;
  Scan();  // Fills in next_.
}


template<typename Char>
void ExperimentalScanner<Char>::SetEnd(int pos) {
  buffer_end_ = buffer_ + pos;
}


template<typename Char>
bool ExperimentalScanner<Char>::ScanRegExpPattern(bool seen_equal) {
  // Scan: ('/' | '/=') RegularExpressionBody '/' RegularExpressionFlags
  bool in_character_class = false;

  // Previous token is either '/' or '/=', in the second case, the
  // pattern starts at =.
  next_.beg_pos = (cursor_ - buffer_) - (seen_equal ? 2 : 1);
  next_.end_pos = (cursor_ - buffer_) - (seen_equal ? 1 : 0);

  // Scan regular expression body: According to ECMA-262, 3rd, 7.8.5,
  // the scanner should pass uninterpreted bodies to the RegExp
  // constructor.
  if (cursor_ >= buffer_end_) return false;

  while (*cursor_ != '/' || in_character_class) {
    if (unicode_cache_->IsLineTerminator(*cursor_)) return false;
    if (*cursor_ == '\\') {  // Escape sequence.
      ++cursor_;
      if (cursor_ >= buffer_end_ || unicode_cache_->IsLineTerminator(*cursor_))
        return false;
      ++cursor_;
      if (cursor_ >= buffer_end_) return false;
      // If the escape allows more characters, i.e., \x??, \u????, or \c?,
      // only "safe" characters are allowed (letters, digits, underscore),
      // otherwise the escape isn't valid and the invalid character has
      // its normal meaning. I.e., we can just continue scanning without
      // worrying whether the following characters are part of the escape
      // or not, since any '/', '\\' or '[' is guaranteed to not be part
      // of the escape sequence.

      // TODO(896): At some point, parse RegExps more throughly to capture
      // octal esacpes in strict mode.
    } else {  // Unescaped character.
      if (*cursor_ == '[') in_character_class = true;
      if (*cursor_ == ']') in_character_class = false;
      if (++cursor_ >= buffer_end_) return false;
    }
  }
  ++cursor_;  // consume '/'
  return true;
}


template<typename Char>
bool ExperimentalScanner<Char>::ScanRegExpFlags() {
  // Scan regular expression flags.
  while (cursor_ < buffer_end_ && unicode_cache_->IsIdentifierPart(*cursor_)) {
    if (*cursor_ != '\\') {
      if (++cursor_ >= buffer_end_) break;
    } else {
      if (!ScanLiteralUnicodeEscape()) break;
      if (++cursor_ >= buffer_end_) break;
    }
  }
  next_.end_pos = cursor_ - buffer_ - 1;
  return true;
}


template<typename Char>
uc32 ExperimentalScanner<Char>::ScanHexNumber(int length) {
  // We have seen \uXXXX, let's see what it is.
  uc32 x = 0;
  for (const Char* s = cursor_ - length; s != cursor_; ++s) {
    int d = HexValue(*s);
    if (d < 0) {
      return -1;
    }
    x = x * 16 + d;
  }
  return x;
}


template<typename Char>
const Char* ExperimentalScanner<Char>::ScanHexNumber(
    const Char* cursor, const Char* end, uc32* result) {
  uc32 x = 0;
  for ( ; cursor < end; ++cursor) {
    int d = HexValue(*cursor);
    if (d < 0) {
      *result = -1;
      return NULL;
    }
    x = x * 16 + d;
  }
  *result = x;
  return cursor;
}


// Octal escapes of the forms '\0xx' and '\xxx' are not a part of
// ECMA-262. Other JS VMs support them.
template<typename Char>
const Char* ExperimentalScanner<Char>::ScanOctalEscape(
    const Char* start, const Char* end, uc32* result) {
  uc32 x = *result - '0';
  const Char* cursor;
  for (cursor = start; cursor < end; cursor++) {
    int d = *cursor - '0';
    if (d < 0 || d > 7) break;
    int nx = x * 8 + d;
    if (nx >= 256) break;
    x = nx;
  }
  *result = x;
  return cursor;
}


template<typename Char>
bool ExperimentalScanner<Char>::ScanLiteralUnicodeEscape() {
  ASSERT(cursor_ < buffer_end_);
  Char primary_char = *(cursor_);
  ASSERT(primary_char == '\\');
  if (++cursor_ >= buffer_end_) return false;
  primary_char = *(cursor_);
  int i = 1;
  if (primary_char == 'u') {
    i++;
    while (i < 6) {
      if (++cursor_ >= buffer_end_) return false;
      primary_char = *(cursor_);
      if (!IsHexDigit(primary_char)) break;
      i++;
    }
  }
  return i == 6;
}


template<typename Char>
const Char* ExperimentalScanner<Char>::ScanIdentifierUnicodeEscape(
    const Char* cursor, const Char* end, uc32* result) {
  ASSERT(*cursor == '\\');
  if (++cursor >= end) return NULL;
  if (*cursor != 'u') return NULL;
  ++cursor;
  if (cursor + 4 > end) return NULL;
  cursor = ScanHexNumber(cursor, cursor + 4, result);
  return cursor;
}


template<typename Char>
const Char* ExperimentalScanner<Char>::ScanEscape(
    const Char* cursor, const Char* end, LiteralBuffer* literal) {
  ASSERT(*cursor == '\\');
  if (++cursor >= end) return NULL;
  uc32 c = *cursor;
  if (++cursor > end) return NULL;
  // Skip escaped newlines.
  if (unicode_cache_->IsLineTerminator(c)) {
    uc32 peek = *cursor;
    // Allow CR+LF newlines in multiline string literals.
    if (IsCarriageReturn(c) && IsLineFeed(peek)) cursor++;
    // Allow LF+CR newlines in multiline string literals.
    if (IsLineFeed(c) && IsCarriageReturn(peek)) cursor++;
    return cursor;
  }

  switch (c) {
    case '\'':  // fall through
    case '"' :  // fall through
    case '\\': break;
    case 'b' : c = '\b'; break;
    case 'f' : c = '\f'; break;
    case 'n' : c = '\n'; break;
    case 'r' : c = '\r'; break;
    case 't' : c = '\t'; break;
    case 'u' : {
      ASSERT(cursor + 4 <= end);
      cursor = ScanHexNumber(cursor, cursor + 4, &c);
      if (cursor == NULL) return NULL;
      break;
    }
    case 'v' : c = '\v'; break;
    case 'x' : {
      ASSERT(cursor + 2 <= end);
      cursor = ScanHexNumber(cursor, cursor + 2, &c);
      if (cursor == NULL) return NULL;
      break;
    }
    case '0' :  // fall through
    case '1' :  // fall through
    case '2' :  // fall through
    case '3' :  // fall through
    case '4' :  // fall through
    case '5' :  // fall through
    case '6' :  // fall through
    case '7' :
      if (end > cursor + 2) end = cursor + 2;
      cursor = ScanOctalEscape(cursor, end, &c); break;
  }

  // According to ECMA-262, section 7.8.4, characters not covered by the
  // above cases should be illegal, but they are commonly handled as
  // non-escaped characters by JS VMs.
  literal->AddChar(c);
  return cursor;
}

template<typename Char>
ScannerBase::Location ExperimentalScanner<Char>::octal_position() const {
  if (!last_octal_end_)
    return Location::invalid();
  // The last octal might be an octal escape or an octal number. Whichever it
  // is, we'll find the start by just scanning back until we hit a non-octal
  // character.
  const Char* temp_cursor = last_octal_end_ - 1;
  while (temp_cursor >= buffer_ && *temp_cursor >= '0' && *temp_cursor <= '7')
    --temp_cursor;
  return Location(temp_cursor - buffer_ + 1, last_octal_end_ - buffer_);
}

} }

#endif  // V8_LEXER_EXPERIMENTAL_SCANNER_H
