// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/scanner-character-streams.h"

#include "include/v8.h"
#include "src/counters.h"
#include "src/globals.h"
#include "src/handles.h"
#include "src/objects-inl.h"
#include "src/parsing/scanner.h"
#include "src/unicode-inl.h"

namespace v8 {
namespace internal {

namespace {
const unibrow::uchar kUtf8Bom = 0xFEFF;
}  // namespace

template <typename Char>
struct HeapStringType;

template <>
struct HeapStringType<uint8_t> {
  typedef SeqOneByteString String;
};

template <>
struct HeapStringType<uint16_t> {
  typedef SeqTwoByteString String;
};

template <typename Char>
struct Range {
  const Char* start;
  const Char* end;

  size_t length() { return static_cast<size_t>(end - start); }
  bool unaligned_start() const {
    return reinterpret_cast<intptr_t>(start) % sizeof(Char) == 1;
  }
};

// A Char stream backed by an on-heap SeqOneByteString or SeqTwoByteString.
template <typename Char>
class OnHeapStream {
 public:
  typedef typename HeapStringType<Char>::String String;

  OnHeapStream(Handle<String> string, size_t start_offset, size_t end)
      : string_(string), start_offset_(start_offset), length_(end) {}

  Range<Char> GetDataAt(size_t pos) {
    return {&string_->GetChars()[start_offset_ + Min(length_, pos)],
            &string_->GetChars()[start_offset_ + length_]};
  }

  static const bool kCanAccessHeap = true;

 private:
  Handle<String> string_;
  const size_t start_offset_;
  const size_t length_;
};

// A Char stream backed by an off-heap ExternalOneByteString or
// ExternalTwoByteString.
template <typename Char>
class ExternalStringStream {
 public:
  ExternalStringStream(const Char* data, size_t end)
      : data_(data), length_(end) {}

  Range<Char> GetDataAt(size_t pos) {
    return {&data_[Min(length_, pos)], &data_[length_]};
  }

  static const bool kCanAccessHeap = false;

 private:
  const Char* const data_;
  const size_t length_;
};

// A Char stream backed by multiple source-stream provided off-heap chunks.
template <typename Char>
class ChunkedStream {
 public:
  ChunkedStream(ScriptCompiler::ExternalSourceStream* source,
                RuntimeCallStats* stats)
      : source_(source), stats_(stats) {}

  Range<Char> GetDataAt(size_t pos) {
    Chunk chunk = FindChunk(pos);
    size_t buffer_end = chunk.length;
    size_t buffer_pos = Min(buffer_end, pos - chunk.position);
    return {&chunk.data[buffer_pos], &chunk.data[buffer_end]};
  }

  ~ChunkedStream() {
    for (Chunk& chunk : chunks_) delete[] chunk.data;
  }

  static const bool kCanAccessHeap = false;

 private:
  struct Chunk {
    Chunk(const Char* const data, size_t position, size_t length)
        : data(data), position(position), length(length) {}
    const Char* const data;
    // The logical position of data.
    const size_t position;
    const size_t length;
    size_t end_position() const { return position + length; }
  };

  Chunk FindChunk(size_t position) {
    while (V8_UNLIKELY(chunks_.empty())) FetchChunk(size_t{0});

    // Walk forwards while the position is in front of the current chunk.
    while (position >= chunks_.back().end_position() &&
           chunks_.back().length > 0) {
      FetchChunk(chunks_.back().end_position());
    }

    // Walk backwards.
    for (auto reverse_it = chunks_.rbegin(); reverse_it != chunks_.rend();
         ++reverse_it) {
      if (reverse_it->position <= position) return *reverse_it;
    }

    UNREACHABLE();
  }

  virtual void ProcessChunk(const uint8_t* data, size_t position,
                            size_t length) {
    // Incoming data has to be aligned to Char size.
    DCHECK_EQ(0, length % sizeof(Char));
    chunks_.emplace_back(reinterpret_cast<const Char*>(data), position,
                         length / sizeof(Char));
  }

  void FetchChunk(size_t position) {
    const uint8_t* data = nullptr;
    size_t length;
    {
      RuntimeCallTimerScope scope(stats_,
                                  RuntimeCallCounterId::kGetMoreDataCallback);
      length = source_->GetMoreData(&data);
    }
    ProcessChunk(data, position, length);
  }

  ScriptCompiler::ExternalSourceStream* source_;
  RuntimeCallStats* stats_;

 protected:
  std::vector<struct Chunk> chunks_;
};

template <typename Char>
class Utf8ChunkedStream : public ChunkedStream<uint16_t> {
 public:
  Utf8ChunkedStream(ScriptCompiler::ExternalSourceStream* source,
                    RuntimeCallStats* stats)
      : ChunkedStream<uint16_t>(source, stats) {}

