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
#include "handles.h"
#include "scanner.h"

namespace v8 {
namespace internal {

class LexerBase;

class LexerGCHandler {
 public:
  explicit LexerGCHandler(Isolate* isolate) : isolate_(isolate) {}
  void AddLexer(LexerBase* lexer);
  void RemoveLexer(LexerBase* lexer);
  void UpdateLexersAfterGC();

 private:
  typedef std::set<LexerBase*> LexerSet;
  Isolate* isolate_;
  LexerSet lexers_;
};


class LexerBase {
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

  explicit LexerBase(UnicodeCache* unicode_cache);

  virtual ~LexerBase();

  // Returns the next token and advances input.
  Token::Value Next();

  // Returns the current token again.
  Token::Value current_token() const { return current_.token; }

  // Returns the location information for the current token
  // (the token last returned by Next()).
  Location location() const {
    return Location(current_.beg_pos, current_.end_pos);
  }

  // One token look-ahead (past the token returned by Next()).
  Token::Value peek() const { return next_.token; }

  Location peek_location() const {
    return Location(next_.beg_pos, next_.end_pos);
  }

  // Seek forward to the given position. This operation works for simple cases
  // such as seeking forward until simple delimiter tokens, which is what it is
  // used for. After this call, we will have the token at the given position as
  // the "next" token. The "current" token will be invalid. FIXME: for utf-8,
  // we need to decide if pos is counted in characters or in bytes.
  virtual void SeekForward(int pos) = 0;

  // Scans the input as a regular expression pattern, previous character(s) must
  // be /(=). Returns true if a pattern is scanned. FIXME: this won't work for
  // utf-8 newlines.
  virtual bool ScanRegExpPattern(bool seen_equal) = 0;

  // Returns true if regexp flags are scanned (always since flags can
  // be empty).
  virtual bool ScanRegExpFlags() = 0;

  // // Returns the location of the last seen octal literal.
  virtual Location octal_position() const = 0;

  virtual void clear_octal_position() = 0;

  // Returns true if there was a line terminator before the peek'ed token,
  // possibly inside a multi-line comment.
  bool HasAnyLineTerminatorBeforeNext() const {
    return has_line_terminator_before_next_ ||
           has_multiline_comment_before_next_;
  }

  Vector<const uint8_t> literal_one_byte_string() {
    EnsureCurrentLiteralIsValid();
    return current_literal_->one_byte_string;
  }

  Vector<const uint16_t> literal_two_byte_string() {
    EnsureCurrentLiteralIsValid();
    return current_literal_->two_byte_string;
  }

  int literal_length() {
    EnsureCurrentLiteralIsValid();
    return current_literal_->length;
  }

  bool is_literal_one_byte() {
    EnsureCurrentLiteralIsValid();
    return current_literal_->is_one_byte;
  }

  bool is_literal_contextual_keyword(Vector<const uint8_t> keyword) {
    if (!is_literal_one_byte()) return false;
    Vector<const uint8_t> literal = literal_one_byte_string();
    return literal.length() == keyword.length() &&
        (memcmp(literal.start(), keyword.start(), literal.length()) == 0);
  }

  bool literal_contains_escapes() const {
    return current_.has_escapes;
  }

  Vector<const uint8_t> next_literal_one_byte_string() {
    EnsureNextLiteralIsValid();
    return next_literal_->one_byte_string;
  }

  Vector<const uint16_t> next_literal_two_byte_string() {
    EnsureNextLiteralIsValid();
    return next_literal_->two_byte_string;
  }

  int next_literal_length() {
    EnsureNextLiteralIsValid();
    return next_literal_->length;
  }

  bool is_next_literal_one_byte() {
    EnsureNextLiteralIsValid();
    return next_literal_->is_one_byte;
  }

  bool is_next_contextual_keyword(Vector<const uint8_t> keyword) {
    if (!is_next_literal_one_byte()) return false;
    Vector<const uint8_t> literal = next_literal_one_byte_string();
    return literal.length() == keyword.length() &&
        (memcmp(literal.start(), keyword.start(), literal.length()) == 0);
  }

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

