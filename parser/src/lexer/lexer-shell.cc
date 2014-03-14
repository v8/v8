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


enum Encoding {
  LATIN1,
  UTF8,
  UTF16,
  UTF8TO16,  // Convert stream via scanner input stream
  UTF8TO16_PRECONVERT  // Convert stream during file read
};


struct LexerShellSettings {
  Encoding encoding;
  bool print_tokens;
  bool print_tokens_for_compare;
  bool break_after_illegal;
  bool eos_test;
  int repeat;
  bool harmony_numeric_literals;
  bool harmony_modules;
  bool harmony_scoping;
  LexerShellSettings()
      : encoding(LATIN1),
        print_tokens(false),
        print_tokens_for_compare(false),
        break_after_illegal(false),
        eos_test(false),
        repeat(1),
        harmony_numeric_literals(false),
        harmony_modules(false),
        harmony_scoping(false) {}
};


static uint16_t* ConvertUtf8ToUtf16(const uint16_t* const data_in,
                                    unsigned* length) {
  const unsigned file_size = *length;
  const uint8_t* char_data = reinterpret_cast<const uint8_t*>(data_in);
  const uint32_t kMaxUtf16Character = 0xffff;
  // Get utf8 length.
  unsigned utf16_chars = 0;
  {
    unsigned position = 0;
    while (position < file_size) {
      uint32_t c = char_data[position];
      if (c <= unibrow::Utf8::kMaxOneByteChar) {
        position++;
      } else {
        c =  unibrow::Utf8::CalculateValue(char_data + position,
                                           file_size - position,
                                           &position);
      }
      if (c > kMaxUtf16Character) {
        utf16_chars += 2;
      } else {
        utf16_chars += 1;
      }
    }
  }
  // Write new buffer out.
  uint16_t* data = new uint16_t[utf16_chars];
  unsigned position = 0;
  unsigned i = 0;
  while (position < file_size) {
    uint32_t c = char_data[position];
    if (c <= unibrow::Utf8::kMaxOneByteChar) {
      position++;
    } else {
      c =  unibrow::Utf8::CalculateValue(char_data + position,
                                         file_size - position,
                                         &position);
    }
    if (c > kMaxUtf16Character) {
      data[i++] = unibrow::Utf16::LeadSurrogate(c);
      data[i++] = unibrow::Utf16::TrailSurrogate(c);
    } else {
      data[i++] = static_cast<uc16>(c);
    }
  }
  *length = 2 * utf16_chars;
  return data;
}


static uint16_t* Repeat(int repeat,
                        const uint16_t* const data_in,
                        unsigned* length) {
  const unsigned file_size = *length;
  unsigned size = file_size * repeat;
  uint16_t* data = new uint16_t[size / 2 + size % 2];
  uint8_t* char_data = reinterpret_cast<uint8_t*>(data);
  for (int i = 0; i < repeat; i++) {
    memcpy(&char_data[i * file_size], data_in, file_size);
  }
  *length = size;
  return data;
}


static uint16_t* ReadFile(const char* name, unsigned* length) {
  FILE* file = fopen(name, "rb");
  CHECK(file != NULL);
  // Get file size.
  fseek(file, 0, SEEK_END);
  unsigned file_size = ftell(file);
  rewind(file);
  // Read file contents.
  uint16_t* data = new uint16_t[file_size / 2 + file_size % 2];
  uint8_t* char_data = reinterpret_cast<uint8_t*>(data);
  for (unsigned i = 0; i < file_size;) {
    i += fread(&char_data[i], 1, file_size - i, file);
  }
  fclose(file);
  *length = file_size;
  return data;
}


static uint16_t* ReadFile(const char* name,
                          const LexerShellSettings& settings,
                          unsigned* length) {
  uint16_t* data = ReadFile(name, length);
  CHECK_GE(*length, 0);
  if (*length == 0) return data;

  if (settings.encoding == UTF8TO16_PRECONVERT) {
    uint16_t* new_data = ConvertUtf8ToUtf16(data, length);
    delete data;
    data = new_data;
  }

  if (settings.repeat > 1) {
    uint16_t* new_data = Repeat(settings.repeat, data, length);
    delete data;
    data = new_data;
  }

  return data;
}


static bool HasLiteral(Token::Value token) {
  return token == Token::IDENTIFIER ||
         token == Token::STRING ||
         token == Token::NUMBER;
}


template<typename Char>
static void Copy(const Vector<Char>& literal,
                 SmartArrayPointer<const uint16_t>* result,
                 int* literal_length) {
  uint16_t* data = new uint16_t[literal.length()];
  result->Reset(data);
  for (int i = 0; i < literal.length(); i++) {
    data[i] = literal[i];
  }
  *literal_length = literal.length();
}


