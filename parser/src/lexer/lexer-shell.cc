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
#include "lexer/lexer.h"

using namespace v8::internal;

static byte* ReadFile(const char* name, const byte** end, int repeat,
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

  if (!convert_to_utf16) return chars;

  // Length of new_chars is not strictly accurate, but should be enough.
  uint16_t* new_chars = new uint16_t[size];
  {
    Utf8ToUtf16CharacterStream stream(chars, size);
    uint16_t* cursor = new_chars;
    // uc32 c;
    // The 32-bit char type is probably only so that we can have -1 as a return
    // value. If the char is not -1, it should fit into 16 bits.
    CHECK(false);
    // while ((c = stream.Advance()) != -1) {
    //   *cursor++ = c;
    // }
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


struct LexerShellSettings {
  Encoding encoding;
  bool print_tokens;
  bool break_after_illegal;
  bool eos_test;
  int repeat;
  bool harmony_numeric_literals;
  bool harmony_modules;
  bool harmony_scoping;
  LexerShellSettings()
      : encoding(LATIN1),
        print_tokens(false),
        break_after_illegal(false),
        eos_test(false),
        repeat(1),
        harmony_numeric_literals(false),
        harmony_modules(false),
        harmony_scoping(false) {}
};


struct TokenWithLocation {
  Token::Value value;
  size_t beg;
  size_t end;
  std::vector<int> literal;
  bool is_ascii;
  // The location of the latest octal position when the token was seen.
  int octal_beg;
  int octal_end;
  TokenWithLocation() :
      value(Token::ILLEGAL), beg(0), end(0), is_ascii(false) { }
  TokenWithLocation(Token::Value value, size_t beg, size_t end,
                    int octal_beg) :
      value(value), beg(beg), end(end), is_ascii(false), octal_beg(octal_beg) {
  }
  bool operator==(const TokenWithLocation& other) {
    return value == other.value && beg == other.beg && end == other.end &&
           literal == other.literal && is_ascii == other.is_ascii &&
        octal_beg == other.octal_beg;
  }
  bool operator!=(const TokenWithLocation& other) {
    return !(*this == other);
  }
  void Print(const char* prefix) const {
    printf("%s %11s at (%d, %d)",
           prefix, Token::Name(value),
           static_cast<int>(beg), static_cast<int>(end));
    if (literal.size() > 0) {
      for (size_t i = 0; i < literal.size(); i++) {
        printf(is_ascii ? " %02x" : " %04x", literal[i]);
      }
      printf(" (is ascii: %d)", is_ascii);
    }
    printf(" (last octal start: %d)\n", octal_beg);
  }
};


static bool HasLiteral(Token::Value token) {
  return token == Token::IDENTIFIER ||
         token == Token::STRING ||
         token == Token::NUMBER;
}


template<typename Char>
static std::vector<int> ToStdVector(const Vector<Char>& literal) {
  std::vector<int> result;
  for (int i = 0; i < literal.length(); i++) {
    result.push_back(literal[i]);
  }
  return result;
}


template<typename Scanner>
static TokenWithLocation GetTokenWithLocation(
    Scanner *scanner, Token::Value token) {
  int beg = scanner->location().beg_pos;
  int end = scanner->location().end_pos;
  TokenWithLocation result(token, beg, end, scanner->octal_position().beg_pos);
  if (HasLiteral(token)) {
    result.is_ascii = scanner->is_literal_ascii();
    if (scanner->is_literal_ascii()) {
      result.literal = ToStdVector(scanner->literal_ascii_string());
    } else {
      result.literal = ToStdVector(scanner->literal_utf16_string());
    }
  }
  return result;
}


static TimeDelta RunLexer(const byte* source,
                          const byte* source_end,
                          Isolate* isolate,
                          std::vector<TokenWithLocation>* tokens,
                          const LexerShellSettings& settings) {
  SmartPointer<Utf16CharacterStream> stream;
  switch (settings.encoding) {
    case UTF8:
    case UTF8TO16:
      stream.Reset(new Utf8ToUtf16CharacterStream(source, source_end - source));
      break;
    case UTF16: {
      Handle<String> result = isolate->factory()->NewStringFromTwoByte(
          Vector<const uint16_t>(
              reinterpret_cast<const uint16_t*>(source),
              (source_end - source) / 2));
      stream.Reset(
          new GenericStringUtf16CharacterStream(result, 0, result->length()));
      break;
    }
    case LATIN1: {
      Handle<String> result = isolate->factory()->NewStringFromOneByte(
          Vector<const uint8_t>(source, source_end - source));
      stream.Reset(
          new GenericStringUtf16CharacterStream(result, 0, result->length()));
      break;
    }
  }
  Scanner scanner(isolate->unicode_cache());
  scanner.SetHarmonyNumericLiterals(settings.harmony_numeric_literals);
  scanner.SetHarmonyModules(settings.harmony_modules);
  scanner.SetHarmonyScoping(settings.harmony_scoping);
  ElapsedTimer timer;
  timer.Start();
  scanner.Initialize(stream.get());
  Token::Value token;
  do {
    token = scanner.Next();
    if (settings.print_tokens) {
      tokens->push_back(GetTokenWithLocation(&scanner, token));
    } else if (HasLiteral(token)) {
      if (scanner.is_literal_ascii()) {
        scanner.literal_ascii_string();
      } else {
        scanner.literal_utf16_string();
      }
    }
  } while (token != Token::EOS);
  return timer.Elapsed();
}


static TimeDelta ProcessFile(
    const char* fname,
    Isolate* isolate,
    const LexerShellSettings& settings,
    int truncate_by,
    bool* can_truncate) {
  if (settings.print_tokens) {
    printf("Processing file %s, truncating by %d bytes\n", fname, truncate_by);
  }
  HandleScope handle_scope(isolate);
  std::vector<TokenWithLocation> tokens;
  TimeDelta time;
  {
    const byte* buffer_end = 0;
    const byte* buffer = ReadFile(fname, &buffer_end, settings.repeat, false);
    if (truncate_by > buffer_end - buffer) {
      *can_truncate = false;
    } else {
      buffer_end -= truncate_by;
      time = RunLexer(buffer, buffer_end, isolate, &tokens, settings);
    }
    delete[] buffer;
  }
  if (settings.print_tokens) {
    printf("No of tokens:\t%d\n", static_cast<int>(tokens.size()));
    for (size_t i = 0; i < tokens.size(); ++i) {
      tokens[i].Print("=>");
      if (tokens[i].value == Token::ILLEGAL) {
        if (settings.break_after_illegal)
          break;
      }
    }
  }
  return time;
}


int main(int argc, char* argv[]) {
  v8::V8::InitializeICU();
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  std::vector<std::string> fnames;
  LexerShellSettings settings;
  for (int i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--latin1") == 0) {
      settings.encoding = LATIN1;
    } else if (strcmp(argv[i], "--utf8") == 0) {
      settings.encoding = UTF8;
    } else if (strcmp(argv[i], "--utf16") == 0) {
      settings.encoding = UTF16;
    } else if (strcmp(argv[i], "--utf8to16") == 0) {
      settings.encoding = UTF8TO16;
    } else if (strcmp(argv[i], "--print-tokens") == 0) {
      settings.print_tokens = true;
    } else if (strcmp(argv[i], "--no-baseline") == 0) {
      // Ignore.
    } else if (strcmp(argv[i], "--no-experimental") == 0) {
      // Ignore.
    } else if (strcmp(argv[i], "--no-check") == 0) {
      // Ignore.
    } else if (strcmp(argv[i], "--break-after-illegal") == 0) {
      settings.break_after_illegal = true;
    } else if (strcmp(argv[i], "--use-harmony") == 0) {
      settings.harmony_numeric_literals = true;
      settings.harmony_modules = true;
      settings.harmony_scoping = true;
    } else if (strncmp(argv[i], "--benchmark=", 12) == 0) {
      // Ignore.
    } else if (strncmp(argv[i], "--repeat=", 9) == 0) {
      std::string repeat_str = std::string(argv[i]).substr(9);
      settings.repeat = atoi(repeat_str.c_str());
    } else if (strcmp(argv[i], "--eos-test") == 0) {
      settings.eos_test = true;
    } else if (i > 0 && argv[i][0] != '-') {
      fnames.push_back(std::string(argv[i]));
    }
  }
  {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    CHECK(!context.IsEmpty());
    v8::Context::Scope scope(context);
    Isolate* internal_isolate = Isolate::Current();
    double total_time = 0;
    for (size_t i = 0; i < fnames.size(); i++) {
      std::pair<TimeDelta, TimeDelta> times;
      bool can_truncate = settings.eos_test;
      int truncate_by = 0;
      do {
        TimeDelta t = ProcessFile(fnames[i].c_str(),
                                  internal_isolate,
                                  settings,
                                  truncate_by,
                                  &can_truncate);
        total_time += t.InMillisecondsF();
        ++truncate_by;
      } while (can_truncate);
    }
    printf("RunTime: %.f ms\n", total_time);
  }
  v8::V8::Dispose();
  return 0;
}
