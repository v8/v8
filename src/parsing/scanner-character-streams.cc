// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/scanner-character-streams.h"

#include <memory>

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
constexpr unibrow::uchar kUtf8Bom = 0xfeff;
constexpr uint8_t kUtf8BomBytes[] = {0xef, 0xbb, 0xbf};
}  // namespace

// ----------------------------------------------------------------------------
// BufferedUtf16CharacterStreams
//
// A buffered character stream based on a random access character
// source (ReadBlock can be called with pos() pointing to any position,
// even positions before the current).
class BufferedUtf16CharacterStream : public Utf16CharacterStream {
 public:
  BufferedUtf16CharacterStream();

 protected:
  static constexpr size_t kBufferCharacterSize = 512;

  bool ReadBlock() override;

  // FillBuffer should read up to kBufferSize characters at position and store
  // them into buffer_[0..]. It returns the number of characters stored.
  virtual size_t FillBuffer(size_t position) = 0;

  // Fixed sized buffer that this class reads from.
  // The base class' buffer_start_ should always point to buffer_.
  uc16 buffer_[kBufferCharacterSize];

  static constexpr size_t kBufferSizeInBytes =
      sizeof(BufferedUtf16CharacterStream::buffer_);
};

BufferedUtf16CharacterStream::BufferedUtf16CharacterStream()
    : Utf16CharacterStream(buffer_, buffer_, buffer_, 0) {}

bool BufferedUtf16CharacterStream::ReadBlock() {
  size_t position = pos();
  buffer_pos_ = position;
  buffer_start_ = buffer_cursor_ = buffer_;
  buffer_end_ = buffer_ + FillBuffer(position);
  DCHECK_EQ(pos(), position);
  DCHECK_LE(buffer_end_, buffer_start_ + kBufferCharacterSize);
  return buffer_cursor_ < buffer_end_;
}

// ----------------------------------------------------------------------------
// GenericStringUtf16CharacterStream.
//
// A stream w/ a data source being a (flattened) Handle<String>.

class GenericStringUtf16CharacterStream : public BufferedUtf16CharacterStream {
 public:
  GenericStringUtf16CharacterStream(Handle<String> data, size_t start_position,
                                    size_t end_position);

  bool can_access_heap() override { return true; }

 protected:
  size_t FillBuffer(size_t position) override;

  Handle<String> string_;
  size_t length_;
};

GenericStringUtf16CharacterStream::GenericStringUtf16CharacterStream(
    Handle<String> data, size_t start_position, size_t end_position)
    : string_(data), length_(end_position) {
  DCHECK_GE(end_position, start_position);
  DCHECK_GE(static_cast<size_t>(string_->length()),
            end_position - start_position);
  buffer_pos_ = start_position;
}

size_t GenericStringUtf16CharacterStream::FillBuffer(size_t from_pos) {
  if (from_pos >= length_) return 0;

  size_t length = i::Min(kBufferCharacterSize, length_ - from_pos);
  String::WriteToFlat<uc16>(*string_, buffer_, static_cast<int>(from_pos),
                            static_cast<int>(from_pos + length));
  return length;
}

// ----------------------------------------------------------------------------
// ExternalTwoByteStringUtf16CharacterStream.
//
// A stream whose data source is a Handle<ExternalTwoByteString>. It avoids
// all data copying.

class ExternalTwoByteStringUtf16CharacterStream : public Utf16CharacterStream {
 public:
  ExternalTwoByteStringUtf16CharacterStream(Handle<ExternalTwoByteString> data,
                                            size_t start_position,
                                            size_t end_position);

  bool can_access_heap() override { return false; }

 private:
  bool ReadBlock() override;

  const uc16* raw_data_;  // Pointer to the actual array of characters.
  size_t start_pos_;
  size_t end_pos_;
};

