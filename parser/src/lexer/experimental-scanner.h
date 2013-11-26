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

  // FIXME: implement these
  Vector<const char> literal_ascii_string() {
    return Vector<const char>();  // FIXME
  }
  Vector<const uc16> literal_utf16_string() {
    return Vector<const uc16>();  // FIXME
  }
  bool is_literal_ascii() {
    return true;  // FIXME
  }
  bool is_literal_contextual_keyword(Vector<const char> keyword) {
    return false;  // FIXME
  }
  int literal_length() const {
    return 0;  // FIXME
  }
  bool literal_contains_escapes() const {
    return false;  // FIXME
  }

  Vector<const char> next_literal_ascii_string() {
    return Vector<const char>();  // FIXME
  }
  Vector<const uc16> next_literal_utf16_string() {
    return Vector<const uc16>();  // FIXME
  }
  bool is_next_literal_ascii() {
    return true;  // FIXME
  }
  bool is_next_contextual_keyword(Vector<const char> keyword) {
    return false;  // FIXME
  }
  int next_literal_length() const {
    return 0;  // FIXME
  }

  uc32 ScanOctalEscape(uc32 c, int length) { return 0; }  // FIXME

  Location octal_position() const {
    return Location(0, 0);  // FIXME
  }
  void clear_octal_position() { }  // FIXME

  void SeekForward(int pos) { }  // FIXME

  // Scans the input as a regular expression pattern, previous
  // character(s) must be /(=). Returns true if a pattern is scanned.
  bool ScanRegExpPattern(bool seen_equal) { return false; }  // FIXME
  // Returns true if regexp flags are scanned (always since flags can
  // be empty).
  bool ScanRegExpFlags() { return false; }  // FIXME

 protected:
  struct TokenDesc {
    Token::Value token;
    int beg_pos;
    int end_pos;
  };

  virtual void Scan() = 0;
  virtual uc32 ScanHexNumber(int length) = 0;
  virtual void SetBufferBasedOnHandle() = 0;

  static void UpdateBuffersAfterGC(v8::Isolate*, GCType, GCCallbackFlags);


  bool ValidIdentifierPart() {
      return unicode_cache_->IsIdentifierPart(ScanHexNumber(4));
  }

  bool ValidIdentifierStart() {
    return unicode_cache_->IsIdentifierStart(ScanHexNumber(4));
  }

  Isolate* isolate_;
  UnicodeCache* unicode_cache_;

  bool has_line_terminator_before_next_;

  TokenDesc current_;  // desc for current token (as returned by Next())
  TokenDesc next_;     // desc for next token (one token look-ahead)

  bool harmony_numeric_literals_;
  bool harmony_modules_;
  bool harmony_scoping_;

 private:
  static std::set<ScannerBase*>* scanners_;
};


template<typename YYCTYPE>
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
        marker_(NULL) {
    ASSERT(source->IsFlat());
    SetBufferBasedOnHandle();
    Scan();
  }

  virtual ~ExperimentalScanner() { }

  virtual void Scan();
  virtual uc32 ScanHexNumber(int length);

  virtual void SetBufferBasedOnHandle() {
    // We get a raw pointer from the Handle, but we also update it every time
    // there is a GC, so it is safe.
    DisallowHeapAllocation no_gc;
    const YYCTYPE* new_buffer = GetNewBufferBasedOnHandle();
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

  const YYCTYPE* GetNewBufferBasedOnHandle() const;

 private:
  Handle<String> source_handle_;
  const YYCTYPE* buffer_;
  const YYCTYPE* buffer_end_;
  const YYCTYPE* start_;
  const YYCTYPE* cursor_;
  const YYCTYPE* marker_;
};


template<typename YYCTYPE>
uc32 ExperimentalScanner<YYCTYPE>::ScanHexNumber(int length) {
  // We have seen \uXXXX, let's see what it is.
  // FIXME: we never end up in here if only a subset of the 4 chars are valid
  // hex digits -> handle the case where they're not.
  uc32 x = 0;
  for (const YYCTYPE* s = cursor_ - length; s != cursor_; ++s) {
    int d = HexValue(*s);
    if (d < 0) {
      return -1;
    }
    x = x * 16 + d;
  }
  return x;
}


} }

#endif  // V8_LEXER_EXPERIMENTAL_SCANNER_H
