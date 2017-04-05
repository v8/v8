// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_DECODER_H_
#define V8_WASM_DECODER_H_

#include <memory>

#include "src/base/compiler-specific.h"
#include "src/flags.h"
#include "src/signature.h"
#include "src/utils.h"
#include "src/wasm/wasm-result.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace wasm {

#if DEBUG
#define TRACE(...)                                    \
  do {                                                \
    if (FLAG_trace_wasm_decoder) PrintF(__VA_ARGS__); \
  } while (false)
#define TRACE_IF(cond, ...)                                     \
  do {                                                          \
    if (FLAG_trace_wasm_decoder && (cond)) PrintF(__VA_ARGS__); \
  } while (false)
#else
#define TRACE(...)
#define TRACE_IF(...)
#endif

// A helper utility to decode bytes, integers, fields, varints, etc, from
// a buffer of bytes.
class Decoder {
 public:
  Decoder(const byte* start, const byte* end)
      : start_(start), pc_(start), end_(end), error_pc_(nullptr) {}
  Decoder(const byte* start, const byte* pc, const byte* end)
      : start_(start), pc_(pc), end_(end), error_pc_(nullptr) {}

  virtual ~Decoder() {}

  inline bool check(const byte* pc, unsigned length, const char* msg) {
    DCHECK_LE(start_, pc);
    if (V8_UNLIKELY(pc + length > end_)) {
      error(pc, msg);
      return false;
    }
    return true;
  }

  // Reads a single 8-bit byte, reporting an error if out of bounds.
  inline uint8_t checked_read_u8(const byte* pc,
                                 const char* msg = "expected 1 byte") {
    return check(pc, 1, msg) ? *pc : 0;
  }

  // Reads 16-bit word, reporting an error if out of bounds.
  inline uint16_t checked_read_u16(const byte* pc,
                                   const char* msg = "expected 2 bytes") {
    return check(pc, 2, msg) ? read_u16(pc) : 0;
  }

  // Reads 32-bit word, reporting an error if out of bounds.
  inline uint32_t checked_read_u32(const byte* pc,
                                   const char* msg = "expected 4 bytes") {
    return check(pc, 4, msg) ? read_u32(pc) : 0;
  }

  // Reads 64-bit word, reporting an error if out of bounds.
  inline uint64_t checked_read_u64(const byte* pc,
                                   const char* msg = "expected 8 bytes") {
    return check(pc, 8, msg) ? read_u64(pc) : 0;
  }

  // Reads a variable-length unsigned integer (little endian).
  uint32_t checked_read_u32v(const byte* pc, unsigned* length,
                             const char* name = "LEB32") {
    return checked_read_leb<uint32_t, false, false>(pc, length, name);
  }

  // Reads a variable-length signed integer (little endian).
  int32_t checked_read_i32v(const byte* pc, unsigned* length,
                            const char* name = "signed LEB32") {
    return checked_read_leb<int32_t, false, false>(pc, length, name);
  }

  // Reads a variable-length unsigned integer (little endian).
  uint64_t checked_read_u64v(const byte* pc, unsigned* length,
                             const char* name = "LEB64") {
    return checked_read_leb<uint64_t, false, false>(pc, length, name);
  }

  // Reads a variable-length signed integer (little endian).
  int64_t checked_read_i64v(const byte* pc, unsigned* length,
                            const char* name = "signed LEB64") {
    return checked_read_leb<int64_t, false, false>(pc, length, name);
  }

  // Reads a single 16-bit unsigned integer (little endian).
  inline uint16_t read_u16(const byte* ptr) {
    DCHECK(ptr >= start_ && (ptr + 2) <= end_);
    return ReadLittleEndianValue<uint16_t>(ptr);
  }

  // Reads a single 32-bit unsigned integer (little endian).
  inline uint32_t read_u32(const byte* ptr) {
    DCHECK(ptr >= start_ && (ptr + 4) <= end_);
    return ReadLittleEndianValue<uint32_t>(ptr);
  }

  // Reads a single 64-bit unsigned integer (little endian).
  inline uint64_t read_u64(const byte* ptr) {
    DCHECK(ptr >= start_ && (ptr + 8) <= end_);
    return ReadLittleEndianValue<uint64_t>(ptr);
  }

