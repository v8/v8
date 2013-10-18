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
#include "lexer.h"

using namespace v8::internal;

const byte* ReadFile(const char* name, Isolate* isolate, int* size) {
  FILE* file = fopen(name, "rb");
  *size = 0;
  if (file == NULL) return NULL;

  fseek(file, 0, SEEK_END);
  *size = ftell(file);
  rewind(file);

  byte* chars = new byte[*size + 1];
  chars[*size] = 0;
  for (int i = 0; i < *size;) {
    int read = static_cast<int>(fread(&chars[i], 1, *size - i, file));
    i += read;
  }
  fclose(file);
  return chars;
}


class BaselineScanner {
 public:
  BaselineScanner(const char* fname, Isolate* isolate) {
    int length = 0;
    source_ = ReadFile(fname, isolate, &length);
    unicode_cache_ = new UnicodeCache();
    scanner_ = new Scanner(unicode_cache_);
    stream_ = new Utf8ToUtf16CharacterStream(source_, length);
    scanner_->Initialize(stream_);
  }

  ~BaselineScanner() {
    delete scanner_;
    delete stream_;
    delete unicode_cache_;
    delete[] source_;
  }

  Token::Value Next(int* beg_pos, int* end_pos) {
    Token::Value res = scanner_->Next();
    *beg_pos = scanner_->location().beg_pos;
    *end_pos = scanner_->location().end_pos;
    return res;
  }

 private:
  UnicodeCache* unicode_cache_;
  Scanner* scanner_;
  const byte* source_;
  Utf8ToUtf16CharacterStream* stream_;
};

ExperimentalScanner::ExperimentalScanner(const char* fname,
                                         bool read_all_at_once)
    : current_(0),
      fetched_(0),
      read_all_at_once_(read_all_at_once),
      source_(0),
      length_(0) {
  file_ = fopen(fname, "rb");
  scanner_ = new PushScanner(this);
  if (read_all_at_once_) {
    source_ = ReadFile(fname, NULL, &length_);
    token_.resize(1500);
    beg_.resize(1500);
    end_.resize(1500);
  } else {
    token_.resize(BUFFER_SIZE);
    beg_.resize(BUFFER_SIZE);
    end_.resize(BUFFER_SIZE);
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
    scanner_->push(source_, length_ + 1);
  } else {
    uint8_t chars[BUFFER_SIZE];
    int n = static_cast<int>(fread(&chars, 1, BUFFER_SIZE, file_));
    for (int i = n; i < BUFFER_SIZE; i++) chars[i] = 0;
    scanner_->push(chars, BUFFER_SIZE);
  }
}


Token::Value ExperimentalScanner::Next(int* beg_pos, int* end_pos) {
  while (current_ == fetched_)
    FillTokens();
  *beg_pos = beg_[current_];
  *end_pos = end_[current_];
  Token::Value res = token_[current_];
  if (res != Token::Token::EOS)
    current_++;
  return res;
}


void ExperimentalScanner::Record(Token::Value token, int beg, int end) {
  if (token == Token::EOS) end--;
  if (fetched_ >= token_.size()) {
    token_.resize(token_.size() * 2);
    beg_.resize(beg_.size() * 2);
    end_.resize(end_.size() * 2);
  }
  token_[fetched_] = token;
  beg_[fetched_] = beg;
  end_[fetched_] = end;
  fetched_++;
}


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
      ExperimentalScanner experimental(argv[1], true);

      std::vector<Token::Value> baseline_tokens, experimental_tokens;
      std::vector<size_t> baseline_beg, baseline_end, experimental_beg,
          experimental_end;
      Token::Value token;
      int beg, end;

      TimeDelta baseline_time, experimental_time;
      ElapsedTimer timer;
      {
        timer.Start();
        do {
          token = baseline.Next(&beg, &end);
          baseline_tokens.push_back(token);
          baseline_beg.push_back(beg);
          baseline_end.push_back(end);
        } while (token != Token::EOS);
        baseline_time = timer.Elapsed();
      }

      {
        timer.Start();
        do {
          token = experimental.Next(&beg, &end);
          experimental_tokens.push_back(token);
          experimental_beg.push_back(beg);
          experimental_end.push_back(end);
        } while (token != Token::EOS);
        experimental_time = timer.Elapsed();
      }

      for (size_t i = 0; i < experimental_tokens.size(); ++i) {
        printf("=> %11s at (%d, %d)\n",
               Token::Name(experimental_tokens[i]),
               experimental_beg[i], experimental_end[i]);
        if (experimental_tokens[i] != baseline_tokens[i] ||
            experimental_beg[i] != baseline_beg[i] ||
            experimental_end[i] != baseline_end[i]) {
          printf("MISMATCH:\n");
          printf("Expected: %s at (%d, %d)\n",
                 Token::Name(baseline_tokens[i]),
                 baseline_beg[i], baseline_end[i]);
          printf("Actual:   %s at (%d, %d)\n",
                 Token::Name(experimental_tokens[i]),
                 experimental_beg[i], experimental_end[i]);
          return 1;
        }
      }
      printf("No of tokens: %d\n", experimental_tokens.size());
      printf("Baseline: %f ms\nExperimental %f ms\n",
             baseline_time.InMillisecondsF(),
             experimental_time.InMillisecondsF());
    }
  }
  v8::V8::Dispose();
  return 0;
}
