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

#include "even-more-experimental-scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#include "experimental-scanner.h"

using namespace v8::internal;

namespace {

inline int HexValue(uc32 c) {
  c -= '0';
  if (static_cast<unsigned>(c) <= 9) return c;
  c = (c | 0x20) - ('a' - '0');  // detect 0x11..0x16 and 0x31..0x36.
  if (static_cast<unsigned>(c) <= 5) return c + 10;
  return -1;
}

}

namespace v8 {
namespace internal {

EvenMoreExperimentalScanner::EvenMoreExperimentalScanner(
    ExperimentalScanner* sink,
    UnicodeCache* unicode_cache)
    : unicode_cache_(unicode_cache),
      buffer_(NULL),
      buffer_end_(NULL),
      start_(NULL),
      cursor_(NULL),
      sink_(sink) {}


EvenMoreExperimentalScanner::~EvenMoreExperimentalScanner() {
}


uc32 EvenMoreExperimentalScanner::ScanHexNumber(int length) {
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


bool EvenMoreExperimentalScanner::ValidIdentifierPart() {
  return unicode_cache_->IsIdentifierPart(ScanHexNumber(4));
}


bool EvenMoreExperimentalScanner::ValidIdentifierStart() {
  return unicode_cache_->IsIdentifierStart(ScanHexNumber(4));
}


uint32_t EvenMoreExperimentalScanner::push(const void *input, int input_size) {
  // FIXME: for now, we can only push once and that'll read the input.
  if (input_size == 0)
    return 0;
  buffer_ = const_cast<YYCTYPE*>(reinterpret_cast<const YYCTYPE*>(input));
  cursor_ = buffer_;
  start_ = buffer_;
  buffer_end_ = buffer_ + input_size;
  return DoLex();
}


void EvenMoreExperimentalScanner::send(Token::Value token) {
  int beg = start_ - buffer_;
  int end = cursor_ - buffer_;
  sink_->Record(token, beg, end);
}


}
}