  UnicodeCache* unicode_cache() { return unicode_cache_; }

 protected:
  struct TokenDesc {
    Token::Value token;
    int beg_pos;
    int end_pos;
    bool has_escapes;
    bool is_onebyte;
  };

  struct LiteralDesc {
    int beg_pos;
    bool is_one_byte;
    bool is_in_buffer;
    int offset;
    int length;
    Vector<const uint8_t> one_byte_string;
    Vector<const uint16_t> two_byte_string;
    LiteralBuffer buffer;
    LiteralDesc() : beg_pos(-1), is_one_byte(false), is_in_buffer(false),
                    offset(0), length(0) { }
    bool Valid(int pos) { return beg_pos == pos; }
  };

  virtual void Scan() = 0;

  virtual void UpdateBufferBasedOnHandle() = 0;
  virtual bool FillLiteral(const TokenDesc& token, LiteralDesc* literal) = 0;
  virtual Handle<String> InternalizeLiteral(LiteralDesc* literal) = 0;
  virtual Handle<String> AllocateLiteral(LiteralDesc* literal,
                                         PretenureFlag tenured) = 0;

  void ResetLiterals() {
    if (!current_literal_->is_in_buffer) current_literal_->beg_pos = -1;
    if (!next_literal_->is_in_buffer) next_literal_->beg_pos = -1;
  }

  void EnsureCurrentLiteralIsValid() {
    if (!current_literal_->Valid(current_.beg_pos)) {
      FillLiteral(current_, current_literal_);
    }
  }

  void EnsureNextLiteralIsValid() {
    if (!next_literal_->Valid(next_.beg_pos)) {
      FillLiteral(next_, next_literal_);
    }
  }

  UnicodeCache* unicode_cache_;

  bool has_line_terminator_before_next_;
  // Whether there is a multiline comment *with a line break* before the next
  // token.
  bool has_multiline_comment_before_next_;

  TokenDesc current_;  // desc for current token (as returned by Next())
  TokenDesc next_;     // desc for next token (one token look-ahead)

  LiteralDesc* current_literal_;
  LiteralDesc* next_literal_;
  LiteralDesc literals_[2];

  bool harmony_numeric_literals_;
  bool harmony_modules_;
  bool harmony_scoping_;

  friend class Scanner;
  friend class LexerGCHandler;
};


template<typename Char>
class Lexer : public LexerBase {
 public:
  Lexer(UnicodeCache* unicode_cache,
        Handle<String> source,
        int start_position,
        int end_position);
  Lexer(UnicodeCache* unicode_cache, const Char* source_ptr, int length);
  virtual ~Lexer();

  virtual void SeekForward(int pos);
  virtual bool ScanRegExpPattern(bool seen_equal);
  virtual bool ScanRegExpFlags();
  virtual Location octal_position() const;
  virtual void clear_octal_position() { last_octal_end_ = NULL; }

 protected:
  virtual void Scan();

  const Char* GetNewBufferBasedOnHandle() const;
  virtual void UpdateBufferBasedOnHandle();

  virtual bool FillLiteral(const TokenDesc& token, LiteralDesc* literal);
  virtual Handle<String> InternalizeLiteral(LiteralDesc* literal);
  virtual Handle<String> AllocateLiteral(LiteralDesc* literal,
                                         PretenureFlag tenured);

 private:
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

  // Returns true if the literal of the token can be represented as a
  // substring of the source.
  bool IsSubstringOfSource(const TokenDesc& token);

  bool CopyToLiteralBuffer(const Char* start,
                           const Char* end,
                           const TokenDesc& token,
                           LiteralDesc* literal);

