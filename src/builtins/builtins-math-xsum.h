// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_MATH_XSUM_H_
#define V8_BUILTINS_BUILTINS_MATH_XSUM_H_

#include <array>

#include "include/v8-internal.h"
#include "src/base/macros.h"

namespace v8 {
namespace internal {

class Xsum {
 public:
  V8_EXPORT Xsum();

  V8_EXPORT void Add(double value);
  V8_EXPORT double Round();

 private:
  static constexpr int kMantissaBits = 52;
  static constexpr int kExpBits = 11;
  static constexpr int64_t kMantissaMask = (int64_t{1} << kMantissaBits) - 1;
  static constexpr int64_t kExpMask = (1 << kExpBits) - 1;
  static constexpr int64_t kExpBias = (1 << (kExpBits - 1)) - 1;
  static constexpr int kSignBit = kMantissaBits + kExpBits;
  static constexpr uint64_t kSignMask = uint64_t{1} << kSignBit;

  static constexpr int kSchunkBits = 64;
  static constexpr int kLowExpBits = 5;
  static constexpr int kLowExpMask = (1 << kLowExpBits) - 1;
  static constexpr int kHighExpBits = kExpBits - kLowExpBits;
  static constexpr int kSchunks = (1 << kHighExpBits) + 3;  // 67

  static constexpr int kLowMantissaBits = 1 << kLowExpBits;  // 32
  static constexpr int64_t kLowMantissaMask =
      (int64_t{1} << kLowMantissaBits) - 1;

  static constexpr int kSmallCarryBits =
      (kSchunkBits - 1) - kMantissaBits;                               // 11
  static constexpr int kSmallCarryTerms = (1 << kSmallCarryBits) - 1;  // 2047

  void AddInfNan(int64_t ivalue);
  // Returns the index of the uppermost non-zero chunk.
  int CarryPropagate();
  void Add1NoCarry(double value);

  std::array<int64_t, kSchunks> chunk_;
  int64_t inf_;
  int64_t nan_;
  int adds_until_propagate_;
};

V8_EXPORT int Xsum_Init(Address small_accumulator);
V8_EXPORT int Xsum_Add(Address small_accumulator, double);
V8_EXPORT double Xsum_Round(Address small_accumulator);

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_MATH_XSUM_H_
