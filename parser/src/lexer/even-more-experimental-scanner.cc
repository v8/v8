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

using namespace v8::internal;

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

  byte* chars = new byte[*size];
  for (int i = 0; i < file_size;) {
    int read = static_cast<int>(fread(&chars[i], 1, file_size - i, file));
    i += read;
  }
  fclose(file);

  for (int i = file_size; i < *size; i++) {
    chars[i] = chars[i - file_size];
  }

  return chars;
}


EvenMoreExperimentalScanner::EvenMoreExperimentalScanner(
    const char* fname,
    Isolate* isolate,
    int repeat)
    : unicode_cache_(isolate->unicode_cache()) {
  int size = 0;
  buffer_ = const_cast<YYCTYPE*>(reinterpret_cast<const YYCTYPE*>(
      ReadFile(fname, isolate, &size, repeat)));
  buffer_end_ = buffer_ + size / sizeof(YYCTYPE);
  start_ = buffer_;
  cursor_ = buffer_;
  marker_ = buffer_;
}


EvenMoreExperimentalScanner::~EvenMoreExperimentalScanner() {
  delete[] buffer_;
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


}
}