  STATIC_ASSERT(sizeof(Char) == sizeof(uint16_t));
  void ProcessChunk(const uint8_t* data, size_t position, size_t length) final {
    if (length == 0) {
      unibrow::uchar t = unibrow::Utf8::ValueOfIncrementalFinish(&state_);
      if (t != unibrow::Utf8::kBufferEmpty) {
        DCHECK_EQ(t, unibrow::Utf8::kBadChar);
        incomplete_char_ = 0;
        uint16_t* result = new uint16_t[1];
        result[0] = unibrow::Utf8::kBadChar;
        chunks_.emplace_back(result, position, 1);
        position++;
      }
      chunks_.emplace_back(nullptr, position, 0);
      delete[] data;
      return;
    }

    // First count the number of complete characters that can be produced.

    unibrow::Utf8::State state = state_;
    uint32_t incomplete_char = incomplete_char_;
    bool seen_bom = seen_bom_;

    size_t i = 0;
    size_t chars = 0;
    while (i < length) {
      unibrow::uchar t = unibrow::Utf8::ValueOfIncremental(data[i], &i, &state,
                                                           &incomplete_char);
      if (!seen_bom && t == kUtf8Bom && position + chars == 0) {
        seen_bom = true;
        // BOM detected at beginning of the stream. Don't copy it.
      } else if (t != unibrow::Utf8::kIncomplete) {
        chars++;
        if (t > unibrow::Utf16::kMaxNonSurrogateCharCode) chars++;
      }
    }

    // Process the data.

    // If there aren't any complete characters, update the state without
    // producing a chunk.
    if (chars == 0) {
      state_ = state;
      incomplete_char_ = incomplete_char;
      seen_bom_ = seen_bom;
      delete[] data;
      return;
    }

    // Update the state and produce a chunk with complete characters.
    uint16_t* result = new uint16_t[chars];
    uint16_t* cursor = result;
    i = 0;

    while (i < length) {
      unibrow::uchar t = unibrow::Utf8::ValueOfIncremental(data[i], &i, &state_,
                                                           &incomplete_char_);
      if (V8_LIKELY(t < kUtf8Bom)) {
        *(cursor++) = static_cast<uc16>(t);  // The by most frequent case.
      } else if (t == unibrow::Utf8::kIncomplete) {
        continue;
      } else if (!seen_bom_ && t == kUtf8Bom && position == 0 &&
                 cursor == result) {
        // BOM detected at beginning of the stream. Don't copy it.
        seen_bom_ = true;
      } else if (t <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
        *(cursor++) = static_cast<uc16>(t);
      } else {
        *(cursor++) = unibrow::Utf16::LeadSurrogate(t);
        *(cursor++) = unibrow::Utf16::TrailSurrogate(t);
      }
    }

    chunks_.emplace_back(result, position, chars);
    delete[] data;
  }

 private:
  uint32_t incomplete_char_ = 0;
  unibrow::Utf8::State state_ = unibrow::Utf8::State::kAccept;
  bool seen_bom_ = false;
};

// Provides a unbuffered utf-16 view on the bytes from the underlying
// ByteStream.
template <typename Char, template <typename T> class ByteStream>
class UnbufferedCharacterStream : public CharacterStream<Char> {
 public:
  template <class... TArgs>
  UnbufferedCharacterStream(size_t pos, TArgs... args) : byte_stream_(args...) {
    this->buffer_pos_ = pos;
  }

 protected:
  bool ReadBlock() final {
    size_t position = this->pos();
    this->buffer_pos_ = position;
    Range<Char> range = this->byte_stream_.GetDataAt(position);
    this->buffer_start_ = range.start;
    this->buffer_end_ = range.end;
    this->buffer_cursor_ = range.start;
    if (range.length() == 0) return false;

    DCHECK(!range.unaligned_start());
    DCHECK_LE(this->buffer_start_, this->buffer_end_);
    return true;
  }

  bool can_access_heap() final { return ByteStream<Char>::kCanAccessHeap; }

