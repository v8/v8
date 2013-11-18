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

#define YYCTYPE uint8_t

namespace v8 {
namespace internal {

class ExperimentalScanner;
class UnicodeCache;

class EvenMoreExperimentalScanner {
 public:
  explicit EvenMoreExperimentalScanner(ExperimentalScanner* sink,
                                       UnicodeCache* unicode_cache);

  ~EvenMoreExperimentalScanner();

  void send(v8::internal::Token::Value token);
  uint32_t push(const void *input, int input_size);

 private:
  uint32_t DoLex();

  bool ValidIdentifierStart();
  bool ValidIdentifierPart();
  uc32 ScanHexNumber(int length);

  UnicodeCache* unicode_cache_;

  YYCTYPE* buffer_;
  YYCTYPE* buffer_end_;
  YYCTYPE* start_;
  YYCTYPE* cursor_;
  bool just_seen_line_terminator_;

  YYCTYPE yych;
  ExperimentalScanner* sink_;
};

} }

#endif  // V8_LEXER_LEXER_H