ExternalTwoByteStringUtf16CharacterStream::
    ExternalTwoByteStringUtf16CharacterStream(
        Handle<ExternalTwoByteString> data, size_t start_position,
        size_t end_position)
    : raw_data_(data->GetTwoByteData(static_cast<int>(start_position))),
      start_pos_(start_position),
      end_pos_(end_position) {
  buffer_start_ = raw_data_;
  buffer_cursor_ = raw_data_;
  buffer_end_ = raw_data_ + (end_pos_ - start_pos_);
  buffer_pos_ = start_pos_;
}

bool ExternalTwoByteStringUtf16CharacterStream::ReadBlock() {
  size_t position = pos();
  bool have_data = start_pos_ <= position && position < end_pos_;
  if (have_data) {
    buffer_pos_ = start_pos_;
    buffer_cursor_ = raw_data_ + (position - start_pos_),
    buffer_end_ = raw_data_ + (end_pos_ - start_pos_);
  } else {
    buffer_pos_ = position;
    buffer_cursor_ = raw_data_;
    buffer_end_ = raw_data_;
  }
  return have_data;
}

// ----------------------------------------------------------------------------
// ExternalOneByteStringUtf16CharacterStream
//
// A stream whose data source is a Handle<ExternalOneByteString>.

class ExternalOneByteStringUtf16CharacterStream
    : public BufferedUtf16CharacterStream {
 public:
  ExternalOneByteStringUtf16CharacterStream(Handle<ExternalOneByteString> data,
                                            size_t start_position,
                                            size_t end_position);

  // For testing:
  ExternalOneByteStringUtf16CharacterStream(const char* data, size_t length);

  bool can_access_heap() override { return false; }

 protected:
  size_t FillBuffer(size_t position) override;

  const uint8_t* raw_data_;  // Pointer to the actual array of characters.
  size_t length_;
};

ExternalOneByteStringUtf16CharacterStream::
    ExternalOneByteStringUtf16CharacterStream(
        Handle<ExternalOneByteString> data, size_t start_position,
        size_t end_position)
    : raw_data_(data->GetChars()), length_(end_position) {
  DCHECK(end_position >= start_position);
  buffer_pos_ = start_position;
}

ExternalOneByteStringUtf16CharacterStream::
    ExternalOneByteStringUtf16CharacterStream(const char* data, size_t length)
    : raw_data_(reinterpret_cast<const uint8_t*>(data)), length_(length) {}

size_t ExternalOneByteStringUtf16CharacterStream::FillBuffer(size_t from_pos) {
  if (from_pos >= length_) return 0;

  size_t length = Min(kBufferCharacterSize, length_ - from_pos);
  i::CopyCharsUnsigned(buffer_, raw_data_ + from_pos, length);
  return length;
}