  ByteStream<Char> byte_stream_;
};

// Provides a unbuffered utf-16 view on the bytes from the underlying
// ByteStream.
template <typename Char>
class RelocatingCharacterStream
    : public UnbufferedCharacterStream<Char, OnHeapStream> {
 public:
  template <class... TArgs>
  RelocatingCharacterStream(Isolate* isolate, size_t pos, TArgs... args)
      : UnbufferedCharacterStream<Char, OnHeapStream>(pos, args...),
        isolate_(isolate) {
    isolate->heap()->AddGCEpilogueCallback(UpdateBufferPointersCallback,
                                           v8::kGCTypeAll, this);
  }

 private:
  ~RelocatingCharacterStream() final {
    isolate_->heap()->RemoveGCEpilogueCallback(UpdateBufferPointersCallback,
                                               this);
  }

  static void UpdateBufferPointersCallback(v8::Isolate* v8_isolate,
                                           v8::GCType type,
                                           v8::GCCallbackFlags flags,
                                           void* stream) {
    reinterpret_cast<RelocatingCharacterStream<Char>*>(stream)
        ->UpdateBufferPointers();
  }

  void UpdateBufferPointers() {
    Range<Char> range = this->byte_stream_.GetDataAt(0);
    if (range.start != this->buffer_start_) {
      this->buffer_cursor_ =
          (this->buffer_cursor_ - this->buffer_start_) + range.start;
      this->buffer_start_ = range.start;
      this->buffer_end_ = range.end;
    }
  }

  Isolate* isolate_;
};

// ----------------------------------------------------------------------------
// ScannerStream: Create stream instances.

#define SPECIALIZE(Call, ...)                                             \
  (is_two_byte_                                                           \
       ? static_cast<CharacterStream<uint16_t>*>(this)->Call(__VA_ARGS__) \
       : static_cast<CharacterStream<uint8_t>*>(this)->Call(__VA_ARGS__))

uc32 ScannerStream::Advance() { return SPECIALIZE(Advance); }

void ScannerStream::Seek(size_t pos) { SPECIALIZE(Seek, pos); }

size_t ScannerStream::pos() { return SPECIALIZE(pos); }

void ScannerStream::Back() { SPECIALIZE(Back); }

#undef SPECIALIZE

ScannerStream* ScannerStream::For(Isolate* isolate, Handle<String> data) {
  return ScannerStream::For(isolate, data, 0, data->length());
}

ScannerStream* ScannerStream::For(Isolate* isolate, Handle<String> data,
                                  int start_pos, int end_pos) {
  DCHECK_GE(start_pos, 0);
  DCHECK_LE(start_pos, end_pos);
  DCHECK_LE(end_pos, data->length());
  size_t start_offset = 0;
  if (data->IsSlicedString()) {
    SlicedString* string = SlicedString::cast(*data);
    start_offset = string->offset();
    String* parent = string->parent();
    if (parent->IsThinString()) parent = ThinString::cast(parent)->actual();
    data = handle(parent, isolate);
  } else {
    data = String::Flatten(isolate, data);
  }
  if (data->IsExternalOneByteString()) {
    return new UnbufferedCharacterStream<uint8_t, ExternalStringStream>(
        static_cast<size_t>(start_pos),
        ExternalOneByteString::cast(*data)->GetChars() + start_offset,
        static_cast<size_t>(end_pos));
  } else if (data->IsExternalTwoByteString()) {
    return new UnbufferedCharacterStream<uint16_t, ExternalStringStream>(
        static_cast<size_t>(start_pos),
        ExternalTwoByteString::cast(*data)->GetChars() + start_offset,
        static_cast<size_t>(end_pos));
  } else if (data->IsSeqOneByteString()) {
    return new RelocatingCharacterStream<uint8_t>(
        isolate, static_cast<size_t>(start_pos),
        Handle<SeqOneByteString>::cast(data), start_offset,
        static_cast<size_t>(end_pos));
  } else if (data->IsSeqTwoByteString()) {
    return new RelocatingCharacterStream<uint16_t>(
        isolate, static_cast<size_t>(start_pos),
        Handle<SeqTwoByteString>::cast(data), start_offset,
        static_cast<size_t>(end_pos));
  } else {
    UNREACHABLE();
  }
}

std::unique_ptr<CharacterStream<uint8_t>> ScannerStream::ForTesting(
    const char* data) {
  return ScannerStream::ForTesting(data, strlen(data));
}

std::unique_ptr<CharacterStream<uint8_t>> ScannerStream::ForTesting(
    const char* data, size_t length) {
  return std::unique_ptr<CharacterStream<uint8_t>>(
      new UnbufferedCharacterStream<uint8_t, ExternalStringStream>(
          static_cast<size_t>(0), reinterpret_cast<const uint8_t*>(data),
          static_cast<size_t>(length)));
}

ScannerStream* ScannerStream::For(
    ScriptCompiler::ExternalSourceStream* source_stream,
    v8::ScriptCompiler::StreamedSource::Encoding encoding,
    RuntimeCallStats* stats) {
  switch (encoding) {
    case v8::ScriptCompiler::StreamedSource::TWO_BYTE:
      return new UnbufferedCharacterStream<uint16_t, ChunkedStream>(
          static_cast<size_t>(0), source_stream, stats);
    case v8::ScriptCompiler::StreamedSource::ONE_BYTE:
      return new UnbufferedCharacterStream<uint8_t, ChunkedStream>(
          static_cast<size_t>(0), source_stream, stats);
    case v8::ScriptCompiler::StreamedSource::UTF8:
      return new UnbufferedCharacterStream<uint16_t, Utf8ChunkedStream>(
          static_cast<size_t>(0), source_stream, stats);
  }
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8
