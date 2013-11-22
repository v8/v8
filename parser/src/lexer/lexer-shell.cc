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
#include <string>
#include <vector>
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

#include "experimental-scanner.h"

using namespace v8::internal;

byte* ReadFile(const char* name, byte** end, int repeat,
               bool convert_to_utf16) {
  FILE* file = fopen(name, "rb");
  if (file == NULL) return NULL;

  fseek(file, 0, SEEK_END);
  int file_size = ftell(file);
  rewind(file);

  int size = file_size * repeat;

  byte* chars = new byte[size];
  for (int i = 0; i < file_size;) {
    int read = static_cast<int>(fread(&chars[i], 1, file_size - i, file));
    i += read;
  }
  fclose(file);

  for (int i = file_size; i < size; i++) {
    chars[i] = chars[i - file_size];
  }
  *end = &chars[size];

  if (!convert_to_utf16)
    return chars;

  // Length of new_chars is not strictly accurate, but should be enough.
  uint16_t* new_chars = new uint16_t[size];
  {
    Utf8ToUtf16CharacterStream stream(chars, size);
    uint16_t* cursor = new_chars;
    uc32 c;
    // The 32-bit char type is probably only so that we can have -1 as a return
    // value. If the char is not -1, it should fit into 16 bits.
    while ((c = stream.Advance()) != -1)
      *cursor++ = c;
    *end = reinterpret_cast<byte*>(cursor);
  }
  delete[] chars;
  return reinterpret_cast<byte*>(new_chars);
}


enum Encoding {
  LATIN1,
  UTF8,
  UTF16,
  UTF8TO16  // Read as UTF8, convert to UTF16 before giving it to the lexers.
};


struct HarmonySettings {
  bool numeric_literals;
  bool modules;
  bool scoping;
  HarmonySettings() : numeric_literals(false), modules(false), scoping(false) {}
};