namespace {

// A linked-list container for the chunk data.
// Shared by ChunksView and Chunks (which may execute on different threads).
struct Chunk {
  enum Type : uint8_t { ONE_BYTE, TWO_BYTE };
  Chunk() : Chunk(ONE_BYTE, 0, nullptr, 0, 0, 0) {}
  Chunk(Type type, int first_char_start_offset, const uint8_t* data,
        size_t byte_length, size_t char_length, size_t char_pos)
      : type(type),
        first_char_start_offset(first_char_start_offset),
        data(data),
        byte_length(byte_length),
        char_length(char_length),
        char_pos(char_pos) {}
  ~Chunk() { delete next.Value(); }
  static Chunk* CreateOneByte(const uint8_t* data, size_t byte_length,
                              size_t char_pos, size_t start_offset = 0) {
    return new Chunk(ONE_BYTE, static_cast<int>(start_offset), data,
                     byte_length, byte_length - start_offset, char_pos);
  }
  static Chunk* CreateTwoByte(const uint8_t* data, size_t byte_length,
                              size_t char_pos, bool odd_start) {
    size_t char_length = (odd_start + byte_length) / 2;
    return new Chunk(TWO_BYTE, odd_start, data, byte_length, char_length,
                     char_pos);
  }
  // Position of the first character which starts in this chunk.
  // This is always greater or equal to char_pos, so if no char starts in this
  // chunk (i.e. char_length == 0) char_pos+1 is returned.
  size_t FirstCharPosition() const {
    // There are no split chars for one byte chunks, but there might be some
    // start offset
    if (type == ONE_BYTE) return char_pos;
    return char_pos + (first_char_start_offset != 0);
  }
  size_t OffsetOf(size_t position) const {
    DCHECK_LE(FirstCharPosition(), position);
    DCHECK_GE(char_pos + char_length, position);
    switch (type) {
      case ONE_BYTE:
        return position - FirstCharPosition() + first_char_start_offset;
      case TWO_BYTE:
        return 2 * (position - FirstCharPosition()) + first_char_start_offset;
    }
    UNREACHABLE();
  }
  size_t CopyToBuffer(uint8_t* buffer, size_t size, size_t offset) const {
    switch (type) {
      case ONE_BYTE: {
        size_t chars_to_copy = i::Min(size / 2, byte_length - offset);
        i::CopyCharsUnsigned(reinterpret_cast<uc16*>(buffer), &data[offset],
                             chars_to_copy);
        return 2 * chars_to_copy;
      }
      case TWO_BYTE: {
        size_t bytes_to_copy = i::Min(size, byte_length - offset);
        i::MemCopy(buffer, &data[offset], bytes_to_copy);
        return bytes_to_copy;
      }
    }
    UNREACHABLE();
  }
  const Type type;
  // Offset to the first byte of first character that starts in this chunk.
  const int first_char_start_offset;
  const std::unique_ptr<const uint8_t[]> data;
  const size_t byte_length;
  const size_t char_length;
  const size_t char_pos;
  base::AtomicValue<const Chunk*> next{nullptr};
  const Chunk* prev = nullptr;
};

// An interface that delivers a sequence of Chunks.
class ChunkSource {
 public:
  virtual ~ChunkSource() {}
  // Fetch and return next chunk.
  // Returns nullptr if source is exhausted.
  // Should not be called after source is exhausted.
  virtual Chunk* GetNextChunk() = 0;
};

// Manages chunks, provides seeking to chunk containing start byte or end byte
// of a character. It cannot fetch new data, but can read data fetched by viewed
// stream (also data fetched after this stream view was created). Multiple
// ChunksViews can operate on same chunks concurrently.
class ChunksView {
 public:
  ChunksView(const ChunksView& other) = default;
  virtual ~ChunksView() {}

  // Return the chunk containing the last byte of the char at position.
  // Position is in chars not bytes.
  // If position is behind the end of the stream, nullptr is returned.
  virtual const Chunk* SeekToEndOf(size_t position) {
    DCHECK_NOT_NULL(current_);
    DCHECK_NOT_NULL(last_seen_);
    // We almost always 'stream', meaning we want data from the next chunk
    if (V8_LIKELY(position >= last_seen_->char_pos)) {
      // Fast-forward to last seen chunk
      current_ = last_seen_;
      // Continue going forward until position found or out of chunks
      while (position >= current_->char_pos + current_->char_length) {
        const Chunk* next = current_->next.Value();
        if (!next) return nullptr;
        current_ = last_seen_ = next;
      }
    } else if (position >= current_->char_pos) {
      // Go forward until position found or out of chunks
      while (position >= current_->char_pos + current_->char_length) {
        current_ = current_->next.Value();
        if (!current_) return nullptr;
      }
    } else {
      // Shortcut jump to first chunk
      DCHECK_NOT_NULL(start_.get());
      DCHECK_EQ(start_->char_pos, 0);
      DCHECK_EQ(start_->byte_length, 0);
      const Chunk* first = start_->next.Value();
      DCHECK_NOT_NULL(first);
      DCHECK_EQ(first->char_pos, 0);
      if (position < first->char_pos + first->char_length) {
        current_ = first;
      }
      // Go backwards until position found
      while (position < current_->char_pos) {
        current_ = current_->prev;
        DCHECK_NOT_NULL(current_);
      }
    }
    DCHECK_LE(current_->char_pos, position);
    DCHECK_LT(position, current_->char_pos + current_->char_length);
    return current_;
  }

