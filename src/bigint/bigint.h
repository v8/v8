// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BIGINT_BIGINT_H_
#define V8_BIGINT_BIGINT_H_

#include <stdint.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace v8 {
namespace bigint {

// To play nice with embedders' macros, we define our own DCHECK here.
// It's only used in this file, and undef'ed at the end.
#ifdef DEBUG
#define BIGINT_H_DCHECK(cond)                         \
  if (!(cond)) {                                      \
    std::cerr << __FILE__ << ":" << __LINE__ << ": "; \
    std::cerr << "Assertion failed: " #cond "\n";     \
    abort();                                          \
  }

extern bool kAdvancedAlgorithmsEnabledInLibrary;
#else
#define BIGINT_H_DCHECK(cond) (void(0))
#endif

// The type of a digit: a register-width unsigned integer.
using digit_t = uintptr_t;
using signed_digit_t = intptr_t;
#if UINTPTR_MAX == 0xFFFFFFFF
// 32-bit platform.
using twodigit_t = uint64_t;
#define HAVE_TWODIGIT_T 1
static constexpr int kLog2DigitBits = 5;
#elif UINTPTR_MAX == 0xFFFFFFFFFFFFFFFF
// 64-bit platform.
static constexpr int kLog2DigitBits = 6;
#if defined(__SIZEOF_INT128__)
using twodigit_t = __uint128_t;
#define HAVE_TWODIGIT_T 1
#endif  // defined(__SIZEOF_INT128__)
#else
#error Unsupported platform.
#endif
static constexpr int kDigitBits = 1 << kLog2DigitBits;
static_assert(kDigitBits == 8 * sizeof(digit_t), "inconsistent type sizes");

// Describes an array of digits, also known as a BigInt. Unsigned.
// Does not own the memory it points at, and only gives read-only access to it.
// Digits are stored in little-endian order.
class Digits {
 public:
  // This is the constructor intended for public consumption.
  Digits(digit_t* mem, int len) : digits_(mem), len_(len) {
    // Require 4-byte alignment (even on 64-bit platforms).
    // TODO(jkummerow): See if we can tighten BigInt alignment in V8 to
    // system pointer size, and raise this requirement to that.
    BIGINT_H_DCHECK((reinterpret_cast<uintptr_t>(mem) & 3) == 0);
  }
  // Provides a "slice" view into another Digits object.
  Digits(Digits src, int offset, int len)
      : digits_(src.digits_ + offset),
        len_(std::max(0, std::min(src.len_ - offset, len))) {
    BIGINT_H_DCHECK(offset >= 0);
  }
  // Alternative way to get a "slice" view into another Digits object.
  Digits operator+(int i) {
    BIGINT_H_DCHECK(i >= 0 && i <= len_);
    return Digits(digits_ + i, len_ - i);
  }

  // Provides access to individual digits.
  digit_t operator[](int i) {
    BIGINT_H_DCHECK(i >= 0 && i < len_);
    return read_4byte_aligned(i);
  }
  // Convenience accessor for the most significant digit.
  digit_t msd() {
    BIGINT_H_DCHECK(len_ > 0);
    return read_4byte_aligned(len_ - 1);
  }
  // Checks "pointer equality" (does not compare digits contents).
  bool operator==(const Digits& other) const {
    return digits_ == other.digits_ && len_ == other.len_;
  }

  // Decrements {len_} until there are no leading zero digits left.
  void Normalize() {
    while (len_ > 0 && msd() == 0) len_--;
  }
  // Unconditionally drops exactly one leading zero digit.
  void TrimOne() {
    BIGINT_H_DCHECK(len_ > 0 && msd() == 0);
    len_--;
  }

  int len() { return len_; }
  const digit_t* digits() const { return digits_; }

 protected:
  friend class ShiftedDigits;
  digit_t* digits_;
  int len_;