  // One of source_handle_ or source_ptr_ is set.
  // If source_ptr_ is set, isolate_ is 0 and no isolate accesses are allowed.
  Isolate* isolate_;
  const Handle<String> source_handle_;
  const Char* const source_ptr_;
  const int start_position_;
  const int end_position_;
  // Stream variables.
  const Char* buffer_;
  const Char* buffer_end_;
  const Char* start_;
  const Char* cursor_;
  // Where we have seen the last octal number or an octal escape inside a
  // string. Used by octal_position().
  const Char* last_octal_end_;
};


#ifdef V8_USE_GENERATED_LEXER


// Match old scanner interface.
class Scanner {
 public:
  typedef LexerBase::Location Location;

  explicit Scanner(UnicodeCache* unicode_cache);

  ~Scanner() { delete lexer_; }

  void Initialize(Utf16CharacterStream* source);

  inline void SeekForward(int pos) { lexer_->SeekForward(pos); }

  inline bool ScanRegExpPattern(bool seen_equal) {
    return lexer_->ScanRegExpPattern(seen_equal);
  }

  inline bool ScanRegExpFlags() { return lexer_->ScanRegExpFlags(); }

  inline Location octal_position() const { return lexer_->octal_position(); }

  inline void clear_octal_position() { lexer_->clear_octal_position(); }

  inline Token::Value Next() { return lexer_->Next(); }

  inline Token::Value current_token() { return lexer_->current_token(); }

  inline Location location() { return lexer_->location(); }

  inline Token::Value peek() const { return lexer_->peek(); }

  inline Location peek_location() const { return lexer_->peek_location(); }

  inline UnicodeCache* unicode_cache() { return lexer_->unicode_cache(); }

  inline bool HarmonyScoping() const {
    return harmony_scoping_;
  }

  inline void SetHarmonyScoping(bool scoping) {
    harmony_scoping_ = scoping;
    SyncSettings();
  }

  inline bool HarmonyModules() const {
    return harmony_modules_;
  }

  inline void SetHarmonyModules(bool modules) {
    harmony_modules_ = modules;
    SyncSettings();
  }

  inline bool HarmonyNumericLiterals() const {
    return harmony_numeric_literals_;
  }

  inline void SetHarmonyNumericLiterals(bool numeric_literals) {
    harmony_numeric_literals_ = numeric_literals;
    SyncSettings();
  }

  inline bool HasAnyLineTerminatorBeforeNext() const {
    return lexer_->HasAnyLineTerminatorBeforeNext();
  }

  inline Vector<const char> literal_ascii_string() {
    return Vector<const char>::cast(lexer_->literal_one_byte_string());
  }

  inline Vector<const uc16> literal_utf16_string() {
    return lexer_->literal_two_byte_string();
  }

  inline int literal_length() {
    return lexer_->literal_length();
  }

  inline bool is_literal_ascii() {
    return lexer_->is_literal_one_byte();
  }

  inline bool is_literal_contextual_keyword(
      Vector<const char>& keyword) {  // NOLINT
    return lexer_->is_literal_contextual_keyword(
        Vector<const uint8_t>::cast(keyword));
  }

  inline bool literal_contains_escapes() const {
    return lexer_->literal_contains_escapes();
  }

  inline Vector<const char> next_literal_ascii_string() {
    return Vector<const char>::cast(lexer_->next_literal_one_byte_string());
  }

  inline Vector<const uc16> next_literal_utf16_string() {
    return lexer_->next_literal_two_byte_string();
  }

  inline int next_literal_length() {
    return lexer_->next_literal_length();
  }

  inline bool is_next_literal_ascii() {
    return lexer_->is_next_literal_one_byte();
  }

  inline bool is_next_contextual_keyword(
      Vector<const char>& keyword) {  // NOLINT
    return lexer_->is_next_contextual_keyword(
        Vector<const uint8_t>::cast(keyword));
  }

 private:
  void SyncSettings();

  UnicodeCache* unicode_cache_;
  LexerBase* lexer_;
  bool harmony_numeric_literals_;
  bool harmony_modules_;
  bool harmony_scoping_;
};


#endif


} }

#endif  // V8_LEXER_EXPERIMENTAL_SCANNER_H