  // Reads a 8-bit unsigned integer (byte) and advances {pc_}.
  uint8_t consume_u8(const char* name = nullptr) {
    TRACE("  +%d  %-20s: ", static_cast<int>(pc_ - start_),
          name ? name : "uint8_t");
    if (checkAvailable(1)) {
      byte val = *(pc_++);
      TRACE("%02x = %d\n", val, val);
      return val;
    }
    return traceOffEnd<uint8_t, true>();
  }

  // Reads a 16-bit unsigned integer (little endian) and advances {pc_}.
  uint16_t consume_u16(const char* name = nullptr) {
    TRACE("  +%d  %-20s: ", static_cast<int>(pc_ - start_),
          name ? name : "uint16_t");
    if (checkAvailable(2)) {
      uint16_t val = read_u16(pc_);
      TRACE("%02x %02x = %d\n", pc_[0], pc_[1], val);
      pc_ += 2;
      return val;
    }
    return traceOffEnd<uint16_t, true>();
  }

  // Reads a single 32-bit unsigned integer (little endian) and advances {pc_}.
  uint32_t consume_u32(const char* name = nullptr) {
    TRACE("  +%d  %-20s: ", static_cast<int>(pc_ - start_),
          name ? name : "uint32_t");
    if (checkAvailable(4)) {
      uint32_t val = read_u32(pc_);
      TRACE("%02x %02x %02x %02x = %u\n", pc_[0], pc_[1], pc_[2], pc_[3], val);
      pc_ += 4;
      return val;
    }
    return traceOffEnd<uint32_t, true>();
  }

  // Reads a LEB128 variable-length unsigned 32-bit integer and advances {pc_}.
  uint32_t consume_u32v(const char* name = nullptr) {
    unsigned length = 0;
    return checked_read_leb<uint32_t, true, true>(pc_, &length, name);
  }

  // Reads a LEB128 variable-length signed 32-bit integer and advances {pc_}.
  int32_t consume_i32v(const char* name = nullptr) {
    unsigned length = 0;
    return checked_read_leb<int32_t, true, true>(pc_, &length, name);
  }

  // Consume {size} bytes and send them to the bit bucket, advancing {pc_}.
  void consume_bytes(uint32_t size, const char* name = "skip") {
    // Only trace if the name is not null.
    TRACE_IF(name, "  +%d  %-20s: %d bytes\n", static_cast<int>(pc_ - start_),
             name, size);
    if (checkAvailable(size)) {
      pc_ += size;
    } else {
      pc_ = end_;
    }
  }

  // Check that at least {size} bytes exist between {pc_} and {end_}.
  bool checkAvailable(int size) {
    intptr_t pc_overflow_value = std::numeric_limits<intptr_t>::max() - size;
    if (size < 0 || (intptr_t)pc_ > pc_overflow_value) {
      errorf(pc_, "reading %d bytes would underflow/overflow", size);
      return false;
    } else if (pc_ < start_ || end_ < (pc_ + size)) {
      errorf(pc_, "expected %d bytes, fell off end", size);
      return false;
    } else {
      return true;
    }
  }

  void error(const char* msg) { errorf(pc_, "%s", msg); }

  void error(const byte* pc, const char* msg) { errorf(pc, "%s", msg); }

  // Sets internal error state.
  void PRINTF_FORMAT(3, 4) errorf(const byte* pc, const char* format, ...) {
    // Only report the first error.
    if (!ok()) return;
#if DEBUG
    if (FLAG_wasm_break_on_decoder_error) {
      base::OS::DebugBreak();
    }
#endif
    const int kMaxErrorMsg = 256;
    char* buffer = new char[kMaxErrorMsg];
    va_list arguments;
    va_start(arguments, format);
    base::OS::VSNPrintF(buffer, kMaxErrorMsg - 1, format, arguments);
    va_end(arguments);
    error_msg_.reset(buffer);
    error_pc_ = pc;
    onFirstError();
  }

  // Behavior triggered on first error, overridden in subclasses.
  virtual void onFirstError() {}

