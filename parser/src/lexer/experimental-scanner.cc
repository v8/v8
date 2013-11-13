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

#include "experimental-scanner.h"

#include "v8.h"

#include "objects.h"
#include "objects-inl.h"
#include "spaces-inl.h"
#include "isolate.h"
#include "lexer.h"
#include "even-more-experimental-scanner.h"

namespace v8 {
namespace internal {


const byte* ReadFile(const char* name, Isolate* isolate,
                     int* size, int repeat) {
  FILE* file = fopen(name, "rb");
  *size = 0;
  if (file == NULL) return NULL;

  fseek(file, 0, SEEK_END);
  int file_size = ftell(file);
  rewind(file);

  *size = file_size * repeat;

  byte* chars = new byte[*size + 1];
  for (int i = 0; i < file_size;) {
    int read = static_cast<int>(fread(&chars[i], 1, file_size - i, file));
    i += read;
  }
  fclose(file);

  for (int i = file_size; i < *size; i++) {
    chars[i] = chars[i - file_size];
  }
  chars[*size] = 0;

  return chars;
}


ExperimentalScanner::ExperimentalScanner(const char* fname,
                                         bool read_all_at_once,
                                         Isolate* isolate,
                                         int repeat)
    : current_(0),
      fetched_(0),
      read_all_at_once_(read_all_at_once),
      already_pushed_(false),
      source_(0),
      length_(0) {
  CHECK(repeat == 1 || read_all_at_once);
  file_ = fopen(fname, "rb");
  // python generated version
  scanner_ = new EvenMoreExperimentalScanner(this, isolate->unicode_cache());
  // re2c version
  // scanner_ = new PushScanner(this, isolate->unicode_cache());
  if (read_all_at_once_) {
    source_ = ReadFile(fname, isolate, &length_, repeat);
    token_.resize(length_);
  } else {
    token_.resize(BUFFER_SIZE);
  }
}


ExperimentalScanner::~ExperimentalScanner() {
  fclose(file_);
  delete[] source_;
}


void ExperimentalScanner::FillTokens() {
  current_ = 0;
  fetched_ = 0;
  if (read_all_at_once_) {
    if (!already_pushed_) {
      scanner_->push(source_, length_ + 1);
      already_pushed_ = true;
    } else {
      scanner_->push(0, 0);
    }
  } else {
    uint8_t chars[BUFFER_SIZE];
    int n = static_cast<int>(fread(&chars, 1, BUFFER_SIZE, file_));
    for (int i = n; i < BUFFER_SIZE; i++) chars[i] = 0;
    scanner_->push(chars, BUFFER_SIZE);
  }
}


Token::Value ExperimentalScanner::Next() {
  while (current_ == fetched_)
    FillTokens();
  return token_[current_++].value;
}


Token::Value ExperimentalScanner::current_token() {
  return token_[current_ - 1].value;
}


ExperimentalScanner::Location ExperimentalScanner::location() {
  return Location(token_[current_ - 1].beg, token_[current_ - 1].end);
}


void ExperimentalScanner::Record(Token::Value token, int beg, int end) {
  if (token == Token::EOS) end--;
  if (fetched_ >= token_.size()) {
    token_.resize(token_.size() * 2);
  }
  token_[fetched_].value = token;
  token_[fetched_].beg = beg;
  token_[fetched_].end = end;
  fetched_++;
}

} }  // namespace v8::internal
