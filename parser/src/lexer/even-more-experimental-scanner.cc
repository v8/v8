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

  byte* file_contents = new byte[file_size];
  for (int i = 0; i < file_size;) {
    int read =
        static_cast<int>(fread(&file_contents[i], 1, file_size - i, file));
    i += read;
  }
  fclose(file);

  // If the file contains the UTF16 little endian magic bytes, skip them.
  // FIXME: what if we see big endian magic bytes? Do we do the right thing for
  // big endian anyway?
  byte* start = file_contents;
  if (*start == 0xff && *(start + 1) == 0xfe) {
    start += 2;
    file_size -= 2;
  }

  *size = file_size * repeat;
  byte* chars = new byte[*size];

  for (int i = 0; i < *size; i++) {
    chars[i] = start[i % file_size];
  }

  delete file_contents;

  return chars;
}



}
}
