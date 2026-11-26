// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_LITERAL_BUFFER_H_
#define V8_PARSING_LITERAL_BUFFER_H_

#include "include/v8config.h"
#include "src/base/strings.h"
#include "src/base/unique-array.h"
#include "src/base/vector.h"
#include "src/strings/unicode-decoder.h"
#include "src/utils/memcopy.h"

namespace v8 {
namespace internal {

// LiteralBuffer -  Collector of chars of literals.
class LiteralBuffer final {
 public:
  LiteralBuffer() = default;
  ~LiteralBuffer() = default;

  LiteralBuffer(const LiteralBuffer&) = delete;
  LiteralBuffer& operator=(const LiteralBuffer&) = delete;

  V8_INLINE void AddChar(char code_unit) {
    DCHECK(IsValidAscii(code_unit));
    AddOneByteChar(static_cast<uint8_t>(code_unit));
  }

  V8_INLINE void AddChar(base::uc32 code_unit) {
    if (is_one_byte()) {
      if (code_unit <= static_cast<base::uc32>(unibrow::Latin1::kMaxChar)) {
        AddOneByteChar(static_cast<uint8_t>(code_unit));
        return;
      }
      ConvertToTwoByte();
    }
    AddTwoByteChar(code_unit);
  }

  // Adds a range of UTF-16 code units. The buffer must be in two-byte mode.
  V8_INLINE void AddRangeFromUtf16(const uint16_t* begin, const uint16_t* end) {
    DCHECK(!is_one_byte());
    size_t size = static_cast<size_t>(end - begin) * base::kUC16Size;
    if (V8_UNLIKELY(position_ + size > backing_store_.size())) {
      ExpandBufferTo(position_ + size);
    }
    MemCopy(backing_store_.begin() + position_, begin, size);
    position_ += size;
  }

  // Ensures at least {min_extra} bytes of free space are available in one-byte
  // mode and returns a pointer to the current write position. Callers must
  // commit the number of bytes actually written via AdvancePosition.
  V8_INLINE uint8_t* EnsureOneByteCapacity(size_t min_extra) {
    DCHECK(is_one_byte());
    if (V8_UNLIKELY(position_ + min_extra > backing_store_.size())) {
      ExpandBufferTo(position_ + min_extra);
    }
    return backing_store_.begin() + position_;
  }

  V8_INLINE uint8_t* one_byte_cursor() {
    DCHECK(is_one_byte());
    return backing_store_.begin() + position_;
  }

  V8_INLINE size_t one_byte_free_capacity() const {
    DCHECK(is_one_byte());
    return backing_store_.size() - position_;
  }

  V8_INLINE void AdvancePosition(size_t delta) {
    DCHECK(is_one_byte());
    DCHECK_LE(position_ + delta, backing_store_.size());
    position_ += delta;
  }

  bool is_one_byte() const { return is_one_byte_; }

  bool Equals(base::Vector<const char> keyword) const {
    return is_one_byte() && keyword.size() == position_ &&
           (memcmp(keyword.begin(), backing_store_.begin(), position_) == 0);
  }

  base::Vector<const uint16_t> two_byte_literal() const {
    return literal<uint16_t>();
  }

  base::Vector<const uint8_t> one_byte_literal() const {
    return literal<uint8_t>();
  }

  template <typename Char>
  base::Vector<const Char> literal() const {
    DCHECK_EQ(is_one_byte_, sizeof(Char) == 1);
    DCHECK_EQ(position_ & (sizeof(Char) - 1), 0);
    return base::Vector<const Char>(
        reinterpret_cast<const Char*>(backing_store_.begin()),
        position_ >> (sizeof(Char) - 1));
  }

  int length() const {
    return static_cast<int>(is_one_byte() ? position_ : (position_ >> 1));
  }

  void Start() {
    position_ = 0;
    is_one_byte_ = true;
  }

 private:
  static constexpr size_t kInitialCapacity = 256;
  static constexpr size_t kGrowthFactor = 4;
  static constexpr size_t kMaxGrowth = 1 * MB;

  inline bool IsValidAscii(char code_unit) {
    // Control characters and printable characters span the range of
    // valid ASCII characters (0-127). Chars are unsigned on some
    // platforms which causes compiler warnings if the validity check
    // tests the lower bound >= 0 as it's always true.
    return iscntrl(code_unit) || isprint(code_unit);
  }

  V8_INLINE void AddOneByteCharUnchecked(uint8_t one_byte_char) {
    backing_store_[position_] = one_byte_char;
    position_ += kOneByteSize;
  }

  V8_INLINE void AddOneByteChar(uint8_t one_byte_char) {
    DCHECK(is_one_byte());
    if (V8_UNLIKELY(position_ >= backing_store_.size())) {
      return ExpandBufferAndAddOneByteChar(one_byte_char);
    }
    AddOneByteCharUnchecked(one_byte_char);
  }

  void AddTwoByteChar(base::uc32 code_unit);
  size_t NewCapacity(size_t min_capacity);
  V8_NOINLINE V8_PRESERVE_MOST void ExpandBuffer();
  V8_NOINLINE V8_PRESERVE_MOST void ExpandBufferTo(size_t min_size);
  V8_NOINLINE V8_PRESERVE_MOST void ExpandBufferAndAddOneByteChar(
      uint8_t one_byte_char);
  void ConvertToTwoByte();

  base::UniqueArray<uint8_t> backing_store_;
  size_t position_ = 0;
  bool is_one_byte_ = true;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_LITERAL_BUFFER_H_