  // Return the chunk containing the first byte of the char at position.
  // Position is in chars not bytes.
  // If position is behind the end of the stream, nullptr is returned.
  // This call also guarantees that a whole char is available.
  const Chunk* SeekToStartOf(size_t position) {
    const Chunk* chunk = SeekToEndOf(position);
    if (chunk) {
      while (V8_UNLIKELY(position < chunk->FirstCharPosition())) {
        chunk = chunk->prev;
        DCHECK_NOT_NULL(chunk);
      }
      current_ = chunk;
    }
    return chunk;
  }

  // Returns next chunk if there is one.
  // This function never blocks/fetches new data.
  const Chunk* MoveToNext() {
    DCHECK_NOT_NULL(current_);
    DCHECK_NOT_NULL(last_seen_);
    const Chunk* next = current_->next.Value();
    if (next) {
      // Stream view may move to a previously unseen chunk
      if (current_ == last_seen_) {
        last_seen_ = next;
      }
      current_ = next;
    }
    return next;
  }

 protected:
  ChunksView()
      : start_(std::make_shared<Chunk>()),
        current_(start_.get()),
        last_seen_(current_) {}

  // Start sentinel shared between stream and stream views
  // start_->next is first chunk with real data
  const std::shared_ptr<Chunk> start_;
  // Chunk we're currently at.
  // Seek*, MoveToNext alter/work in respect to this.
  const Chunk* current_;
  // Last chunk this instance of ChunksView has seen so far.
  const Chunk* last_seen_;
};

// A ChunksView subclass that does fetch new data from a ChunkSource if needed.
class Chunks : public ChunksView {
 public:
  Chunks(std::unique_ptr<ChunkSource> source, RuntimeCallStats* stats)
      : source_(std::move(source)),
        stats_(stats),
        last_added_(start_.get()),
        source_exhausted_(false) {}
  const Chunk* SeekToEndOf(size_t position) override {
    if (!source_exhausted_) {
      GetNewDataIfNeeded(position);
    }
    return ChunksView::SeekToEndOf(position);
  }

 protected:
  void GetNewDataIfNeeded(size_t position) {
    DCHECK(!source_exhausted_);
    DCHECK_NOT_NULL(last_added_);
    size_t char_end_pos = last_added_->char_pos + last_added_->char_length;
    // Get more data if needed.
    if (char_end_pos <= position) {
      RuntimeCallTimerScope scope(stats_,
                                  &RuntimeCallStats::GetMoreDataCallback);
      while (char_end_pos <= position) {
        Chunk* chunk = source_->GetNextChunk();
        if (chunk) {
          chunk->prev = last_added_;
          last_added_->next.SetValue(chunk);
          last_added_ = chunk;
          char_end_pos += chunk->char_length;
          DCHECK_EQ(char_end_pos,
                    last_added_->char_pos + last_added_->char_length);
        } else {
          source_exhausted_ = true;
          break;
        }
      }
    }
    DCHECK_NOT_NULL(last_added_);
    last_seen_ = last_added_;
    // We either got the chunk we were looking for, or source is exhausted.
    DCHECK_IMPLIES(!source_exhausted_, position < char_end_pos);
  }
  const std::unique_ptr<ChunkSource> source_;
  RuntimeCallStats* const stats_;
  Chunk* last_added_;
  bool source_exhausted_;
};

// ChunkSource for one-byte ExternalSourceStream
class OneByteChunkSource : public ChunkSource {
 public:
  explicit OneByteChunkSource(ScriptCompiler::ExternalSourceStream* source)
      : source_(source) {}
  Chunk* GetNextChunk() override {
    const uint8_t* data = nullptr;
    DCHECK_NOT_NULL(source_);
    const size_t byte_len = source_->GetMoreData(&data);
    if (V8_UNLIKELY(byte_len == 0)) {
      delete[] data;
      return nullptr;
    }
    size_t char_pos = byte_pos_;
    byte_pos_ += byte_len;
    return Chunk::CreateOneByte(data, byte_len, char_pos);
  }