 private:
  // We require externally-provided digits arrays to be 4-byte aligned, but
  // not necessarily 8-byte aligned; so on 64-bit platforms we use memcpy
  // to allow unaligned reads.
  digit_t read_4byte_aligned(int i) {
    if (sizeof(digit_t) == 4) {
      return digits_[i];
    } else {
      digit_t result;
      memcpy(&result, digits_ + i, sizeof(result));
      return result;
    }
  }
};

// Writable version of a Digits array.
// Does not own the memory it points at.
class RWDigits : public Digits {
 public:
  RWDigits(digit_t* mem, int len) : Digits(mem, len) {}
  RWDigits(RWDigits src, int offset, int len) : Digits(src, offset, len) {}
  RWDigits operator+(int i) {
    BIGINT_H_DCHECK(i >= 0 && i <= len_);
    return RWDigits(digits_ + i, len_ - i);
  }

#if UINTPTR_MAX == 0xFFFFFFFF
  digit_t& operator[](int i) {
    BIGINT_H_DCHECK(i >= 0 && i < len_);
    return digits_[i];
  }
#else
  // 64-bit platform. We only require digits arrays to be 4-byte aligned,
  // so we use a wrapper class to allow regular array syntax while
  // performing unaligned memory accesses under the hood.
  class WritableDigitReference {
   public:
    // Support "X[i] = x" notation.
    void operator=(digit_t digit) { memcpy(ptr_, &digit, sizeof(digit)); }
    // Support "X[i] = Y[j]" notation.
    WritableDigitReference& operator=(const WritableDigitReference& src) {
      memcpy(ptr_, src.ptr_, sizeof(digit_t));
      return *this;
    }
    // Support "x = X[i]" notation.
    operator digit_t() {
      digit_t result;
      memcpy(&result, ptr_, sizeof(result));
      return result;
    }

   private:
    // This class is not for public consumption.
    friend class RWDigits;
    // Primary constructor.
    explicit WritableDigitReference(digit_t* ptr)
        : ptr_(reinterpret_cast<uint32_t*>(ptr)) {}
    // Required for returning WDR instances from "operator[]" below.
    WritableDigitReference(const WritableDigitReference& src) = default;

    uint32_t* ptr_;
  };

  WritableDigitReference operator[](int i) {
    BIGINT_H_DCHECK(i >= 0 && i < len_);
    return WritableDigitReference(digits_ + i);
  }
#endif

  digit_t* digits() { return digits_; }
  void set_len(int len) { len_ = len; }

  void Clear() { memset(digits_, 0, len_ * sizeof(digit_t)); }
};

class Platform {
 public:
  virtual ~Platform() = default;

  // If you want the ability to interrupt long-running operations, implement
  // a Platform subclass that overrides this method. It will be queried
  // every now and then by long-running operations.
  virtual bool InterruptRequested() { return false; }
};

// These are the operations that this library supports.
// The signatures follow the convention:
//
//   void Operation(RWDigits results, Digits inputs);
//
// You must preallocate the result; use the respective {OperationResultLength}
// function to determine its minimum required length. The actual result may
// be smaller, so you should call result.Normalize() on the result.
//
// The operations are divided into two groups: "fast" (O(n) with small
// coefficient) operations are exposed directly as free functions, "slow"
// operations are methods on a {Processor} object, which provides
// support for interrupting execution via the {Platform}'s {InterruptRequested}
// mechanism when it takes too long. These functions return a {Status} value.

// Returns r such that r < 0 if A < B; r > 0 if A > B; r == 0 if A == B.
// Defined here to be inlineable, which helps ia32 a lot (64-bit platforms
// don't care).
inline int Compare(Digits A, Digits B) {
  A.Normalize();
  B.Normalize();
  int diff = A.len() - B.len();
  if (diff != 0) return diff;
  int i = A.len() - 1;
  while (i >= 0 && A[i] == B[i]) i--;
  if (i < 0) return 0;
  return A[i] > B[i] ? 1 : -1;
}

// Z := X + Y
void Add(RWDigits Z, Digits X, Digits Y);
// Addition of signed integers. Returns true if the result is negative.
bool AddSigned(RWDigits Z, Digits X, bool x_negative, Digits Y,
               bool y_negative);

// Z := X - Y. Requires X >= Y.
void Subtract(RWDigits Z, Digits X, Digits Y);
// Subtraction of signed integers. Returns true if the result is negative.
bool SubtractSigned(RWDigits Z, Digits X, bool x_negative, Digits Y,
                    bool y_negative);

enum class Status { kOk, kInterrupted };

class FromStringAccumulator;

class Processor {
 public:
  // Takes ownership of {platform}.
  static Processor* New(Platform* platform);

