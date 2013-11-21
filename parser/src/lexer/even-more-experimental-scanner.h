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

#ifndef V8_LEXER_EVEN_MORE_EXPERIMENTAL_SCANNER_H
#define V8_LEXER_EVEN_MORE_EXPERIMENTAL_SCANNER_H

#include "token.h"
#include "flags.h"
#include "v8stdint.h"

// FIXME: some of this is probably not needed.
#include "allocation.h"
#include "ast.h"
#include "preparse-data-format.h"
#include "preparse-data.h"
#include "scopes.h"
#include "preparser.h"
#include "api.h"
#include "ast.h"
#include "bootstrapper.h"
#include "char-predicates-inl.h"
#include "codegen.h"
#include "compiler.h"
#include "func-name-inferrer.h"
#include "messages.h"
#include "parser.h"
#include "platform.h"
#include "preparser.h"
#include "runtime.h"
#include "scanner-character-streams.h"
#include "scopeinfo.h"
#include "string-stream.h"

namespace v8 {
namespace internal {

class ExperimentalScanner;
class UnicodeCache;

template<typename YYCTYPE>
class EvenMoreExperimentalScanner {
 public:
  explicit EvenMoreExperimentalScanner(
      const char* fname,
      Isolate* isolate,
      int repeat,
      bool convert_to_utf16);

  ~EvenMoreExperimentalScanner();

  Token::Value Next(int* beg_pos, int* end_pos);

 private:
  bool ValidIdentifierStart();
  bool ValidIdentifierPart();
  uc32 ScanHexNumber(int length);

  UnicodeCache* unicode_cache_;

  YYCTYPE* buffer_;
  YYCTYPE* buffer_end_;
  YYCTYPE* start_;
  YYCTYPE* cursor_;
  YYCTYPE* marker_;
  bool just_seen_line_terminator_;

  YYCTYPE yych;
};

const byte* ReadFile(const char* name, Isolate* isolate, int* size, int repeat);

template<typename YYCTYPE>
EvenMoreExperimentalScanner<YYCTYPE>::EvenMoreExperimentalScanner(
    const char* fname,
    Isolate* isolate,
    int repeat,
    bool convert_to_utf16)
    : unicode_cache_(isolate->unicode_cache()) {
  int size = 0;
  buffer_ = const_cast<YYCTYPE*>(reinterpret_cast<const YYCTYPE*>(
      ReadFile(fname, isolate, &size, repeat)));

  if (convert_to_utf16) {
    Utf8ToUtf16CharacterStream stream(reinterpret_cast<const byte*>(buffer_),
                                      size);
    uint16_t* new_buffer = new uint16_t[size];
    uint16_t* cursor = new_buffer;
    uc32 c;
    // The 32-bit char type is probably only so that we can have -1 as a return
    // value. If the char is not -1, it should fit into 16 bits.
    while ((c = stream.Advance()) != -1)
      *cursor++ = c;
    delete[] buffer_;
    buffer_ = reinterpret_cast<YYCTYPE*>(new_buffer);
    buffer_end_ = reinterpret_cast<YYCTYPE*>(cursor);
  } else {
    buffer_end_ = buffer_ + size / sizeof(YYCTYPE);
  }

  start_ = buffer_;
  cursor_ = buffer_;
  marker_ = buffer_;
}


template<typename YYCTYPE>
EvenMoreExperimentalScanner<YYCTYPE>::~EvenMoreExperimentalScanner() {
  delete[] buffer_;
}


template<typename YYCTYPE>
uc32 EvenMoreExperimentalScanner<YYCTYPE>::ScanHexNumber(int length) {
  // We have seen \uXXXX, let's see what it is.
  // FIXME: we never end up in here if only a subset of the 4 chars are valid
  // hex digits -> handle the case where they're not.
  uc32 x = 0;
  for (YYCTYPE* s = cursor_ - length; s != cursor_; ++s) {
    int d = HexValue(*s);
    if (d < 0) {
      return -1;
    }
    x = x * 16 + d;
  }
  return x;
}


template<typename YYCTYPE>
bool EvenMoreExperimentalScanner<YYCTYPE>::ValidIdentifierPart() {
  return unicode_cache_->IsIdentifierPart(ScanHexNumber(4));
}


template<typename YYCTYPE>
bool EvenMoreExperimentalScanner<YYCTYPE>::ValidIdentifierStart() {
  return unicode_cache_->IsIdentifierStart(ScanHexNumber(4));
}

} }

#endif  // V8_LEXER_LEXER_H