 private:
  ScriptCompiler::ExternalSourceStream* const source_;
  size_t byte_pos_ = 0;
};

// ChunkSource for two-byte ExternalSourceStream
class TwoByteChunkSource : public ChunkSource {
 public:
  explicit TwoByteChunkSource(ScriptCompiler::ExternalSourceStream* source)
      : source_(source) {}
  Chunk* GetNextChunk() override {
    const uint8_t* data = nullptr;
    DCHECK_NOT_NULL(source_);
    const size_t byte_len = source_->GetMoreData(&data);
    if (V8_UNLIKELY(byte_len == 0)) {
      delete[] data;
      return nullptr;
    }
    bool odd_start = (byte_pos_ % 2) == 1;
    size_t char_pos = byte_pos_ / 2;
    byte_pos_ += byte_len;
    return Chunk::CreateTwoByte(data, byte_len, char_pos, odd_start);
  }

 private:
  ScriptCompiler::ExternalSourceStream* const source_;
  size_t byte_pos_ = 0;
};

// ChunkSource that decodes incoming utf8 to two-byte chunks if needed.
// Byte Order Mark at the beginning of the stream is skipped.
// ASCII only chunks are kept as one-byte chunks.
// Performance is optimized for source files with mostly ASCII characters.
class Utf8ChunkSource : public ChunkSource {
 public:
  explicit Utf8ChunkSource(ScriptCompiler::ExternalSourceStream* source)
      : source_(source),
        incomplete_char_(unibrow::Utf8::Utf8IncrementalBuffer(0)) {}

 protected:
  size_t ComputeAsciiPrefixLength(const uint8_t* data, size_t byte_length) {
    if (incomplete_char_ != unibrow::Utf8::Utf8IncrementalBuffer(0)) {
      return 0;
    }
    for (size_t i = 0; i < byte_length; ++i) {
      if (data[i] > unibrow::Utf8::kMaxOneByteChar) {
        return i;
      }
    }
    return byte_length;
  }
  size_t ConvertToUtf16(const uint8_t* data, size_t byte_length,
                        size_t ascii_prefix_len, const uint8_t** result) {
    DCHECK_IMPLIES(ascii_prefix_len != 0,
                   incomplete_char_ == unibrow::Utf8::Utf8IncrementalBuffer(0));
    // We get at most 1 character per byte.
    // Only exception is the first byte of chunk which may finish an incomplete
    // surrogate pair, hence addition of 1.
    uc16* decoded_data = new uc16[byte_length + 1];
    i::CopyCharsUnsigned(decoded_data, data, ascii_prefix_len);
    if (ascii_prefix_len > 0) {
      is_at_first_char_ = false;
    }
    size_t decoded_len = ascii_prefix_len;
    for (size_t i = ascii_prefix_len; i < byte_length; ++i) {
      unibrow::uchar t =
          unibrow::Utf8::ValueOfIncremental(data[i], &incomplete_char_);
      if (t == unibrow::Utf8::kIncomplete) continue;
      if (V8_LIKELY(t < kUtf8Bom)) {
        decoded_data[decoded_len++] = static_cast<uc16>(t);
      } else if (V8_UNLIKELY(is_at_first_char_ && t == kUtf8Bom)) {
        // Skip BOM at the beginning of the stream
        is_at_first_char_ = false;
      } else if (t <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
        decoded_data[decoded_len++] = static_cast<uc16>(t);
      } else {
        decoded_data[decoded_len++] = unibrow::Utf16::LeadSurrogate(t);
        decoded_data[decoded_len++] = unibrow::Utf16::TrailSurrogate(t);
      }
    }
    *result = reinterpret_cast<uint8_t*>(decoded_data);
    return 2 * decoded_len;
  }