  // Use this for any std::unique_ptr holding an instance of {Processor}.
  class Destroyer {
   public:
    void operator()(Processor* proc) { proc->Destroy(); }
  };
  // When not using std::unique_ptr, call this to delete the instance.
  void Destroy();

  // Z := X * Y
  Status Multiply(RWDigits Z, Digits X, Digits Y);
  // Q := A / B
  Status Divide(RWDigits Q, Digits A, Digits B);
  // R := A % B
  Status Modulo(RWDigits R, Digits A, Digits B);

  // {out_length} initially contains the allocated capacity of {out}, and
  // upon return will be set to the actual length of the result string.
  Status ToString(char* out, int* out_length, Digits X, int radix, bool sign);

  Status FromString(RWDigits Z, FromStringAccumulator* accumulator);
};

inline int AddResultLength(int x_length, int y_length) {
  return std::max(x_length, y_length) + 1;
}
inline int AddSignedResultLength(int x_length, int y_length, bool same_sign) {
  return same_sign ? AddResultLength(x_length, y_length)
                   : std::max(x_length, y_length);
}
inline int SubtractResultLength(int x_length, int y_length) { return x_length; }
inline int SubtractSignedResultLength(int x_length, int y_length,
                                      bool same_sign) {
  return same_sign ? std::max(x_length, y_length)
                   : AddResultLength(x_length, y_length);
}
inline int MultiplyResultLength(Digits X, Digits Y) {
  return X.len() + Y.len();
}
constexpr int kBarrettThreshold = 13310;
inline int DivideResultLength(Digits A, Digits B) {
#if V8_ADVANCED_BIGINT_ALGORITHMS
  BIGINT_H_DCHECK(kAdvancedAlgorithmsEnabledInLibrary);
  // The Barrett division algorithm needs one extra digit for temporary use.
  int kBarrettExtraScratch = B.len() >= kBarrettThreshold ? 1 : 0;
#else
  // If this fails, set -DV8_ADVANCED_BIGINT_ALGORITHMS in any compilation unit
  // that #includes this header.
  BIGINT_H_DCHECK(!kAdvancedAlgorithmsEnabledInLibrary);
  constexpr int kBarrettExtraScratch = 0;
#endif
  return A.len() - B.len() + 1 + kBarrettExtraScratch;
}
inline int ModuloResultLength(Digits B) { return B.len(); }

int ToStringResultLength(Digits X, int radix, bool sign);
// In DEBUG builds, the result of {ToString} will be initialized to this value.
constexpr char kStringZapValue = '?';

// Support for parsing BigInts from Strings, using an Accumulator object
// for intermediate state.

class ProcessorImpl;

#if defined(__GNUC__) || defined(__clang__)
// Clang supports this since 3.9, GCC since 5.x.
#define HAVE_BUILTIN_MUL_OVERFLOW 1
#else
#define HAVE_BUILTIN_MUL_OVERFLOW 0
#endif

// A container object for all metadata required for parsing a BigInt from
// a string.
// Aggressively optimized not to waste instructions for small cases, while
// also scaling transparently to huge cases.
// Defined here in the header so that {ConsumeChar} can be inlined.
class FromStringAccumulator {
 public:
  // {max_digits} is only used for refusing to grow beyond a given size
  // (see "Step 1" below). Does not cause pre-allocation, so feel free to
  // specify a large maximum.
  // TODO(jkummerow): The limit applies to the number of intermediate chunks,
  // whereas the final result will be slightly smaller (depending on {radix}).
  // So setting max_digits=N here will, for sufficiently large N, not actually
  // allow parsing BigInts with N digits. We can fix that if/when anyone cares.
  FromStringAccumulator(int radix, int max_digits)
      : radix_(radix),
#if !HAVE_BUILTIN_MUL_OVERFLOW
        max_multiplier_((~digit_t{0}) / radix),
#endif
        max_digits_(max_digits),
        limit_digit_(radix < 10 ? radix : 10),
        limit_alpha_(radix > 10 ? radix - 10 : 0) {
  }