class BaselineScanner {
 public:
  BaselineScanner(const char* fname,
                  Isolate* isolate,
                  Encoding encoding,
                  ElapsedTimer* timer,
                  int repeat,
                  HarmonySettings harmony_settings)
      : stream_(NULL) {
    byte* end = 0;
    source_ = ReadFile(fname, &end, repeat, false);
    unicode_cache_ = new UnicodeCache();
    scanner_ = new Scanner(unicode_cache_);
    scanner_->SetHarmonyNumericLiterals(harmony_settings.numeric_literals);
    scanner_->SetHarmonyModules(harmony_settings.modules);
    scanner_->SetHarmonyScoping(harmony_settings.scoping);
    switch (encoding) {
      case UTF8:
      case UTF8TO16:
        stream_ = new Utf8ToUtf16CharacterStream(source_, end - source_);
        break;
      case UTF16: {
        Handle<String> result = isolate->factory()->NewStringFromTwoByte(
            Vector<const uint16_t>(
                reinterpret_cast<const uint16_t*>(source_),
                (end - source_) / 2));
        stream_ =
            new GenericStringUtf16CharacterStream(result, 0, result->length());
        break;
      }
      case LATIN1: {
        Handle<String> result = isolate->factory()->NewStringFromOneByte(
            Vector<const uint8_t>(source_, end - source_));
        stream_ =
            new GenericStringUtf16CharacterStream(result, 0, result->length());
        break;
      }
    }
    timer->Start();
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
  BufferedUtf16CharacterStream* stream_;
};


struct TokenWithLocation {
  Token::Value value;
  size_t beg;
  size_t end;
  TokenWithLocation() : value(Token::ILLEGAL), beg(0), end(0) { }
  TokenWithLocation(Token::Value value, size_t beg, size_t end) :
      value(value), beg(beg), end(end) { }
  bool operator==(const TokenWithLocation& other) {
    return value == other.value && beg == other.beg && end == other.end;
  }
  bool operator!=(const TokenWithLocation& other) {
    return !(*this == other);
  }
  void Print(const char* prefix) const {
    printf("%s %11s at (%d, %d)\n",
           prefix, Token::Name(value),
           static_cast<int>(beg), static_cast<int>(end));
  }
};


TimeDelta RunBaselineScanner(const char* fname,
                             Isolate* isolate,
                             Encoding encoding,
                             bool dump_tokens,
                             std::vector<TokenWithLocation>* tokens,
                             int repeat,
                             HarmonySettings harmony_settings) {
  ElapsedTimer timer;
  BaselineScanner scanner(
      fname, isolate, encoding, &timer, repeat, harmony_settings);
  Token::Value token;
  int beg, end;
  do {
    token = scanner.Next(&beg, &end);
    if (dump_tokens) {
      tokens->push_back(TokenWithLocation(token, beg, end));
    }
  } while (token != Token::EOS);
  return timer.Elapsed();
}


template<typename YYCTYPE>
TimeDelta RunExperimentalScanner(const char* fname,
                                 Isolate* isolate,
                                 Encoding encoding,
                                 bool dump_tokens,
                                 std::vector<TokenWithLocation>* tokens,
                                 int repeat,
                                 HarmonySettings harmony_settings) {
  ElapsedTimer timer;
  byte* buffer_end = 0;
  YYCTYPE* buffer = reinterpret_cast<YYCTYPE*>(
      ReadFile(fname, &buffer_end, repeat, encoding == UTF8TO16));

  ExperimentalScanner<YYCTYPE> scanner(
      buffer, reinterpret_cast<YYCTYPE*>(buffer_end), isolate);
  scanner.SetHarmonyNumericLiterals(harmony_settings.numeric_literals);
  scanner.SetHarmonyModules(harmony_settings.modules);
  scanner.SetHarmonyScoping(harmony_settings.scoping);

  timer.Start();
  Token::Value token;
  int beg, end;
  do {
    token = scanner.Next(&beg, &end);
    if (dump_tokens) {
      tokens->push_back(TokenWithLocation(token, beg, end));
    }
  } while (token != Token::EOS);
  return timer.Elapsed();
}


void PrintTokens(const char* name,
                 const std::vector<TokenWithLocation>& tokens) {
  printf("No of tokens: %d\n",
         static_cast<int>(tokens.size()));
  printf("%s:\n", name);
  for (size_t i = 0; i < tokens.size(); ++i) {
    tokens[i].Print("=>");
  }
}


std::pair<TimeDelta, TimeDelta> ProcessFile(
    const char* fname,
    Encoding encoding,
    Isolate* isolate,
    bool run_baseline,
    bool run_experimental,
    bool print_tokens,
    bool check_tokens,
    bool break_after_illegal,
    int repeat,
    HarmonySettings harmony_settings) {
  if (print_tokens) {
    printf("Processing file %s\n", fname);
  }
  HandleScope handle_scope(isolate);
  std::vector<TokenWithLocation> baseline_tokens, experimental_tokens;
  TimeDelta baseline_time, experimental_time;
  if (run_baseline) {
    baseline_time = RunBaselineScanner(
        fname, isolate, encoding, print_tokens || check_tokens,
        &baseline_tokens, repeat, harmony_settings);
  }
  if (run_experimental) {
    switch (encoding) {
      case LATIN1:
        experimental_time = RunExperimentalScanner<uint8_t>(
            fname, isolate, encoding, print_tokens || check_tokens,
            &experimental_tokens, repeat, harmony_settings);
        break;
      case UTF16:
        experimental_time = RunExperimentalScanner<uint16_t>(
            fname, isolate, encoding, print_tokens || check_tokens,
            &experimental_tokens, repeat, harmony_settings);
        break;
      case UTF8TO16:
        experimental_time = RunExperimentalScanner<uint16_t>(
            fname, isolate, encoding, print_tokens || check_tokens,
            &experimental_tokens, repeat, harmony_settings);
        break;
      default:
        printf("Encoding not supported by the experimental scanner\n");
        exit(1);
        break;
    }
  }
  if (print_tokens && !run_experimental) {
    PrintTokens("Baseline", baseline_tokens);
  }
  if (print_tokens && !run_baseline) {
    PrintTokens("Experimental", experimental_tokens);
  }
  if ((print_tokens || check_tokens) && run_baseline && run_experimental) {
    if (print_tokens) {
      printf("No of tokens in Baseline:     %d\n",
             static_cast<int>(baseline_tokens.size()));
      printf("No of tokens in Experimental: %d\n",
             static_cast<int>(experimental_tokens.size()));
      printf("Baseline and Experimental:\n");
    }
    for (size_t i = 0; i < experimental_tokens.size(); ++i) {
      if (print_tokens) experimental_tokens[i].Print("=>");
      if (baseline_tokens[i].value == Token::ILLEGAL) {
        if (experimental_tokens[i].value != Token::ILLEGAL ||
            experimental_tokens[i].beg != baseline_tokens[i].beg) {
          printf("MISMATCH:\n");
          baseline_tokens[i].Print("Expected: ");
          experimental_tokens[i].Print("Actual:   ");
          exit(1);
        }
        if (break_after_illegal)
          break;
        continue;
      }
      if (experimental_tokens[i] != baseline_tokens[i]) {
        printf("MISMATCH:\n");
        baseline_tokens[i].Print("Expected: ");
        experimental_tokens[i].Print("Actual:   ");
        exit(1);
      }
    }
  }
  return std::make_pair(baseline_time, experimental_time);
}


int main(int argc, char* argv[]) {
  v8::V8::InitializeICU();
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  Encoding encoding = LATIN1;
  bool print_tokens = false;
  bool run_baseline = true;
  bool run_experimental = true;
  bool check_tokens = true;
  bool break_after_illegal = false;
  std::vector<std::string> fnames;
  std::string benchmark;
  int repeat = 1;
  HarmonySettings harmony_settings;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--latin1") == 0) {
      encoding = LATIN1;
    } else if (strcmp(argv[i], "--utf8") == 0) {
      encoding = UTF8;
    } else if (strcmp(argv[i], "--utf16") == 0) {
      encoding = UTF16;
    } else if (strcmp(argv[i], "--utf8to16") == 0) {
      encoding = UTF8TO16;
    } else if (strcmp(argv[i], "--print-tokens") == 0) {
      print_tokens = true;
    } else if (strcmp(argv[i], "--no-baseline") == 0) {
      run_baseline = false;
    } else if (strcmp(argv[i], "--no-experimental") == 0) {
      run_experimental = false;
    } else if (strcmp(argv[i], "--no-check") == 0) {
      check_tokens = false;
    } else if (strcmp(argv[i], "--break-after-illegal") == 0) {
      break_after_illegal = true;
    } else if (strcmp(argv[i], "--use-harmony") == 0) {
      harmony_settings.numeric_literals = true;
      harmony_settings.modules = true;
      harmony_settings.scoping = true;
    } else if (strncmp(argv[i], "--benchmark=", 12) == 0) {
      benchmark = std::string(argv[i]).substr(12);
    } else if (strncmp(argv[i], "--repeat=", 9) == 0) {
      std::string repeat_str = std::string(argv[i]).substr(9);
      repeat = atoi(repeat_str.c_str());
    } else if (i > 0 && argv[i][0] != '-') {
      fnames.push_back(std::string(argv[i]));
    }
  }
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  {
    v8::HandleScope handle_scope(isolate);
    v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();
    v8::Local<v8::Context> context = v8::Context::New(isolate, NULL, global);
    ASSERT(!context.IsEmpty());
    {
      v8::Context::Scope scope(context);
      Isolate* isolate = Isolate::Current();
      double baseline_total = 0, experimental_total = 0;
      for (size_t i = 0; i < fnames.size(); i++) {
        std::pair<TimeDelta, TimeDelta> times;
        check_tokens = check_tokens && run_baseline && run_experimental;
        times = ProcessFile(fnames[i].c_str(), encoding, isolate, run_baseline,
                            run_experimental, print_tokens, check_tokens,
                            break_after_illegal, repeat,
                            harmony_settings);
        baseline_total += times.first.InMillisecondsF();
        experimental_total += times.second.InMillisecondsF();
      }
      if (run_baseline) {
        printf("Baseline%s(RunTime): %.f ms\n", benchmark.c_str(),
               baseline_total);
      }
      if (run_experimental) {
        if (benchmark.empty()) benchmark = "Experimental";
        printf("%s(RunTime): %.f ms\n", benchmark.c_str(),
               experimental_total);
      }
    }
  }
  v8::V8::Dispose();
  return 0;
}