  // Skip possible BOM at the beginning of the stream.
  // Return number of bytes skipped.
  size_t SkipBOM(const uint8_t* data, size_t byte_length) {
    if (V8_UNLIKELY(is_at_first_char_ && byte_length >= 3 &&
                    data[0] == kUtf8BomBytes[0] &&
                    data[1] == kUtf8BomBytes[1] &&
                    data[2] == kUtf8BomBytes[2])) {
      return 3;
    }
    return 0;
  }

  Chunk* GetNextChunk() override {
    const uint8_t* data = nullptr;
    size_t byte_length = 0;
    if (!is_at_end_) {
      byte_length = source_->GetMoreData(&data);
    }
    // end of stream
    if (V8_UNLIKELY(byte_length == 0)) {
      delete[] data;
      is_at_end_ = true;
      unibrow::uchar t =
          unibrow::Utf8::ValueOfIncrementalFinish(&incomplete_char_);
      if (t != unibrow::Utf8::kBufferEmpty) {
        DCHECK(t < unibrow::Utf16::kMaxNonSurrogateCharCode);
        uc16* char_data = new uc16[1];
        char_data[0] = static_cast<uc16>(t);
        return Chunk::CreateTwoByte(reinterpret_cast<uint8_t*>(char_data), 2,
                                    char_pos_++, false);
      }
      return nullptr;
    }
    size_t skipped = SkipBOM(data, byte_length);
    if (V8_UNLIKELY(skipped == byte_length)) {
      is_at_first_char_ = false;
      delete[] data;
      return GetNextChunk();
    }
    size_t ascii_prefix_len =
        ComputeAsciiPrefixLength(data + skipped, byte_length - skipped);
    bool is_ascii_only = ascii_prefix_len == (byte_length - skipped);
    if (V8_LIKELY(is_ascii_only)) {
      is_at_first_char_ = false;
      Chunk* chunk =
          Chunk::CreateOneByte(data, byte_length, char_pos_, skipped);
      char_pos_ += chunk->char_length;
      return chunk;
    }
    const uint8_t* decoded_data;
    size_t decoded_byte_len = ConvertToUtf16(
        data + skipped, byte_length - skipped, ascii_prefix_len, &decoded_data);
    delete[] data;
    if (V8_UNLIKELY(decoded_byte_len == 0)) {
      delete[] decoded_data;
      return GetNextChunk();
    }
    is_at_first_char_ = false;
    Chunk* chunk =
        Chunk::CreateTwoByte(decoded_data, decoded_byte_len, char_pos_, false);
    char_pos_ += chunk->char_length;
    return chunk;
  }

 private:
  ScriptCompiler::ExternalSourceStream* source_;
  unibrow::Utf8::Utf8IncrementalBuffer incomplete_char_;
  bool is_at_first_char_ = true;
  bool is_at_end_ = false;
  size_t char_pos_ = 0;
};

}  // anonymous namespace

// A stream of chunked data
class ExternalStreamingStream : public BufferedUtf16CharacterStream {
 public:
  explicit ExternalStreamingStream(std::unique_ptr<ChunkSource> source,
                                   RuntimeCallStats* stats)
      : chunks_(new Chunks(std::move(source), stats)) {}

  bool can_access_heap() override { return false; }

 protected:
  bool ReadBlock() override;
  size_t FillBuffer(size_t position) override;

  std::unique_ptr<ChunksView> chunks_;
};

bool ExternalStreamingStream::ReadBlock() {
  size_t position = pos();
  // Find chunk in which the position belongs
  const Chunk* chunk = chunks_->SeekToEndOf(position);

  // Out of data? Return false.
  if (!chunk) {
    buffer_pos_ = position;
    buffer_cursor_ = buffer_end_ = buffer_start_;
    return false;
  }

  bool odd_start = chunk->first_char_start_offset == 1;

  if (odd_start || chunk->type != Chunk::TWO_BYTE) {
    return BufferedUtf16CharacterStream::ReadBlock();
  }

  // Aligned access is important on MIPS and ARM.
  DCHECK_EQ((reinterpret_cast<uintptr_t>(chunk->data.get()) % 2), 0);

  buffer_start_ = reinterpret_cast<const uint16_t*>(chunk->data.get());
  buffer_end_ = buffer_start_ + chunk->char_length;
  buffer_pos_ = chunk->char_pos;
  buffer_cursor_ = buffer_start_ + (position - buffer_pos_);
  DCHECK_EQ(position, pos());
  return true;
}

