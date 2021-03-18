// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/wasm/wasm-simd-utils.h"

#include <cmath>

#include "src/base/logging.h"
#include "src/base/memory.h"
#include "src/common/globals.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/compiler/c-signature.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {

bool IsExtreme(float x) {
  float abs_x = std::fabs(x);
  const float kSmallFloatThreshold = 1.0e-32f;
  const float kLargeFloatThreshold = 1.0e32f;
  return abs_x != 0.0f &&  // 0 or -0 are fine.
         (abs_x < kSmallFloatThreshold || abs_x > kLargeFloatThreshold);
}

bool IsSameNan(float expected, float actual) {
  // Sign is non-deterministic.
  uint32_t expected_bits = bit_cast<uint32_t>(expected) & ~0x80000000;
  uint32_t actual_bits = bit_cast<uint32_t>(actual) & ~0x80000000;
  // Some implementations convert signaling NaNs to quiet NaNs.
  return (expected_bits == actual_bits) ||
         ((expected_bits | 0x00400000) == actual_bits);
}

bool IsCanonical(float actual) {
  uint32_t actual_bits = bit_cast<uint32_t>(actual);
  // Canonical NaN has quiet bit and no payload.
  return (actual_bits & 0xFFC00000) == actual_bits;
}

void CheckFloatResult(float x, float y, float expected, float actual,
                      bool exact) {
  if (std::isnan(expected)) {
    CHECK(std::isnan(actual));
    if (std::isnan(x) && IsSameNan(x, actual)) return;
    if (std::isnan(y) && IsSameNan(y, actual)) return;
    if (IsSameNan(expected, actual)) return;
    if (IsCanonical(actual)) return;
    // This is expected to assert; it's useful for debugging.
    CHECK_EQ(bit_cast<uint32_t>(expected), bit_cast<uint32_t>(actual));
  } else {
    if (exact) {
      CHECK_EQ(expected, actual);
      // The sign of 0's must match.
      CHECK_EQ(std::signbit(expected), std::signbit(actual));
      return;
    }
    // Otherwise, perform an approximate equality test. First check for
    // equality to handle +/-Infinity where approximate equality doesn't work.
    if (expected == actual) return;

    // 1% error allows all platforms to pass easily.
    constexpr float kApproximationError = 0.01f;
    float abs_error = std::abs(expected) * kApproximationError,
          min = expected - abs_error, max = expected + abs_error;
    CHECK_LE(min, actual);
    CHECK_GE(max, actual);
  }
}

void RunF32x4UnOpTest(TestExecutionTier execution_tier, LowerSimd lower_simd,
                      WasmOpcode opcode, FloatUnOp expected_op, bool exact) {
  WasmRunner<int32_t, float> r(execution_tier, lower_simd);
  // Global to hold output.
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  byte value = 0;
  byte temp1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value))),
        WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_LOCAL_GET(temp1))),
        WASM_ONE);

  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    // Extreme values have larger errors so skip them for approximation tests.
    if (!exact && IsExtreme(x)) continue;
    float expected = expected_op(x);
#if V8_OS_AIX
    if (!MightReverseSign<FloatUnOp>(expected_op))
      expected = FpOpWorkaround<float>(x, expected);
#endif
    if (!PlatformCanRepresent(expected)) continue;
    r.Call(x);
    for (int i = 0; i < 4; i++) {
      float actual = ReadLittleEndianValue<float>(&g[i]);
      CheckFloatResult(x, x, expected, actual, exact);
    }
  }

  FOR_FLOAT32_NAN_INPUTS(i) {
    float x = bit_cast<float>(nan_test_array[i]);
    if (!PlatformCanRepresent(x)) continue;
    // Extreme values have larger errors so skip them for approximation tests.
    if (!exact && IsExtreme(x)) continue;
    float expected = expected_op(x);
    if (!PlatformCanRepresent(expected)) continue;
    r.Call(x);
    for (int i = 0; i < 4; i++) {
      float actual = ReadLittleEndianValue<float>(&g[i]);
      CheckFloatResult(x, x, expected, actual, exact);
    }
  }
}

bool IsExtreme(double x) {
  double abs_x = std::fabs(x);
  const double kSmallFloatThreshold = 1.0e-298;
  const double kLargeFloatThreshold = 1.0e298;
  return abs_x != 0.0f &&  // 0 or -0 are fine.
         (abs_x < kSmallFloatThreshold || abs_x > kLargeFloatThreshold);
}

bool IsSameNan(double expected, double actual) {
  // Sign is non-deterministic.
  uint64_t expected_bits = bit_cast<uint64_t>(expected) & ~0x8000000000000000;
  uint64_t actual_bits = bit_cast<uint64_t>(actual) & ~0x8000000000000000;
  // Some implementations convert signaling NaNs to quiet NaNs.
  return (expected_bits == actual_bits) ||
         ((expected_bits | 0x0008000000000000) == actual_bits);
}

bool IsCanonical(double actual) {
  uint64_t actual_bits = bit_cast<uint64_t>(actual);
  // Canonical NaN has quiet bit and no payload.
  return (actual_bits & 0xFFF8000000000000) == actual_bits;
}

void CheckDoubleResult(double x, double y, double expected, double actual,
                       bool exact) {
  if (std::isnan(expected)) {
    CHECK(std::isnan(actual));
    if (std::isnan(x) && IsSameNan(x, actual)) return;
    if (std::isnan(y) && IsSameNan(y, actual)) return;
    if (IsSameNan(expected, actual)) return;
    if (IsCanonical(actual)) return;
    // This is expected to assert; it's useful for debugging.
    CHECK_EQ(bit_cast<uint64_t>(expected), bit_cast<uint64_t>(actual));
  } else {
    if (exact) {
      CHECK_EQ(expected, actual);
      // The sign of 0's must match.
      CHECK_EQ(std::signbit(expected), std::signbit(actual));
      return;
    }
    // Otherwise, perform an approximate equality test. First check for
    // equality to handle +/-Infinity where approximate equality doesn't work.
    if (expected == actual) return;

    // 1% error allows all platforms to pass easily.
    constexpr double kApproximationError = 0.01f;
    double abs_error = std::abs(expected) * kApproximationError,
           min = expected - abs_error, max = expected + abs_error;
    CHECK_LE(min, actual);
    CHECK_GE(max, actual);
  }
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
