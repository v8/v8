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

#include <vector>

#include "flags.h"
#include "token.h"

namespace v8 {
namespace internal {

class PushScanner;

class ExperimentalScanner {
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

  struct SavedToken {
    int beg, end;
    Token::Value value;
  };

  ExperimentalScanner(const char* fname,
                      bool read_all_at_once,
                      Isolate* isolate);
  ~ExperimentalScanner();

  Token::Value Next();
  Token::Value current_token();
  Location location();

  void Record(v8::internal::Token::Value token, int beg_pos, int end_pos);

 private:
  void FillTokens();
  static const int BUFFER_SIZE = 256;
  std::vector<SavedToken> token_;
  size_t current_;
  size_t fetched_;
  FILE* file_;
  PushScanner* scanner_;
  bool read_all_at_once_;
  bool already_pushed_;
  const v8::internal::byte* source_;
  int length_;
};

} }  // namespace v8::internal

#endif