class TokenWithLocation {
 public:
  Token::Value value;
  int beg;
  int end;
  bool is_one_byte;
  SmartArrayPointer<const uint16_t> literal;
  int literal_length;
  // The location of the latest octal position when the token was seen.
  int octal_beg;
  int octal_end;
  TokenWithLocation(Token::Value token,
                    Scanner* scanner,
                    Handle<String> literal_string)
      : value(token) {
    beg = scanner->location().beg_pos;
    end = scanner->location().end_pos;
    octal_beg = scanner->octal_position().beg_pos;
    octal_end = scanner->octal_position().end_pos;
    is_one_byte = false;
    literal_length = 0;
    if (!literal_string.is_null()) {
      DisallowHeapAllocation no_alloc;
      String::FlatContent content = literal_string->GetFlatContent();
      if (content.IsAscii()) {
        Copy(content.ToOneByteVector(), &literal, &literal_length);
      } else {
        Copy(content.ToUC16Vector(), &literal, &literal_length);
      }
    }
  }
  void Print(bool do_compare) const {
    if (value == Token::ILLEGAL && do_compare) {
      printf("%-15s (%d)\n", Token::Name(value), beg);
      return;
    }
    printf("%-15s (%d, %d)", Token::Name(value), beg, end);
    if (literal_length > 0) {
      // TODO(dcarney): need some sort of checksum.
      for (int i = 0; i < literal_length; i++) {
        printf(is_one_byte ? " %02x" : " %04x", literal[i]);
      }
      printf(" (is_one_byte: %d)", is_one_byte);
    }
    if (octal_beg >= 0) {
      printf(" (last octal start: %d)", octal_beg);
    }
    printf("\n");
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TokenWithLocation);
};


static TimeDelta RunLexer(const uint16_t* source,
                          const uint8_t* source_end,
                          Isolate* isolate,
                          const LexerShellSettings& settings) {
  SmartPointer<Utf16CharacterStream> stream;
  const uint8_t* one_byte_source = reinterpret_cast<const uint8_t*>(source);
  int bytes = source_end - one_byte_source;
  switch (settings.encoding) {
    case UTF8TO16:
    case UTF8:
      stream.Reset(new Utf8ToUtf16CharacterStream(one_byte_source, bytes));
      break;
    case UTF8TO16_PRECONVERT:
    case UTF16: {
      CHECK_EQ(0, bytes % 2);
      Handle<String> result = isolate->factory()->NewStringFromTwoByte(
          Vector<const uint16_t>(source, bytes / 2));
      stream.Reset(
          new GenericStringUtf16CharacterStream(result, 0, result->length()));
      break;
    }
    case LATIN1: {
      Handle<String> result = isolate->factory()->NewStringFromOneByte(
          Vector<const uint8_t>(one_byte_source, bytes));
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
  std::vector<TokenWithLocation*> tokens;
  timer.Start();
  scanner.Initialize(stream.get());
  Token::Value token;
  do {
    token = scanner.Next();
    Handle<String> literal;
    if (HasLiteral(token)) {
      literal = scanner.AllocateInternalizedString(isolate);
    }
    if (settings.print_tokens) {
      tokens.push_back(new TokenWithLocation(token, &scanner, literal));
    }
    if (token == Token::ILLEGAL && settings.break_after_illegal) break;
  } while (token != Token::EOS);
  // Dump tokens.
  if (settings.print_tokens) {
    if (!settings.print_tokens_for_compare) {
      printf("No of tokens:\t%d\n", static_cast<int>(tokens.size()));
    }
    for (size_t i = 0; i < tokens.size(); ++i) {
      tokens[i]->Print(settings.print_tokens_for_compare);
    }
  }
  for (size_t i = 0; i < tokens.size(); ++i) {
    delete tokens[i];
  }
  return timer.Elapsed();
}


static TimeDelta ProcessFile(
    const char* fname,
    Isolate* isolate,
    const LexerShellSettings& settings,
    int truncate_by,
    bool* can_truncate) {
  if (settings.print_tokens && !settings.print_tokens_for_compare) {
    printf("Processing file %s, truncating by %d bytes\n", fname, truncate_by);
  }
  HandleScope handle_scope(isolate);
  TimeDelta time;
  {
    unsigned length_in_bytes;
    const uint16_t* buffer = ReadFile(fname, settings, &length_in_bytes);
    const uint8_t* char_data = reinterpret_cast<const uint8_t*>(buffer);
    const uint8_t* buffer_end = &char_data[length_in_bytes];
    if (truncate_by > buffer_end - char_data) {
      *can_truncate = false;
    } else {
      buffer_end -= truncate_by;
      time = RunLexer(buffer, buffer_end, isolate, settings);
    }
    delete[] buffer;
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
#ifdef V8_USE_GENERATED_LEXER
      settings.encoding = UTF8TO16_PRECONVERT;
#else
      settings.encoding = UTF8TO16;
#endif
    } else if (strcmp(argv[i], "--print-tokens") == 0) {
      settings.print_tokens = true;
    } else if (strcmp(argv[i], "--print-tokens-for-compare") == 0) {
      settings.print_tokens = true;
      settings.print_tokens_for_compare = true;
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
    if (!settings.print_tokens_for_compare) {
      printf("RunTime: %.f ms\n", total_time);
    }
  }
  v8::V8::Dispose();
  return 0;
}
