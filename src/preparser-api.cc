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

#include "../include/v8-preparser.h"
#include "globals.h"
#include "checks.h"
#include "allocation.h"
#include "utils.h"
#include "list.h"
#include "scanner-base.h"
#include "preparse-data.h"
#include "preparser.h"

namespace v8 {
namespace internal {

// UTF16Buffer based on a v8::UnicodeInputStream.
class InputStreamUTF16Buffer : public UC16CharacterStream {
 public:
  explicit InputStreamUTF16Buffer(v8::UnicodeInputStream* stream)
      : UC16CharacterStream(),
        stream_(stream),
        pushback_active_(false) {
    buffer_cursor_ = buffer_end_ = buffer_ + kPushBackSize;
  }

  virtual ~InputStreamUTF16Buffer() { }

  virtual void PushBack(uc16 ch) {
    ASSERT(pos_ > 0);
    if (buffer_cursor_ > buffer_) {
      // While we can stay within the buffer, just do so.
      *--buffer_cursor_ = ch;
      pos_--;
      return;
    }
    if (!pushback_active_) {
      // Push back the entire buffer to the stream and let the
      // stream handle pushbacks from now.
      // We leave buffer_cursor_ == buffer_end_, so the next read
      // will fill the buffer from the current position.
      // This should happen exceedingly rarely.
      while (buffer_end_ > buffer_) {
        stream_->PushBack(*--buffer_end_);
      }
      buffer_cursor_ = buffer_end_;
      pushback_active_ = true;
    }
    stream_->PushBack(ch);
    pos_--;
  }

 protected:
  virtual bool ReadBlock() {
    // Copy the top of the buffer into the pushback area.
    pushback_active_ = false;
    int32_t value;
    uc16* buffer_start = buffer_ + kPushBackSize;
    buffer_cursor_ = buffer_end_ = buffer_start;
    while ((value = stream_->Next()) >= 0) {
      if (value > static_cast<int32_t>(unibrow::Utf8::kMaxThreeByteChar)) {
        value = unibrow::Utf8::kBadChar;
      }
      // buffer_end_ is a const pointer, but buffer_ is writable.
      buffer_start[buffer_end_++ - buffer_start] = static_cast<uc16>(value);
      if (buffer_end_ == buffer_ + kPushBackSize + kBufferSize) break;
    }
    return buffer_end_ > buffer_start;
  }

  virtual unsigned SlowSeekForward(unsigned pos) {
    // Seeking in the input is not used by preparsing.
    // It's only used by the real parser based on preparser data.
    UNIMPLEMENTED();
    return 0;
  }

 private:
  static const unsigned kBufferSize = 512;
  static const unsigned kPushBackSize = 16;
  v8::UnicodeInputStream* const stream_;
  // Buffer holding first kPushBackSize characters of pushback buffer,
  // then kBufferSize chars of read-ahead.
  // The pushback buffer is only used if pushing back characters past
  // the start of a block.
  uc16 buffer_[kBufferSize + kPushBackSize];
  bool pushback_active_;
};


class StandAloneJavaScriptScanner : public JavaScriptScanner {
 public:
  void Initialize(UC16CharacterStream* source) {
    source_ = source;
    literal_flags_ = kLiteralString | kLiteralIdentifier;
    Init();
    // Skip initial whitespace allowing HTML comment ends just like
    // after a newline and scan first token.
    has_line_terminator_before_next_ = true;
    SkipWhiteSpace();
    Scan();
  }
};


// Functions declared by allocation.h

void FatalProcessOutOfMemory(const char* reason) {
  V8_Fatal(__FILE__, __LINE__, reason);
}

bool EnableSlowAsserts() { return true; }


}  // namespace internal.


UnicodeInputStream::~UnicodeInputStream() { }


PreParserData Preparse(UnicodeInputStream* input, size_t max_stack) {
  internal::InputStreamUTF16Buffer buffer(input);
  uintptr_t stack_limit = reinterpret_cast<uintptr_t>(&buffer) - max_stack;
  internal::StandAloneJavaScriptScanner scanner;
  scanner.Initialize(&buffer);
  internal::CompleteParserRecorder recorder;
  preparser::PreParser::PreParseResult result =
      preparser::PreParser::PreParseProgram(&scanner,
                                            &recorder,
                                            true,
                                            stack_limit);
  if (result == preparser::PreParser::kPreParseStackOverflow) {
    return PreParserData::StackOverflow();
  }
  internal::Vector<unsigned> pre_data = recorder.ExtractData();
  size_t size = pre_data.length() * sizeof(pre_data[0]);
  unsigned char* data = reinterpret_cast<unsigned char*>(pre_data.start());
  return PreParserData(size, data);
}

}  // namespace v8.


// Used by ASSERT macros and other immediate exits.
extern "C" void V8_Fatal(const char* file, int line, const char* format, ...) {
  exit(EXIT_FAILURE);
}
