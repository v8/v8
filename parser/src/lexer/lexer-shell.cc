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

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "v8.h"

#include "api.h"
#include "ast.h"
#include "char-predicates-inl.h"
#include "messages.h"
#include "platform.h"
#include "runtime.h"
#include "scanner-character-streams.h"
#include "scopeinfo.h"
#include "string-stream.h"
#include "scanner.h"

using namespace v8::internal;

Handle<String> ReadFile(const char* name, Isolate* isolate) {
  FILE* file = fopen(name, "rb");
  if (file == NULL) return Handle<String>();

  fseek(file, 0, SEEK_END);
  int size = ftell(file);
  rewind(file);

  uint8_t* chars = new uint8_t[size + 1];
  chars[size] = '\0';
  for (int i = 0; i < size;) {
    int read = static_cast<int>(fread(&chars[i], 1, size - i, file));
    i += read;
  }
  fclose(file);
  Handle<String> result = isolate->factory()->NewStringFromOneByte(
      Vector<const uint8_t>(chars, size));
  delete[] chars;
  return result;
}


class BaselineScanner {
 public:
  BaselineScanner(const char* fname, Isolate* isolate) {
    Handle<String> source = ReadFile(fname, isolate);
    unicode_cache_ = new UnicodeCache();
    scanner_ = new Scanner(unicode_cache_);
    stream_ = new GenericStringUtf16CharacterStream(
                      source, 0, source->length());
    scanner_->Initialize(stream_);
  }

  ~BaselineScanner() {
    delete scanner_;
    delete stream_;
    delete unicode_cache_;
  }

  Token::Value Next() {
    return scanner_->Next();
  }

  Scanner::Location Location() {
    return scanner_->location();
  }

 private:
  UnicodeCache* unicode_cache_;
  Scanner* scanner_;
  GenericStringUtf16CharacterStream* stream_;
};


class ExperimentalScanner {
  explicit ExperimentalScanner(const char* fname);
  ~ExperimentalScanner();
  Token::Value Next();
  Scanner::Location Location();
};


int main(int argc, char* argv[]) {
  v8::V8::InitializeICU();
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  {
    v8::HandleScope handle_scope(isolate);
    v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
    v8::Local<v8::Context> context = v8::Context::New(isolate, NULL, global);
    ASSERT(!context.IsEmpty());
    {
      v8::Context::Scope scope(context);
      Isolate* isolate = Isolate::Current();
      HandleScope handle_scope(isolate);
      BaselineScanner baseline(argv[1], isolate);
      Token::Value current;
      while ((current = baseline.Next()) != Token::EOS) {
        printf("%11s => (%d, %d)\n",
               Token::Name(current),
               baseline.Location().beg_pos,
               baseline.Location().end_pos);
      }
    }
  }
  v8::V8::Dispose();
  return 0;
}
