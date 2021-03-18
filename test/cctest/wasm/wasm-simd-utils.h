// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "src/base/macros.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/wasm/wasm-run-utils.h"

namespace v8 {
namespace internal {
namespace wasm {

using FloatUnOp = float (*)(float);

// Test some values not included in the float inputs from value_helper. These
// tests are useful for opcodes that are synthesized during code gen, like Min
// and Max on ia32 and x64.
static constexpr uint32_t nan_test_array[] = {
    // Bit patterns of quiet NaNs and signaling NaNs, with or without
    // additional payload.
    0x7FC00000, 0xFFC00000, 0x7FFFFFFF, 0xFFFFFFFF, 0x7F876543, 0xFF876543,
    // NaN with top payload bit unset.
    0x7FA00000,
    // Both Infinities.
    0x7F800000, 0xFF800000,
    // Some "normal" numbers, 1 and -1.
    0x3F800000, 0xBF800000};

#define FOR_FLOAT32_NAN_INPUTS(i) \
  for (size_t i = 0; i < arraysize(nan_test_array); ++i)

// Test some values not included in the double inputs from value_helper. These
// tests are useful for opcodes that are synthesized during code gen, like Min
// and Max on ia32 and x64.
static constexpr uint64_t double_nan_test_array[] = {
    // quiet NaNs, + and -
    0x7FF8000000000001, 0xFFF8000000000001,
    // with payload
    0x7FF8000000000011, 0xFFF8000000000011,
    // signaling NaNs, + and -
    0x7FF0000000000001, 0xFFF0000000000001,
    // with payload
    0x7FF0000000000011, 0xFFF0000000000011,
    // Both Infinities.
    0x7FF0000000000000, 0xFFF0000000000000,
    // Some "normal" numbers, 1 and -1.
    0x3FF0000000000000, 0xBFF0000000000000};

#define FOR_FLOAT64_NAN_INPUTS(i) \
  for (size_t i = 0; i < arraysize(double_nan_test_array); ++i)

// Returns true if the platform can represent the result.
template <typename T>
bool PlatformCanRepresent(T x) {
#if V8_TARGET_ARCH_ARM
  return std::fpclassify(x) != FP_SUBNORMAL;
#else
  return true;
#endif
}

// Returns true for very small and very large numbers. We skip these test
// values for the approximation instructions, which don't work at the extremes.
bool IsExtreme(float x);
bool IsSameNan(float expected, float actual);
bool IsCanonical(float actual);
void CheckFloatResult(float x, float y, float expected, float actual,
                      bool exact = true);
bool IsExtreme(double x);
bool IsSameNan(double expected, double actual);
bool IsCanonical(double actual);
void CheckDoubleResult(double x, double y, double expected, double actual,
                       bool exact = true);

void RunF32x4UnOpTest(TestExecutionTier execution_tier, LowerSimd lower_simd,
                      WasmOpcode opcode, FloatUnOp expected_op,
                      bool exact = true);
}  // namespace wasm
}  // namespace internal
}  // namespace v8