size_t ExternalStreamingStream::FillBuffer(size_t position) {
  size_t copied_bytes = 0;
  const Chunk* chunk = chunks_->SeekToStartOf(position);
  // Out of data? Return 0.
  if (!chunk) {
    return 0;
  }
  size_t offset = chunk->OffsetOf(position);
  while (copied_bytes < kBufferSizeInBytes && chunk) {
    // Stop copying if next chunk could be used directly and at least one char
    // was copied.
    if (chunk->type == Chunk::TWO_BYTE && copied_bytes > 2 &&
        chunk->first_char_start_offset == 0) {
      break;
    }
    size_t space_left = kBufferSizeInBytes - copied_bytes;
    DCHECK_IMPLIES(chunk->type == Chunk::ONE_BYTE, (copied_bytes % 2) == 0);
    copied_bytes += chunk->CopyToBuffer(
        reinterpret_cast<uint8_t*>(buffer_) + copied_bytes, space_left, offset);
    // chunk after the first one are copied from beginning
    offset = 0;
    chunk = chunks_->MoveToNext();
  }
  return copied_bytes / 2;
}

// ----------------------------------------------------------------------------
// ScannerStream: Create stream instances.

Utf16CharacterStream* ScannerStream::For(Handle<String> data) {
  return ScannerStream::For(data, 0, data->length());
}

Utf16CharacterStream* ScannerStream::For(Handle<String> data, int start_pos,
                                         int end_pos) {
  DCHECK(start_pos >= 0);
  DCHECK(start_pos <= end_pos);
  DCHECK(end_pos <= data->length());
  if (data->IsExternalOneByteString()) {
    return new ExternalOneByteStringUtf16CharacterStream(
        Handle<ExternalOneByteString>::cast(data),
        static_cast<size_t>(start_pos), static_cast<size_t>(end_pos));
  } else if (data->IsExternalTwoByteString()) {
    return new ExternalTwoByteStringUtf16CharacterStream(
        Handle<ExternalTwoByteString>::cast(data),
        static_cast<size_t>(start_pos), static_cast<size_t>(end_pos));
  } else {
    // TODO(vogelheim): Maybe call data.Flatten() first?
    return new GenericStringUtf16CharacterStream(
        data, static_cast<size_t>(start_pos), static_cast<size_t>(end_pos));
  }
}

std::unique_ptr<Utf16CharacterStream> ScannerStream::ForTesting(
    const char* data) {
  return ScannerStream::ForTesting(data, strlen(data));
}

std::unique_ptr<Utf16CharacterStream> ScannerStream::ForTesting(
    const char* data, size_t length) {
  return std::unique_ptr<Utf16CharacterStream>(
      new ExternalOneByteStringUtf16CharacterStream(data, length));
}

Utf16CharacterStream* ScannerStream::For(
    ScriptCompiler::ExternalSourceStream* source_stream,
    v8::ScriptCompiler::StreamedSource::Encoding encoding,
    RuntimeCallStats* stats) {
  std::unique_ptr<ChunkSource> source;
  switch (encoding) {
    case v8::ScriptCompiler::StreamedSource::TWO_BYTE:
      source.reset(new TwoByteChunkSource(source_stream));
      break;
    case v8::ScriptCompiler::StreamedSource::ONE_BYTE:
      source.reset(new OneByteChunkSource(source_stream));
      break;
    case v8::ScriptCompiler::StreamedSource::UTF8:
      source.reset(new Utf8ChunkSource(source_stream));
      break;
  }
  DCHECK_NOT_NULL(source.get());
  return new ExternalStreamingStream(std::move(source), stats);
}

}  // namespace internal
}  // namespace v8
