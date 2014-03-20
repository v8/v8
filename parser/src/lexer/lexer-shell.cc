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
  UTF8TO16,
  UTF8TOLATIN1,
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


struct FileData {
  const char* file_name;
  unsigned length_in_bytes;
  Encoding encoding;
  const uint16_t* data;
};


static uint16_t* ConvertUtf8ToUtf16(const uint16_t* const data_in,
                                    unsigned* length_in_bytes,
                                    bool* is_one_byte) {
  const unsigned file_size = *length_in_bytes;
  const uint8_t* char_data = reinterpret_cast<const uint8_t*>(data_in);
  const uint32_t kMaxUtf16Character = 0xffff;
  // Get utf8 length.
  unsigned utf16_chars = 0;
  *is_one_byte = true;
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
      if (c > unibrow::Latin1::kMaxChar) *is_one_byte = false;
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
  *length_in_bytes = 2 * utf16_chars;
  return data;
}


static uint16_t* ConvertUtf16ToLatin1(const uint16_t* const data_in,
                                      unsigned* length_in_bytes) {
  const unsigned size = *length_in_bytes / 2 + *length_in_bytes % 2;
  uint16_t* data = new uint16_t[size];
  uint8_t* char_data = reinterpret_cast<uint8_t*>(data);
  CopyChars(char_data, data_in, size);
  *length_in_bytes = size;
  return data;
}


static uint16_t* Repeat(int repeat,
                        const uint16_t* const data_in,
                        unsigned* length_in_bytes) {
  const unsigned file_size = *length_in_bytes;
  unsigned size = file_size * repeat;
  uint16_t* data = new uint16_t[size / 2 + size % 2];
  uint8_t* char_data = reinterpret_cast<uint8_t*>(data);
  for (int i = 0; i < repeat; i++) {
    memcpy(&char_data[i * file_size], data_in, file_size);
  }
  *length_in_bytes = size;
  return data;
}


static uint16_t* ReadFile(const char* name, unsigned* length_in_bytes) {
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
  *length_in_bytes = file_size;
  return data;
}


static FileData ReadFile(const char* file_name,
                         const LexerShellSettings& settings) {
  unsigned length_in_bytes;
  uint16_t* data = ReadFile(file_name, &length_in_bytes);
  CHECK_GE(length_in_bytes, 0);

  Encoding encoding = settings.encoding;
  if (encoding == UTF8TO16 || encoding == UTF8TOLATIN1) {
    bool is_one_byte;
    uint16_t* new_data = ConvertUtf8ToUtf16(
        data, &length_in_bytes, &is_one_byte);
    if (encoding == UTF8TOLATIN1 && is_one_byte) {
      encoding = LATIN1;
    } else {
      encoding = UTF16;
    }
    delete data;
    data = new_data;
  }

  if (settings.encoding == UTF8TOLATIN1 && encoding == LATIN1) {
    uint16_t* new_data = ConvertUtf16ToLatin1(data, &length_in_bytes);
    delete data;
    data = new_data;
  }

  if (settings.repeat > 1) {
    uint16_t* new_data = Repeat(settings.repeat, data, &length_in_bytes);
    delete data;
    data = new_data;
  }

  FileData file_data;
  file_data.file_name = file_name;
  file_data.data = data;
  file_data.length_in_bytes = length_in_bytes;
  file_data.encoding = encoding;

  return file_data;
}


static bool HasLiteral(Token::Value token) {
  return token == Token::IDENTIFIER ||
         token == Token::STRING ||
         token == Token::NUMBER;
}