  ~FromStringAccumulator() {
    delete parts_;
    delete multipliers_;
  }

  // Step 1: Call this method repeatedly to read all characters.
  // This method will return quickly; it does not perform heavy processing.
  enum class Result { kOk, kInvalidChar, kMaxSizeExceeded };
  Result ConsumeChar(uint32_t c) {
    digit_t d;
    if (c - '0' < limit_digit_) {
      d = c - '0';
    } else if ((c | 32u) - 'a' < limit_alpha_) {
      d = (c | 32u) - 'a' + 10;
    } else {
      return Result::kInvalidChar;
    }
#if HAVE_BUILTIN_MUL_OVERFLOW
    digit_t m;
    if (!__builtin_mul_overflow(multiplier_, radix_, &m)) {
      multiplier_ = m;
      part_ = part_ * radix_ + d;
    }
#else
    if (multiplier_ <= max_multiplier_) {
      multiplier_ *= radix_;
      part_ = part_ * radix_ + d;
    }
#endif
    else {  // NOLINT(readability/braces)
      if (!AddPart(multiplier_, part_)) return Result::kMaxSizeExceeded;
      multiplier_ = radix_;
      part_ = d;
    }
    return Result::kOk;
  }

  // Step 2: Call this method to determine the required size for the result.
  int ResultLength() {
    if (!parts_) return part_ > 0 ? 1 : 0;
    if (multiplier_ > 1) {
      multipliers_->push_back(multiplier_);
      parts_->push_back(part_);
      // {ResultLength} should be idempotent.
      multiplier_ = 1;
      part_ = 0;
    }
    return parts_size();
  }

  // Step 3: Use BigIntProcessor::FromString() to retrieve the result into an
  // {RWDigits} struct allocated for the size returned by step 2.

 private:
  friend class ProcessorImpl;
  int parts_size() { return static_cast<int>(parts_->size()); }

  bool AddPart(digit_t multiplier, digit_t part) {
    if (!parts_) {
      parts_ = new std::vector<digit_t>;
      multipliers_ = new std::vector<digit_t>;
    } else if (parts_size() == max_digits_) {
      return false;
    }
    multipliers_->push_back(multiplier);
    parts_->push_back(part);
    return true;
  }

  const digit_t radix_;
#if !HAVE_BUILTIN_MUL_OVERFLOW
  const digit_t max_multiplier_;
#endif
  // The next part to be added to {parts_}, or the only part when sufficient.
  digit_t part_{0};
  digit_t multiplier_{1};
  const int max_digits_;
  const uint32_t limit_digit_;
  const uint32_t limit_alpha_;
  // Avoid allocating these unless we actually need them.
  std::vector<digit_t>* parts_{nullptr};
  std::vector<digit_t>* multipliers_{nullptr};
};

}  // namespace bigint
}  // namespace v8

#undef BIGINT_H_DCHECK
#undef HAVE_BUILTIN_MUL_OVERFLOW

#endif  // V8_BIGINT_BIGINT_H_
