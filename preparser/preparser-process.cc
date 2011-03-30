// Copyright 2010 the V8 project authors. All rights reserved.
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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "../include/v8stdint.h"
#include "../include/v8-preparser.h"

// This file is only used for testing the stand-alone preparser
// library.
// The first (and only) argument must be the path of a JavaScript file.
// This file is preparsed and the resulting preparser data is written
// to stdout. Diagnostic output is output on stderr.
// The file must contain only ASCII characters (UTF-8 isn't supported).
// The file is read into memory, so it should have a reasonable size.


// Adapts an ASCII string to the UnicodeInputStream interface.
class AsciiInputStream : public v8::UnicodeInputStream {
 public:
  AsciiInputStream(uint8_t* buffer, size_t length)
      : buffer_(buffer),
        end_offset_(static_cast<int>(length)),
        offset_(0) { }

  virtual ~AsciiInputStream() { }

  virtual void PushBack(int32_t ch) {
    offset_--;
#ifdef DEBUG
    if (offset_ < 0 ||
        (ch != ((offset_ >= end_offset_) ? -1 : buffer_[offset_]))) {
      fprintf(stderr, "Invalid pushback: '%c' at offset %d.", ch, offset_);
      exit(1);
    }
#endif
  }

  virtual int32_t Next() {
    if (offset_ >= end_offset_) {
      offset_++;  // Increment anyway to allow symmetric pushbacks.
      return -1;
    }
    uint8_t next_char = buffer_[offset_];
#ifdef DEBUG
    if (next_char > 0x7fu) {
      fprintf(stderr, "Non-ASCII character in input: '%c'.", next_char);
      exit(1);
    }
#endif
    offset_++;
    return static_cast<int32_t>(next_char);
  }

 private:
  const uint8_t* buffer_;
  const int end_offset_;
  int offset_;
};


bool ReadBuffer(FILE* source, void* buffer, size_t length) {
  size_t actually_read = fread(buffer, 1, length, source);
  return (actually_read == length);
}


bool WriteBuffer(FILE* dest, const void* buffer, size_t length) {
  size_t actually_written = fwrite(buffer, 1, length, dest);
  return (actually_written == length);
}


template <typename T>
class ScopedPointer {
 public:
  explicit ScopedPointer(T* pointer) : pointer_(pointer) {}
  ~ScopedPointer() { delete[] pointer_; }
  T& operator[](int index) { return pointer_[index]; }
  T* operator*() { return pointer_ ;}
 private:
  T* pointer_;
};


int main(int argc, char* argv[]) {
  // Check for filename argument.
  if (argc < 2) {
    fprintf(stderr, "ERROR: No filename on command line.\n");
    fflush(stderr);
    return EXIT_FAILURE;
  }
  const char* filename = argv[1];

  // Open JS file.
  FILE* input = fopen(filename, "rb");
  if (input == NULL) {
    perror("ERROR: Error opening file");
    fflush(stderr);
    return EXIT_FAILURE;
  }

  // Find length of JS file.
  if (fseek(input, 0, SEEK_END) != 0) {
    perror("ERROR: Error during seek");
    fflush(stderr);
    return EXIT_FAILURE;
  }
  size_t length = static_cast<size_t>(ftell(input));
  rewind(input);

  // Read JS file into memory buffer.
  ScopedPointer<uint8_t> buffer(new uint8_t[length]);
  if (!ReadBuffer(input, *buffer, length)) {
    perror("ERROR: Reading file");
    fflush(stderr);
    return EXIT_FAILURE;
  }
  fclose(input);

  // Preparse input file.
  AsciiInputStream input_buffer(*buffer, length);
  size_t kMaxStackSize = 64 * 1024 * sizeof(void*);  // NOLINT
  v8::PreParserData data = v8::Preparse(&input_buffer, kMaxStackSize);

  // Fail if stack overflow.
  if (data.stack_overflow()) {
    fprintf(stderr, "ERROR: Stack overflow\n");
    fflush(stderr);
    return EXIT_FAILURE;
  }

  // Print preparser data to stdout.
  uint32_t size = data.size();
  fprintf(stderr, "LOG: Success, data size: %u\n", size);
  fflush(stderr);
  if (!WriteBuffer(stdout, data.data(), size)) {
    perror("ERROR: Writing data");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