  // Debugging helper to print bytes up to the end.
  template <typename T, bool update_pc>
  T traceOffEnd() {
    for (const byte* ptr = pc_; ptr < end_; ptr++) {
      TRACE("%02x ", *ptr);
    }
    TRACE("<end>\n");
    if (update_pc) pc_ = end_;
    return T{0};
  }

  // Converts the given value to a {Result}, copying the error if necessary.
  template <typename T>
  Result<T> toResult(T val) {
    Result<T> result;
    if (failed()) {
      TRACE("Result error: %s\n", error_msg_.get());
      result.error_code = kError;
      result.start = start_;
      result.error_pc = error_pc_;
      // transfer ownership of the error to the result.
      result.error_msg.reset(error_msg_.release());
    } else {
      result.error_code = kSuccess;
    }
    result.val = std::move(val);
    return result;
  }

  // Resets the boundaries of this decoder.
  void Reset(const byte* start, const byte* end) {
    start_ = start;
    pc_ = start;
    end_ = end;
    error_pc_ = nullptr;
    error_msg_.reset();
  }

  bool ok() const { return error_msg_ == nullptr; }
  bool failed() const { return !ok(); }
  bool more() const { return pc_ < end_; }

  const byte* start() const { return start_; }
  const byte* pc() const { return pc_; }
  uint32_t pc_offset() const { return static_cast<uint32_t>(pc_ - start_); }
  const byte* end() const { return end_; }

 protected:
  const byte* start_;
  const byte* pc_;
  const byte* end_;
  const byte* error_pc_;
  std::unique_ptr<char[]> error_msg_;

 private:
  template <typename IntType, bool advance_pc, bool trace>
  inline IntType checked_read_leb(const byte* pc, unsigned* length,
                                  const char* name = "varint") {
    DCHECK_IMPLIES(advance_pc, pc == pc_);
    constexpr bool is_signed = std::is_signed<IntType>::value;
    TRACE_IF(trace, "  +%d  %-20s: ", static_cast<int>(pc - start_), name);
    constexpr int kMaxLength = (sizeof(IntType) * 8 + 6) / 7;
    const byte* ptr = pc;
    const byte* end = Min(end_, ptr + kMaxLength);
    int shift = 0;
    byte b = 0;
    IntType result = 0;
    for (;;) {
      if (V8_UNLIKELY(ptr >= end)) {
        TRACE_IF(trace, "<end> ");
        errorf(ptr, "expected %s", name);
        break;
      }
      b = *ptr++;
      TRACE_IF(trace, "%02x ", b);
      result = result | ((static_cast<IntType>(b) & 0x7F) << shift);
      shift += 7;
      if ((b & 0x80) == 0) break;
    }
    DCHECK_LE(ptr - pc, kMaxLength);
    *length = static_cast<unsigned>(ptr - pc);
    if (advance_pc) pc_ = ptr;
    if (*length == kMaxLength) {
      // A signed-LEB128 must sign-extend the final byte, excluding its
      // most-significant bit; e.g. for a 32-bit LEB128:
      //   kExtraBits = 4  (== 32 - (5-1) * 7)
      // For unsigned values, the extra bits must be all zero.
      // For signed values, the extra bits *plus* the most significant bit must
      // either be 0, or all ones.
      constexpr int kExtraBits = (sizeof(IntType) * 8) - ((kMaxLength - 1) * 7);
      constexpr int kSignExtBits = kExtraBits - (is_signed ? 1 : 0);
      const byte checked_bits = b & (0xFF << kSignExtBits);
      constexpr byte kSignExtendedExtraBits = 0x7f & (0xFF << kSignExtBits);
      if (checked_bits != 0 &&
          (!is_signed || checked_bits != kSignExtendedExtraBits)) {
        error(ptr, "extra bits in varint");
        return 0;
      }
    }
    if (is_signed && *length < kMaxLength) {
      int sign_ext_shift = 8 * sizeof(IntType) - shift;
      // Perform sign extension.
      result = (result << sign_ext_shift) >> sign_ext_shift;
    }
    if (trace && is_signed) {
      TRACE("= %" PRIi64 "\n", static_cast<int64_t>(result));
    } else if (trace) {
      TRACE("= %" PRIu64 "\n", static_cast<uint64_t>(result));
    }
    return result;
  }
};

#undef TRACE
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_DECODER_H_