class TokenWithLocation {
 public:
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
      literal_length = literal_string->length();
      literal.Reset(new uint16_t[literal_length]);
      if (content.IsAscii()) {
        is_one_byte = true;
        CopyChars(
          literal.get(), content.ToOneByteVector().start(), literal_length);
      } else {
        CopyChars(
          literal.get(), content.ToUC16Vector().start(), literal_length);
      }
    }
  }
  void Print(bool do_compare) const {
    if (value == Token::ILLEGAL && do_compare) {
      printf("%-15s (%d)\n", Token::Name(value), beg);
      return;
    }
    printf("%-15s (%d, %d)", Token::Name(value), beg, end);
    if (literal.get() != NULL) {
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
  Token::Value value;
  int beg;
  int end;
  bool is_one_byte;
  SmartArrayPointer<uint16_t> literal;
  int literal_length;
  // The location of the latest octal position when the token was seen.
  int octal_beg;
  int octal_end;

  DISALLOW_COPY_AND_ASSIGN(TokenWithLocation);
};


typedef std::vector<TokenWithLocation*> TokenVector;


static TimeDelta RunLexer(const uint16_t* source,
                          const uint8_t* source_end,
                          Isolate* isolate,
                          Encoding output_encoding,
                          const LexerShellSettings& settings,
                          TokenVector* tokens) {
  SmartPointer<Utf16CharacterStream> stream;
  const uint8_t* one_byte_source = reinterpret_cast<const uint8_t*>(source);
  CHECK_GE(source_end - one_byte_source, 0);
  int bytes = source_end - one_byte_source;
  switch (output_encoding) {
    case UTF8:
      stream.Reset(new Utf8ToUtf16CharacterStream(one_byte_source, bytes));
      break;
    case UTF16: {
      CHECK_EQ(0, bytes % 2);
      Handle<String> result = isolate->factory()->NewStringFromTwoByte(
          Vector<const uint16_t>(source, bytes / 2), false);
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
    case UTF8TO16:
    case UTF8TOLATIN1:
      CHECK(false);
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
    Handle<String> literal;
    if (HasLiteral(token)) {
      literal = scanner.AllocateInternalizedString(isolate);
    }
    if (settings.print_tokens) {
      tokens->push_back(new TokenWithLocation(token, &scanner, literal));
    }
    if (token == Token::ILLEGAL && settings.break_after_illegal) break;
  } while (token != Token::EOS);
  return timer.Elapsed();
}


static void Run(const LexerShellSettings& settings,
                const FileData& file_data) {
  Isolate* isolate = Isolate::Current();
  HandleScope handle_scope(isolate);
  v8::Context::Scope scope(v8::Context::New(v8::Isolate::GetCurrent()));
  double total_time = 0;
  std::vector<TokenWithLocation*> tokens;
  const uint16_t* const buffer = file_data.data;
  const uint8_t* const char_data = reinterpret_cast<const uint8_t*>(buffer);
  for (unsigned truncate_by = 0;
       truncate_by <= file_data.length_in_bytes;
       truncate_by += file_data.encoding == UTF16 ? 2 : 1) {
    if (settings.print_tokens && !settings.print_tokens_for_compare) {
      printf("Processing file %s, truncating by %d bytes\n",
             file_data.file_name, truncate_by);
    }
    HandleScope handle_scope(isolate);
    const uint8_t* buffer_end =
      &char_data[file_data.length_in_bytes] - truncate_by;
    TimeDelta delta = RunLexer(
        buffer, buffer_end, isolate, file_data.encoding, settings, &tokens);
    total_time += delta.InMillisecondsF();
    // Dump tokens.
    if (settings.print_tokens) {
      if (!settings.print_tokens_for_compare) {
        printf("No of tokens:\t%d\n", static_cast<int>(tokens.size()));
      }
      for (size_t i = 0; i < tokens.size(); ++i) {
        tokens[i]->Print(settings.print_tokens_for_compare);
      }
    }
    // Destroy tokens.
    for (size_t i = 0; i < tokens.size(); ++i) {
      delete tokens[i];
    }
    tokens.clear();
    if (!settings.eos_test) break;
  }
  if (!settings.print_tokens_for_compare) {
    printf("RunTime: %.f ms\n", total_time);
  }
}


int main(int argc, char* argv[]) {
  v8::V8::InitializeICU();
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  std::string file_name;
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
      settings.encoding = UTF8TO16;
#else
      settings.encoding = UTF8;
#endif
    } else if (strcmp(argv[i], "--utf8tolatin1") == 0) {
#ifdef V8_USE_GENERATED_LEXER
      settings.encoding = UTF8TOLATIN1;
#else
      settings.encoding = UTF8;
#endif
    } else if (strcmp(argv[i], "--print-tokens") == 0) {
      settings.print_tokens = true;
    } else if (strcmp(argv[i], "--print-tokens-for-compare") == 0) {
      settings.print_tokens = true;
      settings.print_tokens_for_compare = true;
    } else if (strcmp(argv[i], "--break-after-illegal") == 0) {
      settings.break_after_illegal = true;
    } else if (strcmp(argv[i], "--use-harmony") == 0) {
      settings.harmony_numeric_literals = true;
      settings.harmony_modules = true;
      settings.harmony_scoping = true;
    } else if (strncmp(argv[i], "--repeat=", 9) == 0) {
      std::string repeat_str = std::string(argv[i]).substr(9);
      settings.repeat = atoi(repeat_str.c_str());
    } else if (strcmp(argv[i], "--eos-test") == 0) {
      settings.eos_test = true;
    } else if (i > 0 && argv[i][0] != '-') {
      file_name = std::string(argv[i]);
    }
  }
  CHECK_NE(0, file_name.size());
  FileData file_data = ReadFile(file_name.c_str(), settings);
  Run(settings, file_data);
  delete[] file_data.data;
  v8::V8::Dispose();
  return 0;
}
