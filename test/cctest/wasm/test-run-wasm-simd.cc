// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/memory.h"
#include "src/base/numerics/safe_conversions.h"
#include "src/base/overflowing-math.h"
#include "src/base/utils/random-number-generator.h"
#include "src/base/vector.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/opcodes.h"
#include "src/flags/flags.h"
#include "src/utils/utils.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-runner.h"
#include "test/cctest/wasm/wasm-simd-utils.h"
#include "test/common/flag-utils.h"
#include "test/common/value-helper.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_simd {

namespace {

using Shuffle = std::array<int8_t, kSimd128Size>;

// For signed integral types, use base::AddWithWraparound.
template <typename T, typename = std::enable_if_t<std::is_floating_point_v<T>>>
T Add(T a, T b) {
  return a + b;
}

// For signed integral types, use base::SubWithWraparound.
template <typename T, typename = std::enable_if_t<std::is_floating_point_v<T>>>
T Sub(T a, T b) {
  return a - b;
}

// For signed integral types, use base::MulWithWraparound.
template <typename T, typename = std::enable_if_t<std::is_floating_point_v<T>>>
T Mul(T a, T b) {
  return a * b;
}

template <typename T>
T UnsignedMinimum(T a, T b) {
  using UnsignedT = std::make_unsigned_t<T>;
  return static_cast<UnsignedT>(a) <= static_cast<UnsignedT>(b) ? a : b;
}

template <typename T>
T UnsignedMaximum(T a, T b) {
  using UnsignedT = std::make_unsigned_t<T>;
  return static_cast<UnsignedT>(a) >= static_cast<UnsignedT>(b) ? a : b;
}

template <typename T, typename U = T>
U Equal(T a, T b) {
  return a == b ? -1 : 0;
}

template <>
int32_t Equal(float a, float b) {
  return a == b ? -1 : 0;
}

template <>
int64_t Equal(double a, double b) {
  return a == b ? -1 : 0;
}

template <typename T, typename U = T>
U NotEqual(T a, T b) {
  return a != b ? -1 : 0;
}

template <>
int32_t NotEqual(float a, float b) {
  return a != b ? -1 : 0;
}

template <>
int64_t NotEqual(double a, double b) {
  return a != b ? -1 : 0;
}

template <typename T, typename U = T>
U Less(T a, T b) {
  return a < b ? -1 : 0;
}

template <>
int32_t Less(float a, float b) {
  return a < b ? -1 : 0;
}

template <>
int64_t Less(double a, double b) {
  return a < b ? -1 : 0;
}

template <typename T, typename U = T>
U LessEqual(T a, T b) {
  return a <= b ? -1 : 0;
}

template <>
int32_t LessEqual(float a, float b) {
  return a <= b ? -1 : 0;
}

template <>
int64_t LessEqual(double a, double b) {
  return a <= b ? -1 : 0;
}

template <typename T, typename U = T>
U Greater(T a, T b) {
  return a > b ? -1 : 0;
}

template <>
int32_t Greater(float a, float b) {
  return a > b ? -1 : 0;
}

template <>
int64_t Greater(double a, double b) {
  return a > b ? -1 : 0;
}

template <typename T, typename U = T>
U GreaterEqual(T a, T b) {
  return a >= b ? -1 : 0;
}

template <>
int32_t GreaterEqual(float a, float b) {
  return a >= b ? -1 : 0;
}

template <>
int64_t GreaterEqual(double a, double b) {
  return a >= b ? -1 : 0;
}

template <typename T>
T UnsignedLess(T a, T b) {
  using UnsignedT = std::make_unsigned_t<T>;
  return static_cast<UnsignedT>(a) < static_cast<UnsignedT>(b) ? -1 : 0;
}

template <typename T>
T UnsignedLessEqual(T a, T b) {
  using UnsignedT = std::make_unsigned_t<T>;
  return static_cast<UnsignedT>(a) <= static_cast<UnsignedT>(b) ? -1 : 0;
}

template <typename T>
T UnsignedGreater(T a, T b) {
  using UnsignedT = std::make_unsigned_t<T>;
  return static_cast<UnsignedT>(a) > static_cast<UnsignedT>(b) ? -1 : 0;
}

template <typename T>
T UnsignedGreaterEqual(T a, T b) {
  using UnsignedT = std::make_unsigned_t<T>;
  return static_cast<UnsignedT>(a) >= static_cast<UnsignedT>(b) ? -1 : 0;
}

template <typename T>
T LogicalShiftLeft(T a, int shift) {
  using UnsignedT = std::make_unsigned_t<T>;
  return static_cast<UnsignedT>(a) << (shift % (sizeof(T) * 8));
}

template <typename T>
T LogicalShiftRight(T a, int shift) {
  using UnsignedT = std::make_unsigned_t<T>;
  return static_cast<UnsignedT>(a) >> (shift % (sizeof(T) * 8));
}

// Define our own ArithmeticShiftRight instead of using the one from utils.h
// because the shift amount needs to be taken modulo lane width.
template <typename T>
T ArithmeticShiftRight(T a, int shift) {
  return a >> (shift % (sizeof(T) * 8));
}

template <typename T>
T Abs(T a) {
  return std::abs(a);
}

template <typename T>
T BitwiseNot(T a) {
  return ~a;
}

template <typename T>
T BitwiseAnd(T a, T b) {
  return a & b;
}

template <typename T>
T BitwiseOr(T a, T b) {
  return a | b;
}
template <typename T>
T BitwiseXor(T a, T b) {
  return a ^ b;
}
template <typename T>
T BitwiseAndNot(T a, T b) {
  return a & (~b);
}

template <typename T>
T BitwiseSelect(T a, T b, T c) {
  return (a & c) | (b & ~c);
}

}  // namespace

#define WASM_SIMD_CHECK_LANE_S(TYPE, value, LANE_TYPE, lane_value, lane_index) \
  WASM_IF(WASM_##LANE_TYPE##_NE(WASM_LOCAL_GET(lane_value),                    \
                                WASM_SIMD_##TYPE##_EXTRACT_LANE(               \
                                    lane_index, WASM_LOCAL_GET(value))),       \
          WASM_RETURN(WASM_ZERO))

// Unsigned Extracts are only available for I8x16, I16x8 types
#define WASM_SIMD_CHECK_LANE_U(TYPE, value, LANE_TYPE, lane_value, lane_index) \
  WASM_IF(WASM_##LANE_TYPE##_NE(WASM_LOCAL_GET(lane_value),                    \
                                WASM_SIMD_##TYPE##_EXTRACT_LANE_U(             \
                                    lane_index, WASM_LOCAL_GET(value))),       \
          WASM_RETURN(WASM_ZERO))

WASM_EXEC_TEST(S128Globals) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up a global to hold input and output vectors.
  int32_t* g0 = r.builder().AddGlobal<int32_t>(kWasmS128);
  int32_t* g1 = r.builder().AddGlobal<int32_t>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(1, WASM_GLOBAL_GET(0)), WASM_ONE});

  FOR_INT32_INPUTS(x) {
    for (int i = 0; i < 4; i++) {
      LANE(g0, i) = x;
    }
    r.Call();
    int32_t expected = x;
    for (int i = 0; i < 4; i++) {
      int32_t actual = LANE(g1, i);
      CHECK_EQ(actual, expected);
    }
  }
}

WASM_EXEC_TEST(F32x4Splat) {
  WasmRunner<int32_t, float> r(execution_tier);
  // Set up a global to hold output vector.
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  uint8_t param1 = 0;
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(param1))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    r.Call(x);
    float expected = x;
    for (int i = 0; i < 4; i++) {
      float actual = LANE(g, i);
      if (std::isnan(expected)) {
        CHECK(std::isnan(actual));
      } else {
        CHECK_EQ(actual, expected);
      }
    }
  }
}

WASM_EXEC_TEST(F32x4ReplaceLane) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up a global to hold input/output vector.
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  // Build function to replace each lane with its (FP) index.
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_F32(3.14159f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     0, WASM_LOCAL_GET(temp1), WASM_F32(0.0f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(temp1), WASM_F32(1.0f))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(temp1), WASM_F32(2.0f))),
           WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_REPLACE_LANE(
                                  3, WASM_LOCAL_GET(temp1), WASM_F32(3.0f))),
           WASM_ONE});

  r.Call();
  for (int i = 0; i < 4; i++) {
    CHECK_EQ(static_cast<float>(i), LANE(g, i));
  }
}

// Tests both signed and unsigned conversion.
WASM_EXEC_TEST(F32x4ConvertI32x4) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Create two output vectors to hold signed and unsigned results.
  float* g0 = r.builder().AddGlobal<float>(kWasmS128);
  float* g1 = r.builder().AddGlobal<float>(kWasmS128);
  // Build fn to splat test value, perform conversions, and write the results.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(kExprF32x4SConvertI32x4,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(1, WASM_SIMD_UNOP(kExprF32x4UConvertI32x4,
                                             WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT32_INPUTS(x) {
    r.Call(x);
    float expected_signed = static_cast<float>(x);
    float expected_unsigned = static_cast<float>(static_cast<uint32_t>(x));
    for (int i = 0; i < 4; i++) {
      CHECK_EQ(expected_signed, LANE(g0, i));
      CHECK_EQ(expected_unsigned, LANE(g1, i));
    }
  }
}

template <typename FloatType, typename ScalarType>
void RunF128CompareOpConstImmTest(
    TestExecutionTier execution_tier, WasmOpcode cmp_opcode,
    WasmOpcode splat_opcode, ScalarType (*expected_op)(FloatType, FloatType)) {
  for (FloatType x : compiler::ValueHelper::GetVector<FloatType>()) {
    if (!PlatformCanRepresent(x)) continue;
    WasmRunner<int32_t, FloatType> r(execution_tier);
    // Set up globals to hold mask output for left and right cases
    ScalarType* g1 = r.builder().template AddGlobal<ScalarType>(kWasmS128);
    ScalarType* g2 = r.builder().template AddGlobal<ScalarType>(kWasmS128);
    // Build fn to splat test values, perform compare op on both sides, and
    // write the result.
    uint8_t value = 0;
    uint8_t temp = r.AllocateLocal(kWasmS128);
    uint8_t const_buffer[kSimd128Size];
    for (size_t i = 0; i < kSimd128Size / sizeof(FloatType); i++) {
      WriteLittleEndianValue<FloatType>(
          reinterpret_cast<FloatType*>(&const_buffer[0]) + i, x);
    }
    r.Build(
        {WASM_LOCAL_SET(temp,
                        WASM_SIMD_OPN(splat_opcode, WASM_LOCAL_GET(value))),
         WASM_GLOBAL_SET(
             0, WASM_SIMD_BINOP(cmp_opcode, WASM_SIMD_CONSTANT(const_buffer),
                                WASM_LOCAL_GET(temp))),
         WASM_GLOBAL_SET(1, WASM_SIMD_BINOP(cmp_opcode, WASM_LOCAL_GET(temp),
                                            WASM_SIMD_CONSTANT(const_buffer))),
         WASM_ONE});
    for (FloatType y : compiler::ValueHelper::GetVector<FloatType>()) {
      if (!PlatformCanRepresent(y)) continue;
      FloatType diff = x - y;  // Model comparison as subtraction.
      if (!PlatformCanRepresent(diff)) continue;
      r.Call(y);
      ScalarType expected1 = expected_op(x, y);
      ScalarType expected2 = expected_op(y, x);
      for (size_t i = 0; i < kSimd128Size / sizeof(ScalarType); i++) {
        CHECK_EQ(expected1, LANE(g1, i));
        CHECK_EQ(expected2, LANE(g2, i));
      }
    }
  }
}

WASM_EXEC_TEST(F32x4Abs) {
  RunF32x4UnOpTest(execution_tier, kExprF32x4Abs, std::abs);
}

WASM_EXEC_TEST(F32x4Neg) {
  RunF32x4UnOpTest(execution_tier, kExprF32x4Neg, Negate);
}

WASM_EXEC_TEST(F32x4Sqrt) {
  RunF32x4UnOpTest(execution_tier, kExprF32x4Sqrt, std::sqrt);
}

WASM_EXEC_TEST(F32x4Ceil) {
  RunF32x4UnOpTest(execution_tier, kExprF32x4Ceil, ceilf, true);
}

WASM_EXEC_TEST(F32x4Floor) {
  RunF32x4UnOpTest(execution_tier, kExprF32x4Floor, floorf, true);
}

WASM_EXEC_TEST(F32x4Trunc) {
  RunF32x4UnOpTest(execution_tier, kExprF32x4Trunc, truncf, true);
}

WASM_EXEC_TEST(F32x4NearestInt) {
  RunF32x4UnOpTest(execution_tier, kExprF32x4NearestInt, nearbyintf, true);
}

WASM_EXEC_TEST(F32x4Add) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4Add, Add);
}
WASM_EXEC_TEST(F32x4Sub) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4Sub, Sub);
}
WASM_EXEC_TEST(F32x4Mul) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4Mul, Mul);
}
WASM_EXEC_TEST(F32x4Div) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4Div, base::Divide);
}
WASM_EXEC_TEST(F32x4Min) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4Min, JSMin);
}
WASM_EXEC_TEST(F32x4Max) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4Max, JSMax);
}

WASM_EXEC_TEST(F32x4Pmin) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4Pmin, Minimum);
}

WASM_EXEC_TEST(F32x4Pmax) {
  RunF32x4BinOpTest(execution_tier, kExprF32x4Pmax, Maximum);
}

WASM_EXEC_TEST(F32x4Eq) {
  RunF32x4CompareOpTest(execution_tier, kExprF32x4Eq, Equal);
}

WASM_EXEC_TEST(F32x4Ne) {
  RunF32x4CompareOpTest(execution_tier, kExprF32x4Ne, NotEqual);
}

WASM_EXEC_TEST(F32x4Gt) {
  RunF32x4CompareOpTest(execution_tier, kExprF32x4Gt, Greater);
}

WASM_EXEC_TEST(F32x4Ge) {
  RunF32x4CompareOpTest(execution_tier, kExprF32x4Ge, GreaterEqual);
}

WASM_EXEC_TEST(F32x4Lt) {
  RunF32x4CompareOpTest(execution_tier, kExprF32x4Lt, Less);
}

WASM_EXEC_TEST(F32x4Le) {
  RunF32x4CompareOpTest(execution_tier, kExprF32x4Le, LessEqual);
}

template <typename ScalarType>
void RunShiftAddTestSequence(TestExecutionTier execution_tier,
                             WasmOpcode shiftr_opcode, WasmOpcode add_opcode,
                             WasmOpcode splat_opcode, int32_t imm,
                             ScalarType (*shift_fn)(ScalarType, int32_t)) {
  WasmRunner<int32_t, ScalarType> r(execution_tier);
  // globals to store results for left and right cases
  ScalarType* g1 = r.builder().template AddGlobal<ScalarType>(kWasmS128);
  ScalarType* g2 = r.builder().template AddGlobal<ScalarType>(kWasmS128);
  uint8_t param = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  auto expected_fn = [shift_fn](ScalarType x, ScalarType y, uint32_t imm) {
    return base::AddWithWraparound(x, shift_fn(y, imm));
  };
  r.Build(
      {WASM_LOCAL_SET(temp1,
                      WASM_SIMD_OPN(splat_opcode, WASM_LOCAL_GET(param))),
       WASM_LOCAL_SET(temp2,
                      WASM_SIMD_OPN(splat_opcode, WASM_LOCAL_GET(param))),
       WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(add_opcode,
                                          WASM_SIMD_BINOP(shiftr_opcode,
                                                          WASM_LOCAL_GET(temp2),
                                                          WASM_I32V(imm)),
                                          WASM_LOCAL_GET(temp1))),
       WASM_GLOBAL_SET(1, WASM_SIMD_BINOP(add_opcode, WASM_LOCAL_GET(temp1),
                                          WASM_SIMD_BINOP(shiftr_opcode,
                                                          WASM_LOCAL_GET(temp2),
                                                          WASM_I32V(imm)))),

       WASM_ONE});
  for (ScalarType x : compiler::ValueHelper::GetVector<ScalarType>()) {
    r.Call(x);
    ScalarType expected = expected_fn(x, x, imm);
    for (size_t i = 0; i < kSimd128Size / sizeof(ScalarType); i++) {
      CHECK_EQ(expected, LANE(g1, i));
      CHECK_EQ(expected, LANE(g2, i));
    }
  }
}

WASM_EXEC_TEST(F32x4EqZero) {
  RunF128CompareOpConstImmTest<float, int32_t>(execution_tier, kExprF32x4Eq,
                                               kExprF32x4Splat, Equal);
}

WASM_EXEC_TEST(F32x4NeZero) {
  RunF128CompareOpConstImmTest<float, int32_t>(execution_tier, kExprF32x4Ne,
                                               kExprF32x4Splat, NotEqual);
}

WASM_EXEC_TEST(F32x4GtZero) {
  RunF128CompareOpConstImmTest<float, int32_t>(execution_tier, kExprF32x4Gt,
                                               kExprF32x4Splat, Greater);
}

WASM_EXEC_TEST(F32x4GeZero) {
  RunF128CompareOpConstImmTest<float, int32_t>(execution_tier, kExprF32x4Ge,
                                               kExprF32x4Splat, GreaterEqual);
}

WASM_EXEC_TEST(F32x4LtZero) {
  RunF128CompareOpConstImmTest<float, int32_t>(execution_tier, kExprF32x4Lt,
                                               kExprF32x4Splat, Less);
}

WASM_EXEC_TEST(F32x4LeZero) {
  RunF128CompareOpConstImmTest<float, int32_t>(execution_tier, kExprF32x4Le,
                                               kExprF32x4Splat, LessEqual);
}

WASM_EXEC_TEST(I64x2Splat) {
  WasmRunner<int32_t, int64_t> r(execution_tier);
  // Set up a global to hold output vector.
  int64_t* g = r.builder().AddGlobal<int64_t>(kWasmS128);
  uint8_t param1 = 0;
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(param1))),
           WASM_ONE});

  FOR_INT64_INPUTS(x) {
    r.Call(x);
    int64_t expected = x;
    for (int i = 0; i < 2; i++) {
      int64_t actual = LANE(g, i);
      CHECK_EQ(actual, expected);
    }
  }
}

WASM_EXEC_TEST(I64x2ExtractLane) {
  WasmRunner<int64_t> r(execution_tier);
  r.AllocateLocal(kWasmI64);
  r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(
               0, WASM_SIMD_I64x2_EXTRACT_LANE(
                      0, WASM_SIMD_I64x2_SPLAT(WASM_I64V(0xFFFFFFFFFF)))),
           WASM_LOCAL_SET(1, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(0))),
           WASM_SIMD_I64x2_EXTRACT_LANE(1, WASM_LOCAL_GET(1))});
  CHECK_EQ(0xFFFFFFFFFF, r.Call());
}

WASM_EXEC_TEST(I64x2ReplaceLane) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up a global to hold input/output vector.
  int64_t* g = r.builder().AddGlobal<int64_t>(kWasmS128);
  // Build function to replace each lane with its index.
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I64x2_SPLAT(WASM_I64V(-1))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I64x2_REPLACE_LANE(
                                     0, WASM_LOCAL_GET(temp1), WASM_I64V(0))),
           WASM_GLOBAL_SET(0, WASM_SIMD_I64x2_REPLACE_LANE(
                                  1, WASM_LOCAL_GET(temp1), WASM_I64V(1))),
           WASM_ONE});

  r.Call();
  for (int64_t i = 0; i < 2; i++) {
    CHECK_EQ(i, LANE(g, i));
  }
}

WASM_EXEC_TEST(I64x2Neg) {
  RunI64x2UnOpTest(execution_tier, kExprI64x2Neg, base::NegateWithWraparound);
}

WASM_EXEC_TEST(I64x2Abs) {
  RunI64x2UnOpTest(execution_tier, kExprI64x2Abs, std::abs);
}

WASM_EXEC_TEST(I64x2Shl) {
  RunI64x2ShiftOpTest(execution_tier, kExprI64x2Shl, LogicalShiftLeft);
}

WASM_EXEC_TEST(I64x2ShrS) {
  RunI64x2ShiftOpTest(execution_tier, kExprI64x2ShrS, ArithmeticShiftRight);
}

WASM_EXEC_TEST(I64x2ShrU) {
  RunI64x2ShiftOpTest(execution_tier, kExprI64x2ShrU, LogicalShiftRight);
}

WASM_EXEC_TEST(I64x2ShiftAdd) {
  for (int imm = 0; imm <= 64; imm++) {
    RunShiftAddTestSequence<int64_t>(execution_tier, kExprI64x2ShrU,
                                     kExprI64x2Add, kExprI64x2Splat, imm,
                                     LogicalShiftRight);
    RunShiftAddTestSequence<int64_t>(execution_tier, kExprI64x2ShrS,
                                     kExprI64x2Add, kExprI64x2Splat, imm,
                                     ArithmeticShiftRight);
  }
}

WASM_EXEC_TEST(I64x2Add) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2Add, base::AddWithWraparound);
}

WASM_EXEC_TEST(I64x2Sub) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2Sub, base::SubWithWraparound);
}

WASM_EXEC_TEST(I64x2Eq) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2Eq, Equal);
}

WASM_EXEC_TEST(I64x2Ne) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2Ne, NotEqual);
}

WASM_EXEC_TEST(I64x2LtS) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2LtS, Less);
}

WASM_EXEC_TEST(I64x2LeS) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2LeS, LessEqual);
}

WASM_EXEC_TEST(I64x2GtS) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2GtS, Greater);
}

WASM_EXEC_TEST(I64x2GeS) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2GeS, GreaterEqual);
}

namespace {

template <typename ScalarType>
void RunICompareOpConstImmTest(TestExecutionTier execution_tier,
                               WasmOpcode cmp_opcode, WasmOpcode splat_opcode,
                               ScalarType (*expected_op)(ScalarType,
                                                         ScalarType)) {
  for (ScalarType x : compiler::ValueHelper::GetVector<ScalarType>()) {
    WasmRunner<int32_t, ScalarType> r(execution_tier);
    // Set up global to hold mask output for left and right cases
    ScalarType* g1 = r.builder().template AddGlobal<ScalarType>(kWasmS128);
    ScalarType* g2 = r.builder().template AddGlobal<ScalarType>(kWasmS128);
    // Build fn to splat test values, perform compare op on both sides, and
    // write the result.
    uint8_t value = 0;
    uint8_t temp = r.AllocateLocal(kWasmS128);
    uint8_t const_buffer[kSimd128Size];
    for (size_t i = 0; i < kSimd128Size / sizeof(ScalarType); i++) {
      WriteLittleEndianValue<ScalarType>(
          reinterpret_cast<ScalarType*>(&const_buffer[0]) + i, x);
    }
    r.Build(
        {WASM_LOCAL_SET(temp,
                        WASM_SIMD_OPN(splat_opcode, WASM_LOCAL_GET(value))),
         WASM_GLOBAL_SET(
             0, WASM_SIMD_BINOP(cmp_opcode, WASM_SIMD_CONSTANT(const_buffer),
                                WASM_LOCAL_GET(temp))),
         WASM_GLOBAL_SET(1, WASM_SIMD_BINOP(cmp_opcode, WASM_LOCAL_GET(temp),
                                            WASM_SIMD_CONSTANT(const_buffer))),
         WASM_ONE});
    for (ScalarType y : compiler::ValueHelper::GetVector<ScalarType>()) {
      r.Call(y);
      ScalarType expected1 = expected_op(x, y);
      ScalarType expected2 = expected_op(y, x);
      for (size_t i = 0; i < kSimd128Size / sizeof(ScalarType); i++) {
        CHECK_EQ(expected1, LANE(g1, i));
        CHECK_EQ(expected2, LANE(g2, i));
      }
    }
  }
}

}  // namespace

WASM_EXEC_TEST(I64x2EqZero) {
  RunICompareOpConstImmTest<int64_t>(execution_tier, kExprI64x2Eq,
                                     kExprI64x2Splat, Equal);
}

WASM_EXEC_TEST(I64x2NeZero) {
  RunICompareOpConstImmTest<int64_t>(execution_tier, kExprI64x2Ne,
                                     kExprI64x2Splat, NotEqual);
}

WASM_EXEC_TEST(I64x2GtZero) {
  RunICompareOpConstImmTest<int64_t>(execution_tier, kExprI64x2GtS,
                                     kExprI64x2Splat, Greater);
}

WASM_EXEC_TEST(I64x2GeZero) {
  RunICompareOpConstImmTest<int64_t>(execution_tier, kExprI64x2GeS,
                                     kExprI64x2Splat, GreaterEqual);
}

WASM_EXEC_TEST(I64x2LtZero) {
  RunICompareOpConstImmTest<int64_t>(execution_tier, kExprI64x2LtS,
                                     kExprI64x2Splat, Less);
}

WASM_EXEC_TEST(I64x2LeZero) {
  RunICompareOpConstImmTest<int64_t>(execution_tier, kExprI64x2LeS,
                                     kExprI64x2Splat, LessEqual);
}

WASM_EXEC_TEST(F64x2Splat) {
  WasmRunner<int32_t, double> r(execution_tier);
  // Set up a global to hold output vector.
  double* g = r.builder().AddGlobal<double>(kWasmS128);
  uint8_t param1 = 0;
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(param1))),
           WASM_ONE});

  FOR_FLOAT64_INPUTS(x) {
    r.Call(x);
    double expected = x;
    for (int i = 0; i < 2; i++) {
      double actual = LANE(g, i);
      if (std::isnan(expected)) {
        CHECK(std::isnan(actual));
      } else {
        CHECK_EQ(actual, expected);
      }
    }
  }
}

WASM_EXEC_TEST(F64x2ExtractLane) {
  WasmRunner<double, double> r(execution_tier);
  uint8_t param1 = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmF64);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(
               temp1, WASM_SIMD_F64x2_EXTRACT_LANE(
                          0, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(param1)))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(temp1))),
           WASM_SIMD_F64x2_EXTRACT_LANE(1, WASM_LOCAL_GET(temp2))});
  FOR_FLOAT64_INPUTS(x) {
    double actual = r.Call(x);
    double expected = x;
    if (std::isnan(expected)) {
      CHECK(std::isnan(actual));
    } else {
      CHECK_EQ(actual, expected);
    }
  }
}

WASM_EXEC_TEST(F64x2ReplaceLane) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up globals to hold input/output vector.
  double* g0 = r.builder().AddGlobal<double>(kWasmS128);
  double* g1 = r.builder().AddGlobal<double>(kWasmS128);
  // Build function to replace each lane with its (FP) index.
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F64x2_SPLAT(WASM_F64(1e100))),
           // Replace lane 0.
           WASM_GLOBAL_SET(0, WASM_SIMD_F64x2_REPLACE_LANE(
                                  0, WASM_LOCAL_GET(temp1), WASM_F64(0.0f))),
           // Replace lane 1.
           WASM_GLOBAL_SET(1, WASM_SIMD_F64x2_REPLACE_LANE(
                                  1, WASM_LOCAL_GET(temp1), WASM_F64(1.0f))),
           WASM_ONE});

  r.Call();
  CHECK_EQ(0., LANE(g0, 0));
  CHECK_EQ(1e100, LANE(g0, 1));
  CHECK_EQ(1e100, LANE(g1, 0));
  CHECK_EQ(1., LANE(g1, 1));
}

WASM_EXEC_TEST(F64x2ExtractLaneWithI64x2) {
  WasmRunner<int64_t> r(execution_tier);
  r.Build({WASM_IF_ELSE_L(
      WASM_F64_EQ(WASM_SIMD_F64x2_EXTRACT_LANE(
                      0, WASM_SIMD_I64x2_SPLAT(WASM_I64V(1e15))),
                  WASM_F64_REINTERPRET_I64(WASM_I64V(1e15))),
      WASM_I64V(1), WASM_I64V(0))});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(I64x2ExtractWithF64x2) {
  WasmRunner<int64_t> r(execution_tier);
  r.Build(
      {WASM_IF_ELSE_L(WASM_I64_EQ(WASM_SIMD_I64x2_EXTRACT_LANE(
                                      0, WASM_SIMD_F64x2_SPLAT(WASM_F64(1e15))),
                                  WASM_I64_REINTERPRET_F64(WASM_F64(1e15))),
                      WASM_I64V(1), WASM_I64V(0))});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(F64x2Abs) {
  RunF64x2UnOpTest(execution_tier, kExprF64x2Abs, std::abs);
}

WASM_EXEC_TEST(F64x2Neg) {
  RunF64x2UnOpTest(execution_tier, kExprF64x2Neg, Negate);
}

WASM_EXEC_TEST(F64x2Sqrt) {
  RunF64x2UnOpTest(execution_tier, kExprF64x2Sqrt, std::sqrt);
}

WASM_EXEC_TEST(F64x2Ceil) {
  RunF64x2UnOpTest(execution_tier, kExprF64x2Ceil, ceil, true);
}

WASM_EXEC_TEST(F64x2Floor) {
  RunF64x2UnOpTest(execution_tier, kExprF64x2Floor, floor, true);
}

WASM_EXEC_TEST(F64x2Trunc) {
  RunF64x2UnOpTest(execution_tier, kExprF64x2Trunc, trunc, true);
}

WASM_EXEC_TEST(F64x2NearestInt) {
  RunF64x2UnOpTest(execution_tier, kExprF64x2NearestInt, nearbyint, true);
}

template <typename SrcType>
void RunF64x2ConvertLowI32x4Test(TestExecutionTier execution_tier,
                                 WasmOpcode opcode) {
  WasmRunner<int32_t, SrcType> r(execution_tier);
  double* g = r.builder().template AddGlobal<double>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(
               0, WASM_SIMD_UNOP(
                      opcode,
                      // Set top lane of i64x2 == set top 2 lanes of i32x4.
                      WASM_SIMD_I64x2_REPLACE_LANE(
                          1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(0)),
                          WASM_ZERO64))),
           WASM_ONE});

  for (SrcType x : compiler::ValueHelper::GetVector<SrcType>()) {
    r.Call(x);
    double expected = static_cast<double>(x);
    for (int i = 0; i < 2; i++) {
      double actual = LANE(g, i);
      CheckDoubleResult(x, x, expected, actual, true);
    }
  }
}

WASM_EXEC_TEST(F64x2ConvertLowI32x4S) {
  RunF64x2ConvertLowI32x4Test<int32_t>(execution_tier,
                                       kExprF64x2ConvertLowI32x4S);
}

WASM_EXEC_TEST(F64x2ConvertLowI32x4U) {
  RunF64x2ConvertLowI32x4Test<uint32_t>(execution_tier,
                                        kExprF64x2ConvertLowI32x4U);
}

template <typename SrcType>
void RunI32x4TruncSatF64x2Test(TestExecutionTier execution_tier,
                               WasmOpcode opcode) {
  WasmRunner<int32_t, double> r(execution_tier);
  SrcType* g = r.builder().AddGlobal<SrcType>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(opcode, WASM_SIMD_F64x2_SPLAT(
                                                         WASM_LOCAL_GET(0)))),
           WASM_ONE});

  FOR_FLOAT64_INPUTS(x) {
    r.Call(x);
    SrcType expected = base::saturated_cast<SrcType>(x);
    for (int i = 0; i < 2; i++) {
      SrcType actual = LANE(g, i);
      CHECK_EQ(expected, actual);
    }
    // Top lanes are zero-ed.
    for (int i = 2; i < 4; i++) {
      CHECK_EQ(0, LANE(g, i));
    }
  }
}

WASM_EXEC_TEST(I32x4TruncSatF64x2SZero) {
  RunI32x4TruncSatF64x2Test<int32_t>(execution_tier,
                                     kExprI32x4TruncSatF64x2SZero);
}

WASM_EXEC_TEST(I32x4TruncSatF64x2UZero) {
  RunI32x4TruncSatF64x2Test<uint32_t>(execution_tier,
                                      kExprI32x4TruncSatF64x2UZero);
}

WASM_EXEC_TEST(F32x4DemoteF64x2Zero) {
  WasmRunner<int32_t, double> r(execution_tier);
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(
               0, WASM_SIMD_UNOP(kExprF32x4DemoteF64x2Zero,
                                 WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(0)))),
           WASM_ONE});

  FOR_FLOAT64_INPUTS(x) {
    r.Call(x);
    float expected = DoubleToFloat32(x);
    for (int i = 0; i < 2; i++) {
      float actual = LANE(g, i);
      CheckFloatResult(x, x, expected, actual, true);
    }
    for (int i = 2; i < 4; i++) {
      float actual = LANE(g, i);
      CheckFloatResult(x, x, 0, actual, true);
    }
  }
}

WASM_EXEC_TEST(F64x2PromoteLowF32x4) {
  WasmRunner<int32_t, float> r(execution_tier);
  double* g = r.builder().AddGlobal<double>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(
               0, WASM_SIMD_UNOP(kExprF64x2PromoteLowF32x4,
                                 WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(0)))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    r.Call(x);
    double expected = static_cast<double>(x);
    for (int i = 0; i < 2; i++) {
      double actual = LANE(g, i);
      CheckDoubleResult(x, x, expected, actual, true);
    }
  }
}

// Test F64x2PromoteLowF32x4 with S128Load64Zero optimization (only on some
// architectures). These 2 opcodes should be fused into a single instruction
// with memory operands, which is tested in instruction-selector tests. This
// test checks that we get correct results.
WASM_EXEC_TEST(F64x2PromoteLowF32x4WithS128Load64Zero) {
  {
    WasmRunner<int32_t> r(execution_tier);
    double* g = r.builder().AddGlobal<double>(kWasmS128);
    float* memory =
        r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
    r.builder().RandomizeMemory();
    r.builder().WriteMemory(&memory[0], 1.0f);
    r.builder().WriteMemory(&memory[1], 3.0f);
    r.builder().WriteMemory(&memory[2], 5.0f);
    r.builder().WriteMemory(&memory[3], 8.0f);

    // Load at 4 (index) + 4 (offset) bytes, which is 2 floats.
    r.Build({WASM_GLOBAL_SET(
                 0, WASM_SIMD_UNOP(kExprF64x2PromoteLowF32x4,
                                   WASM_SIMD_LOAD_OP_OFFSET(kExprS128Load64Zero,
                                                            WASM_I32V(4), 4))),
             WASM_ONE});

    r.Call();
    CHECK_EQ(5.0f, LANE(g, 0));
    CHECK_EQ(8.0f, LANE(g, 1));
  }

  {
    // OOB tests.
    WasmRunner<int32_t> r(execution_tier);
    r.builder().AddGlobal<double>(kWasmS128);
    r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(kExprF64x2PromoteLowF32x4,
                                               WASM_SIMD_LOAD_OP(
                                                   kExprS128Load64Zero,
                                                   WASM_I32V(kWasmPageSize)))),
             WASM_ONE});

    CHECK_TRAP(r.Call());
  }
}

WASM_EXEC_TEST(F64x2Add) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2Add, Add);
}

WASM_EXEC_TEST(F64x2Sub) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2Sub, Sub);
}

WASM_EXEC_TEST(F64x2Mul) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2Mul, Mul);
}

WASM_EXEC_TEST(F64x2Div) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2Div, base::Divide);
}

WASM_EXEC_TEST(F64x2Pmin) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2Pmin, Minimum);
}

WASM_EXEC_TEST(F64x2Pmax) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2Pmax, Maximum);
}

WASM_EXEC_TEST(F64x2Eq) {
  RunF64x2CompareOpTest(execution_tier, kExprF64x2Eq, Equal);
}

WASM_EXEC_TEST(F64x2Ne) {
  RunF64x2CompareOpTest(execution_tier, kExprF64x2Ne, NotEqual);
}

WASM_EXEC_TEST(F64x2Gt) {
  RunF64x2CompareOpTest(execution_tier, kExprF64x2Gt, Greater);
}

WASM_EXEC_TEST(F64x2Ge) {
  RunF64x2CompareOpTest(execution_tier, kExprF64x2Ge, GreaterEqual);
}

WASM_EXEC_TEST(F64x2Lt) {
  RunF64x2CompareOpTest(execution_tier, kExprF64x2Lt, Less);
}

WASM_EXEC_TEST(F64x2Le) {
  RunF64x2CompareOpTest(execution_tier, kExprF64x2Le, LessEqual);
}

WASM_EXEC_TEST(F64x2EqZero) {
  RunF128CompareOpConstImmTest<double, int64_t>(execution_tier, kExprF64x2Eq,
                                                kExprF64x2Splat, Equal);
}

WASM_EXEC_TEST(F64x2NeZero) {
  RunF128CompareOpConstImmTest<double, int64_t>(execution_tier, kExprF64x2Ne,
                                                kExprF64x2Splat, NotEqual);
}

WASM_EXEC_TEST(F64x2GtZero) {
  RunF128CompareOpConstImmTest<double, int64_t>(execution_tier, kExprF64x2Gt,
                                                kExprF64x2Splat, Greater);
}

WASM_EXEC_TEST(F64x2GeZero) {
  RunF128CompareOpConstImmTest<double, int64_t>(execution_tier, kExprF64x2Ge,
                                                kExprF64x2Splat, GreaterEqual);
}

WASM_EXEC_TEST(F64x2LtZero) {
  RunF128CompareOpConstImmTest<double, int64_t>(execution_tier, kExprF64x2Lt,
                                                kExprF64x2Splat, Less);
}

WASM_EXEC_TEST(F64x2LeZero) {
  RunF128CompareOpConstImmTest<double, int64_t>(execution_tier, kExprF64x2Le,
                                                kExprF64x2Splat, LessEqual);
}

WASM_EXEC_TEST(F64x2Min) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2Min, JSMin);
}

WASM_EXEC_TEST(F64x2Max) {
  RunF64x2BinOpTest(execution_tier, kExprF64x2Max, JSMax);
}

WASM_EXEC_TEST(I64x2Mul) {
  RunI64x2BinOpTest(execution_tier, kExprI64x2Mul, base::MulWithWraparound);
}

WASM_EXEC_TEST(I32x4Splat) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Set up a global to hold output vector.
  int32_t* g = r.builder().AddGlobal<int32_t>(kWasmS128);
  uint8_t param1 = 0;
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(param1))),
           WASM_ONE});

  FOR_INT32_INPUTS(x) {
    r.Call(x);
    int32_t expected = x;
    for (int i = 0; i < 4; i++) {
      int32_t actual = LANE(g, i);
      CHECK_EQ(actual, expected);
    }
  }
}

WASM_EXEC_TEST(I32x4ReplaceLane) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up a global to hold input/output vector.
  int32_t* g = r.builder().AddGlobal<int32_t>(kWasmS128);
  // Build function to replace each lane with its index.
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_SPLAT(WASM_I32V(-1))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_REPLACE_LANE(
                                     0, WASM_LOCAL_GET(temp1), WASM_I32V(0))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(temp1), WASM_I32V(1))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(temp1), WASM_I32V(2))),
           WASM_GLOBAL_SET(0, WASM_SIMD_I32x4_REPLACE_LANE(
                                  3, WASM_LOCAL_GET(temp1), WASM_I32V(3))),
           WASM_ONE});

  r.Call();
  for (int32_t i = 0; i < 4; i++) {
    CHECK_EQ(i, LANE(g, i));
  }
}

WASM_EXEC_TEST(I16x8Splat) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Set up a global to hold output vector.
  int16_t* g = r.builder().AddGlobal<int16_t>(kWasmS128);
  uint8_t param1 = 0;
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(param1))),
           WASM_ONE});

  FOR_INT16_INPUTS(x) {
    r.Call(x);
    int16_t expected = x;
    for (int i = 0; i < 8; i++) {
      int16_t actual = LANE(g, i);
      CHECK_EQ(actual, expected);
    }
  }

  // Test values that do not fit in an int16.
  FOR_INT32_INPUTS(x) {
    r.Call(x);
    int16_t expected = truncate_to_int16(x);
    for (int i = 0; i < 8; i++) {
      int16_t actual = LANE(g, i);
      CHECK_EQ(actual, expected);
    }
  }
}

WASM_EXEC_TEST(I16x8ReplaceLane) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up a global to hold input/output vector.
  int16_t* g = r.builder().AddGlobal<int16_t>(kWasmS128);
  // Build function to replace each lane with its index.
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_I32V(-1))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_REPLACE_LANE(
                                     0, WASM_LOCAL_GET(temp1), WASM_I32V(0))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(temp1), WASM_I32V(1))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(temp1), WASM_I32V(2))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(temp1), WASM_I32V(3))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_REPLACE_LANE(
                                     4, WASM_LOCAL_GET(temp1), WASM_I32V(4))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_REPLACE_LANE(
                                     5, WASM_LOCAL_GET(temp1), WASM_I32V(5))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_REPLACE_LANE(
                                     6, WASM_LOCAL_GET(temp1), WASM_I32V(6))),
           WASM_GLOBAL_SET(0, WASM_SIMD_I16x8_REPLACE_LANE(
                                  7, WASM_LOCAL_GET(temp1), WASM_I32V(7))),
           WASM_ONE});

  r.Call();
  for (int16_t i = 0; i < 8; i++) {
    CHECK_EQ(i, LANE(g, i));
  }
}

WASM_EXEC_TEST(I8x16BitMask) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  uint8_t value1 = r.AllocateLocal(kWasmS128);

  r.Build(
      {WASM_LOCAL_SET(value1, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(0))),
       WASM_LOCAL_SET(value1, WASM_SIMD_I8x16_REPLACE_LANE(
                                  0, WASM_LOCAL_GET(value1), WASM_I32V(0))),
       WASM_LOCAL_SET(value1, WASM_SIMD_I8x16_REPLACE_LANE(
                                  1, WASM_LOCAL_GET(value1), WASM_I32V(-1))),
       WASM_SIMD_UNOP(kExprI8x16BitMask, WASM_LOCAL_GET(value1))});

  FOR_INT8_INPUTS(x) {
    int32_t actual = r.Call(x);
    // Lane 0 is always 0 (positive), lane 1 is always -1.
    int32_t expected = std::signbit(static_cast<double>(x)) ? 0xFFFE : 0x0002;
    CHECK_EQ(actual, expected);
  }
}

WASM_EXEC_TEST(I16x8BitMask) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  uint8_t value1 = r.AllocateLocal(kWasmS128);

  r.Build(
      {WASM_LOCAL_SET(value1, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(0))),
       WASM_LOCAL_SET(value1, WASM_SIMD_I16x8_REPLACE_LANE(
                                  0, WASM_LOCAL_GET(value1), WASM_I32V(0))),
       WASM_LOCAL_SET(value1, WASM_SIMD_I16x8_REPLACE_LANE(
                                  1, WASM_LOCAL_GET(value1), WASM_I32V(-1))),
       WASM_SIMD_UNOP(kExprI16x8BitMask, WASM_LOCAL_GET(value1))});

  FOR_INT16_INPUTS(x) {
    int32_t actual = r.Call(x);
    // Lane 0 is always 0 (positive), lane 1 is always -1.
    int32_t expected = std::signbit(static_cast<double>(x)) ? 0xFE : 2;
    CHECK_EQ(actual, expected);
  }
}

WASM_EXEC_TEST(I32x4BitMask) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  uint8_t value1 = r.AllocateLocal(kWasmS128);

  r.Build(
      {WASM_LOCAL_SET(value1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(0))),
       WASM_LOCAL_SET(value1, WASM_SIMD_I32x4_REPLACE_LANE(
                                  0, WASM_LOCAL_GET(value1), WASM_I32V(0))),
       WASM_LOCAL_SET(value1, WASM_SIMD_I32x4_REPLACE_LANE(
                                  1, WASM_LOCAL_GET(value1), WASM_I32V(-1))),
       WASM_SIMD_UNOP(kExprI32x4BitMask, WASM_LOCAL_GET(value1))});

  FOR_INT32_INPUTS(x) {
    int32_t actual = r.Call(x);
    // Lane 0 is always 0 (positive), lane 1 is always -1.
    int32_t expected = std::signbit(static_cast<double>(x)) ? 0xE : 2;
    CHECK_EQ(actual, expected);
  }
}

WASM_EXEC_TEST(I64x2BitMask) {
  WasmRunner<int32_t, int64_t> r(execution_tier);
  uint8_t value1 = r.AllocateLocal(kWasmS128);

  r.Build(
      {WASM_LOCAL_SET(value1, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(0))),
       WASM_LOCAL_SET(value1, WASM_SIMD_I64x2_REPLACE_LANE(
                                  0, WASM_LOCAL_GET(value1), WASM_I64V_1(0))),
       WASM_SIMD_UNOP(kExprI64x2BitMask, WASM_LOCAL_GET(value1))});

  for (int64_t x : compiler::ValueHelper::GetVector<int64_t>()) {
    int32_t actual = r.Call(x);
    // Lane 0 is always 0 (positive).
    int32_t expected = std::signbit(static_cast<double>(x)) ? 0x2 : 0x0;
    CHECK_EQ(actual, expected);
  }
}

WASM_EXEC_TEST(I8x16Splat) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Set up a global to hold output vector.
  int8_t* g = r.builder().AddGlobal<int8_t>(kWasmS128);
  uint8_t param1 = 0;
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(param1))),
           WASM_ONE});

  FOR_INT8_INPUTS(x) {
    r.Call(x);
    int8_t expected = x;
    for (int i = 0; i < 16; i++) {
      int8_t actual = LANE(g, i);
      CHECK_EQ(actual, expected);
    }
  }

  // Test values that do not fit in an int16.
  FOR_INT16_INPUTS(x) {
    r.Call(x);
    int8_t expected = truncate_to_int8(x);
    for (int i = 0; i < 16; i++) {
      int8_t actual = LANE(g, i);
      CHECK_EQ(actual, expected);
    }
  }
}

WASM_EXEC_TEST(I8x16ReplaceLane) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up a global to hold input/output vector.
  int8_t* g = r.builder().AddGlobal<int8_t>(kWasmS128);
  // Build function to replace each lane with its index.
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_SPLAT(WASM_I32V(-1))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     0, WASM_LOCAL_GET(temp1), WASM_I32V(0))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(temp1), WASM_I32V(1))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(temp1), WASM_I32V(2))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(temp1), WASM_I32V(3))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     4, WASM_LOCAL_GET(temp1), WASM_I32V(4))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     5, WASM_LOCAL_GET(temp1), WASM_I32V(5))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     6, WASM_LOCAL_GET(temp1), WASM_I32V(6))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     7, WASM_LOCAL_GET(temp1), WASM_I32V(7))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     8, WASM_LOCAL_GET(temp1), WASM_I32V(8))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     9, WASM_LOCAL_GET(temp1), WASM_I32V(9))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     10, WASM_LOCAL_GET(temp1), WASM_I32V(10))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     11, WASM_LOCAL_GET(temp1), WASM_I32V(11))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     12, WASM_LOCAL_GET(temp1), WASM_I32V(12))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     13, WASM_LOCAL_GET(temp1), WASM_I32V(13))),
           WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_REPLACE_LANE(
                                     14, WASM_LOCAL_GET(temp1), WASM_I32V(14))),
           WASM_GLOBAL_SET(0, WASM_SIMD_I8x16_REPLACE_LANE(
                                  15, WASM_LOCAL_GET(temp1), WASM_I32V(15))),
           WASM_ONE});

  r.Call();
  for (int8_t i = 0; i < 16; i++) {
    CHECK_EQ(i, LANE(g, i));
  }
}

// Use doubles to ensure exact conversion.
int32_t ConvertToInt(double val, bool unsigned_integer) {
  if (std::isnan(val)) return 0;
  if (unsigned_integer) {
    if (val < 0) return 0;
    if (val > kMaxUInt32) return kMaxUInt32;
    return static_cast<uint32_t>(val);
  } else {
    if (val < kMinInt) return kMinInt;
    if (val > kMaxInt) return kMaxInt;
    return static_cast<int>(val);
  }
}

// Tests both signed and unsigned conversion.
WASM_EXEC_TEST(I32x4ConvertF32x4) {
  WasmRunner<int32_t, float> r(execution_tier);
  // Create two output vectors to hold signed and unsigned results.
  int32_t* g0 = r.builder().AddGlobal<int32_t>(kWasmS128);
  int32_t* g1 = r.builder().AddGlobal<int32_t>(kWasmS128);
  // Build fn to splat test value, perform conversions, and write the results.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(kExprI32x4SConvertF32x4,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(1, WASM_SIMD_UNOP(kExprI32x4UConvertF32x4,
                                             WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_FLOAT32_INPUTS(x) {
    if (!PlatformCanRepresent(x)) continue;
    r.Call(x);
    int32_t expected_signed = ConvertToInt(x, false);
    int32_t expected_unsigned = ConvertToInt(x, true);
    for (int i = 0; i < 4; i++) {
      CHECK_EQ(expected_signed, LANE(g0, i));
      CHECK_EQ(expected_unsigned, LANE(g1, i));
    }
  }
}

// Tests both signed and unsigned conversion from I16x8 (unpacking).
WASM_EXEC_TEST(I32x4ConvertI16x8) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Create four output vectors to hold signed and unsigned results.
  int32_t* g0 = r.builder().AddGlobal<int32_t>(kWasmS128);
  int32_t* g1 = r.builder().AddGlobal<int32_t>(kWasmS128);
  int32_t* g2 = r.builder().AddGlobal<int32_t>(kWasmS128);
  int32_t* g3 = r.builder().AddGlobal<int32_t>(kWasmS128);
  // Build fn to splat test value, perform conversions, and write the results.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(kExprI32x4SConvertI16x8High,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(1, WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(2, WASM_SIMD_UNOP(kExprI32x4UConvertI16x8High,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(3, WASM_SIMD_UNOP(kExprI32x4UConvertI16x8Low,
                                             WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT16_INPUTS(x) {
    r.Call(x);
    int32_t expected_signed = static_cast<int32_t>(x);
    int32_t expected_unsigned = static_cast<int32_t>(static_cast<uint16_t>(x));
    for (int i = 0; i < 4; i++) {
      CHECK_EQ(expected_signed, LANE(g0, i));
      CHECK_EQ(expected_signed, LANE(g1, i));
      CHECK_EQ(expected_unsigned, LANE(g2, i));
      CHECK_EQ(expected_unsigned, LANE(g3, i));
    }
  }
}

// Tests both signed and unsigned conversion from I32x4 (unpacking).
WASM_EXEC_TEST(I64x2ConvertI32x4) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Create four output vectors to hold signed and unsigned results.
  int64_t* g0 = r.builder().AddGlobal<int64_t>(kWasmS128);
  int64_t* g1 = r.builder().AddGlobal<int64_t>(kWasmS128);
  uint64_t* g2 = r.builder().AddGlobal<uint64_t>(kWasmS128);
  uint64_t* g3 = r.builder().AddGlobal<uint64_t>(kWasmS128);
  // Build fn to splat test value, perform conversions, and write the results.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(kExprI64x2SConvertI32x4High,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(1, WASM_SIMD_UNOP(kExprI64x2SConvertI32x4Low,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(2, WASM_SIMD_UNOP(kExprI64x2UConvertI32x4High,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(3, WASM_SIMD_UNOP(kExprI64x2UConvertI32x4Low,
                                             WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT32_INPUTS(x) {
    r.Call(x);
    int64_t expected_signed = static_cast<int64_t>(x);
    uint64_t expected_unsigned =
        static_cast<uint64_t>(static_cast<uint32_t>(x));
    for (int i = 0; i < 2; i++) {
      CHECK_EQ(expected_signed, LANE(g0, i));
      CHECK_EQ(expected_signed, LANE(g1, i));
      CHECK_EQ(expected_unsigned, LANE(g2, i));
      CHECK_EQ(expected_unsigned, LANE(g3, i));
    }
  }
}

WASM_EXEC_TEST(I32x4Neg) {
  RunI32x4UnOpTest(execution_tier, kExprI32x4Neg, base::NegateWithWraparound);
}

WASM_EXEC_TEST(I32x4Abs) {
  RunI32x4UnOpTest(execution_tier, kExprI32x4Abs, std::abs);
}

WASM_EXEC_TEST(S128Not) {
  RunI32x4UnOpTest(execution_tier, kExprS128Not, [](int32_t x) { return ~x; });
}

template <typename Narrow, typename Wide>
void RunExtAddPairwiseTest(TestExecutionTier execution_tier,
                           WasmOpcode ext_add_pairwise, WasmOpcode splat,
                           Shuffle interleaving_shuffle) {
  constexpr int num_lanes = kSimd128Size / sizeof(Wide);
  WasmRunner<int32_t, Narrow, Narrow> r(execution_tier);
  Wide* g = r.builder().template AddGlobal<Wide>(kWasmS128);

  r.Build({WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, interleaving_shuffle,
                                      WASM_SIMD_UNOP(splat, WASM_LOCAL_GET(0)),
                                      WASM_SIMD_UNOP(splat, WASM_LOCAL_GET(1))),
           WASM_SIMD_OP(ext_add_pairwise), kExprGlobalSet, 0, WASM_ONE});

  auto v = compiler::ValueHelper::GetVector<Narrow>();
  // Iterate vector from both ends to try and splat two different values.
  for (auto i = v.begin(), j = v.end() - 1; i < v.end(); i++, j--) {
    r.Call(*i, *j);
    Wide expected = AddLong<Wide>(*i, *j);
    for (int l = 0; l < num_lanes; l++) {
      CHECK_EQ(expected, LANE(g, l));
    }
  }
}

// interleave even lanes from one input and odd lanes from another.
constexpr Shuffle interleave_16x8_shuffle = {0, 1, 18, 19, 4,  5,  22, 23,
                                             8, 9, 26, 27, 12, 13, 30, 31};
constexpr Shuffle interleave_8x16_shuffle = {0, 17, 2,  19, 4,  21, 6,  23,
                                             8, 25, 10, 27, 12, 29, 14, 31};

WASM_EXEC_TEST(I32x4ExtAddPairwiseI16x8S) {
  RunExtAddPairwiseTest<int16_t, int32_t>(
      execution_tier, kExprI32x4ExtAddPairwiseI16x8S, kExprI16x8Splat,
      interleave_16x8_shuffle);
}

WASM_EXEC_TEST(I32x4ExtAddPairwiseI16x8U) {
  RunExtAddPairwiseTest<uint16_t, uint32_t>(
      execution_tier, kExprI32x4ExtAddPairwiseI16x8U, kExprI16x8Splat,
      interleave_16x8_shuffle);
}

WASM_EXEC_TEST(I16x8ExtAddPairwiseI8x16S) {
  RunExtAddPairwiseTest<int8_t, int16_t>(
      execution_tier, kExprI16x8ExtAddPairwiseI8x16S, kExprI8x16Splat,
      interleave_8x16_shuffle);
}

WASM_EXEC_TEST(I16x8ExtAddPairwiseI8x16U) {
  RunExtAddPairwiseTest<uint8_t, uint16_t>(
      execution_tier, kExprI16x8ExtAddPairwiseI8x16U, kExprI8x16Splat,
      interleave_8x16_shuffle);
}

WASM_EXEC_TEST(I32x4Add) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4Add, base::AddWithWraparound);
}

WASM_EXEC_TEST(I32x4Sub) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4Sub, base::SubWithWraparound);
}

WASM_EXEC_TEST(I32x4Mul) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4Mul, base::MulWithWraparound);
}

WASM_EXEC_TEST(I32x4MinS) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4MinS, Minimum);
}

WASM_EXEC_TEST(I32x4MaxS) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4MaxS, Maximum);
}

WASM_EXEC_TEST(I32x4MinU) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4MinU, UnsignedMinimum);
}
WASM_EXEC_TEST(I32x4MaxU) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4MaxU,

                    UnsignedMaximum);
}

WASM_EXEC_TEST(S128And) {
  RunI32x4BinOpTest(execution_tier, kExprS128And,
                    [](int32_t x, int32_t y) { return x & y; });
}

enum ConstSide { kConstLeft, kConstRight };

template <typename ScalarType>
using BinOp = ScalarType (*)(ScalarType, ScalarType);
template <typename ScalarType>
void RunS128ConstBinOpTest(TestExecutionTier execution_tier,
                           ConstSide const_side, WasmOpcode binop_opcode,
                           WasmOpcode splat_opcode,
                           BinOp<ScalarType> expected_op) {
  for (ScalarType x : compiler::ValueHelper::GetVector<ScalarType>()) {
    WasmRunner<int32_t, ScalarType> r(execution_tier);
    // Global to hold output.
    ScalarType* g = r.builder().template AddGlobal<ScalarType>(kWasmS128);
    // Build a function to splat one argument into a local,
    // and execute the op with a const as the second argument
    uint8_t value = 0;
    uint8_t temp = r.AllocateLocal(kWasmS128);
    uint8_t const_buffer[16];
    for (size_t i = 0; i < kSimd128Size / sizeof(ScalarType); i++) {
      WriteLittleEndianValue<ScalarType>(
          reinterpret_cast<ScalarType*>(&const_buffer[0]) + i, x);
    }
    switch (const_side) {
      case kConstLeft:
        r.Build({WASM_LOCAL_SET(
                     temp, WASM_SIMD_OPN(splat_opcode, WASM_LOCAL_GET(value))),
                 WASM_GLOBAL_SET(
                     0, WASM_SIMD_BINOP(binop_opcode,
                                        WASM_SIMD_CONSTANT(const_buffer),
                                        WASM_LOCAL_GET(temp))),
                 WASM_ONE});
        break;
      case kConstRight:
        r.Build({WASM_LOCAL_SET(
                     temp, WASM_SIMD_OPN(splat_opcode, WASM_LOCAL_GET(value))),
                 WASM_GLOBAL_SET(
                     0, WASM_SIMD_BINOP(binop_opcode, WASM_LOCAL_GET(temp),
                                        WASM_SIMD_CONSTANT(const_buffer))),
                 WASM_ONE});
        break;
    }
    for (ScalarType y : compiler::ValueHelper::GetVector<ScalarType>()) {
      r.Call(y);
      ScalarType expected =
          (const_side == kConstLeft) ? expected_op(x, y) : expected_op(y, x);
      for (size_t i = 0; i < kSimd128Size / sizeof(ScalarType); i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

WASM_EXEC_TEST(S128AndImm) {
  RunS128ConstBinOpTest<int32_t>(execution_tier, kConstLeft, kExprS128And,
                                 kExprI32x4Splat,
                                 [](int32_t x, int32_t y) { return x & y; });
  RunS128ConstBinOpTest<int32_t>(execution_tier, kConstRight, kExprS128And,
                                 kExprI32x4Splat,
                                 [](int32_t x, int32_t y) { return x & y; });
  RunS128ConstBinOpTest<int16_t>(
      execution_tier, kConstLeft, kExprS128And, kExprI16x8Splat,
      [](int16_t x, int16_t y) { return static_cast<int16_t>(x & y); });
  RunS128ConstBinOpTest<int16_t>(
      execution_tier, kConstRight, kExprS128And, kExprI16x8Splat,
      [](int16_t x, int16_t y) { return static_cast<int16_t>(x & y); });
}

WASM_EXEC_TEST(S128Or) {
  RunI32x4BinOpTest(execution_tier, kExprS128Or,
                    [](int32_t x, int32_t y) { return x | y; });
}

WASM_EXEC_TEST(S128Xor) {
  RunI32x4BinOpTest(execution_tier, kExprS128Xor,
                    [](int32_t x, int32_t y) { return x ^ y; });
}

// Bitwise operation, doesn't really matter what simd type we test it with.
WASM_EXEC_TEST(S128AndNot) {
  RunI32x4BinOpTest(execution_tier, kExprS128AndNot,
                    [](int32_t x, int32_t y) { return x & ~y; });
}

WASM_EXEC_TEST(S128AndNotImm) {
  RunS128ConstBinOpTest<int32_t>(execution_tier, kConstLeft, kExprS128AndNot,
                                 kExprI32x4Splat,
                                 [](int32_t x, int32_t y) { return x & ~y; });
  RunS128ConstBinOpTest<int32_t>(execution_tier, kConstRight, kExprS128AndNot,
                                 kExprI32x4Splat,
                                 [](int32_t x, int32_t y) { return x & ~y; });
  RunS128ConstBinOpTest<int16_t>(
      execution_tier, kConstLeft, kExprS128AndNot, kExprI16x8Splat,
      [](int16_t x, int16_t y) { return static_cast<int16_t>(x & ~y); });
  RunS128ConstBinOpTest<int16_t>(
      execution_tier, kConstRight, kExprS128AndNot, kExprI16x8Splat,
      [](int16_t x, int16_t y) { return static_cast<int16_t>(x & ~y); });
}

WASM_EXEC_TEST(I32x4Eq) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4Eq, Equal);
}

WASM_EXEC_TEST(I32x4Ne) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4Ne, NotEqual);
}

WASM_EXEC_TEST(I32x4LtS) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4LtS, Less);
}

WASM_EXEC_TEST(I32x4LeS) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4LeS, LessEqual);
}

WASM_EXEC_TEST(I32x4GtS) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4GtS, Greater);
}

WASM_EXEC_TEST(I32x4GeS) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4GeS, GreaterEqual);
}

WASM_EXEC_TEST(I32x4LtU) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4LtU, UnsignedLess);
}

WASM_EXEC_TEST(I32x4LeU) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4LeU, UnsignedLessEqual);
}

WASM_EXEC_TEST(I32x4GtU) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4GtU, UnsignedGreater);
}

WASM_EXEC_TEST(I32x4GeU) {
  RunI32x4BinOpTest(execution_tier, kExprI32x4GeU, UnsignedGreaterEqual);
}

WASM_EXEC_TEST(I32x4EqZero) {
  RunICompareOpConstImmTest<int32_t>(execution_tier, kExprI32x4Eq,
                                     kExprI32x4Splat, Equal);
}

WASM_EXEC_TEST(I32x4NeZero) {
  RunICompareOpConstImmTest<int32_t>(execution_tier, kExprI32x4Ne,
                                     kExprI32x4Splat, NotEqual);
}

WASM_EXEC_TEST(I32x4GtZero) {
  RunICompareOpConstImmTest<int32_t>(execution_tier, kExprI32x4GtS,
                                     kExprI32x4Splat, Greater);
}

WASM_EXEC_TEST(I32x4GeZero) {
  RunICompareOpConstImmTest<int32_t>(execution_tier, kExprI32x4GeS,
                                     kExprI32x4Splat, GreaterEqual);
}

WASM_EXEC_TEST(I32x4LtZero) {
  RunICompareOpConstImmTest<int32_t>(execution_tier, kExprI32x4LtS,
                                     kExprI32x4Splat, Less);
}

WASM_EXEC_TEST(I32x4LeZero) {
  RunICompareOpConstImmTest<int32_t>(execution_tier, kExprI32x4LeS,
                                     kExprI32x4Splat, LessEqual);
}

WASM_EXEC_TEST(I32x4Shl) {
  RunI32x4ShiftOpTest(execution_tier, kExprI32x4Shl, LogicalShiftLeft);
}

WASM_EXEC_TEST(I32x4ShrS) {
  RunI32x4ShiftOpTest(execution_tier, kExprI32x4ShrS, ArithmeticShiftRight);
}

WASM_EXEC_TEST(I32x4ShrU) {
  RunI32x4ShiftOpTest(execution_tier, kExprI32x4ShrU, LogicalShiftRight);
}

WASM_EXEC_TEST(I32x4ShiftAdd) {
  for (int imm = 0; imm <= 32; imm++) {
    RunShiftAddTestSequence<int32_t>(execution_tier, kExprI32x4ShrU,
                                     kExprI32x4Add, kExprI32x4Splat, imm,
                                     LogicalShiftRight);
    RunShiftAddTestSequence<int32_t>(execution_tier, kExprI32x4ShrS,
                                     kExprI32x4Add, kExprI32x4Splat, imm,
                                     ArithmeticShiftRight);
  }
}

// Tests both signed and unsigned conversion from I8x16 (unpacking).
WASM_EXEC_TEST(I16x8ConvertI8x16) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Create four output vectors to hold signed and unsigned results.
  int16_t* g0 = r.builder().AddGlobal<int16_t>(kWasmS128);
  int16_t* g1 = r.builder().AddGlobal<int16_t>(kWasmS128);
  int16_t* g2 = r.builder().AddGlobal<int16_t>(kWasmS128);
  int16_t* g3 = r.builder().AddGlobal<int16_t>(kWasmS128);
  // Build fn to splat test value, perform conversions, and write the results.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_UNOP(kExprI16x8SConvertI8x16High,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(1, WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(2, WASM_SIMD_UNOP(kExprI16x8UConvertI8x16High,
                                             WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(3, WASM_SIMD_UNOP(kExprI16x8UConvertI8x16Low,
                                             WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT8_INPUTS(x) {
    r.Call(x);
    int16_t expected_signed = static_cast<int16_t>(x);
    int16_t expected_unsigned = static_cast<int16_t>(static_cast<uint8_t>(x));
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(expected_signed, LANE(g0, i));
      CHECK_EQ(expected_signed, LANE(g1, i));
      CHECK_EQ(expected_unsigned, LANE(g2, i));
      CHECK_EQ(expected_unsigned, LANE(g3, i));
    }
  }
}

// Tests both signed and unsigned conversion from I32x4 (packing).
WASM_EXEC_TEST(I16x8ConvertI32x4) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Create output vectors to hold signed and unsigned results.
  int16_t* g0 = r.builder().AddGlobal<int16_t>(kWasmS128);
  int16_t* g1 = r.builder().AddGlobal<int16_t>(kWasmS128);
  // Build fn to splat test value, perform conversions, and write the results.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(kExprI16x8SConvertI32x4,
                                              WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(1, WASM_SIMD_BINOP(kExprI16x8UConvertI32x4,
                                              WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT32_INPUTS(x) {
    r.Call(x);
    int16_t expected_signed = base::saturated_cast<int16_t>(x);
    int16_t expected_unsigned = base::saturated_cast<uint16_t>(x);
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(expected_signed, LANE(g0, i));
      CHECK_EQ(expected_unsigned, LANE(g1, i));
    }
  }
}

WASM_EXEC_TEST(I16x8Neg) {
  RunI16x8UnOpTest(execution_tier, kExprI16x8Neg, base::NegateWithWraparound);
}

WASM_EXEC_TEST(I16x8Abs) {
  RunI16x8UnOpTest(execution_tier, kExprI16x8Abs, Abs);
}

WASM_EXEC_TEST(I16x8Add) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8Add, base::AddWithWraparound);
}

WASM_EXEC_TEST(I16x8AddSatS) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8AddSatS, SaturateAdd<int16_t>);
}

WASM_EXEC_TEST(I16x8Sub) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8Sub, base::SubWithWraparound);
}

WASM_EXEC_TEST(I16x8SubSatS) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8SubSatS, SaturateSub<int16_t>);
}

WASM_EXEC_TEST(I16x8Mul) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8Mul, base::MulWithWraparound);
}

WASM_EXEC_TEST(I16x8MinS) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8MinS, Minimum);
}

WASM_EXEC_TEST(I16x8MaxS) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8MaxS, Maximum);
}

WASM_EXEC_TEST(I16x8AddSatU) {
  RunI16x8BinOpTest<uint16_t>(execution_tier, kExprI16x8AddSatU,
                              SaturateAdd<uint16_t>);
}

WASM_EXEC_TEST(I16x8SubSatU) {
  RunI16x8BinOpTest<uint16_t>(execution_tier, kExprI16x8SubSatU,
                              SaturateSub<uint16_t>);
}

WASM_EXEC_TEST(I16x8MinU) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8MinU, UnsignedMinimum);
}

WASM_EXEC_TEST(I16x8MaxU) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8MaxU, UnsignedMaximum);
}

WASM_EXEC_TEST(I16x8Eq) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8Eq, Equal);
}

WASM_EXEC_TEST(I16x8Ne) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8Ne, NotEqual);
}

WASM_EXEC_TEST(I16x8LtS) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8LtS, Less);
}

WASM_EXEC_TEST(I16x8LeS) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8LeS, LessEqual);
}

WASM_EXEC_TEST(I16x8GtS) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8GtS, Greater);
}

WASM_EXEC_TEST(I16x8GeS) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8GeS, GreaterEqual);
}

WASM_EXEC_TEST(I16x8GtU) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8GtU, UnsignedGreater);
}

WASM_EXEC_TEST(I16x8GeU) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8GeU, UnsignedGreaterEqual);
}

WASM_EXEC_TEST(I16x8LtU) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8LtU, UnsignedLess);
}

WASM_EXEC_TEST(I16x8LeU) {
  RunI16x8BinOpTest(execution_tier, kExprI16x8LeU, UnsignedLessEqual);
}

WASM_EXEC_TEST(I16x8EqZero) {
  RunICompareOpConstImmTest<int16_t>(execution_tier, kExprI16x8Eq,
                                     kExprI16x8Splat, Equal);
}

WASM_EXEC_TEST(I16x8NeZero) {
  RunICompareOpConstImmTest<int16_t>(execution_tier, kExprI16x8Ne,
                                     kExprI16x8Splat, NotEqual);
}

WASM_EXEC_TEST(I16x8GtZero) {
  RunICompareOpConstImmTest<int16_t>(execution_tier, kExprI16x8GtS,
                                     kExprI16x8Splat, Greater);
}

WASM_EXEC_TEST(I16x8GeZero) {
  RunICompareOpConstImmTest<int16_t>(execution_tier, kExprI16x8GeS,
                                     kExprI16x8Splat, GreaterEqual);
}

WASM_EXEC_TEST(I16x8LtZero) {
  RunICompareOpConstImmTest<int16_t>(execution_tier, kExprI16x8LtS,
                                     kExprI16x8Splat, Less);
}

WASM_EXEC_TEST(I16x8LeZero) {
  RunICompareOpConstImmTest<int16_t>(execution_tier, kExprI16x8LeS,
                                     kExprI16x8Splat, LessEqual);
}

WASM_EXEC_TEST(I16x8RoundingAverageU) {
  RunI16x8BinOpTest<uint16_t>(execution_tier, kExprI16x8RoundingAverageU,
                              RoundingAverageUnsigned);
}

WASM_EXEC_TEST(I16x8Q15MulRSatS) {
  RunI16x8BinOpTest<int16_t>(execution_tier, kExprI16x8Q15MulRSatS,
                             SaturateRoundingQMul<int16_t>);
}

namespace {
enum class MulHalf { kLow, kHigh };

// Helper to run ext mul tests. It will splat 2 input values into 2 v128, call
// the mul op on these operands, and set the result into a global.
// It will zero the top or bottom half of one of the operands, this will catch
// mistakes if we are multiply the incorrect halves.
template <typename S, typename T, typename OpType = T (*)(S, S)>
void RunExtMulTest(TestExecutionTier execution_tier, WasmOpcode opcode,
                   OpType expected_op, WasmOpcode splat, MulHalf half) {
  WasmRunner<int32_t, S, S> r(execution_tier);
  int lane_to_zero = half == MulHalf::kLow ? 1 : 0;
  T* g = r.builder().template AddGlobal<T>(kWasmS128);

  r.Build({WASM_GLOBAL_SET(
               0, WASM_SIMD_BINOP(opcode,
                                  WASM_SIMD_I64x2_REPLACE_LANE(
                                      lane_to_zero,
                                      WASM_SIMD_UNOP(splat, WASM_LOCAL_GET(0)),
                                      WASM_I64V_1(0)),
                                  WASM_SIMD_UNOP(splat, WASM_LOCAL_GET(1)))),
           WASM_ONE});

  constexpr int lanes = kSimd128Size / sizeof(T);
  for (S x : compiler::ValueHelper::GetVector<S>()) {
    for (S y : compiler::ValueHelper::GetVector<S>()) {
      r.Call(x, y);
      T expected = expected_op(x, y);
      for (int i = 0; i < lanes; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}
}  // namespace

WASM_EXEC_TEST(I16x8ExtMulLowI8x16S) {
  RunExtMulTest<int8_t, int16_t>(execution_tier, kExprI16x8ExtMulLowI8x16S,
                                 MultiplyLong, kExprI8x16Splat, MulHalf::kLow);
}

WASM_EXEC_TEST(I16x8ExtMulHighI8x16S) {
  RunExtMulTest<int8_t, int16_t>(execution_tier, kExprI16x8ExtMulHighI8x16S,
                                 MultiplyLong, kExprI8x16Splat, MulHalf::kHigh);
}

WASM_EXEC_TEST(I16x8ExtMulLowI8x16U) {
  RunExtMulTest<uint8_t, uint16_t>(execution_tier, kExprI16x8ExtMulLowI8x16U,
                                   MultiplyLong, kExprI8x16Splat,
                                   MulHalf::kLow);
}

WASM_EXEC_TEST(I16x8ExtMulHighI8x16U) {
  RunExtMulTest<uint8_t, uint16_t>(execution_tier, kExprI16x8ExtMulHighI8x16U,
                                   MultiplyLong, kExprI8x16Splat,
                                   MulHalf::kHigh);
}

WASM_EXEC_TEST(I32x4ExtMulLowI16x8S) {
  RunExtMulTest<int16_t, int32_t>(execution_tier, kExprI32x4ExtMulLowI16x8S,
                                  MultiplyLong, kExprI16x8Splat, MulHalf::kLow);
}

WASM_EXEC_TEST(I32x4ExtMulHighI16x8S) {
  RunExtMulTest<int16_t, int32_t>(execution_tier, kExprI32x4ExtMulHighI16x8S,
                                  MultiplyLong, kExprI16x8Splat,
                                  MulHalf::kHigh);
}

WASM_EXEC_TEST(I32x4ExtMulLowI16x8U) {
  RunExtMulTest<uint16_t, uint32_t>(execution_tier, kExprI32x4ExtMulLowI16x8U,
                                    MultiplyLong, kExprI16x8Splat,
                                    MulHalf::kLow);
}

WASM_EXEC_TEST(I32x4ExtMulHighI16x8U) {
  RunExtMulTest<uint16_t, uint32_t>(execution_tier, kExprI32x4ExtMulHighI16x8U,
                                    MultiplyLong, kExprI16x8Splat,
                                    MulHalf::kHigh);
}

WASM_EXEC_TEST(I64x2ExtMulLowI32x4S) {
  RunExtMulTest<int32_t, int64_t>(execution_tier, kExprI64x2ExtMulLowI32x4S,
                                  MultiplyLong, kExprI32x4Splat, MulHalf::kLow);
}

WASM_EXEC_TEST(I64x2ExtMulHighI32x4S) {
  RunExtMulTest<int32_t, int64_t>(execution_tier, kExprI64x2ExtMulHighI32x4S,
                                  MultiplyLong, kExprI32x4Splat,
                                  MulHalf::kHigh);
}

WASM_EXEC_TEST(I64x2ExtMulLowI32x4U) {
  RunExtMulTest<uint32_t, uint64_t>(execution_tier, kExprI64x2ExtMulLowI32x4U,
                                    MultiplyLong, kExprI32x4Splat,
                                    MulHalf::kLow);
}

WASM_EXEC_TEST(I64x2ExtMulHighI32x4U) {
  RunExtMulTest<uint32_t, uint64_t>(execution_tier, kExprI64x2ExtMulHighI32x4U,
                                    MultiplyLong, kExprI32x4Splat,
                                    MulHalf::kHigh);
}

namespace {
// Test add(mul(x, y, z) optimizations.
template <typename S, typename T>
void RunExtMulAddOptimizationTest(TestExecutionTier execution_tier,
                                  WasmOpcode ext_mul, WasmOpcode narrow_splat,
                                  WasmOpcode wide_splat, WasmOpcode wide_add,
                                  std::function<T(T, T)> addop) {
  WasmRunner<int32_t, S, T> r(execution_tier);
  T* g = r.builder().template AddGlobal<T>(kWasmS128);

  // global[0] =
  //   add(
  //     splat(local[1]),
  //     extmul(splat(local[0]), splat(local[0])))
  r.Build(
      {WASM_GLOBAL_SET(
           0, WASM_SIMD_BINOP(
                  wide_add, WASM_SIMD_UNOP(wide_splat, WASM_LOCAL_GET(1)),
                  WASM_SIMD_BINOP(
                      ext_mul, WASM_SIMD_UNOP(narrow_splat, WASM_LOCAL_GET(0)),
                      WASM_SIMD_UNOP(narrow_splat, WASM_LOCAL_GET(0))))),
       WASM_ONE});

  constexpr int lanes = kSimd128Size / sizeof(T);
  for (S x : compiler::ValueHelper::GetVector<S>()) {
    for (T y : compiler::ValueHelper::GetVector<T>()) {
      r.Call(x, y);

      T expected = addop(MultiplyLong<T, S>(x, x), y);
      for (int i = 0; i < lanes; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}
}  // namespace

// Helper which defines high/low, signed/unsigned test cases for extmul + add
// optimization.
#define EXTMUL_ADD_OPTIMIZATION_TEST(NarrowType, NarrowShape, WideType,  \
                                     WideShape)                          \
  WASM_EXEC_TEST(WideShape##ExtMulLow##NarrowShape##SAddOptimization) {  \
    RunExtMulAddOptimizationTest<NarrowType, WideType>(                  \
        execution_tier, kExpr##WideShape##ExtMulLow##NarrowShape##S,     \
        kExpr##NarrowShape##Splat, kExpr##WideShape##Splat,              \
        kExpr##WideShape##Add, base::AddWithWraparound<WideType>);       \
  }                                                                      \
  WASM_EXEC_TEST(WideShape##ExtMulHigh##NarrowShape##SAddOptimization) { \
    RunExtMulAddOptimizationTest<NarrowType, WideType>(                  \
        execution_tier, kExpr##WideShape##ExtMulHigh##NarrowShape##S,    \
        kExpr##NarrowShape##Splat, kExpr##WideShape##Splat,              \
        kExpr##WideShape##Add, base::AddWithWraparound<WideType>);       \
  }                                                                      \
  WASM_EXEC_TEST(WideShape##ExtMulLow##NarrowShape##UAddOptimization) {  \
    RunExtMulAddOptimizationTest<u##NarrowType, u##WideType>(            \
        execution_tier, kExpr##WideShape##ExtMulLow##NarrowShape##U,     \
        kExpr##NarrowShape##Splat, kExpr##WideShape##Splat,              \
        kExpr##WideShape##Add, std::plus<u##WideType>());                \
  }                                                                      \
  WASM_EXEC_TEST(WideShape##ExtMulHigh##NarrowShape##UAddOptimization) { \
    RunExtMulAddOptimizationTest<u##NarrowType, u##WideType>(            \
        execution_tier, kExpr##WideShape##ExtMulHigh##NarrowShape##U,    \
        kExpr##NarrowShape##Splat, kExpr##WideShape##Splat,              \
        kExpr##WideShape##Add, std::plus<u##WideType>());                \
  }

EXTMUL_ADD_OPTIMIZATION_TEST(int8_t, I8x16, int16_t, I16x8)
EXTMUL_ADD_OPTIMIZATION_TEST(int16_t, I16x8, int32_t, I32x4)

#undef EXTMUL_ADD_OPTIMIZATION_TEST

WASM_EXEC_TEST(I32x4DotI16x8S) {
  WasmRunner<int32_t, int16_t, int16_t> r(execution_tier);
  int32_t* g = r.builder().template AddGlobal<int32_t>(kWasmS128);
  uint8_t value1 = 0, value2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value2))),
           WASM_GLOBAL_SET(
               0, WASM_SIMD_BINOP(kExprI32x4DotI16x8S, WASM_LOCAL_GET(temp1),
                                  WASM_LOCAL_GET(temp2))),
           WASM_ONE});

  for (int16_t x : compiler::ValueHelper::GetVector<int16_t>()) {
    for (int16_t y : compiler::ValueHelper::GetVector<int16_t>()) {
      r.Call(x, y);
      // x * y * 2 can overflow (0x8000), the behavior is to wraparound.
      int32_t expected = base::MulWithWraparound(x * y, 2);
      for (int i = 0; i < 4; i++) {
        CHECK_EQ(expected, LANE(g, i));
      }
    }
  }
}

WASM_EXEC_TEST(I16x8Shl) {
  RunI16x8ShiftOpTest(execution_tier, kExprI16x8Shl, LogicalShiftLeft);
}

WASM_EXEC_TEST(I16x8ShrS) {
  RunI16x8ShiftOpTest(execution_tier, kExprI16x8ShrS, ArithmeticShiftRight);
}

WASM_EXEC_TEST(I16x8ShrU) {
  RunI16x8ShiftOpTest(execution_tier, kExprI16x8ShrU, LogicalShiftRight);
}

WASM_EXEC_TEST(I16x8ShiftAdd) {
  for (int imm = 0; imm <= 16; imm++) {
    RunShiftAddTestSequence<int16_t>(execution_tier, kExprI16x8ShrU,
                                     kExprI16x8Add, kExprI16x8Splat, imm,
                                     LogicalShiftRight);
    RunShiftAddTestSequence<int16_t>(execution_tier, kExprI16x8ShrS,
                                     kExprI16x8Add, kExprI16x8Splat, imm,
                                     ArithmeticShiftRight);
  }
}

WASM_EXEC_TEST(I8x16Neg) {
  RunI8x16UnOpTest(execution_tier, kExprI8x16Neg, base::NegateWithWraparound);
}

WASM_EXEC_TEST(I8x16Abs) {
  RunI8x16UnOpTest(execution_tier, kExprI8x16Abs, Abs);
}

WASM_EXEC_TEST(I8x16Popcnt) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Global to hold output.
  int8_t* g = r.builder().AddGlobal<int8_t>(kWasmS128);
  // Build fn to splat test value, perform unop, and write the result.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(
               0, WASM_SIMD_UNOP(kExprI8x16Popcnt, WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_UINT8_INPUTS(x) {
    r.Call(x);
    unsigned expected = base::bits::CountPopulation(x);
    for (int i = 0; i < 16; i++) {
      CHECK_EQ(expected, LANE(g, i));
    }
  }
}

// Tests both signed and unsigned conversion from I16x8 (packing).
WASM_EXEC_TEST(I8x16ConvertI16x8) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Create output vectors to hold signed and unsigned results.
  int8_t* g_s = r.builder().AddGlobal<int8_t>(kWasmS128);
  uint8_t* g_u = r.builder().AddGlobal<uint8_t>(kWasmS128);
  // Build fn to splat test value, perform conversions, and write the results.
  uint8_t value = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(kExprI8x16SConvertI16x8,
                                              WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp1))),
           WASM_GLOBAL_SET(1, WASM_SIMD_BINOP(kExprI8x16UConvertI16x8,
                                              WASM_LOCAL_GET(temp1),
                                              WASM_LOCAL_GET(temp1))),
           WASM_ONE});

  FOR_INT16_INPUTS(x) {
    r.Call(x);
    int8_t expected_signed = base::saturated_cast<int8_t>(x);
    uint8_t expected_unsigned = base::saturated_cast<uint8_t>(x);
    for (int i = 0; i < 16; i++) {
      CHECK_EQ(expected_signed, LANE(g_s, i));
      CHECK_EQ(expected_unsigned, LANE(g_u, i));
    }
  }
}

WASM_EXEC_TEST(I8x16Add) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16Add, base::AddWithWraparound);
}

WASM_EXEC_TEST(I8x16AddSatS) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16AddSatS, SaturateAdd<int8_t>);
}

WASM_EXEC_TEST(I8x16Sub) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16Sub, base::SubWithWraparound);
}

WASM_EXEC_TEST(I8x16SubSatS) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16SubSatS, SaturateSub<int8_t>);
}

WASM_EXEC_TEST(I8x16MinS) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16MinS, Minimum);
}

WASM_EXEC_TEST(I8x16MaxS) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16MaxS, Maximum);
}

WASM_EXEC_TEST(I8x16AddSatU) {
  RunI8x16BinOpTest<uint8_t>(execution_tier, kExprI8x16AddSatU,
                             SaturateAdd<uint8_t>);
}

WASM_EXEC_TEST(I8x16SubSatU) {
  RunI8x16BinOpTest<uint8_t>(execution_tier, kExprI8x16SubSatU,
                             SaturateSub<uint8_t>);
}

WASM_EXEC_TEST(I8x16MinU) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16MinU, UnsignedMinimum);
}

WASM_EXEC_TEST(I8x16MaxU) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16MaxU, UnsignedMaximum);
}

WASM_EXEC_TEST(I8x16Eq) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16Eq, Equal);
}

WASM_EXEC_TEST(I8x16Ne) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16Ne, NotEqual);
}

WASM_EXEC_TEST(I8x16GtS) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16GtS, Greater);
}

WASM_EXEC_TEST(I8x16GeS) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16GeS, GreaterEqual);
}

WASM_EXEC_TEST(I8x16LtS) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16LtS, Less);
}

WASM_EXEC_TEST(I8x16LeS) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16LeS, LessEqual);
}

WASM_EXEC_TEST(I8x16GtU) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16GtU, UnsignedGreater);
}

WASM_EXEC_TEST(I8x16GeU) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16GeU, UnsignedGreaterEqual);
}

WASM_EXEC_TEST(I8x16LtU) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16LtU, UnsignedLess);
}

WASM_EXEC_TEST(I8x16LeU) {
  RunI8x16BinOpTest(execution_tier, kExprI8x16LeU, UnsignedLessEqual);
}

WASM_EXEC_TEST(I8x16EqZero) {
  RunICompareOpConstImmTest<int8_t>(execution_tier, kExprI8x16Eq,
                                    kExprI8x16Splat, Equal);
}

WASM_EXEC_TEST(I8x16NeZero) {
  RunICompareOpConstImmTest<int8_t>(execution_tier, kExprI8x16Ne,
                                    kExprI8x16Splat, NotEqual);
}

WASM_EXEC_TEST(I8x16GtZero) {
  RunICompareOpConstImmTest<int8_t>(execution_tier, kExprI8x16GtS,
                                    kExprI8x16Splat, Greater);
}

WASM_EXEC_TEST(I8x16GeZero) {
  RunICompareOpConstImmTest<int8_t>(execution_tier, kExprI8x16GeS,
                                    kExprI8x16Splat, GreaterEqual);
}

WASM_EXEC_TEST(I8x16LtZero) {
  RunICompareOpConstImmTest<int8_t>(execution_tier, kExprI8x16LtS,
                                    kExprI8x16Splat, Less);
}

WASM_EXEC_TEST(I8x16LeZero) {
  RunICompareOpConstImmTest<int8_t>(execution_tier, kExprI8x16LeS,
                                    kExprI8x16Splat, LessEqual);
}

WASM_EXEC_TEST(I8x16RoundingAverageU) {
  RunI8x16BinOpTest<uint8_t>(execution_tier, kExprI8x16RoundingAverageU,
                             RoundingAverageUnsigned);
}

WASM_EXEC_TEST(I8x16Shl) {
  RunI8x16ShiftOpTest(execution_tier, kExprI8x16Shl, LogicalShiftLeft);
}

WASM_EXEC_TEST(I8x16ShrS) {
  RunI8x16ShiftOpTest(execution_tier, kExprI8x16ShrS, ArithmeticShiftRight);
}

WASM_EXEC_TEST(I8x16ShrU) {
  RunI8x16ShiftOpTest(execution_tier, kExprI8x16ShrU, LogicalShiftRight);
}

WASM_EXEC_TEST(I8x16ShiftAdd) {
  for (int imm = 0; imm <= 8; imm++) {
    RunShiftAddTestSequence<int8_t>(execution_tier, kExprI8x16ShrU,
                                    kExprI8x16Add, kExprI8x16Splat, imm,
                                    LogicalShiftRight);
    RunShiftAddTestSequence<int8_t>(execution_tier, kExprI8x16ShrS,
                                    kExprI8x16Add, kExprI8x16Splat, imm,
                                    ArithmeticShiftRight);
  }
}

// Test Select by making a mask where the 0th and 3rd lanes are true and the
// rest false, and comparing for non-equality with zero to convert to a boolean
// vector.
#define WASM_SIMD_SELECT_TEST(format)                                       \
  WASM_EXEC_TEST(S##format##Select) {                                       \
    WasmRunner<int32_t, int32_t, int32_t> r(execution_tier);                \
    uint8_t val1 = 0;                                                       \
    uint8_t val2 = 1;                                                       \
    uint8_t src1 = r.AllocateLocal(kWasmS128);                              \
    uint8_t src2 = r.AllocateLocal(kWasmS128);                              \
    uint8_t zero = r.AllocateLocal(kWasmS128);                              \
    uint8_t mask = r.AllocateLocal(kWasmS128);                              \
    r.Build(                                                                \
        {WASM_LOCAL_SET(src1,                                               \
                        WASM_SIMD_I##format##_SPLAT(WASM_LOCAL_GET(val1))), \
         WASM_LOCAL_SET(src2,                                               \
                        WASM_SIMD_I##format##_SPLAT(WASM_LOCAL_GET(val2))), \
         WASM_LOCAL_SET(zero, WASM_SIMD_I##format##_SPLAT(WASM_ZERO)),      \
         WASM_LOCAL_SET(mask, WASM_SIMD_I##format##_REPLACE_LANE(           \
                                  1, WASM_LOCAL_GET(zero), WASM_I32V(-1))), \
         WASM_LOCAL_SET(mask, WASM_SIMD_I##format##_REPLACE_LANE(           \
                                  2, WASM_LOCAL_GET(mask), WASM_I32V(-1))), \
         WASM_LOCAL_SET(                                                    \
             mask,                                                          \
             WASM_SIMD_SELECT(                                              \
                 format, WASM_LOCAL_GET(src1), WASM_LOCAL_GET(src2),        \
                 WASM_SIMD_BINOP(kExprI##format##Ne, WASM_LOCAL_GET(mask),  \
                                 WASM_LOCAL_GET(zero)))),                   \
         WASM_SIMD_CHECK_LANE_S(I##format, mask, I32, val2, 0),             \
         WASM_SIMD_CHECK_LANE_S(I##format, mask, I32, val1, 1),             \
         WASM_SIMD_CHECK_LANE_S(I##format, mask, I32, val1, 2),             \
         WASM_SIMD_CHECK_LANE_S(I##format, mask, I32, val2, 3), WASM_ONE}); \
                                                                            \
    CHECK_EQ(1, r.Call(0x12, 0x34));                                        \
  }

WASM_SIMD_SELECT_TEST(32x4)
WASM_SIMD_SELECT_TEST(16x8)
WASM_SIMD_SELECT_TEST(8x16)

// Test Select by making a mask where the 0th and 3rd lanes are non-zero and the
// rest 0. The mask is not the result of a comparison op.
#define WASM_SIMD_NON_CANONICAL_SELECT_TEST(format)                          \
  WASM_EXEC_TEST(S##format##NonCanonicalSelect) {                            \
    WasmRunner<int32_t, int32_t, int32_t, int32_t> r(execution_tier);        \
    uint8_t val1 = 0;                                                        \
    uint8_t val2 = 1;                                                        \
    uint8_t combined = 2;                                                    \
    uint8_t src1 = r.AllocateLocal(kWasmS128);                               \
    uint8_t src2 = r.AllocateLocal(kWasmS128);                               \
    uint8_t zero = r.AllocateLocal(kWasmS128);                               \
    uint8_t mask = r.AllocateLocal(kWasmS128);                               \
    r.Build(                                                                 \
        {WASM_LOCAL_SET(src1,                                                \
                        WASM_SIMD_I##format##_SPLAT(WASM_LOCAL_GET(val1))),  \
         WASM_LOCAL_SET(src2,                                                \
                        WASM_SIMD_I##format##_SPLAT(WASM_LOCAL_GET(val2))),  \
         WASM_LOCAL_SET(zero, WASM_SIMD_I##format##_SPLAT(WASM_ZERO)),       \
         WASM_LOCAL_SET(mask, WASM_SIMD_I##format##_REPLACE_LANE(            \
                                  1, WASM_LOCAL_GET(zero), WASM_I32V(0xF))), \
         WASM_LOCAL_SET(mask, WASM_SIMD_I##format##_REPLACE_LANE(            \
                                  2, WASM_LOCAL_GET(mask), WASM_I32V(0xF))), \
         WASM_LOCAL_SET(mask, WASM_SIMD_SELECT(format, WASM_LOCAL_GET(src1), \
                                               WASM_LOCAL_GET(src2),         \
                                               WASM_LOCAL_GET(mask))),       \
         WASM_SIMD_CHECK_LANE_S(I##format, mask, I32, val2, 0),              \
         WASM_SIMD_CHECK_LANE_S(I##format, mask, I32, combined, 1),          \
         WASM_SIMD_CHECK_LANE_S(I##format, mask, I32, combined, 2),          \
         WASM_SIMD_CHECK_LANE_S(I##format, mask, I32, val2, 3), WASM_ONE});  \
                                                                             \
    CHECK_EQ(1, r.Call(0x12, 0x34, 0x32));                                   \
  }

WASM_SIMD_NON_CANONICAL_SELECT_TEST(32x4)
WASM_SIMD_NON_CANONICAL_SELECT_TEST(16x8)
WASM_SIMD_NON_CANONICAL_SELECT_TEST(8x16)

// Test binary ops with two lane test patterns, all lanes distinct.
template <typename T>
void RunBinaryLaneOpTest(
    TestExecutionTier execution_tier, WasmOpcode simd_op,
    const std::array<T, kSimd128Size / sizeof(T)>& expected) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up two test patterns as globals, e.g. [0, 1, 2, 3] and [4, 5, 6, 7].
  T* src0 = r.builder().AddGlobal<T>(kWasmS128);
  T* src1 = r.builder().AddGlobal<T>(kWasmS128);
  static const int kElems = kSimd128Size / sizeof(T);
  for (int i = 0; i < kElems; i++) {
    LANE(src0, i) = i;
    LANE(src1, i) = kElems + i;
  }
  if (simd_op == kExprI8x16Shuffle) {
    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_I8x16_SHUFFLE_OP(simd_op, expected,
                                                           WASM_GLOBAL_GET(0),
                                                           WASM_GLOBAL_GET(1))),
             WASM_ONE});
  } else {
    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_BINOP(simd_op, WASM_GLOBAL_GET(0),
                                                WASM_GLOBAL_GET(1))),
             WASM_ONE});
  }

  CHECK_EQ(1, r.Call());
  for (size_t i = 0; i < expected.size(); i++) {
    CHECK_EQ(LANE(src0, i), expected[i]);
  }
}

// Test shuffle ops.
void RunShuffleOpTest(TestExecutionTier execution_tier, WasmOpcode simd_op,
                      const std::array<int8_t, kSimd128Size>& shuffle) {
  // Test the original shuffle.
  RunBinaryLaneOpTest<int8_t>(execution_tier, simd_op, shuffle);

  // Test a non-canonical (inputs reversed) version of the shuffle.
  std::array<int8_t, kSimd128Size> other_shuffle(shuffle);
  for (size_t i = 0; i < shuffle.size(); ++i) other_shuffle[i] ^= kSimd128Size;
  RunBinaryLaneOpTest<int8_t>(execution_tier, simd_op, other_shuffle);

  // Test the swizzle (one-operand) version of the shuffle.
  std::array<int8_t, kSimd128Size> swizzle(shuffle);
  for (size_t i = 0; i < shuffle.size(); ++i) swizzle[i] &= (kSimd128Size - 1);
  RunBinaryLaneOpTest<int8_t>(execution_tier, simd_op, swizzle);

  // Test the non-canonical swizzle (one-operand) version of the shuffle.
  std::array<int8_t, kSimd128Size> other_swizzle(shuffle);
  for (size_t i = 0; i < shuffle.size(); ++i) other_swizzle[i] |= kSimd128Size;
  RunBinaryLaneOpTest<int8_t>(execution_tier, simd_op, other_swizzle);
}

#define SHUFFLE_LIST(V)  \
  V(S128Identity)        \
  V(S64x2UnzipLeft)      \
  V(S64x2UnzipRight)     \
  V(S32x4Dup)            \
  V(S32x4ZipLeft)        \
  V(S32x4ZipRight)       \
  V(S32x4UnzipLeft)      \
  V(S32x4UnzipRight)     \
  V(S32x4TransposeLeft)  \
  V(S32x4TransposeRight) \
  V(S32x4OneLaneSwizzle) \
  V(S32x4Reverse)        \
  V(S32x2Reverse)        \
  V(S32x4Irregular)      \
  V(S32x4DupAndCopyOne)  \
  V(S32x4DupAndCopyTwo)  \
  V(S32x4Rotate)         \
  V(S16x8Dup)            \
  V(S16x8ZipLeft)        \
  V(S16x8ZipRight)       \
  V(S16x8UnzipLeft)      \
  V(S16x8UnzipRight)     \
  V(S16x8TransposeLeft)  \
  V(S16x8TransposeRight) \
  V(S16x4Reverse)        \
  V(S16x2Reverse)        \
  V(S16x8Irregular)      \
  V(S8x16Dup)            \
  V(S8x16ZipLeft)        \
  V(S8x16ZipRight)       \
  V(S8x16UnzipLeft)      \
  V(S8x16UnzipRight)     \
  V(S8x16TransposeLeft)  \
  V(S8x16TransposeRight) \
  V(S8x8Reverse)         \
  V(S8x4Reverse)         \
  V(S8x2Reverse)         \
  V(S8x16Irregular)

enum ShuffleKey {
#define SHUFFLE_ENUM_VALUE(Name) k##Name,
  SHUFFLE_LIST(SHUFFLE_ENUM_VALUE)
#undef SHUFFLE_ENUM_VALUE
      kNumShuffleKeys
};

using ShuffleMap = std::map<ShuffleKey, const Shuffle>;

ShuffleMap test_shuffles = {
    {kS128Identity,
     {{16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31}}},
    {kS64x2UnzipLeft,
     {{0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23}}},
    {kS64x2UnzipRight,
     {{8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31}}},
    {kS32x4Dup,
     {{16, 17, 18, 19, 16, 17, 18, 19, 16, 17, 18, 19, 16, 17, 18, 19}}},
    {kS32x4ZipLeft, {{0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23}}},
    {kS32x4ZipRight,
     {{8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31}}},
    {kS32x4UnzipLeft,
     {{0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27}}},
    {kS32x4UnzipRight,
     {{4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31}}},
    {kS32x4TransposeLeft,
     {{0, 1, 2, 3, 16, 17, 18, 19, 8, 9, 10, 11, 24, 25, 26, 27}}},
    {kS32x4TransposeRight,
     {{4, 5, 6, 7, 20, 21, 22, 23, 12, 13, 14, 15, 28, 29, 30, 31}}},
    {kS32x4OneLaneSwizzle,  // swizzle only
     {{15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 7, 6, 5, 4}}},
    {kS32x4Reverse,  // swizzle only
     {{3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12}}},
    {kS32x2Reverse,  // swizzle only
     {{4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11}}},
    {kS32x4Irregular,
     {{0, 1, 2, 3, 16, 17, 18, 19, 16, 17, 18, 19, 20, 21, 22, 23}}},
    {kS32x4DupAndCopyOne,  // swizzle only
     {{0, 1, 2, 3, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11}}},
    {kS32x4DupAndCopyTwo,
     {{16, 17, 18, 19, 16, 17, 18, 19, 16, 17, 18, 19, 0, 1, 2, 3}}},
    {kS32x4Rotate, {{4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3}}},
    {kS16x8Dup,
     {{18, 19, 18, 19, 18, 19, 18, 19, 18, 19, 18, 19, 18, 19, 18, 19}}},
    {kS16x8ZipLeft, {{0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21, 6, 7, 22, 23}}},
    {kS16x8ZipRight,
     {{8, 9, 24, 25, 10, 11, 26, 27, 12, 13, 28, 29, 14, 15, 30, 31}}},
    {kS16x8UnzipLeft,
     {{0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29}}},
    {kS16x8UnzipRight,
     {{2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31}}},
    {kS16x8TransposeLeft,
     {{0, 1, 16, 17, 4, 5, 20, 21, 8, 9, 24, 25, 12, 13, 28, 29}}},
    {kS16x8TransposeRight,
     {{2, 3, 18, 19, 6, 7, 22, 23, 10, 11, 26, 27, 14, 15, 30, 31}}},
    {kS16x4Reverse,  // swizzle only
     {{6, 7, 4, 5, 2, 3, 0, 1, 14, 15, 12, 13, 10, 11, 8, 9}}},
    {kS16x2Reverse,  // swizzle only
     {{2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13}}},
    {kS16x8Irregular,
     {{0, 1, 16, 17, 16, 17, 0, 1, 4, 5, 20, 21, 6, 7, 22, 23}}},
    {kS8x16Dup,
     {{19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19}}},
    {kS8x16ZipLeft, {{0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23}}},
    {kS8x16ZipRight,
     {{8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31}}},
    {kS8x16UnzipLeft,
     {{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30}}},
    {kS8x16UnzipRight,
     {{1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31}}},
    {kS8x16TransposeLeft,
     {{0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30}}},
    {kS8x16TransposeRight,
     {{1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31}}},
    {kS8x8Reverse,  // swizzle only
     {{7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8}}},
    {kS8x4Reverse,  // swizzle only
     {{3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12}}},
    {kS8x2Reverse,  // swizzle only
     {{1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14}}},
    {kS8x16Irregular,
     {{0, 16, 0, 16, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23}}},
};

#define SHUFFLE_TEST(Name)                                           \
  WASM_EXEC_TEST(Name) {                                             \
    ShuffleMap::const_iterator it = test_shuffles.find(k##Name);     \
    DCHECK_NE(it, test_shuffles.end());                              \
    RunShuffleOpTest(execution_tier, kExprI8x16Shuffle, it->second); \
  }
SHUFFLE_LIST(SHUFFLE_TEST)
#undef SHUFFLE_TEST
#undef SHUFFLE_LIST

// Test shuffles that blend the two vectors (elements remain in their lanes.)
WASM_EXEC_TEST(S8x16Blend) {
  std::array<int8_t, kSimd128Size> expected;
  for (int bias = 1; bias < kSimd128Size; bias++) {
    for (int i = 0; i < bias; i++) expected[i] = i;
    for (int i = bias; i < kSimd128Size; i++) expected[i] = i + kSimd128Size;
    RunShuffleOpTest(execution_tier, kExprI8x16Shuffle, expected);
  }
}

// Test shuffles that concatenate the two vectors.
WASM_EXEC_TEST(S8x16Concat) {
  std::array<int8_t, kSimd128Size> expected;
  // n is offset or bias of concatenation.
  for (int n = 1; n < kSimd128Size; ++n) {
    int i = 0;
    // last kLanes - n bytes of first vector.
    for (int j = n; j < kSimd128Size; ++j) {
      expected[i++] = j;
    }
    // first n bytes of second vector
    for (int j = 0; j < n; ++j) {
      expected[i++] = j + kSimd128Size;
    }
    RunShuffleOpTest(execution_tier, kExprI8x16Shuffle, expected);
  }
}

WASM_EXEC_TEST(ShuffleShufps) {
  // We reverse engineer the shufps immediates into 8x16 shuffles.
  std::array<int8_t, kSimd128Size> expected;
  for (int mask = 0; mask < 256; mask++) {
    // Each iteration of this loop sets byte[i] of the 32x4 lanes.
    // Low 2 lanes (2-bits each) select from first input.
    uint8_t index0 = (mask & 3) * 4;
    uint8_t index1 = ((mask >> 2) & 3) * 4;
    // Next 2 bits select from src2, so add 16 to the index.
    uint8_t index2 = ((mask >> 4) & 3) * 4 + 16;
    uint8_t index3 = ((mask >> 6) & 3) * 4 + 16;

    for (int i = 0; i < 4; i++) {
      expected[0 + i] = index0 + i;
      expected[4 + i] = index1 + i;
      expected[8 + i] = index2 + i;
      expected[12 + i] = index3 + i;
    }
    RunShuffleOpTest(execution_tier, kExprI8x16Shuffle, expected);
  }
}

WASM_EXEC_TEST(I8x16ShuffleWithZeroInput) {
  WasmRunner<int32_t> r(execution_tier);
  static const int kElems = kSimd128Size / sizeof(uint8_t);
  uint8_t* dst = r.builder().AddGlobal<uint8_t>(kWasmS128);
  uint8_t* src1 = r.builder().AddGlobal<uint8_t>(kWasmS128);

  // src0 is zero, it's used to zero extend src1
  for (int i = 0; i < kElems; i++) {
    LANE(src1, i) = i;
  }

  // Zero extend first 4 elments of src1 to 32 bit
  constexpr std::array<int8_t, 16> shuffle = {16, 1, 2,  3,  17, 5,  6,  7,
                                              18, 9, 10, 11, 19, 13, 14, 15};
  constexpr std::array<int8_t, 16> expected = {0, 0, 0, 0, 1, 0, 0, 0,
                                               2, 0, 0, 0, 3, 0, 0, 0};
  constexpr std::array<int8_t, 16> zeros = {0};

  r.Build(
      {WASM_GLOBAL_SET(0, WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                                     WASM_SIMD_CONSTANT(zeros),
                                                     WASM_GLOBAL_GET(1))),
       WASM_ONE});
  CHECK_EQ(1, r.Call());
  for (int i = 0; i < kElems; i++) {
    CHECK_EQ(LANE(dst, i), expected[i]);
  }
}

struct SwizzleTestArgs {
  const Shuffle input;
  const Shuffle indices;
  const Shuffle expected;
};

static constexpr SwizzleTestArgs swizzle_test_args[] = {
    {{15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
     {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
     {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}},
    {{15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
     {15, 0, 14, 1, 13, 2, 12, 3, 11, 4, 10, 5, 9, 6, 8, 7},
     {0, 15, 1, 14, 2, 13, 3, 12, 4, 11, 5, 10, 6, 9, 7, 8}},
    {{15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
     {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30},
     {15, 13, 11, 9, 7, 5, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0}},
    // all indices are out of range
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
     {16, 17, 18, 19, 20, 124, 125, 126, 127, -1, -2, -3, -4, -5, -6, -7},
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}};

static constexpr base::Vector<const SwizzleTestArgs> swizzle_test_vector =
    base::ArrayVector(swizzle_test_args);

WASM_EXEC_TEST(I8x16Swizzle) {
  // RunBinaryLaneOpTest set up the two globals to be consecutive integers,
  // [0-15] and [16-31]. Using [0-15] as the indices will not sufficiently test
  // swizzle since the expected result is a no-op, using [16-31] will result in
  // all 0s.
  {
    WasmRunner<int32_t> r(execution_tier);
    static const int kElems = kSimd128Size / sizeof(uint8_t);
    uint8_t* dst = r.builder().AddGlobal<uint8_t>(kWasmS128);
    uint8_t* src0 = r.builder().AddGlobal<uint8_t>(kWasmS128);
    uint8_t* src1 = r.builder().AddGlobal<uint8_t>(kWasmS128);
    r.Build({WASM_GLOBAL_SET(
                 0, WASM_SIMD_BINOP(kExprI8x16Swizzle, WASM_GLOBAL_GET(1),
                                    WASM_GLOBAL_GET(2))),
             WASM_ONE});

    for (SwizzleTestArgs si : swizzle_test_vector) {
      for (int i = 0; i < kElems; i++) {
        LANE(src0, i) = si.input[i];
        LANE(src1, i) = si.indices[i];
      }

      CHECK_EQ(1, r.Call());

      for (int i = 0; i < kElems; i++) {
        CHECK_EQ(LANE(dst, i), si.expected[i]);
      }
    }
  }

  {
    // We have an optimization for constant indices, test this case.
    for (SwizzleTestArgs si : swizzle_test_vector) {
      WasmRunner<int32_t> r(execution_tier);
      uint8_t* dst = r.builder().AddGlobal<uint8_t>(kWasmS128);
      uint8_t* src0 = r.builder().AddGlobal<uint8_t>(kWasmS128);
      r.Build({WASM_GLOBAL_SET(
                   0, WASM_SIMD_BINOP(kExprI8x16Swizzle, WASM_GLOBAL_GET(1),
                                      WASM_SIMD_CONSTANT(si.indices))),
               WASM_ONE});

      for (int i = 0; i < kSimd128Size; i++) {
        LANE(src0, i) = si.input[i];
      }

      CHECK_EQ(1, r.Call());

      for (int i = 0; i < kSimd128Size; i++) {
        CHECK_EQ(LANE(dst, i), si.expected[i]);
      }
    }
  }
}

// Combine 3 shuffles a, b, and c by applying both a and b and then applying c
// to those two results.
Shuffle Combine(const Shuffle& a, const Shuffle& b, const Shuffle& c) {
  Shuffle result;
  for (int i = 0; i < kSimd128Size; ++i) {
    result[i] = c[i] < kSimd128Size ? a[c[i]] : b[c[i] - kSimd128Size];
  }
  return result;
}

const Shuffle& GetRandomTestShuffle(v8::base::RandomNumberGenerator* rng) {
  return test_shuffles[static_cast<ShuffleKey>(rng->NextInt(kNumShuffleKeys))];
}

// Test shuffles that are random combinations of 3 test shuffles. Completely
// random shuffles almost always generate the slow general shuffle code, so
// don't exercise as many code paths.
WASM_EXEC_TEST(I8x16ShuffleFuzz) {
  v8::base::RandomNumberGenerator* rng = CcTest::random_number_generator();
  static const int kTests = 100;
  for (int i = 0; i < kTests; ++i) {
    auto shuffle = Combine(GetRandomTestShuffle(rng), GetRandomTestShuffle(rng),
                           GetRandomTestShuffle(rng));
    RunShuffleOpTest(execution_tier, kExprI8x16Shuffle, shuffle);
  }
}

void AppendShuffle(const Shuffle& shuffle, std::vector<uint8_t>* buffer) {
  uint8_t opcode[] = {WASM_SIMD_OP(kExprI8x16Shuffle)};
  for (size_t i = 0; i < arraysize(opcode); ++i) buffer->push_back(opcode[i]);
  for (size_t i = 0; i < kSimd128Size; ++i) buffer->push_back((shuffle[i]));
}

void BuildShuffle(const std::vector<Shuffle>& shuffles,
                  std::vector<uint8_t>* buffer) {
  // Perform the leaf shuffles on globals 0 and 1.
  size_t row_index = (shuffles.size() - 1) / 2;
  for (size_t i = row_index; i < shuffles.size(); ++i) {
    uint8_t operands[] = {WASM_GLOBAL_GET(0), WASM_GLOBAL_GET(1)};
    for (size_t j = 0; j < arraysize(operands); ++j)
      buffer->push_back(operands[j]);
    AppendShuffle(shuffles[i], buffer);
  }
  // Now perform inner shuffles in the correct order on operands on the stack.
  do {
    for (size_t i = row_index / 2; i < row_index; ++i) {
      AppendShuffle(shuffles[i], buffer);
    }
    row_index /= 2;
  } while (row_index != 0);
  uint8_t epilog[] = {kExprGlobalSet, static_cast<uint8_t>(0), WASM_ONE};
  for (size_t j = 0; j < arraysize(epilog); ++j) buffer->push_back(epilog[j]);
}

void RunWasmCode(TestExecutionTier execution_tier,
                 const std::vector<uint8_t>& code,
                 std::array<int8_t, kSimd128Size>* result) {
  WasmRunner<int32_t> r(execution_tier);
  // Set up two test patterns as globals, e.g. [0, 1, 2, 3] and [4, 5, 6, 7].
  int8_t* src0 = r.builder().AddGlobal<int8_t>(kWasmS128);
  int8_t* src1 = r.builder().AddGlobal<int8_t>(kWasmS128);
  for (int i = 0; i < kSimd128Size; ++i) {
    LANE(src0, i) = i;
    LANE(src1, i) = kSimd128Size + i;
  }
  r.Build(code.data(), code.data() + code.size());
  CHECK_EQ(1, r.Call());
  for (size_t i = 0; i < kSimd128Size; i++) {
    (*result)[i] = LANE(src0, i);
  }
}

// Boolean unary operations are 'AllTrue' and 'AnyTrue', which return an integer
// result. Use relational ops on numeric vectors to create the boolean vector
// test inputs. Test inputs with all true, all false, one true, and one false.
#define WASM_SIMD_BOOL_REDUCTION_TEST(format, lanes, int_type)                \
  WASM_EXEC_TEST(ReductionTest##lanes) {                                      \
    WasmRunner<int32_t> r(execution_tier);                                    \
    if (lanes == 2) return;                                                   \
    uint8_t zero = r.AllocateLocal(kWasmS128);                                \
    uint8_t one_one = r.AllocateLocal(kWasmS128);                             \
    uint8_t reduced = r.AllocateLocal(kWasmI32);                              \
    r.Build(                                                                  \
        {WASM_LOCAL_SET(zero, WASM_SIMD_I##format##_SPLAT(int_type(0))),      \
         WASM_LOCAL_SET(                                                      \
             reduced, WASM_SIMD_UNOP(kExprV128AnyTrue,                        \
                                     WASM_SIMD_BINOP(kExprI##format##Eq,      \
                                                     WASM_LOCAL_GET(zero),    \
                                                     WASM_LOCAL_GET(zero)))), \
         WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(reduced), WASM_ZERO),             \
                 WASM_RETURN(WASM_ZERO)),                                     \
         WASM_LOCAL_SET(                                                      \
             reduced, WASM_SIMD_UNOP(kExprV128AnyTrue,                        \
                                     WASM_SIMD_BINOP(kExprI##format##Ne,      \
                                                     WASM_LOCAL_GET(zero),    \
                                                     WASM_LOCAL_GET(zero)))), \
         WASM_IF(WASM_I32_NE(WASM_LOCAL_GET(reduced), WASM_ZERO),             \
                 WASM_RETURN(WASM_ZERO)),                                     \
         WASM_LOCAL_SET(                                                      \
             reduced, WASM_SIMD_UNOP(kExprI##format##AllTrue,                 \
                                     WASM_SIMD_BINOP(kExprI##format##Eq,      \
                                                     WASM_LOCAL_GET(zero),    \
                                                     WASM_LOCAL_GET(zero)))), \
         WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(reduced), WASM_ZERO),             \
                 WASM_RETURN(WASM_ZERO)),                                     \
         WASM_LOCAL_SET(                                                      \
             reduced, WASM_SIMD_UNOP(kExprI##format##AllTrue,                 \
                                     WASM_SIMD_BINOP(kExprI##format##Ne,      \
                                                     WASM_LOCAL_GET(zero),    \
                                                     WASM_LOCAL_GET(zero)))), \
         WASM_IF(WASM_I32_NE(WASM_LOCAL_GET(reduced), WASM_ZERO),             \
                 WASM_RETURN(WASM_ZERO)),                                     \
         WASM_LOCAL_SET(one_one,                                              \
                        WASM_SIMD_I##format##_REPLACE_LANE(                   \
                            lanes - 1, WASM_LOCAL_GET(zero), int_type(1))),   \
         WASM_LOCAL_SET(                                                      \
             reduced, WASM_SIMD_UNOP(kExprV128AnyTrue,                        \
                                     WASM_SIMD_BINOP(kExprI##format##Eq,      \
                                                     WASM_LOCAL_GET(one_one), \
                                                     WASM_LOCAL_GET(zero)))), \
         WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(reduced), WASM_ZERO),             \
                 WASM_RETURN(WASM_ZERO)),                                     \
         WASM_LOCAL_SET(                                                      \
             reduced, WASM_SIMD_UNOP(kExprV128AnyTrue,                        \
                                     WASM_SIMD_BINOP(kExprI##format##Ne,      \
                                                     WASM_LOCAL_GET(one_one), \
                                                     WASM_LOCAL_GET(zero)))), \
         WASM_IF(WASM_I32_EQ(WASM_LOCAL_GET(reduced), WASM_ZERO),             \
                 WASM_RETURN(WASM_ZERO)),                                     \
         WASM_LOCAL_SET(                                                      \
             reduced, WASM_SIMD_UNOP(kExprI##format##AllTrue,                 \
                                     WASM_SIMD_BINOP(kExprI##format##Eq,      \
                                                     WASM_LOCAL_GET(one_one), \
                                                     WASM_LOCAL_GET(zero)))), \
         WASM_IF(WASM_I32_NE(WASM_LOCAL_GET(reduced), WASM_ZERO),             \
                 WASM_RETURN(WASM_ZERO)),                                     \
         WASM_LOCAL_SET(                                                      \
             reduced, WASM_SIMD_UNOP(kExprI##format##AllTrue,                 \
                                     WASM_SIMD_BINOP(kExprI##format##Ne,      \
                                                     WASM_LOCAL_GET(one_one), \
                                                     WASM_LOCAL_GET(zero)))), \
         WASM_IF(WASM_I32_NE(WASM_LOCAL_GET(reduced), WASM_ZERO),             \
                 WASM_RETURN(WASM_ZERO)),                                     \
         WASM_ONE});                                                          \
    CHECK_EQ(1, r.Call());                                                    \
  }

WASM_SIMD_BOOL_REDUCTION_TEST(64x2, 2, WASM_I64V)
WASM_SIMD_BOOL_REDUCTION_TEST(32x4, 4, WASM_I32V)
WASM_SIMD_BOOL_REDUCTION_TEST(16x8, 8, WASM_I32V)
WASM_SIMD_BOOL_REDUCTION_TEST(8x16, 16, WASM_I32V)

WASM_EXEC_TEST(SimdI32x4ExtractWithF32x4) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build(
      {WASM_IF_ELSE_I(WASM_I32_EQ(WASM_SIMD_I32x4_EXTRACT_LANE(
                                      0, WASM_SIMD_F32x4_SPLAT(WASM_F32(30.5))),
                                  WASM_I32_REINTERPRET_F32(WASM_F32(30.5))),
                      WASM_I32V(1), WASM_I32V(0))});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(SimdF32x4ExtractWithI32x4) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build(
      {WASM_IF_ELSE_I(WASM_F32_EQ(WASM_SIMD_F32x4_EXTRACT_LANE(
                                      0, WASM_SIMD_I32x4_SPLAT(WASM_I32V(15))),
                                  WASM_F32_REINTERPRET_I32(WASM_I32V(15))),
                      WASM_I32V(1), WASM_I32V(0))});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(SimdF32x4ExtractLane) {
  WasmRunner<float> r(execution_tier);
  r.AllocateLocal(kWasmF32);
  r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(0, WASM_SIMD_F32x4_EXTRACT_LANE(
                                 0, WASM_SIMD_F32x4_SPLAT(WASM_F32(30.5)))),
           WASM_LOCAL_SET(1, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(0))),
           WASM_SIMD_F32x4_EXTRACT_LANE(1, WASM_LOCAL_GET(1))});
  CHECK_EQ(30.5, r.Call());
}

WASM_EXEC_TEST(SimdF32x4AddWithI32x4) {
  // Choose two floating point values whose sum is normal and exactly
  // representable as a float.
  const int kOne = 0x3F800000;
  const int kTwo = 0x40000000;
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_IF_ELSE_I(
      WASM_F32_EQ(
          WASM_SIMD_F32x4_EXTRACT_LANE(
              0, WASM_SIMD_BINOP(kExprF32x4Add,
                                 WASM_SIMD_I32x4_SPLAT(WASM_I32V(kOne)),
                                 WASM_SIMD_I32x4_SPLAT(WASM_I32V(kTwo)))),
          WASM_F32_ADD(WASM_F32_REINTERPRET_I32(WASM_I32V(kOne)),
                       WASM_F32_REINTERPRET_I32(WASM_I32V(kTwo)))),
      WASM_I32V(1), WASM_I32V(0))});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(SimdI32x4AddWithF32x4) {
  WasmRunner<int32_t> r(execution_tier);
  r.Build({WASM_IF_ELSE_I(
      WASM_I32_EQ(
          WASM_SIMD_I32x4_EXTRACT_LANE(
              0, WASM_SIMD_BINOP(kExprI32x4Add,
                                 WASM_SIMD_F32x4_SPLAT(WASM_F32(21.25)),
                                 WASM_SIMD_F32x4_SPLAT(WASM_F32(31.5)))),
          WASM_I32_ADD(WASM_I32_REINTERPRET_F32(WASM_F32(21.25)),
                       WASM_I32_REINTERPRET_F32(WASM_F32(31.5)))),
      WASM_I32V(1), WASM_I32V(0))});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(SimdI32x4Local) {
  WasmRunner<int32_t> r(execution_tier);
  r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(0, WASM_SIMD_I32x4_SPLAT(WASM_I32V(31))),
           WASM_SIMD_I32x4_EXTRACT_LANE(0, WASM_LOCAL_GET(0))});
  CHECK_EQ(31, r.Call());
}

WASM_EXEC_TEST(SimdI32x4SplatFromExtract) {
  WasmRunner<int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(0, WASM_SIMD_I32x4_EXTRACT_LANE(
                                 0, WASM_SIMD_I32x4_SPLAT(WASM_I32V(76)))),
           WASM_LOCAL_SET(1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(0))),
           WASM_SIMD_I32x4_EXTRACT_LANE(1, WASM_LOCAL_GET(1))});
  CHECK_EQ(76, r.Call());
}

WASM_EXEC_TEST(SimdI32x4For) {
  WasmRunner<int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmS128);
  r.Build(
      {WASM_LOCAL_SET(1, WASM_SIMD_I32x4_SPLAT(WASM_I32V(31))),
       WASM_LOCAL_SET(1, WASM_SIMD_I32x4_REPLACE_LANE(1, WASM_LOCAL_GET(1),
                                                      WASM_I32V(53))),
       WASM_LOCAL_SET(1, WASM_SIMD_I32x4_REPLACE_LANE(2, WASM_LOCAL_GET(1),
                                                      WASM_I32V(23))),
       WASM_LOCAL_SET(0, WASM_I32V(0)),
       WASM_LOOP(
           WASM_LOCAL_SET(1,
                          WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(1),
                                          WASM_SIMD_I32x4_SPLAT(WASM_I32V(1)))),
           WASM_IF(WASM_I32_NE(WASM_INC_LOCAL(0), WASM_I32V(5)), WASM_BR(1))),
       WASM_LOCAL_SET(0, WASM_I32V(1)),
       WASM_IF(WASM_I32_NE(WASM_SIMD_I32x4_EXTRACT_LANE(0, WASM_LOCAL_GET(1)),
                           WASM_I32V(36)),
               WASM_LOCAL_SET(0, WASM_I32V(0))),
       WASM_IF(WASM_I32_NE(WASM_SIMD_I32x4_EXTRACT_LANE(1, WASM_LOCAL_GET(1)),
                           WASM_I32V(58)),
               WASM_LOCAL_SET(0, WASM_I32V(0))),
       WASM_IF(WASM_I32_NE(WASM_SIMD_I32x4_EXTRACT_LANE(2, WASM_LOCAL_GET(1)),
                           WASM_I32V(28)),
               WASM_LOCAL_SET(0, WASM_I32V(0))),
       WASM_IF(WASM_I32_NE(WASM_SIMD_I32x4_EXTRACT_LANE(3, WASM_LOCAL_GET(1)),
                           WASM_I32V(36)),
               WASM_LOCAL_SET(0, WASM_I32V(0))),
       WASM_LOCAL_GET(0)});
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_TEST(SimdF32x4For) {
  WasmRunner<int32_t> r(execution_tier);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmS128);
  r.Build(
      {WASM_LOCAL_SET(1, WASM_SIMD_F32x4_SPLAT(WASM_F32(21.25))),
       WASM_LOCAL_SET(1, WASM_SIMD_F32x4_REPLACE_LANE(3, WASM_LOCAL_GET(1),
                                                      WASM_F32(19.5))),
       WASM_LOCAL_SET(0, WASM_I32V(0)),
       WASM_LOOP(
           WASM_LOCAL_SET(
               1, WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(1),
                                  WASM_SIMD_F32x4_SPLAT(WASM_F32(2.0)))),
           WASM_IF(WASM_I32_NE(WASM_INC_LOCAL(0), WASM_I32V(3)), WASM_BR(1))),
       WASM_LOCAL_SET(0, WASM_I32V(1)),
       WASM_IF(WASM_F32_NE(WASM_SIMD_F32x4_EXTRACT_LANE(0, WASM_LOCAL_GET(1)),
                           WASM_F32(27.25)),
               WASM_LOCAL_SET(0, WASM_I32V(0))),
       WASM_IF(WASM_F32_NE(WASM_SIMD_F32x4_EXTRACT_LANE(3, WASM_LOCAL_GET(1)),
                           WASM_F32(25.5)),
               WASM_LOCAL_SET(0, WASM_I32V(0))),
       WASM_LOCAL_GET(0)});
  CHECK_EQ(1, r.Call());
}

template <typename T, int numLanes = 4>
void SetVectorByLanes(T* v, const std::array<T, numLanes>& arr) {
  for (int lane = 0; lane < numLanes; lane++) {
    LANE(v, lane) = arr[lane];
  }
}

template <typename T>
const T GetScalar(T* v, int lane) {
  DCHECK_GE(lane, 0);
  DCHECK_LT(static_cast<uint32_t>(lane), kSimd128Size / sizeof(T));
  return LANE(v, lane);
}

WASM_EXEC_TEST(SimdI32x4GetGlobal) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Pad the globals with a few unused slots to get a non-zero offset.
  r.builder().AddGlobal<int32_t>(kWasmI32);  // purposefully unused
  r.builder().AddGlobal<int32_t>(kWasmI32);  // purposefully unused
  r.builder().AddGlobal<int32_t>(kWasmI32);  // purposefully unused
  r.builder().AddGlobal<int32_t>(kWasmI32);  // purposefully unused
  int32_t* global = r.builder().AddGlobal<int32_t>(kWasmS128);
  SetVectorByLanes(global, {{0, 1, 2, 3}});
  r.AllocateLocal(kWasmI32);
  r.Build(
      {WASM_LOCAL_SET(1, WASM_I32V(1)),
       WASM_IF(WASM_I32_NE(WASM_I32V(0),
                           WASM_SIMD_I32x4_EXTRACT_LANE(0, WASM_GLOBAL_GET(4))),
               WASM_LOCAL_SET(1, WASM_I32V(0))),
       WASM_IF(WASM_I32_NE(WASM_I32V(1),
                           WASM_SIMD_I32x4_EXTRACT_LANE(1, WASM_GLOBAL_GET(4))),
               WASM_LOCAL_SET(1, WASM_I32V(0))),
       WASM_IF(WASM_I32_NE(WASM_I32V(2),
                           WASM_SIMD_I32x4_EXTRACT_LANE(2, WASM_GLOBAL_GET(4))),
               WASM_LOCAL_SET(1, WASM_I32V(0))),
       WASM_IF(WASM_I32_NE(WASM_I32V(3),
                           WASM_SIMD_I32x4_EXTRACT_LANE(3, WASM_GLOBAL_GET(4))),
               WASM_LOCAL_SET(1, WASM_I32V(0))),
       WASM_LOCAL_GET(1)});
  CHECK_EQ(1, r.Call(0));
}

WASM_EXEC_TEST(SimdI32x4SetGlobal) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  // Pad the globals with a few unused slots to get a non-zero offset.
  r.builder().AddGlobal<int32_t>(kWasmI32);  // purposefully unused
  r.builder().AddGlobal<int32_t>(kWasmI32);  // purposefully unused
  r.builder().AddGlobal<int32_t>(kWasmI32);  // purposefully unused
  r.builder().AddGlobal<int32_t>(kWasmI32);  // purposefully unused
  int32_t* global = r.builder().AddGlobal<int32_t>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(4, WASM_SIMD_I32x4_SPLAT(WASM_I32V(23))),
           WASM_GLOBAL_SET(4, WASM_SIMD_I32x4_REPLACE_LANE(
                                  1, WASM_GLOBAL_GET(4), WASM_I32V(34))),
           WASM_GLOBAL_SET(4, WASM_SIMD_I32x4_REPLACE_LANE(
                                  2, WASM_GLOBAL_GET(4), WASM_I32V(45))),
           WASM_GLOBAL_SET(4, WASM_SIMD_I32x4_REPLACE_LANE(
                                  3, WASM_GLOBAL_GET(4), WASM_I32V(56))),
           WASM_I32V(1)});
  CHECK_EQ(1, r.Call(0));
  CHECK_EQ(GetScalar(global, 0), 23);
  CHECK_EQ(GetScalar(global, 1), 34);
  CHECK_EQ(GetScalar(global, 2), 45);
  CHECK_EQ(GetScalar(global, 3), 56);
}

WASM_EXEC_TEST(SimdF32x4GetGlobal) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  float* global = r.builder().AddGlobal<float>(kWasmS128);
  SetVectorByLanes<float>(global, {{0.0, 1.5, 2.25, 3.5}});
  r.AllocateLocal(kWasmI32);
  r.Build(
      {WASM_LOCAL_SET(1, WASM_I32V(1)),
       WASM_IF(WASM_F32_NE(WASM_F32(0.0),
                           WASM_SIMD_F32x4_EXTRACT_LANE(0, WASM_GLOBAL_GET(0))),
               WASM_LOCAL_SET(1, WASM_I32V(0))),
       WASM_IF(WASM_F32_NE(WASM_F32(1.5),
                           WASM_SIMD_F32x4_EXTRACT_LANE(1, WASM_GLOBAL_GET(0))),
               WASM_LOCAL_SET(1, WASM_I32V(0))),
       WASM_IF(WASM_F32_NE(WASM_F32(2.25),
                           WASM_SIMD_F32x4_EXTRACT_LANE(2, WASM_GLOBAL_GET(0))),
               WASM_LOCAL_SET(1, WASM_I32V(0))),
       WASM_IF(WASM_F32_NE(WASM_F32(3.5),
                           WASM_SIMD_F32x4_EXTRACT_LANE(3, WASM_GLOBAL_GET(0))),
               WASM_LOCAL_SET(1, WASM_I32V(0))),
       WASM_LOCAL_GET(1)});
  CHECK_EQ(1, r.Call(0));
}

WASM_EXEC_TEST(SimdF32x4SetGlobal) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  float* global = r.builder().AddGlobal<float>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_SPLAT(WASM_F32(13.5))),
           WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_REPLACE_LANE(
                                  1, WASM_GLOBAL_GET(0), WASM_F32(45.5))),
           WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_REPLACE_LANE(
                                  2, WASM_GLOBAL_GET(0), WASM_F32(32.25))),
           WASM_GLOBAL_SET(0, WASM_SIMD_F32x4_REPLACE_LANE(
                                  3, WASM_GLOBAL_GET(0), WASM_F32(65.0))),
           WASM_I32V(1)});
  CHECK_EQ(1, r.Call(0));
  CHECK_EQ(GetScalar(global, 0), 13.5f);
  CHECK_EQ(GetScalar(global, 1), 45.5f);
  CHECK_EQ(GetScalar(global, 2), 32.25f);
  CHECK_EQ(GetScalar(global, 3), 65.0f);
}

WASM_EXEC_TEST(SimdLoadStoreLoad) {
  {
    WasmRunner<int32_t> r(execution_tier);
    int32_t* memory =
        r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
    // Load memory, store it, then reload it and extract the first lane. Use a
    // non-zero offset into the memory of 1 lane (4 bytes) to test indexing.
    r.Build(
        {WASM_SIMD_STORE_MEM(WASM_I32V(8), WASM_SIMD_LOAD_MEM(WASM_I32V(4))),
         WASM_SIMD_I32x4_EXTRACT_LANE(0, WASM_SIMD_LOAD_MEM(WASM_I32V(8)))});

    FOR_INT32_INPUTS(i) {
      int32_t expected = i;
      r.builder().WriteMemory(&memory[1], expected);
      CHECK_EQ(expected, r.Call());
    }
  }

  {
    // OOB tests for loads.
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
    r.Build({WASM_SIMD_I32x4_EXTRACT_LANE(
        0, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(0)))});

    for (uint32_t offset = kWasmPageSize - (kSimd128Size - 1);
         offset < kWasmPageSize; ++offset) {
      CHECK_TRAP(r.Call(offset));
    }
  }

  {
    // OOB tests for stores.
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
    r.Build(
        {WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(0), WASM_SIMD_LOAD_MEM(WASM_ZERO)),
         WASM_ONE});

    for (uint32_t offset = kWasmPageSize - (kSimd128Size - 1);
         offset < kWasmPageSize; ++offset) {
      CHECK_TRAP(r.Call(offset));
    }
  }
}

WASM_EXEC_TEST(SimdLoadStoreLoadMemargOffset) {
  {
    WasmRunner<int32_t> r(execution_tier);
    int32_t* memory =
        r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
    constexpr uint8_t offset_1 = 4;
    constexpr uint8_t offset_2 = 8;
    // Load from memory at offset_1, store to offset_2, load from offset_2, and
    // extract first lane. We use non-zero memarg offsets to test offset
    // decoding.
    r.Build({WASM_SIMD_STORE_MEM_OFFSET(
                 offset_2, WASM_ZERO,
                 WASM_SIMD_LOAD_MEM_OFFSET(offset_1, WASM_ZERO)),
             WASM_SIMD_I32x4_EXTRACT_LANE(
                 0, WASM_SIMD_LOAD_MEM_OFFSET(offset_2, WASM_ZERO))});

    FOR_INT32_INPUTS(i) {
      int32_t expected = i;
      // Index 1 of memory (int32_t) will be bytes 4 to 8.
      r.builder().WriteMemory(&memory[1], expected);
      CHECK_EQ(expected, r.Call());
    }
  }

  {
    // OOB tests for loads with offsets.
    for (uint32_t offset = kWasmPageSize - (kSimd128Size - 1);
         offset < kWasmPageSize; ++offset) {
      WasmRunner<int32_t> r(execution_tier);
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
      r.Build({WASM_SIMD_I32x4_EXTRACT_LANE(
          0, WASM_SIMD_LOAD_MEM_OFFSET(U32V_3(offset), WASM_ZERO))});
      CHECK_TRAP(r.Call());
    }
  }

  {
    // OOB tests for stores with offsets
    for (uint32_t offset = kWasmPageSize - (kSimd128Size - 1);
         offset < kWasmPageSize; ++offset) {
      WasmRunner<int32_t, uint32_t> r(execution_tier);
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
      r.Build({WASM_SIMD_STORE_MEM_OFFSET(U32V_3(offset), WASM_ZERO,
                                          WASM_SIMD_LOAD_MEM(WASM_ZERO)),
               WASM_ONE});
      CHECK_TRAP(r.Call(offset));
    }
  }
}

// Test a multi-byte opcode with offset values that encode into valid opcodes.
// This is to exercise decoding logic and make sure we get the lengths right.
WASM_EXEC_TEST(S128Load8SplatOffset) {
  // This offset is [82, 22] when encoded, which contains valid opcodes.
  constexpr int offset = 4354;
  WasmRunner<int32_t> r(execution_tier);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(kWasmPageSize);
  int8_t* global = r.builder().AddGlobal<int8_t>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(
               0, WASM_SIMD_LOAD_OP_OFFSET(kExprS128Load8Splat, WASM_I32V(0),
                                           U32V_2(offset))),
           WASM_ONE});

  // We don't really care about all valid values, so just test for 1.
  int8_t x = 7;
  r.builder().WriteMemory(&memory[offset], x);
  r.Call();
  for (int i = 0; i < 16; i++) {
    CHECK_EQ(x, LANE(global, i));
  }
}

template <typename T>
void RunLoadSplatTest(TestExecutionTier execution_tier, WasmOpcode op) {
  constexpr int lanes = 16 / sizeof(T);
  constexpr int mem_index = 16;  // Load from mem index 16 (bytes).
  {
    WasmRunner<int32_t> r(execution_tier);
    T* memory = r.builder().AddMemoryElems<T>(kWasmPageSize / sizeof(T));
    T* global = r.builder().AddGlobal<T>(kWasmS128);
    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_LOAD_OP(op, WASM_I32V(mem_index))),
             WASM_ONE});

    for (T x : compiler::ValueHelper::GetVector<T>()) {
      // 16-th byte in memory is lanes-th element (size T) of memory.
      r.builder().WriteMemory(&memory[lanes], x);
      r.Call();
      for (int i = 0; i < lanes; i++) {
        CHECK_EQ(x, LANE(global, i));
      }
    }
  }

  // Test for OOB.
  {
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    r.builder().AddMemoryElems<T>(kWasmPageSize / sizeof(T));
    r.builder().AddGlobal<T>(kWasmS128);

    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_LOAD_OP(op, WASM_LOCAL_GET(0))),
             WASM_ONE});

    // Load splats load sizeof(T) bytes.
    for (uint32_t offset = kWasmPageSize - (sizeof(T) - 1);
         offset < kWasmPageSize; ++offset) {
      CHECK_TRAP(r.Call(offset));
    }
  }
}

WASM_EXEC_TEST(S128Load8Splat) {
  RunLoadSplatTest<int8_t>(execution_tier, kExprS128Load8Splat);
}

WASM_EXEC_TEST(S128Load16Splat) {
  RunLoadSplatTest<int16_t>(execution_tier, kExprS128Load16Splat);
}

WASM_EXEC_TEST(S128Load32Splat) {
  RunLoadSplatTest<int32_t>(execution_tier, kExprS128Load32Splat);
}

WASM_EXEC_TEST(S128Load64Splat) {
  RunLoadSplatTest<int64_t>(execution_tier, kExprS128Load64Splat);
}

template <typename S, typename T>
void RunLoadExtendTest(TestExecutionTier execution_tier, WasmOpcode op) {
  static_assert(sizeof(S) < sizeof(T),
                "load extend should go from smaller to larger type");
  constexpr int lanes_s = 16 / sizeof(S);
  constexpr int lanes_t = 16 / sizeof(T);
  constexpr int mem_index = 16;  // Load from mem index 16 (bytes).
  // Load extends always load 64 bits, so alignment values can be from 0 to 3.
  for (uint8_t alignment = 0; alignment <= 3; alignment++) {
    WasmRunner<int32_t> r(execution_tier);
    S* memory = r.builder().AddMemoryElems<S>(kWasmPageSize / sizeof(S));
    T* global = r.builder().AddGlobal<T>(kWasmS128);
    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_LOAD_OP_ALIGNMENT(
                                    op, WASM_I32V(mem_index), alignment)),
             WASM_ONE});

    for (S x : compiler::ValueHelper::GetVector<S>()) {
      for (int i = 0; i < lanes_s; i++) {
        // 16-th byte in memory is lanes-th element (size T) of memory.
        r.builder().WriteMemory(&memory[lanes_s + i], x);
      }
      r.Call();
      for (int i = 0; i < lanes_t; i++) {
        CHECK_EQ(static_cast<T>(x), LANE(global, i));
      }
    }
  }

  // Test for offset.
  {
    WasmRunner<int32_t> r(execution_tier);
    S* memory = r.builder().AddMemoryElems<S>(kWasmPageSize / sizeof(S));
    T* global = r.builder().AddGlobal<T>(kWasmS128);
    constexpr uint8_t offset = sizeof(S);
    r.Build(
        {WASM_GLOBAL_SET(0, WASM_SIMD_LOAD_OP_OFFSET(op, WASM_ZERO, offset)),
         WASM_ONE});

    // Let max_s be the max_s value for type S, we set up the memory as such:
    // memory = [max_s, max_s - 1, ... max_s - (lane_s - 1)].
    constexpr S max_s = std::numeric_limits<S>::max();
    for (int i = 0; i < lanes_s; i++) {
      // Integer promotion due to -, static_cast to narrow.
      r.builder().WriteMemory(&memory[i], static_cast<S>(max_s - i));
    }

    r.Call();

    // Loads will be offset by sizeof(S), so will always start from (max_s - 1).
    for (int i = 0; i < lanes_t; i++) {
      // Integer promotion due to -, static_cast to narrow.
      T expected = static_cast<T>(max_s - i - 1);
      CHECK_EQ(expected, LANE(global, i));
    }
  }

  // Test for OOB.
  {
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    r.builder().AddMemoryElems<S>(kWasmPageSize / sizeof(S));
    r.builder().AddGlobal<T>(kWasmS128);

    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_LOAD_OP(op, WASM_LOCAL_GET(0))),
             WASM_ONE});

    // Load extends load 8 bytes, so should trap from -7.
    for (uint32_t offset = kWasmPageSize - 7; offset < kWasmPageSize;
         ++offset) {
      CHECK_TRAP(r.Call(offset));
    }
  }
}

WASM_EXEC_TEST(S128Load8x8U) {
  RunLoadExtendTest<uint8_t, uint16_t>(execution_tier, kExprS128Load8x8U);
}

WASM_EXEC_TEST(S128Load8x8S) {
  RunLoadExtendTest<int8_t, int16_t>(execution_tier, kExprS128Load8x8S);
}
WASM_EXEC_TEST(S128Load16x4U) {
  RunLoadExtendTest<uint16_t, uint32_t>(execution_tier, kExprS128Load16x4U);
}

WASM_EXEC_TEST(S128Load16x4S) {
  RunLoadExtendTest<int16_t, int32_t>(execution_tier, kExprS128Load16x4S);
}

WASM_EXEC_TEST(S128Load32x2U) {
  RunLoadExtendTest<uint32_t, uint64_t>(execution_tier, kExprS128Load32x2U);
}

WASM_EXEC_TEST(S128Load32x2S) {
  RunLoadExtendTest<int32_t, int64_t>(execution_tier, kExprS128Load32x2S);
}

template <typename S>
void RunLoadZeroTest(TestExecutionTier execution_tier, WasmOpcode op) {
  constexpr int lanes_s = kSimd128Size / sizeof(S);
  constexpr int mem_index = 16;  // Load from mem index 16 (bytes).
  constexpr S sentinel = S{-1};
  S* memory;
  S* global;

  auto initialize_builder = [=](WasmRunner<int32_t>* r) -> std::tuple<S*, S*> {
    S* memory = r->builder().AddMemoryElems<S>(kWasmPageSize / sizeof(S));
    S* global = r->builder().AddGlobal<S>(kWasmS128);
    r->builder().RandomizeMemory();
    r->builder().WriteMemory(&memory[lanes_s], sentinel);
    return std::make_tuple(memory, global);
  };

  // Check all supported alignments.
  constexpr int max_alignment = base::bits::CountTrailingZeros(sizeof(S));
  for (uint8_t alignment = 0; alignment <= max_alignment; alignment++) {
    WasmRunner<int32_t> r(execution_tier);
    std::tie(memory, global) = initialize_builder(&r);

    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_LOAD_OP(op, WASM_I32V(mem_index))),
             WASM_ONE});
    r.Call();

    // Only first lane is set to sentinel.
    CHECK_EQ(sentinel, LANE(global, 0));
    // The other lanes are zero.
    for (int i = 1; i < lanes_s; i++) {
      CHECK_EQ(S{0}, LANE(global, i));
    }
  }

  {
    // Use memarg to specific offset.
    WasmRunner<int32_t> r(execution_tier);
    std::tie(memory, global) = initialize_builder(&r);

    r.Build(
        {WASM_GLOBAL_SET(0, WASM_SIMD_LOAD_OP_OFFSET(op, WASM_ZERO, mem_index)),
         WASM_ONE});
    r.Call();

    // Only first lane is set to sentinel.
    CHECK_EQ(sentinel, LANE(global, 0));
    // The other lanes are zero.
    for (int i = 1; i < lanes_s; i++) {
      CHECK_EQ(S{0}, LANE(global, i));
    }
  }

  // Test for OOB.
  {
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    r.builder().AddMemoryElems<S>(kWasmPageSize / sizeof(S));
    r.builder().AddGlobal<S>(kWasmS128);

    r.Build({WASM_GLOBAL_SET(0, WASM_SIMD_LOAD_OP(op, WASM_LOCAL_GET(0))),
             WASM_ONE});

    // Load extends load sizeof(S) bytes.
    for (uint32_t offset = kWasmPageSize - (sizeof(S) - 1);
         offset < kWasmPageSize; ++offset) {
      CHECK_TRAP(r.Call(offset));
    }
  }
}

WASM_EXEC_TEST(S128Load32Zero) {
  RunLoadZeroTest<int32_t>(execution_tier, kExprS128Load32Zero);
}

WASM_EXEC_TEST(S128Load64Zero) {
  RunLoadZeroTest<int64_t>(execution_tier, kExprS128Load64Zero);
}

template <typename T>
void RunLoadLaneTest(TestExecutionTier execution_tier, WasmOpcode load_op,
                     WasmOpcode splat_op) {
  uint8_t const_op = static_cast<uint8_t>(
      splat_op == kExprI64x2Splat ? kExprI64Const : kExprI32Const);

  constexpr uint8_t lanes_s = kSimd128Size / sizeof(T);
  constexpr int mem_index = 16;  // Load from mem index 16 (bytes).
  constexpr uint8_t splat_value = 33;
  T sentinel = T{-1};

  T* memory;
  T* global;

  auto build_fn = [=, &memory, &global](WasmRunner<int32_t>& r, int mem_index,
                                        uint8_t lane, uint8_t alignment,
                                        uint8_t offset) {
    memory = r.builder().AddMemoryElems<T>(kWasmPageSize / sizeof(T));
    global = r.builder().AddGlobal<T>(kWasmS128);
    r.builder().WriteMemory(&memory[lanes_s], sentinel);
    // Splat splat_value, then only load and replace a single lane with the
    // sentinel value.
    r.Build({WASM_I32V(mem_index), const_op, splat_value,
             WASM_SIMD_OP(splat_op), WASM_SIMD_OP(load_op), alignment, offset,
             lane, kExprGlobalSet, 0, WASM_ONE});
  };

  auto check_results = [=](T* global, int sentinel_lane = 0) {
    // Only one lane is loaded, the rest of the lanes are unchanged.
    for (uint8_t i = 0; i < lanes_s; i++) {
      T expected = i == sentinel_lane ? sentinel : static_cast<T>(splat_value);
      CHECK_EQ(expected, LANE(global, i));
    }
  };

  for (uint8_t lane_index = 0; lane_index < lanes_s; ++lane_index) {
    WasmRunner<int32_t> r(execution_tier);
    build_fn(r, mem_index, lane_index, /*alignment=*/0, /*offset=*/0);
    r.Call();
    check_results(global, lane_index);
  }

  // Check all possible alignments.
  constexpr int max_alignment = base::bits::CountTrailingZeros(sizeof(T));
  for (uint8_t alignment = 0; alignment <= max_alignment; ++alignment) {
    WasmRunner<int32_t> r(execution_tier);
    build_fn(r, mem_index, /*lane=*/0, alignment, /*offset=*/0);
    r.Call();
    check_results(global);
  }

  {
    // Use memarg to specify offset.
    int lane_index = 0;
    WasmRunner<int32_t> r(execution_tier);
    build_fn(r, /*mem_index=*/0, /*lane=*/0, /*alignment=*/0,
             /*offset=*/mem_index);
    r.Call();
    check_results(global, lane_index);
  }

  // Test for OOB.
  {
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    r.builder().AddMemoryElems<T>(kWasmPageSize / sizeof(T));
    r.builder().AddGlobal<T>(kWasmS128);

    r.Build({WASM_LOCAL_GET(0), const_op, splat_value, WASM_SIMD_OP(splat_op),
             WASM_SIMD_OP(load_op), ZERO_ALIGNMENT, ZERO_OFFSET, 0,
             kExprGlobalSet, 0, WASM_ONE});

    // Load lane load sizeof(T) bytes.
    for (uint32_t index = kWasmPageSize - (sizeof(T) - 1);
         index < kWasmPageSize; ++index) {
      CHECK_TRAP(r.Call(index));
    }
  }
}

WASM_EXEC_TEST(S128Load8Lane) {
  RunLoadLaneTest<int8_t>(execution_tier, kExprS128Load8Lane, kExprI8x16Splat);
}

WASM_EXEC_TEST(S128Load16Lane) {
  RunLoadLaneTest<int16_t>(execution_tier, kExprS128Load16Lane,
                           kExprI16x8Splat);
}

WASM_EXEC_TEST(S128Load32Lane) {
  RunLoadLaneTest<int32_t>(execution_tier, kExprS128Load32Lane,
                           kExprI32x4Splat);
}

WASM_EXEC_TEST(S128Load64Lane) {
  RunLoadLaneTest<int64_t>(execution_tier, kExprS128Load64Lane,
                           kExprI64x2Splat);
}

template <typename T>
void RunStoreLaneTest(TestExecutionTier execution_tier, WasmOpcode store_op,
                      WasmOpcode splat_op) {
  constexpr uint8_t lanes = kSimd128Size / sizeof(T);
  constexpr int mem_index = 16;  // Store to mem index 16 (bytes).
  constexpr uint8_t splat_value = 33;
  uint8_t const_op = static_cast<uint8_t>(
      splat_op == kExprI64x2Splat ? kExprI64Const : kExprI32Const);

  T* memory;  // Will be set by build_fn.

  auto build_fn = [=, &memory](WasmRunner<int32_t>& r, int mem_index,
                               uint8_t lane_index, uint8_t alignment,
                               uint8_t offset) {
    memory = r.builder().AddMemoryElems<T>(kWasmPageSize / sizeof(T));
    // Splat splat_value, then only Store and replace a single lane.
    r.Build({WASM_I32V(mem_index), const_op, splat_value,
             WASM_SIMD_OP(splat_op), WASM_SIMD_OP(store_op), alignment, offset,
             lane_index, WASM_ONE});
    r.builder().BlankMemory();
  };

  auto check_results = [=](WasmRunner<int32_t>& r, T* memory) {
    for (uint8_t i = 0; i < lanes; i++) {
      CHECK_EQ(0, r.builder().ReadMemory(&memory[i]));
    }

    CHECK_EQ(splat_value, r.builder().ReadMemory(&memory[lanes]));

    for (uint8_t i = lanes + 1; i < lanes * 2; i++) {
      CHECK_EQ(0, r.builder().ReadMemory(&memory[i]));
    }
  };

  for (uint8_t lane_index = 0; lane_index < lanes; lane_index++) {
    WasmRunner<int32_t> r(execution_tier);
    build_fn(r, mem_index, lane_index, ZERO_ALIGNMENT, ZERO_OFFSET);
    r.Call();
    check_results(r, memory);
  }

  // Check all possible alignments.
  constexpr int max_alignment = base::bits::CountTrailingZeros(sizeof(T));
  for (uint8_t alignment = 0; alignment <= max_alignment; ++alignment) {
    WasmRunner<int32_t> r(execution_tier);
    build_fn(r, mem_index, /*lane_index=*/0, alignment, ZERO_OFFSET);
    r.Call();
    check_results(r, memory);
  }

  {
    // Use memarg for offset.
    WasmRunner<int32_t> r(execution_tier);
    build_fn(r, /*mem_index=*/0, /*lane_index=*/0, ZERO_ALIGNMENT, mem_index);
    r.Call();
    check_results(r, memory);
  }

  // OOB stores
  {
    WasmRunner<int32_t, uint32_t> r(execution_tier);
    r.builder().AddMemoryElems<T>(kWasmPageSize / sizeof(T));

    r.Build({WASM_LOCAL_GET(0), const_op, splat_value, WASM_SIMD_OP(splat_op),
             WASM_SIMD_OP(store_op), ZERO_ALIGNMENT, ZERO_OFFSET, 0, WASM_ONE});

    // StoreLane stores sizeof(T) bytes.
    for (uint32_t index = kWasmPageSize - (sizeof(T) - 1);
         index < kWasmPageSize; ++index) {
      CHECK_TRAP(r.Call(index));
    }
  }
}

WASM_EXEC_TEST(S128Store8Lane) {
  RunStoreLaneTest<int8_t>(execution_tier, kExprS128Store8Lane,
                           kExprI8x16Splat);
}

WASM_EXEC_TEST(S128Store16Lane) {
  RunStoreLaneTest<int16_t>(execution_tier, kExprS128Store16Lane,
                            kExprI16x8Splat);
}

WASM_EXEC_TEST(S128Store32Lane) {
  RunStoreLaneTest<int32_t>(execution_tier, kExprS128Store32Lane,
                            kExprI32x4Splat);
}

WASM_EXEC_TEST(S128Store64Lane) {
  RunStoreLaneTest<int64_t>(execution_tier, kExprS128Store64Lane,
                            kExprI64x2Splat);
}

#define WASM_SIMD_ANYTRUE_TEST(format, lanes, max, param_type)                 \
  WASM_EXEC_TEST(S##format##AnyTrue) {                                         \
    WasmRunner<int32_t, param_type> r(execution_tier);                         \
    if (lanes == 2) return;                                                    \
    uint8_t simd = r.AllocateLocal(kWasmS128);                                 \
    r.Build(                                                                   \
        {WASM_LOCAL_SET(simd, WASM_SIMD_I##format##_SPLAT(WASM_LOCAL_GET(0))), \
         WASM_SIMD_UNOP(kExprV128AnyTrue, WASM_LOCAL_GET(simd))});             \
    CHECK_EQ(1, r.Call(max));                                                  \
    CHECK_EQ(1, r.Call(5));                                                    \
    CHECK_EQ(0, r.Call(0));                                                    \
  }
WASM_SIMD_ANYTRUE_TEST(32x4, 4, 0xffffffff, int32_t)
WASM_SIMD_ANYTRUE_TEST(16x8, 8, 0xffff, int32_t)
WASM_SIMD_ANYTRUE_TEST(8x16, 16, 0xff, int32_t)

// Special any true test cases that splats a -0.0 double into a i64x2.
// This is specifically to ensure that our implementation correct handles that
// 0.0 and -0.0 will be different in an anytrue (IEEE753 says they are equals).
WASM_EXEC_TEST(V128AnytrueWithNegativeZero) {
  WasmRunner<int32_t, int64_t> r(execution_tier);
  uint8_t simd = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(simd, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(0))),
           WASM_SIMD_UNOP(kExprV128AnyTrue, WASM_LOCAL_GET(simd))});
  CHECK_EQ(1, r.Call(0x8000000000000000));
  CHECK_EQ(0, r.Call(0x0000000000000000));
}

#define WASM_SIMD_ALLTRUE_TEST(format, lanes, max, param_type)                 \
  WASM_EXEC_TEST(I##format##AllTrue) {                                         \
    WasmRunner<int32_t, param_type> r(execution_tier);                         \
    if (lanes == 2) return;                                                    \
    uint8_t simd = r.AllocateLocal(kWasmS128);                                 \
    r.Build(                                                                   \
        {WASM_LOCAL_SET(simd, WASM_SIMD_I##format##_SPLAT(WASM_LOCAL_GET(0))), \
         WASM_SIMD_UNOP(kExprI##format##AllTrue, WASM_LOCAL_GET(simd))});      \
    CHECK_EQ(1, r.Call(max));                                                  \
    CHECK_EQ(1, r.Call(0x1));                                                  \
    CHECK_EQ(0, r.Call(0));                                                    \
  }
WASM_SIMD_ALLTRUE_TEST(64x2, 2, 0xffffffffffffffff, int64_t)
WASM_SIMD_ALLTRUE_TEST(32x4, 4, 0xffffffff, int32_t)
WASM_SIMD_ALLTRUE_TEST(16x8, 8, 0xffff, int32_t)
WASM_SIMD_ALLTRUE_TEST(8x16, 16, 0xff, int32_t)

WASM_EXEC_TEST(BitSelect) {
  WasmRunner<int32_t, int32_t> r(execution_tier);
  uint8_t simd = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(
               simd, WASM_SIMD_SELECT(
                         32x4, WASM_SIMD_I32x4_SPLAT(WASM_I32V(0x01020304)),
                         WASM_SIMD_I32x4_SPLAT(WASM_I32V(0)),
                         WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(0)))),
           WASM_SIMD_I32x4_EXTRACT_LANE(0, WASM_LOCAL_GET(simd))});
  CHECK_EQ(0x01020304, r.Call(0xFFFFFFFF));
}

void RunSimdConstTest(TestExecutionTier execution_tier,
                      const std::array<uint8_t, kSimd128Size>& expected) {
  WasmRunner<uint32_t> r(execution_tier);
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t* src0 = r.builder().AddGlobal<uint8_t>(kWasmS128);
  r.Build({WASM_GLOBAL_SET(temp1, WASM_SIMD_CONSTANT(expected)), WASM_ONE});
  CHECK_EQ(1, r.Call());
  for (size_t i = 0; i < expected.size(); i++) {
    CHECK_EQ(LANE(src0, i), expected[i]);
  }
}

WASM_EXEC_TEST(S128Const) {
  std::array<uint8_t, kSimd128Size> expected;
  // Test for generic constant
  for (int i = 0; i < kSimd128Size; i++) {
    expected[i] = i;
  }
  RunSimdConstTest(execution_tier, expected);

  // Keep the first 4 lanes as 0, set the remaining ones.
  for (int i = 0; i < 4; i++) {
    expected[i] = 0;
  }
  for (int i = 4; i < kSimd128Size; i++) {
    expected[i] = i;
  }
  RunSimdConstTest(execution_tier, expected);

  // Check sign extension logic used to pack int32s into int64.
  expected = {0};
  // Set the top bit of lane 3 (top bit of first int32), the rest can be 0.
  expected[3] = 0x80;
  RunSimdConstTest(execution_tier, expected);
}

WASM_EXEC_TEST(S128ConstAllZero) {
  std::array<uint8_t, kSimd128Size> expected = {0};
  RunSimdConstTest(execution_tier, expected);
}

WASM_EXEC_TEST(S128ConstAllOnes) {
  std::array<uint8_t, kSimd128Size> expected;
  // Test for generic constant
  for (int i = 0; i < kSimd128Size; i++) {
    expected[i] = 0xff;
  }
  RunSimdConstTest(execution_tier, expected);
}

WASM_EXEC_TEST(I8x16LeUMixed) {
  RunI8x16MixedRelationalOpTest(execution_tier, kExprI8x16LeU,
                                UnsignedLessEqual);
}
WASM_EXEC_TEST(I8x16LtUMixed) {
  RunI8x16MixedRelationalOpTest(execution_tier, kExprI8x16LtU, UnsignedLess);
}
WASM_EXEC_TEST(I8x16GeUMixed) {
  RunI8x16MixedRelationalOpTest(execution_tier, kExprI8x16GeU,
                                UnsignedGreaterEqual);
}
WASM_EXEC_TEST(I8x16GtUMixed) {
  RunI8x16MixedRelationalOpTest(execution_tier, kExprI8x16GtU, UnsignedGreater);
}

WASM_EXEC_TEST(I16x8LeUMixed) {
  RunI16x8MixedRelationalOpTest(execution_tier, kExprI16x8LeU,
                                UnsignedLessEqual);
}
WASM_EXEC_TEST(I16x8LtUMixed) {
  RunI16x8MixedRelationalOpTest(execution_tier, kExprI16x8LtU, UnsignedLess);
}
WASM_EXEC_TEST(I16x8GeUMixed) {
  RunI16x8MixedRelationalOpTest(execution_tier, kExprI16x8GeU,
                                UnsignedGreaterEqual);
}
WASM_EXEC_TEST(I16x8GtUMixed) {
  RunI16x8MixedRelationalOpTest(execution_tier, kExprI16x8GtU, UnsignedGreater);
}

WASM_EXEC_TEST(I16x8ExtractLaneU_I8x16Splat) {
  // Test that we are correctly signed/unsigned extending when extracting.
  WasmRunner<int32_t, int32_t> r(execution_tier);
  uint8_t simd_val = r.AllocateLocal(kWasmS128);
  r.Build({WASM_LOCAL_SET(simd_val, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(0))),
           WASM_SIMD_I16x8_EXTRACT_LANE_U(0, WASM_LOCAL_GET(simd_val))});
  CHECK_EQ(0xfafa, r.Call(0xfa));
}

enum ExtAddSide { LEFT, RIGHT };

template <typename T, typename U>
void RunAddExtAddPairwiseTest(
    TestExecutionTier execution_tier, ExtAddSide extAddSide,
    WasmOpcode addOpcode,
    const std::array<T, kSimd128Size / sizeof(T)> addInput,
    WasmOpcode extAddOpcode,
    const std::array<U, kSimd128Size / sizeof(U)> extAddInput,
    const std::array<T, kSimd128Size / sizeof(T)> expectedOutput) {
  WasmRunner<int32_t> r(execution_tier);
  T* x = r.builder().AddGlobal<T>(kWasmS128);
  for (size_t i = 0; i < addInput.size(); i++) {
    LANE(x, i) = addInput[i];
  }
  U* y = r.builder().AddGlobal<U>(kWasmS128);
  for (size_t i = 0; i < extAddInput.size(); i++) {
    LANE(y, i) = extAddInput[i];
  }
  switch (extAddSide) {
    case LEFT:
      // x = add(extadd_pairwise_s(y), x)
      r.Build({WASM_GLOBAL_SET(
                   0, WASM_SIMD_BINOP(
                          addOpcode,
                          WASM_SIMD_UNOP(extAddOpcode, WASM_GLOBAL_GET(1)),
                          WASM_GLOBAL_GET(0))),

               WASM_ONE});
      break;
    case RIGHT:
      // x = add(x, extadd_pairwise_s(y))
      r.Build({WASM_GLOBAL_SET(
                   0, WASM_SIMD_BINOP(
                          addOpcode, WASM_GLOBAL_GET(0),
                          WASM_SIMD_UNOP(extAddOpcode, WASM_GLOBAL_GET(1)))),

               WASM_ONE});
      break;
  }
  r.Call();

  for (size_t i = 0; i < expectedOutput.size(); i++) {
    CHECK_EQ(expectedOutput[i], LANE(x, i));
  }
}

WASM_EXEC_TEST(AddExtAddPairwiseI32Right) {
  RunAddExtAddPairwiseTest<int32_t, int16_t>(
      execution_tier, RIGHT, kExprI32x4Add, {1, 2, 3, 4},
      kExprI32x4ExtAddPairwiseI16x8S, {-1, -2, -3, -4, -5, -6, -7, -8},
      {-2, -5, -8, -11});
}

WASM_EXEC_TEST(AddExtAddPairwiseI32Left) {
  RunAddExtAddPairwiseTest<int32_t, int16_t>(
      execution_tier, LEFT, kExprI32x4Add, {1, 2, 3, 4},
      kExprI32x4ExtAddPairwiseI16x8S, {-1, -2, -3, -4, -5, -6, -7, -8},
      {-2, -5, -8, -11});
}

WASM_EXEC_TEST(AddExtAddPairwiseI16Right) {
  RunAddExtAddPairwiseTest<int16_t, int8_t>(
      execution_tier, RIGHT, kExprI16x8Add, {1, 2, 3, 4, 5, 6, 7, 8},
      kExprI16x8ExtAddPairwiseI8x16S,
      {-1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12, -13, -14, -15, -16},
      {-2, -5, -8, -11, -14, -17, -20, -23});
}

WASM_EXEC_TEST(AddExtAddPairwiseI16Left) {
  RunAddExtAddPairwiseTest<int16_t, int8_t>(
      execution_tier, LEFT, kExprI16x8Add, {1, 2, 3, 4, 5, 6, 7, 8},
      kExprI16x8ExtAddPairwiseI8x16S,
      {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
      {4, 9, 14, 19, 24, 29, 34, 39});
}

WASM_EXEC_TEST(AddExtAddPairwiseI32RightUnsigned) {
  RunAddExtAddPairwiseTest<uint32_t, uint16_t>(
      execution_tier, RIGHT, kExprI32x4Add, {1, 2, 3, 4},
      kExprI32x4ExtAddPairwiseI16x8U, {1, 2, 3, 4, 5, 6, 7, 8}, {4, 9, 14, 19});
}

WASM_EXEC_TEST(AddExtAddPairwiseI32LeftUnsigned) {
  RunAddExtAddPairwiseTest<uint32_t, uint16_t>(
      execution_tier, LEFT, kExprI32x4Add, {1, 2, 3, 4},
      kExprI32x4ExtAddPairwiseI16x8U, {1, 2, 3, 4, 5, 6, 7, 8}, {4, 9, 14, 19});
}

// Regression test from https://crbug.com/v8/12237 to exercise a codegen bug
// for i64x2.gts which overwrote one of the inputs.
WASM_EXEC_TEST(Regress_12237) {
  WasmRunner<int32_t, int64_t> r(execution_tier);
  int64_t* g = r.builder().AddGlobal<int64_t>(kWasmS128);
  uint8_t value = 0;
  uint8_t temp = r.AllocateLocal(kWasmS128);
  int64_t local = 123;
  r.Build({WASM_LOCAL_SET(
               temp, WASM_SIMD_OPN(kExprI64x2Splat, WASM_LOCAL_GET(value))),
           WASM_GLOBAL_SET(
               0, WASM_SIMD_BINOP(
                      kExprI64x2GtS, WASM_LOCAL_GET(temp),
                      WASM_SIMD_BINOP(kExprI64x2Sub, WASM_LOCAL_GET(temp),
                                      WASM_LOCAL_GET(temp)))),
           WASM_ONE});
  r.Call(local);
  int64_t expected = Greater(local, local - local);
  for (size_t i = 0; i < kSimd128Size / sizeof(int64_t); i++) {
    CHECK_EQ(expected, LANE(g, 0));
  }
}

#define WASM_EXTRACT_I16x8_TEST(Sign, Type)                                  \
  WASM_EXEC_TEST(I16X8ExtractLane##Sign) {                                   \
    WasmRunner<int32_t, int32_t> r(execution_tier);                          \
    uint8_t int_val = r.AllocateLocal(kWasmI32);                             \
    uint8_t simd_val = r.AllocateLocal(kWasmS128);                           \
    r.Build({WASM_LOCAL_SET(simd_val,                                        \
                            WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(int_val))), \
             WASM_SIMD_CHECK_LANE_U(I16x8, simd_val, I32, int_val, 0),       \
             WASM_SIMD_CHECK_LANE_U(I16x8, simd_val, I32, int_val, 2),       \
             WASM_SIMD_CHECK_LANE_U(I16x8, simd_val, I32, int_val, 4),       \
             WASM_SIMD_CHECK_LANE_U(I16x8, simd_val, I32, int_val, 6),       \
             WASM_ONE});                                                     \
    FOR_##Type##_INPUTS(x) { CHECK_EQ(1, r.Call(x)); }                       \
  }
WASM_EXTRACT_I16x8_TEST(S, UINT16) WASM_EXTRACT_I16x8_TEST(I, INT16)
#undef WASM_EXTRACT_I16x8_TEST

#define WASM_EXTRACT_I8x16_TEST(Sign, Type)                                  \
  WASM_EXEC_TEST(I8x16ExtractLane##Sign) {                                   \
    WasmRunner<int32_t, int32_t> r(execution_tier);                          \
    uint8_t int_val = r.AllocateLocal(kWasmI32);                             \
    uint8_t simd_val = r.AllocateLocal(kWasmS128);                           \
    r.Build({WASM_LOCAL_SET(simd_val,                                        \
                            WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(int_val))), \
             WASM_SIMD_CHECK_LANE_U(I8x16, simd_val, I32, int_val, 1),       \
             WASM_SIMD_CHECK_LANE_U(I8x16, simd_val, I32, int_val, 3),       \
             WASM_SIMD_CHECK_LANE_U(I8x16, simd_val, I32, int_val, 5),       \
             WASM_SIMD_CHECK_LANE_U(I8x16, simd_val, I32, int_val, 7),       \
             WASM_SIMD_CHECK_LANE_U(I8x16, simd_val, I32, int_val, 9),       \
             WASM_SIMD_CHECK_LANE_U(I8x16, simd_val, I32, int_val, 10),      \
             WASM_SIMD_CHECK_LANE_U(I8x16, simd_val, I32, int_val, 11),      \
             WASM_SIMD_CHECK_LANE_U(I8x16, simd_val, I32, int_val, 13),      \
             WASM_ONE});                                                     \
    FOR_##Type##_INPUTS(x) { CHECK_EQ(1, r.Call(x)); }                       \
  }
    WASM_EXTRACT_I8x16_TEST(S, UINT8) WASM_EXTRACT_I8x16_TEST(I, INT8)
#undef WASM_EXTRACT_I8x16_TEST

#ifdef V8_ENABLE_WASM_SIMD256_REVEC

void RunSimd256ConstTest(const std::array<uint8_t, kSimd128Size>& expected) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(32);
  uint8_t param1 = 0;
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Constant>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, compiler::IrOpcode::kS256Const,
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param1),
                            WASM_SIMD_CONSTANT(expected)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param1),
                                   WASM_SIMD_CONSTANT(expected)),
        WASM_ONE);
  }
  CHECK_EQ(1, r.Call(0));
  for (size_t i = 0; i < expected.size(); i++) {
    CHECK_EQ(memory[i], expected[i]);
    CHECK_EQ(memory[i + 16], expected[i]);
  }
}

TEST(RunWasmTurbofan_S256Const) {
  // All zeroes
  std::array<uint8_t, kSimd128Size> expected = {0};
  RunSimd256ConstTest(expected);

  // All ones
  for (int i = 0; i < kSimd128Size; i++) {
    expected[i] = 0xff;
  }
  RunSimd256ConstTest(expected);

  // Test for generic constant
  for (int i = 0; i < kSimd128Size; i++) {
    expected[i] = i;
  }
  RunSimd256ConstTest(expected);

  // Keep the first 4 lanes as 0, set the remaining ones.
  for (int i = 0; i < 4; i++) {
    expected[i] = 0;
  }
  for (int i = 4; i < kSimd128Size; i++) {
    expected[i] = i;
  }
  RunSimd256ConstTest(expected);

  // Check sign extension logic used to pack int32s into int64.
  expected = {0};
  // Set the top bit of lane 3 (top bit of first int32), the rest can be 0.
  expected[3] = 0x80;
  RunSimd256ConstTest(expected);
}

TEST(RunWasmTurbofan_ExtractF128) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int64_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  int64_t* memory = r.builder().AddMemoryElems<int64_t>(12);
  // Add two 256 bit vectors a and b, store the result in c and return the sum
  // of all the int64 elements in c:
  //   simd128 *a,*b,*c,*d;
  //   *c = *a + *b;
  //   *(c+1) = *(a+1) + *(b+1);
  //   *d = *c + *(c+1);
  //   return LANE(d, 0) + LANE(d,1);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Extract128Lane>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, compiler::IrOpcode::kI64x4Add,
        WASM_LOCAL_SET(
            temp1, WASM_SIMD_BINOP(kExprI64x2Add,
                                   WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                                   WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
        WASM_LOCAL_SET(
            temp2,
            WASM_SIMD_BINOP(
                kExprI64x2Add,
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param2)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                   WASM_LOCAL_GET(temp2)),
        WASM_LOCAL_SET(temp3,
                       WASM_SIMD_BINOP(kExprI64x2Add, WASM_LOCAL_GET(temp1),
                                       WASM_LOCAL_GET(temp2))),
        WASM_I64_ADD(WASM_SIMD_I64x2_EXTRACT_LANE(0, WASM_LOCAL_GET(temp3)),
                     WASM_SIMD_I64x2_EXTRACT_LANE(1, WASM_LOCAL_GET(temp3))));
  }
  for (int64_t x : compiler::ValueHelper::GetVector<int64_t>()) {
    for (int64_t y : compiler::ValueHelper::GetVector<int64_t>()) {
      for (int i = 0; i < 4; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + 4], y);
      }
      int64_t expected = base::AddWithWraparound(x, y);
      CHECK_EQ(r.Call(0, 32, 64), expected * 4);
      for (int i = 0; i < 4; i++) {
        CHECK_EQ(expected, memory[i + 8]);
      }
    }
  }
}

TEST(RunWasmTurbofan_F32x8Abs) {
  RunF32x8UnOpRevecTest(kExprF32x4Abs, std::abs, compiler::IrOpcode::kF32x8Abs);
}

TEST(RunWasmTurbofan_F32x8Neg) {
  RunF32x8UnOpRevecTest(kExprF32x4Neg, Negate, compiler::IrOpcode::kF32x8Neg);
}

TEST(RunWasmTurbofan_F32x8Sqrt) {
  RunF32x8UnOpRevecTest(kExprF32x4Sqrt, std::sqrt,
                        compiler::IrOpcode::kF32x8Sqrt);
}

TEST(RunWasmTurbofan_F32x8Add) {
  RunF32x8BinOpRevecTest(kExprF32x4Add, Add, compiler::IrOpcode::kF32x8Add);
}

TEST(RunWasmTurbofan_F32x8Sub) {
  RunF32x8BinOpRevecTest(kExprF32x4Sub, Sub, compiler::IrOpcode::kF32x8Sub);
}

TEST(RunWasmTurbofan_F32x8Mul) {
  RunF32x8BinOpRevecTest(kExprF32x4Mul, Mul, compiler::IrOpcode::kF32x8Mul);
}

TEST(RunWasmTurbofan_F32x8Div) {
  RunF32x8BinOpRevecTest(kExprF32x4Div, base::Divide,
                         compiler::IrOpcode::kF32x8Div);
}

TEST(RunWasmTurbofan_F32x8Min) {
  RunF32x8BinOpRevecTest(kExprF32x4Min, JSMin, compiler::IrOpcode::kF32x8Min);
}

TEST(RunWasmTurbofan_F32x8Max) {
  RunF32x8BinOpRevecTest(kExprF32x4Max, JSMax, compiler::IrOpcode::kF32x8Max);
}

TEST(RunWasmTurbofan_F32x8Pmin) {
  RunF32x8BinOpRevecTest(kExprF32x4Pmin, Minimum,
                         compiler::IrOpcode::kF32x8Pmin);
}

TEST(RunWasmTurbofan_F32x8Pmax) {
  RunF32x8BinOpRevecTest(kExprF32x4Pmax, Maximum,
                         compiler::IrOpcode::kF32x8Pmax);
}

TEST(RunWasmTurbofan_F32x8Eq) {
  RunF32x8CompareOpRevecTest(kExprF32x4Eq, Equal, compiler::IrOpcode::kF32x8Eq);
}

TEST(RunWasmTurbofan_F32x8Ne) {
  RunF32x8CompareOpRevecTest(kExprF32x4Ne, NotEqual,
                             compiler::IrOpcode::kF32x8Ne);
}

TEST(RunWasmTurbofan_F32x8Lt) {
  RunF32x8CompareOpRevecTest(kExprF32x4Lt, Less, compiler::IrOpcode::kF32x8Lt);
}

TEST(RunWasmTurbofan_F32x8Le) {
  RunF32x8CompareOpRevecTest(kExprF32x4Le, LessEqual,
                             compiler::IrOpcode::kF32x8Le);
}

TEST(RunWasmTurbofan_I64x4Shl) {
  RunI64x4ShiftOpRevecTest(kExprI64x2Shl, LogicalShiftLeft,
                           compiler::IrOpcode::kI64x4Shl);
}

TEST(RunWasmTurbofan_I64x4ShrU) {
  RunI64x4ShiftOpRevecTest(kExprI64x2ShrU, LogicalShiftRight,
                           compiler::IrOpcode::kI64x4ShrU);
}

TEST(RunWasmTurbofan_I64x4Add) {
  RunI64x4BinOpRevecTest(kExprI64x2Add, base::AddWithWraparound,
                         compiler::IrOpcode::kI64x4Add);
}

TEST(RunWasmTurbofan_I64x4Sub) {
  RunI64x4BinOpRevecTest(kExprI64x2Sub, base::SubWithWraparound,
                         compiler::IrOpcode::kI64x4Sub);
}

TEST(RunWasmTurbofan_I64x4Mul) {
  RunI64x4BinOpRevecTest(kExprI64x2Mul, base::MulWithWraparound,
                         compiler::IrOpcode::kI64x4Mul);
}

TEST(RunWasmTurbofan_I64x4Eq) {
  RunI64x4BinOpRevecTest(kExprI64x2Eq, Equal, compiler::IrOpcode::kI64x4Eq);
}

TEST(RunWasmTurbofan_I64x4Ne) {
  RunI64x4BinOpRevecTest(kExprI64x2Ne, NotEqual, compiler::IrOpcode::kI64x4Ne);
}

TEST(RunWasmTurbofan_I64x4GtS) {
  RunI64x4BinOpRevecTest(kExprI64x2GtS, Greater, compiler::IrOpcode::kI64x4GtS);
}

TEST(RunWasmTurbofan_I64x4GeS) {
  RunI64x4BinOpRevecTest(kExprI64x2GeS, GreaterEqual,
                         compiler::IrOpcode::kI64x4GeS);
}

TEST(RunWasmTurbofan_F64x4Abs) {
  RunF64x4UnOpRevecTest(kExprF64x2Abs, std::abs, compiler::IrOpcode::kF64x4Abs);
}

TEST(RunWasmTurbofan_F64x4Neg) {
  RunF64x4UnOpRevecTest(kExprF64x2Neg, Negate, compiler::IrOpcode::kF64x4Neg);
}

TEST(RunWasmTurbofan_F64x4Sqrt) {
  RunF64x4UnOpRevecTest(kExprF64x2Sqrt, std::sqrt,
                        compiler::IrOpcode::kF64x4Sqrt);
}

TEST(RunWasmTurbofan_F64x4Add) {
  RunF64x4BinOpRevecTest(kExprF64x2Add, Add, compiler::IrOpcode::kF64x4Add);
}

TEST(RunWasmTurbofan_F64x4Sub) {
  RunF64x4BinOpRevecTest(kExprF64x2Sub, Sub, compiler::IrOpcode::kF64x4Sub);
}

TEST(RunWasmTurbofan_F64x4Mul) {
  RunF64x4BinOpRevecTest(kExprF64x2Mul, Mul, compiler::IrOpcode::kF64x4Mul);
}

TEST(RunWasmTurbofan_F64x4Div) {
  RunF64x4BinOpRevecTest(kExprF64x2Div, base::Divide,
                         compiler::IrOpcode::kF64x4Div);
}

TEST(RunWasmTurbofan_F64x4Min) {
  RunF64x4BinOpRevecTest(kExprF64x2Min, JSMin, compiler::IrOpcode::kF64x4Min);
}

TEST(RunWasmTurbofan_F64x4Max) {
  RunF64x4BinOpRevecTest(kExprF64x2Max, JSMax, compiler::IrOpcode::kF64x4Max);
}

TEST(RunWasmTurbofan_F64x4Pmin) {
  RunF64x4BinOpRevecTest(kExprF64x2Pmin, Minimum,
                         compiler::IrOpcode::kF64x4Pmin);
}

TEST(RunWasmTurbofan_F64x4Pmax) {
  RunF64x4BinOpRevecTest(kExprF64x2Pmax, Maximum,
                         compiler::IrOpcode::kF64x4Pmax);
}

TEST(RunWasmTurbofan_F64x4Eq) {
  RunF64x4CompareOpRevecTest(kExprF64x2Eq, Equal, compiler::IrOpcode::kF64x4Eq);
}

TEST(RunWasmTurbofan_F64x4Ne) {
  RunF64x4CompareOpRevecTest(kExprF64x2Ne, NotEqual,
                             compiler::IrOpcode::kF64x4Ne);
}

TEST(RunWasmTurbofan_F64x4Lt) {
  RunF64x4CompareOpRevecTest(kExprF64x2Lt, Less, compiler::IrOpcode::kF64x4Lt);
}

TEST(RunWasmTurbofan_F64x4Le) {
  RunF64x4CompareOpRevecTest(kExprF64x2Le, LessEqual,
                             compiler::IrOpcode::kF64x4Le);
}

TEST(RunWasmTurbofan_I32x8SConvertF32x8) {
  RunI32x8ConvertF32x8RevecTest<int32_t>(
      kExprI32x4SConvertF32x4, ConvertToInt,
      compiler::IrOpcode::kI32x8SConvertF32x8);
}

TEST(RunWasmTurbofan_I32x8UConvertF32x8) {
  RunI32x8ConvertF32x8RevecTest<uint32_t>(
      kExprI32x4UConvertF32x4, ConvertToInt,
      compiler::IrOpcode::kI32x8UConvertF32x8);
}

TEST(RunWasmTurbofan_F32x8SConvertI32x8) {
  RunF32x8ConvertI32x8RevecTest<int32_t>(
      kExprF32x4SConvertI32x4, compiler::IrOpcode::kF32x8SConvertI32x8);
}

TEST(RunWasmTurbofan_F32x8UConvertI32x8) {
  RunF32x8ConvertI32x8RevecTest<uint32_t>(
      kExprF32x4UConvertI32x4, compiler::IrOpcode::kF32x8UConvertI32x8);
}

TEST(RunWasmTurbofan_I64x4SConvertI32x4) {
  RunIntSignExtensionRevecTest<int32_t, int64_t>(
      kExprI64x2SConvertI32x4Low, kExprI64x2SConvertI32x4High, kExprI32x4Splat,
      compiler::IrOpcode::kI64x4SConvertI32x4);
}

TEST(RunWasmTurbofan_I64x4UConvertI32x4) {
  RunIntSignExtensionRevecTest<uint32_t, uint64_t>(
      kExprI64x2UConvertI32x4Low, kExprI64x2UConvertI32x4High, kExprI32x4Splat,
      compiler::IrOpcode::kI64x4UConvertI32x4);
}

TEST(RunWasmTurbofan_I32x8SConvertI16x8) {
  RunIntSignExtensionRevecTest<int16_t, int32_t>(
      kExprI32x4SConvertI16x8Low, kExprI32x4SConvertI16x8High, kExprI16x8Splat,
      compiler::IrOpcode::kI32x8SConvertI16x8);
}

TEST(RunWasmTurbofan_I32x8UConvertI16x8) {
  RunIntSignExtensionRevecTest<uint16_t, uint32_t>(
      kExprI32x4UConvertI16x8Low, kExprI32x4UConvertI16x8High, kExprI16x8Splat,
      compiler::IrOpcode::kI32x8UConvertI16x8);
}

TEST(RunWasmTurbofan_I16x16SConvertI8x16) {
  RunIntSignExtensionRevecTest<int8_t, int16_t>(
      kExprI16x8SConvertI8x16Low, kExprI16x8SConvertI8x16High, kExprI8x16Splat,
      compiler::IrOpcode::kI16x16SConvertI8x16);
}

TEST(RunWasmTurbofan_I16x16UConvertI8x16) {
  RunIntSignExtensionRevecTest<uint8_t, uint16_t>(
      kExprI16x8UConvertI8x16Low, kExprI16x8UConvertI8x16High, kExprI8x16Splat,
      compiler::IrOpcode::kI16x16UConvertI8x16);
}

TEST(RunWasmTurbofan_I32x8Neg) {
  RunI32x8UnOpRevecTest(kExprI32x4Neg, base::NegateWithWraparound,
                        compiler::IrOpcode::kI32x8Neg);
}

TEST(RunWasmTurbofan_I32x8Abs) {
  RunI32x8UnOpRevecTest(kExprI32x4Abs, std::abs, compiler::IrOpcode::kI32x8Abs);
}

template <typename Narrow, typename Wide>
void RunExtAddPairwiseRevecTest(WasmOpcode ext_add_pairwise) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  // [intput1(128bit)|intput2(128bit)|output(256bit)]
  Narrow* memory =
      r.builder().AddMemoryElems<Narrow>(kSimd128Size / sizeof(Narrow) * 4);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unary>);
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_UNOP(ext_add_pairwise,
                                                  WASM_SIMD_LOAD_MEM(
                                                      WASM_LOCAL_GET(param1)))),
             WASM_LOCAL_SET(
                 temp2, WASM_SIMD_UNOP(ext_add_pairwise,
                                       WASM_SIMD_LOAD_MEM_OFFSET(
                                           offset, WASM_LOCAL_GET(param1)))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp2)),
             WASM_ONE});
  }
  for (Narrow x : compiler::ValueHelper::GetVector<Narrow>()) {
    for (int i = 0; i < static_cast<int>(kSimd128Size / sizeof(Narrow) * 2);
         i++) {
      r.builder().WriteMemory(&memory[i], x);
    }
    r.Call(0, 32);
    Wide expected = AddLong<Wide>(x, x);
    for (int i = 0; i < static_cast<int>(kSimd128Size / sizeof(Wide) * 2);
         i++) {
      CHECK_EQ(memcmp((const void*)&expected,
                      &memory[kSimd128Size / sizeof(Narrow) * 2 + i * 2], 2),
               0);
    }
  }
}

TEST(RunWasmTurbofan_I16x16ExtAddPairwiseI8x32S) {
  RunExtAddPairwiseRevecTest<int8_t, int16_t>(kExprI16x8ExtAddPairwiseI8x16S);
}

TEST(RunWasmTurbofan_I16x16ExtAddPairwiseI8x32U) {
  RunExtAddPairwiseRevecTest<uint8_t, uint16_t>(kExprI16x8ExtAddPairwiseI8x16U);
}

TEST(RunWasmTurbofan_I32x8ExtAddPairwiseI16x16S) {
  RunExtAddPairwiseRevecTest<int16_t, int32_t>(kExprI32x4ExtAddPairwiseI16x8S);
}

TEST(RunWasmTurbofan_I32x8ExtAddPairwiseI16x16U) {
  RunExtAddPairwiseRevecTest<uint16_t, uint32_t>(
      kExprI32x4ExtAddPairwiseI16x8U);
}

TEST(RunWasmTurbofan_S256Not) {
  RunI32x8UnOpRevecTest(kExprS128Not, BitwiseNot, compiler::IrOpcode::kS256Not);
}

TEST(RunWasmTurbofan_S256And) {
  RunI32x8BinOpRevecTest(kExprS128And, BitwiseAnd,
                         compiler::IrOpcode::kS256And);
}

TEST(RunWasmTurbofan_S256Or) {
  RunI32x8BinOpRevecTest(kExprS128Or, BitwiseOr, compiler::IrOpcode::kS256Or);
}

TEST(RunWasmTurbofan_S256Xor) {
  RunI32x8BinOpRevecTest(kExprS128Xor, BitwiseXor,
                         compiler::IrOpcode::kS256Xor);
}

TEST(RunWasmTurbofan_S256AndNot) {
  RunI32x8BinOpRevecTest(kExprS128AndNot, BitwiseAndNot,
                         compiler::IrOpcode::kS256AndNot);
}

TEST(RunWasmTurbofan_S256Select) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(32);
  // Build fn perform bitwise selection on two 256 bit vectors a and b, mask c,
  // store the result in d:
  //   simd128 *a,*b,*c,*d;
  //   *d = select(*a, *b, *c);
  //   *(d+1) = select(*(a+1), *(b+1), *(c+1))
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t param4 = 3;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256TernaryOp,
            compiler::turboshaft::Simd256TernaryOp::Kind::kS256Select>);
    BUILD_AND_CHECK_REVEC_NODE(
        r, compiler::IrOpcode::kS256Select,
        WASM_LOCAL_SET(
            temp1,
            WASM_SIMD_SELECT(32x4, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                             WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)),
                             WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param3)))),
        WASM_LOCAL_SET(
            temp2,
            WASM_SIMD_SELECT(
                32x4, WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param2)),
                WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param3)))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param4), WASM_LOCAL_GET(temp1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param4),
                                   WASM_LOCAL_GET(temp2)),
        WASM_ONE);
  }
  for (auto x : compiler::ValueHelper::GetVector<int32_t>()) {
    for (auto y : compiler::ValueHelper::GetVector<int32_t>()) {
      for (auto z : compiler::ValueHelper::GetVector<int32_t>()) {
        for (int i = 0; i < 4; i++) {
          r.builder().WriteMemory(&memory[i], x);
          r.builder().WriteMemory(&memory[i + 4], x);
          r.builder().WriteMemory(&memory[i + 8], y);
          r.builder().WriteMemory(&memory[i + 12], y);
          r.builder().WriteMemory(&memory[i + 16], z);
          r.builder().WriteMemory(&memory[i + 20], z);
        }
        CHECK_EQ(1, r.Call(0, 32, 64, 96));
        int32_t expected = BitwiseSelect(x, y, z);
        for (int i = 0; i < 4; i++) {
          CHECK_EQ(expected, memory[i + 24]);
          CHECK_EQ(expected, memory[i + 28]);
        }
      }
    }
  }
}

TEST(RunWasmTurbofan_I32x8Add) {
  RunI32x8BinOpRevecTest(kExprI32x4Add, base::AddWithWraparound,
                         compiler::IrOpcode::kI32x8Add);
}

TEST(RunWasmTurbofan_I32x8Sub) {
  RunI32x8BinOpRevecTest(kExprI32x4Sub, base::SubWithWraparound,
                         compiler::IrOpcode::kI32x8Sub);
}

TEST(RunWasmTurbofan_I32x8Mul) {
  RunI32x8BinOpRevecTest(kExprI32x4Mul, base::MulWithWraparound,
                         compiler::IrOpcode::kI32x8Mul);
}

TEST(RunWasmTurbofan_I32x8MinS) {
  RunI32x8BinOpRevecTest(kExprI32x4MinS, Minimum,
                         compiler::IrOpcode::kI32x8MinS);
}

TEST(RunWasmTurbofan_I32x8MinU) {
  RunI32x8BinOpRevecTest(kExprI32x4MinU, UnsignedMinimum,
                         compiler::IrOpcode::kI32x8MinU);
}

TEST(RunWasmTurbofan_I32x8MaxS) {
  RunI32x8BinOpRevecTest(kExprI32x4MaxS, Maximum,
                         compiler::IrOpcode::kI32x8MaxS);
}

TEST(RunWasmTurbofan_I32x8MaxU) {
  RunI32x8BinOpRevecTest(kExprI32x4MaxU, UnsignedMaximum,
                         compiler::IrOpcode::kI32x8MaxU);
}

TEST(RunWasmTurbofan_I32x8Eq) {
  RunI32x8BinOpRevecTest(kExprI32x4Eq, Equal, compiler::IrOpcode::kI32x8Eq);
}

TEST(RunWasmTurbofan_I32x8Ne) {
  RunI32x8BinOpRevecTest(kExprI32x4Ne, NotEqual, compiler::IrOpcode::kI32x8Ne);
}

TEST(RunWasmTurbofan_I32x8GtS) {
  RunI32x8BinOpRevecTest(kExprI32x4GtS, Greater, compiler::IrOpcode::kI32x8GtS);
}

TEST(RunWasmTurbofan_I32x8GtU) {
  RunI32x8BinOpRevecTest<uint32_t>(kExprI32x4GtU, UnsignedGreater,
                                   compiler::IrOpcode::kI32x8GtU);
}

TEST(RunWasmTurbofan_I32x8GeS) {
  RunI32x8BinOpRevecTest(kExprI32x4GeS, GreaterEqual,
                         compiler::IrOpcode::kI32x8GeS);
}

TEST(RunWasmTurbofan_I32x8GeU) {
  RunI32x8BinOpRevecTest<uint32_t>(kExprI32x4GeU, UnsignedGreaterEqual,
                                   compiler::IrOpcode::kI32x8GeU);
}

TEST(RunWasmTurbofan_I32x8Shl) {
  RunI32x8ShiftOpRevecTest(kExprI32x4Shl, LogicalShiftLeft,
                           compiler::IrOpcode::kI32x8Shl);
}

TEST(RunWasmTurbofan_I32x8ShrS) {
  RunI32x8ShiftOpRevecTest(kExprI32x4ShrS, ArithmeticShiftRight,
                           compiler::IrOpcode::kI32x8ShrS);
}

TEST(RunWasmTurbofan_I32x8ShrU) {
  RunI32x8ShiftOpRevecTest(kExprI32x4ShrU, LogicalShiftRight,
                           compiler::IrOpcode::kI32x8ShrU);
}

TEST(RunWasmTurbofan_I16x16Neg) {
  RunI16x16UnOpRevecTest(kExprI16x8Neg, base::NegateWithWraparound,
                         compiler::IrOpcode::kI16x16Neg);
}

TEST(RunWasmTurbofan_I16x16Abs) {
  RunI16x16UnOpRevecTest(kExprI16x8Abs, Abs, compiler::IrOpcode::kI16x16Abs);
}

TEST(RunWasmTurbofan_I16x16Add) {
  RunI16x16BinOpRevecTest(kExprI16x8Add, base::AddWithWraparound,
                          compiler::IrOpcode::kI16x16Add);
}

TEST(RunWasmTurbofan_I16x16Sub) {
  RunI16x16BinOpRevecTest(kExprI16x8Sub, base::SubWithWraparound,
                          compiler::IrOpcode::kI16x16Sub);
}

TEST(RunWasmTurbofan_I16x16Mul) {
  RunI16x16BinOpRevecTest(kExprI16x8Mul, base::MulWithWraparound,
                          compiler::IrOpcode::kI16x16Mul);
}

TEST(RunWasmTurbofan_I16x16AddSatS) {
  RunI16x16BinOpRevecTest<int16_t>(kExprI16x8AddSatS, SaturateAdd,
                                   compiler::IrOpcode::kI16x16AddSatS);
}

TEST(RunWasmTurbofan_I16x16SubSatS) {
  RunI16x16BinOpRevecTest<int16_t>(kExprI16x8SubSatS, SaturateSub,
                                   compiler::IrOpcode::kI16x16SubSatS);
}

TEST(RunWasmTurbofan_I16x16AddSatU) {
  RunI16x16BinOpRevecTest<uint16_t>(kExprI16x8AddSatU, SaturateAdd,
                                    compiler::IrOpcode::kI16x16AddSatU);
}

TEST(RunWasmTurbofan_I16x16SubSatU) {
  RunI16x16BinOpRevecTest<uint16_t>(kExprI16x8SubSatU, SaturateSub,
                                    compiler::IrOpcode::kI16x16SubSatU);
}

TEST(WasmTurbofan_I16x16Eq) {
  RunI16x16BinOpRevecTest(kExprI16x8Eq, Equal, compiler::IrOpcode::kI16x16Eq);
}

TEST(WasmTurbofan_I16x16Ne) {
  RunI16x16BinOpRevecTest(kExprI16x8Ne, NotEqual,
                          compiler::IrOpcode::kI16x16Ne);
}

TEST(WasmTurbofan_I16x16GtS) {
  RunI16x16BinOpRevecTest(kExprI16x8GtS, Greater,
                          compiler::IrOpcode::kI16x16GtS);
}

TEST(WasmTurbofan_I16x16GtU) {
  RunI16x16BinOpRevecTest<uint16_t>(kExprI16x8GtU, UnsignedGreater,
                                    compiler::IrOpcode::kI16x16GtU);
}

TEST(WasmTurbofan_I16x16GeS) {
  RunI16x16BinOpRevecTest(kExprI16x8GeS, GreaterEqual,
                          compiler::IrOpcode::kI16x16GeS);
}

TEST(WasmTurbofan_I16x16GeU) {
  RunI16x16BinOpRevecTest<uint16_t>(kExprI16x8GeU, UnsignedGreaterEqual,
                                    compiler::IrOpcode::kI16x16GeU);
}

TEST(WasmTurbofan_I16x16MinS) {
  RunI16x16BinOpRevecTest(kExprI16x8MinS, Minimum,
                          compiler::IrOpcode::kI16x16MinS);
}

TEST(WasmTurbofan_I16x16MinU) {
  RunI16x16BinOpRevecTest(kExprI16x8MinU, UnsignedMinimum,
                          compiler::IrOpcode::kI16x16MinU);
}

TEST(WasmTurbofan_I16x16MaxS) {
  RunI16x16BinOpRevecTest(kExprI16x8MaxS, Maximum,
                          compiler::IrOpcode::kI16x16MaxS);
}

TEST(WasmTurbofan_I16x16MaxU) {
  RunI16x16BinOpRevecTest(kExprI16x8MaxU, UnsignedMaximum,
                          compiler::IrOpcode::kI16x16MaxU);
}

TEST(WasmTurbofan_I16x16RoundingAverageU) {
  RunI16x16BinOpRevecTest<uint16_t>(
      kExprI16x8RoundingAverageU, RoundingAverageUnsigned,
      compiler::IrOpcode::kI16x16RoundingAverageU);
}

namespace {

bool IsLowHalfExtMulOp(WasmOpcode opcode) {
  switch (opcode) {
    case kExprI16x8ExtMulLowI8x16S:
    case kExprI16x8ExtMulLowI8x16U:
    case kExprI32x4ExtMulLowI16x8S:
    case kExprI32x4ExtMulLowI16x8U:
    case kExprI64x2ExtMulLowI32x4S:
    case kExprI64x2ExtMulLowI32x4U:
      return true;
    case kExprI16x8ExtMulHighI8x16S:
    case kExprI16x8ExtMulHighI8x16U:
    case kExprI32x4ExtMulHighI16x8S:
    case kExprI32x4ExtMulHighI16x8U:
    case kExprI64x2ExtMulHighI32x4S:
    case kExprI64x2ExtMulHighI32x4U:
      return false;

    default:
      UNREACHABLE();
  }
}

}  // namespace

template <typename S, typename T,
          typename compiler::turboshaft::Opcode revec_opcode,
          typename OpType = T (*)(S, S)>
void RunExtMulRevecTest(WasmOpcode opcode_low, WasmOpcode opcode_high,
                        OpType expected_op,
                        ExpectedResult revec_result = ExpectedResult::kPass) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  static_assert(sizeof(T) == 2 * sizeof(S),
                "the element size of dst vector must be twice of src vector in "
                "extended integer multiplication");
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);

  // Build fn perform extmul on two 128 bit vectors a and b, store the result in
  // c and d:
  // v128 a = v128.load(param1);
  // v128 b = v128.load(param2);
  // v128 c = v128.not(v128.not(opcode1(a, b)));
  // v128 d = v128.not(v128.not(opcode2(a, b)));
  // v128.store(param3, c);
  // v128.store(param3 + 16, d);
  // Where opcode1 and opcode2 are extended integer multiplication opcodes, the
  // two v128.not are used to make sure revec is beneficial in revec cost
  // estimation steps.
  uint32_t count = 4 * kSimd128Size / sizeof(S);
  S* memory = r.builder().AddMemoryElems<S>(count);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<revec_opcode>,
        revec_result);
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2))),
             WASM_LOCAL_SET(temp3,
                            WASM_SIMD_BINOP(opcode_low, WASM_LOCAL_GET(temp1),
                                            WASM_LOCAL_GET(temp2))),
             WASM_LOCAL_SET(
                 temp3, WASM_SIMD_UNOP(kExprS128Not,
                                       WASM_SIMD_UNOP(kExprS128Not,
                                                      WASM_LOCAL_GET(temp3)))),
             WASM_LOCAL_SET(temp4,
                            WASM_SIMD_BINOP(opcode_high, WASM_LOCAL_GET(temp1),
                                            WASM_LOCAL_GET(temp2))),
             WASM_LOCAL_SET(
                 temp4, WASM_SIMD_UNOP(kExprS128Not,
                                       WASM_SIMD_UNOP(kExprS128Not,
                                                      WASM_LOCAL_GET(temp4)))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp3)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                        WASM_LOCAL_GET(temp4)),
             WASM_ONE});
  }

  constexpr uint32_t lanes = kSimd128Size / sizeof(S);
  for (S x : compiler::ValueHelper::GetVector<S>()) {
    for (S y : compiler::ValueHelper::GetVector<S>()) {
      for (uint32_t i = 0; i < lanes / 2; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + lanes / 2], y);
        r.builder().WriteMemory(&memory[i + lanes], y);
        r.builder().WriteMemory(&memory[i + lanes + lanes / 2], y);
      }
      r.Call(0, 16, 32);
      T expected_low = expected_op(x, y);
      T expected_high = expected_op(y, y);
      T* output = reinterpret_cast<T*>(memory + 2 * lanes);
      for (uint32_t i = 0; i < lanes / 2; i++) {
        CHECK_EQ(IsLowHalfExtMulOp(opcode_low) ? expected_low : expected_high,
                 output[i]);
        CHECK_EQ(IsLowHalfExtMulOp(opcode_high) ? expected_low : expected_high,
                 output[lanes / 2 + i]);
      }
    }
  }
}

// (low, high) extmul, revec to simd256 extmul, revec succeed.
// (low, low) extmul, force pack, revec succeed.
// (high, high) extmul, force pack, revec succeed.
// (high, low) extmul, revec failed, not
// supported yet.
TEST(RunWasmTurbofan_ForcePackExtMul) {
  // 8x16 to 16x8.
  RunExtMulRevecTest<int8_t, int16_t,
                     compiler::turboshaft::Opcode::kSimd256Binop>(
      kExprI16x8ExtMulLowI8x16S, kExprI16x8ExtMulHighI8x16S, MultiplyLong);
  RunExtMulRevecTest<int8_t, int16_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulLowI8x16S, kExprI16x8ExtMulLowI8x16S, MultiplyLong);
  RunExtMulRevecTest<int8_t, int16_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulHighI8x16S, kExprI16x8ExtMulHighI8x16S, MultiplyLong);
  RunExtMulRevecTest<int8_t, int16_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulHighI8x16S, kExprI16x8ExtMulLowI8x16S, MultiplyLong,
      ExpectedResult::kFail);
  RunExtMulRevecTest<uint8_t, uint16_t,
                     compiler::turboshaft::Opcode::kSimd256Binop>(
      kExprI16x8ExtMulLowI8x16U, kExprI16x8ExtMulHighI8x16U, MultiplyLong);
  RunExtMulRevecTest<uint8_t, uint16_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulLowI8x16U, kExprI16x8ExtMulLowI8x16U, MultiplyLong);
  RunExtMulRevecTest<uint8_t, uint16_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulHighI8x16U, kExprI16x8ExtMulHighI8x16U, MultiplyLong);
  RunExtMulRevecTest<uint8_t, uint16_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulHighI8x16U, kExprI16x8ExtMulLowI8x16U, MultiplyLong,
      ExpectedResult::kFail);

  // 16x8 to 32x4.
  RunExtMulRevecTest<int16_t, int32_t,
                     compiler::turboshaft::Opcode::kSimd256Binop>(
      kExprI32x4ExtMulLowI16x8S, kExprI32x4ExtMulHighI16x8S, MultiplyLong);
  RunExtMulRevecTest<int16_t, int32_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulLowI16x8S, kExprI32x4ExtMulLowI16x8S, MultiplyLong);
  RunExtMulRevecTest<int16_t, int32_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulHighI16x8S, kExprI32x4ExtMulHighI16x8S, MultiplyLong);
  RunExtMulRevecTest<int16_t, int32_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulHighI16x8S, kExprI32x4ExtMulLowI16x8S, MultiplyLong,
      ExpectedResult::kFail);
  RunExtMulRevecTest<uint16_t, uint32_t,
                     compiler::turboshaft::Opcode::kSimd256Binop>(
      kExprI32x4ExtMulLowI16x8U, kExprI32x4ExtMulHighI16x8U, MultiplyLong);
  RunExtMulRevecTest<uint16_t, uint32_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulLowI16x8U, kExprI32x4ExtMulLowI16x8U, MultiplyLong);
  RunExtMulRevecTest<uint16_t, uint32_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulHighI16x8U, kExprI32x4ExtMulHighI16x8U, MultiplyLong);
  RunExtMulRevecTest<uint16_t, uint32_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulHighI16x8U, kExprI32x4ExtMulLowI16x8U, MultiplyLong,
      ExpectedResult::kFail);

  // 32x4 to 64x2.
  RunExtMulRevecTest<int32_t, int64_t,
                     compiler::turboshaft::Opcode::kSimd256Binop>(
      kExprI64x2ExtMulLowI32x4S, kExprI64x2ExtMulHighI32x4S, MultiplyLong);
  RunExtMulRevecTest<int32_t, int64_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulLowI32x4S, kExprI64x2ExtMulLowI32x4S, MultiplyLong);
  RunExtMulRevecTest<int32_t, int64_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulHighI32x4S, kExprI64x2ExtMulHighI32x4S, MultiplyLong);
  RunExtMulRevecTest<int32_t, int64_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulHighI32x4S, kExprI64x2ExtMulLowI32x4S, MultiplyLong,
      ExpectedResult::kFail);
  RunExtMulRevecTest<uint32_t, uint64_t,
                     compiler::turboshaft::Opcode::kSimd256Binop>(
      kExprI64x2ExtMulLowI32x4U, kExprI64x2ExtMulHighI32x4U, MultiplyLong);
  RunExtMulRevecTest<uint32_t, uint64_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulLowI32x4U, kExprI64x2ExtMulLowI32x4U, MultiplyLong);
  RunExtMulRevecTest<uint32_t, uint64_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulHighI32x4U, kExprI64x2ExtMulHighI32x4U, MultiplyLong);
  RunExtMulRevecTest<uint32_t, uint64_t,
                     compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulHighI32x4U, kExprI64x2ExtMulLowI32x4U, MultiplyLong,
      ExpectedResult::kFail);
}

// Similar with RunExtMulRevecTest, but two stores share an extended integer
// multiplication op.
template <typename S, typename T,
          typename compiler::turboshaft::Opcode revec_opcode,
          typename OpType = T (*)(S, S)>
void RunExtMulRevecTestSplat(WasmOpcode opcode, OpType expected_op) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  static_assert(sizeof(T) == 2 * sizeof(S),
                "the element size of dst vector must be twice of src vector in "
                "extended integer multiplication");
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);

  // Build fn perform extmul on two 128 bit vectors a and b, store the result in
  // d and e:
  // v128 a = v128.load(param1);
  // v128 b = v128.load(param2);
  // v128 c = opcode1(a, b)
  // v128 d = v128.not(v128.not(c));
  // v128 e = v128.not(v128.not(c));
  // v128.store(param3, d);
  // v128.store(param3 + 16, e);
  // Where opcode1 is an extended integer multiplication opcode, the two
  // v128.not are used to make sure revec is beneficial in revec cost estimation
  // steps.
  uint32_t count = 4 * kSimd128Size / sizeof(S);
  S* memory = r.builder().AddMemoryElems<S>(count);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<revec_opcode>,
        ExpectedResult::kPass);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2))),
         WASM_LOCAL_SET(temp5, WASM_SIMD_BINOP(opcode, WASM_LOCAL_GET(temp1),
                                               WASM_LOCAL_GET(temp2))),
         WASM_LOCAL_SET(
             temp3, WASM_SIMD_UNOP(
                        kExprS128Not,
                        WASM_SIMD_UNOP(kExprS128Not, WASM_LOCAL_GET(temp5)))),
         WASM_LOCAL_SET(
             temp4, WASM_SIMD_UNOP(
                        kExprS128Not,
                        WASM_SIMD_UNOP(kExprS128Not, WASM_LOCAL_GET(temp5)))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp3)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                    WASM_LOCAL_GET(temp4)),
         WASM_ONE});
  }

  constexpr uint32_t lanes = kSimd128Size / sizeof(S);
  for (S x : compiler::ValueHelper::GetVector<S>()) {
    for (S y : compiler::ValueHelper::GetVector<S>()) {
      for (uint32_t i = 0; i < lanes / 2; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + lanes / 2], y);
        r.builder().WriteMemory(&memory[i + lanes], y);
        r.builder().WriteMemory(&memory[i + lanes + lanes / 2], y);
      }
      r.Call(0, 16, 32);
      T expected_low = expected_op(x, y);
      T expected_high = expected_op(y, y);
      T* output = reinterpret_cast<T*>(memory + 2 * lanes);
      for (uint32_t i = 0; i < lanes; i++) {
        CHECK_EQ(IsLowHalfExtMulOp(opcode) ? expected_low : expected_high,
                 output[i]);
      }
    }
  }
}

TEST(RunWasmTurbofan_ForcePackExtMulSplat) {
  // 8x16 to 16x8.
  RunExtMulRevecTestSplat<int8_t, int16_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulLowI8x16S, MultiplyLong);
  RunExtMulRevecTestSplat<int8_t, int16_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulHighI8x16S, MultiplyLong);
  RunExtMulRevecTestSplat<uint8_t, uint16_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulLowI8x16U, MultiplyLong);
  RunExtMulRevecTestSplat<uint8_t, uint16_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI16x8ExtMulHighI8x16U, MultiplyLong);

  // 16x8 to 32x4.
  RunExtMulRevecTestSplat<int16_t, int32_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulLowI16x8S, MultiplyLong);
  RunExtMulRevecTestSplat<int16_t, int32_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulHighI16x8S, MultiplyLong);
  RunExtMulRevecTestSplat<uint16_t, uint32_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulLowI16x8U, MultiplyLong);
  RunExtMulRevecTestSplat<uint16_t, uint32_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI32x4ExtMulHighI16x8U, MultiplyLong);

  // 32x4 to 64x2.
  RunExtMulRevecTestSplat<int32_t, int64_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulLowI32x4S, MultiplyLong);
  RunExtMulRevecTestSplat<int32_t, int64_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulHighI32x4S, MultiplyLong);
  RunExtMulRevecTestSplat<uint32_t, uint64_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulLowI32x4U, MultiplyLong);
  RunExtMulRevecTestSplat<uint32_t, uint64_t,
                          compiler::turboshaft::Opcode::kSimdPack128To256>(
      kExprI64x2ExtMulHighI32x4U, MultiplyLong);
}

TEST(RunWasmTurbofan_I16x16Shl) {
  RunI16x16ShiftOpRevecTest(kExprI16x8Shl, LogicalShiftLeft,
                            compiler::IrOpcode::kI16x16Shl);
}

TEST(RunWasmTurbofan_I16x16ShrS) {
  RunI16x16ShiftOpRevecTest(kExprI16x8ShrS, ArithmeticShiftRight,
                            compiler::IrOpcode::kI16x16ShrS);
}

TEST(RunWasmTurbofan_I16x16ShrU) {
  RunI16x16ShiftOpRevecTest(kExprI16x8ShrU, LogicalShiftRight,
                            compiler::IrOpcode::kI16x16ShrU);
}

TEST(RunWasmTurbofan_I8x32Neg) {
  RunI8x32UnOpRevecTest(kExprI8x16Neg, base::NegateWithWraparound,
                        compiler::IrOpcode::kI8x32Neg);
}

TEST(RunWasmTurbofan_I8x32Abs) {
  RunI8x32UnOpRevecTest(kExprI8x16Abs, Abs, compiler::IrOpcode::kI8x32Abs);
}

TEST(RunWasmTurbofan_I8x32Add) {
  RunI8x32BinOpRevecTest(kExprI8x16Add, base::AddWithWraparound,
                         compiler::IrOpcode::kI8x32Add);
}

TEST(RunWasmTurbofan_I8x32Sub) {
  RunI8x32BinOpRevecTest(kExprI8x16Sub, base::SubWithWraparound,
                         compiler::IrOpcode::kI8x32Sub);
}

TEST(RunWasmTurbofan_I8x32AddSatS) {
  RunI8x32BinOpRevecTest<int8_t>(kExprI8x16AddSatS, SaturateAdd,
                                 compiler::IrOpcode::kI8x32AddSatS);
}

TEST(RunWasmTurbofan_I8x32SubSatS) {
  RunI8x32BinOpRevecTest<int8_t>(kExprI8x16SubSatS, SaturateSub,
                                 compiler::IrOpcode::kI8x32SubSatS);
}

TEST(RunWasmTurbofan_I8x32AddSatU) {
  RunI8x32BinOpRevecTest<uint8_t>(kExprI8x16AddSatU, SaturateAdd,
                                  compiler::IrOpcode::kI8x32AddSatU);
}

TEST(RunWasmTurbofan_I8x32SubSatU) {
  RunI8x32BinOpRevecTest<uint8_t>(kExprI8x16SubSatU, SaturateSub,
                                  compiler::IrOpcode::kI8x32SubSatU);
}

TEST(RunWasmTurbofan_I8x32Eq) {
  RunI8x32BinOpRevecTest(kExprI8x16Eq, Equal, compiler::IrOpcode::kI8x32Eq);
}

TEST(RunWasmTurbofan_I8x32Ne) {
  RunI8x32BinOpRevecTest(kExprI8x16Ne, NotEqual, compiler::IrOpcode::kI8x32Ne);
}

TEST(RunWasmTurbofan_I8x32GtS) {
  RunI8x32BinOpRevecTest(kExprI8x16GtS, Greater, compiler::IrOpcode::kI8x32GtS);
}

TEST(RunWasmTurbofan_I8x32GtU) {
  RunI8x32BinOpRevecTest<uint8_t>(kExprI8x16GtU, UnsignedGreater,
                                  compiler::IrOpcode::kI8x32GtU);
}

TEST(RunWasmTurbofan_I8x32GeS) {
  RunI8x32BinOpRevecTest(kExprI8x16GeS, GreaterEqual,
                         compiler::IrOpcode::kI8x32GeS);
}

TEST(RunWasmTurbofan_I8x32GeU) {
  RunI8x32BinOpRevecTest<uint8_t>(kExprI8x16GeU, UnsignedGreaterEqual,
                                  compiler::IrOpcode::kI8x32GeU);
}

TEST(RunWasmTurbofan_I8x32MinS) {
  RunI8x32BinOpRevecTest(kExprI8x16MinS, Minimum,
                         compiler::IrOpcode::kI8x32MinS);
}

TEST(RunWasmTurbofan_I8x32MinU) {
  RunI8x32BinOpRevecTest(kExprI8x16MinU, UnsignedMinimum,
                         compiler::IrOpcode::kI8x32MinU);
}

TEST(RunWasmTurbofan_I8x32MaxS) {
  RunI8x32BinOpRevecTest(kExprI8x16MaxS, Maximum,
                         compiler::IrOpcode::kI8x32MaxS);
}

TEST(RunWasmTurbofan_I8x32MaxU) {
  RunI8x32BinOpRevecTest(kExprI8x16MaxU, UnsignedMaximum,
                         compiler::IrOpcode::kI8x32MaxU);
}

TEST(RunWasmTurbofan_I8x32RoundingAverageU) {
  RunI8x32BinOpRevecTest<uint8_t>(kExprI8x16RoundingAverageU,
                                  RoundingAverageUnsigned,
                                  compiler::IrOpcode::kI8x32RoundingAverageU);
}

TEST(RunWasmTurbofan_F32x4AddRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<float, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmF32);
  uint8_t temp6 = r.AllocateLocal(kWasmF32);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256BinopOp,
                      compiler::turboshaft::Simd256BinopOp::Kind::kF32x8Add>);
    // Add a F32x8 vector by a constant vector and store the result to memory.
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_F32(10.0f))),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(
                 temp3, WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp2))),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset, WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(
                 temp4, WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp2))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp3)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp4)),
             WASM_LOCAL_SET(temp5,
                            WASM_SIMD_F32x4_EXTRACT_LANE(
                                1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
             WASM_LOCAL_SET(temp6, WASM_SIMD_F32x4_EXTRACT_LANE(
                                       2, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param2)))),
             WASM_BINOP(kExprF32Add, WASM_LOCAL_GET(temp5),
                        WASM_LOCAL_GET(temp6))});
  }
  r.builder().WriteMemory(&memory[1], 1.0f);
  r.builder().WriteMemory(&memory[6], 2.0f);
  CHECK_EQ(23.0f, r.Call(0, 32));
}

TEST(RunWasmTurbofan_LoadStoreExtractRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<float, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmF32);
  uint8_t temp4 = r.AllocateLocal(kWasmF32);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone());
    // Load a F32x8 vector, calculate the Abs and store the result to memory.
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             temp2, WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1))),
         WASM_SIMD_STORE_MEM(
             WASM_LOCAL_GET(param2),
             WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp1))),
         WASM_SIMD_STORE_MEM_OFFSET(
             offset, WASM_LOCAL_GET(param2),
             WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp2))),
         WASM_LOCAL_SET(temp3,
                        WASM_SIMD_F32x4_EXTRACT_LANE(
                            1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
         WASM_LOCAL_SET(temp4, WASM_SIMD_F32x4_EXTRACT_LANE(
                                   2, WASM_SIMD_LOAD_MEM_OFFSET(
                                          offset, WASM_LOCAL_GET(param2)))),
         WASM_BINOP(kExprF32Add,
                    WASM_BINOP(kExprF32Add, WASM_LOCAL_GET(temp3),
                               WASM_LOCAL_GET(temp4)),
                    WASM_SIMD_F32x4_EXTRACT_LANE(2, WASM_LOCAL_GET(temp2)))});
  }
  r.builder().WriteMemory(&memory[1], -1.0f);
  r.builder().WriteMemory(&memory[6], 2.0f);
  CHECK_EQ(5.0f, r.Call(0, 32));
}

#ifdef V8_TARGET_ARCH_X64
TEST(RunWasmTurbofan_LoadStoreExtract2Revec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<float, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmF32);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone());
    // Load two F32x4 vectors, calculate the Abs and store to memory. Sum up the
    // two F32x4 vectors from both temp and memory. Revectorization still
    // succeeds as we can omit the lane 0 extract on x64.
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             temp2, WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1))),
         WASM_SIMD_STORE_MEM(
             WASM_LOCAL_GET(param2),
             WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp1))),
         WASM_SIMD_STORE_MEM_OFFSET(
             offset, WASM_LOCAL_GET(param2),
             WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp2))),
         WASM_LOCAL_SET(
             temp3,
             WASM_BINOP(kExprF32Add,
                        WASM_SIMD_F32x4_EXTRACT_LANE(
                            1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2))),
                        WASM_SIMD_F32x4_EXTRACT_LANE(
                            1, WASM_SIMD_LOAD_MEM_OFFSET(
                                   offset, WASM_LOCAL_GET(param2))))),
         WASM_BINOP(kExprF32Add, WASM_LOCAL_GET(temp3),
                    WASM_SIMD_F32x4_EXTRACT_LANE(
                        1, WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp1),
                                           WASM_LOCAL_GET(temp2))))});
  }
  r.builder().WriteMemory(&memory[1], 1.0f);
  r.builder().WriteMemory(&memory[5], -2.0f);
  CHECK_EQ(2.0f, r.Call(0, 32));
}

TEST(RunWasmTurbofan_ExtractCallParameterRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<float, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmF32);
  uint8_t temp6 = r.AllocateLocal(kWasmF32);
  constexpr uint8_t offset = 16;

  // Build the callee function.
  ValueType param_types[2] = {kWasmF32, kWasmS128};
  FunctionSig sig(1, 1, param_types);
  WasmFunctionCompiler& t = r.NewFunction(&sig);
  t.Build({WASM_SIMD_F32x4_EXTRACT_LANE(1, WASM_LOCAL_GET(0))});

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256BinopOp,
                      compiler::turboshaft::Simd256BinopOp::Kind::kF32x8Add>);
    // Add a F32x8 vector by a constant vector and store the result to memory.
    // Call a wasm function using the Simd128 result in temp3 as a parameter.
    // Revectorization still succeeds even we omit the lane 0 extract on x64.
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_F32(10.0f))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(temp3,
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp2))),
         WASM_LOCAL_SET(
             temp2, WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(temp4,
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp2))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp3)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp4)),
         WASM_LOCAL_SET(temp5,
                        WASM_SIMD_F32x4_EXTRACT_LANE(
                            1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
         WASM_LOCAL_SET(temp6, WASM_SIMD_F32x4_EXTRACT_LANE(
                                   2, WASM_SIMD_LOAD_MEM_OFFSET(
                                          offset, WASM_LOCAL_GET(param2)))),
         WASM_LOCAL_SET(temp5, WASM_BINOP(kExprF32Add, WASM_LOCAL_GET(temp5),
                                          WASM_LOCAL_GET(temp6))),
         WASM_BINOP(
             kExprF32Add, WASM_LOCAL_GET(temp5),
             WASM_CALL_FUNCTION(t.function_index(), WASM_LOCAL_GET(temp3)))});
  }
  r.builder().WriteMemory(&memory[1], 1.0f);
  r.builder().WriteMemory(&memory[6], 2.0f);
  CHECK_EQ(34.0f, r.Call(0, 32));
}

void RunExtractByShuffleRevecTest(
    const std::array<int8_t, kSimd128Size>& shuffle) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);

  // Build fn to add a i32x8 vector by a constant splat, shuffle the two i32x4
  // vectors and store the result to memory:
  // v128 temp1 = *a; v128 temp2 = *(a + 16);
  // v128 temp3 = temp1 + i32x4.splat(1);
  // v128 temp4 = temp2 + i32x4.splat(2);
  // v128 *(b) = temp3;
  // v128 *(b+16) = temp4;
  // v128 *(b+32) = i8x16.shuffle(temp2, temp1, shuffle)
  // temp1 and temp2 will be extracted from YMM registers, and extract of temp1
  // will be omitted due to alias to xmm at lower 128-bit. Use temp1 at input 1
  // that will be checked by CanBeSimd128Register.
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(80);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Binop>);
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset, WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(
                 temp3, WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_SIMD_I32x4_SPLAT(WASM_I32V(1)))),
             WASM_LOCAL_SET(
                 temp4, WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(temp2),
                                        WASM_SIMD_I32x4_SPLAT(WASM_I32V(1)))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp3)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp4)),
             WASM_SIMD_STORE_MEM_OFFSET(
                 offset * 2, WASM_LOCAL_GET(param2),
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                            WASM_LOCAL_GET(temp2),
                                            WASM_LOCAL_GET(temp1))),
             WASM_ONE});
  }

  for (int i = 0; i < 16; ++i) {
    r.builder().WriteMemory(&memory[i], static_cast<int8_t>(i + 16));
    r.builder().WriteMemory(&memory[i + 16], static_cast<int8_t>(i));
  }
  r.Call(0, 32);
  for (int i = 0; i < 16; ++i) {
    CHECK_EQ(shuffle[i], r.builder().ReadMemory(&memory[i + 64]));
  }
}

TEST(RunWasmTurbofan_Extract128LowForUnzipLow) {
  ShuffleMap::const_iterator it = test_shuffles.find(kS8x16UnzipLeft);
  DCHECK_NE(it, test_shuffles.end());
  // ExtractF128 used by S8x16UnzipLow and checked in ASSEMBLE_SIMD_INSTR.
  RunExtractByShuffleRevecTest(it->second);
}

TEST(RunWasmTurbofan_Extract128LowForUnpackLow) {
  // shuffle32x4 [0,4,1,5]
  const std::array<int8_t, 16> shuffle_unpack_low = {
      0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23};
  // ExtractF128 used by S32x4UnpackLow and checked in
  // ASSEMBLE_SIMD_PUNPCK_SHUFFLE.
  RunExtractByShuffleRevecTest(shuffle_unpack_low);
}

TEST(RunWasmTurbofan_Extract128LowForS32x4Shuffle) {
  // shuffle32x4 [0,1,2,4]
  const std::array<int8_t, 16> shuffle = {0, 1, 2,  3,  4,  5,  6,  7,
                                          8, 9, 10, 11, 16, 17, 18, 19};
  // ExtractF128 used by S32x4Shuffle and checked in ASSEMBLE_SIMD_IMM_INSTR.
  RunExtractByShuffleRevecTest(shuffle);
}

TEST(RunWasmTurbofan_Extract128LowForS16x8Blend) {
  const std::array<int8_t, 16> shuffle_16x8_blend = {
      0, 1, 18, 19, 4, 5, 22, 23, 8, 9, 26, 27, 12, 13, 30, 31};
  // ExtractF128 used by S16x8Blend and checked in ASSEMBLE_SIMD_IMM_SHUFFLE.
  RunExtractByShuffleRevecTest(shuffle_16x8_blend);
}

TEST(RunWasmTurbofan_LoadStoreOOBRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone());
    // Load a F32x8 vector, calculate the Abs and store the result to memory.
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset, WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM(
                 WASM_LOCAL_GET(param2),
                 WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 offset, WASM_LOCAL_GET(param2),
                 WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp2))),
             WASM_ONE});
  }
  r.builder().WriteMemory(&memory[1], -1.0f);
  r.builder().WriteMemory(&memory[6], 2.0f);
  CHECK_TRAP(r.Call(0, kWasmPageSize - 16));
  CHECK_EQ(1.0f,
           r.builder().ReadMemory(&memory[kWasmPageSize / sizeof(float) - 3]));
}
#endif  // V8_TARGET_ARCH_X64

TEST(RunWasmTurbofan_ReversedLoadStoreExtractRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<float, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmF32);
  uint8_t temp4 = r.AllocateLocal(kWasmF32);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone());
    // Load a F32x8 vector and store the result to memory in the order from the
    // high 128-bit address.
    r.Build(
        {WASM_LOCAL_SET(
             temp1, WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp1)),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp2)),
         WASM_LOCAL_SET(temp3,
                        WASM_SIMD_F32x4_EXTRACT_LANE(
                            1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
         WASM_LOCAL_SET(temp4, WASM_SIMD_F32x4_EXTRACT_LANE(
                                   2, WASM_SIMD_LOAD_MEM_OFFSET(
                                          offset, WASM_LOCAL_GET(param2)))),
         WASM_BINOP(kExprF32Add,
                    WASM_BINOP(kExprF32Add, WASM_LOCAL_GET(temp3),
                               WASM_LOCAL_GET(temp4)),
                    WASM_SIMD_F32x4_EXTRACT_LANE(1, WASM_LOCAL_GET(temp2)))});
  }
  r.builder().WriteMemory(&memory[1], 1.0f);
  r.builder().WriteMemory(&memory[6], 2.0f);
  CHECK_EQ(4.0f, r.Call(0, 32));
}

TEST(RunWasmTurbofan_ReturnUseSimd128Revec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<float, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmF32);
  constexpr uint8_t offset = 16;

  // Build the callee function.
  ValueType param_types[3] = {kWasmS128, kWasmI32, kWasmI32};
  FunctionSig sig(1, 2, param_types);
  WasmFunctionCompiler& t = r.NewFunction(&sig);
  uint8_t temp3 = t.AllocateLocal(kWasmS128);
  uint8_t temp4 = t.AllocateLocal(kWasmS128);

  TSSimd256VerifyScope ts_scope(r.zone());
  {
    // Load a F32x8 vector, calculate the Abs and store the result to memory.
    // Return the partial Simd128 result.
    t.Build({WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(temp4, WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset, WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM(
                 WASM_LOCAL_GET(param2),
                 WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp3))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 offset, WASM_LOCAL_GET(param2),
                 WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp4))),
             WASM_LOCAL_GET(temp4)});
  }

  r.Build({WASM_LOCAL_SET(temp1, WASM_CALL_FUNCTION(t.function_index(),
                                                    WASM_LOCAL_GET(param1),
                                                    WASM_LOCAL_GET(param2))),
           WASM_LOCAL_SET(temp2,
                          WASM_SIMD_F32x4_EXTRACT_LANE(
                              1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
           WASM_BINOP(kExprF32Add, WASM_LOCAL_GET(temp2),
                      WASM_SIMD_F32x4_EXTRACT_LANE(2, WASM_LOCAL_GET(temp1)))});

  r.builder().WriteMemory(&memory[1], -1.0f);
  r.builder().WriteMemory(&memory[6], 2.0f);
  CHECK_EQ(3.0f, r.Call(0, 32));
}

TEST(RunWasmTurbofan_TupleUseSimd128Revec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(16);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  // Build a callee function that returns multiple values, one of which is using
  // Simd128 type.
  ValueType param_types[5] = {kWasmI32, kWasmS128, kWasmI32, kWasmI32,
                              kWasmI32};
  FunctionSig sig(3, 2, param_types);
  WasmFunctionCompiler& t = r.NewFunction(&sig);
  std::array<uint8_t, kSimd128Size> one = {1};
  t.Build({WASM_LOCAL_GET(0), WASM_SIMD_CONSTANT(one), WASM_LOCAL_GET(1)});

  // Load a F32x8 vector, calculate the Abs and store the result to memory.
  // Call function t. The return values will be projected and used in TupleOp
  // with drop.
  TSSimd256VerifyScope ts_scope(r.zone());
  r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
           WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(
                                     offset, WASM_LOCAL_GET(param1))),
           WASM_SIMD_STORE_MEM(
               WASM_LOCAL_GET(param2),
               WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp1))),
           WASM_SIMD_STORE_MEM_OFFSET(
               offset, WASM_LOCAL_GET(param2),
               WASM_SIMD_UNOP(kExprF32x4Abs, WASM_LOCAL_GET(temp2))),
           WASM_CALL_FUNCTION(t.function_index(), WASM_LOCAL_GET(param2),
                              WASM_LOCAL_GET(param1)),
           WASM_DROP, WASM_DROP});

  r.builder().WriteMemory(&memory[1], -1.0f);
  r.builder().WriteMemory(&memory[6], 2.0f);
  CHECK_EQ(32, r.Call(0, 32));
  CHECK_EQ(1.0f, r.builder().ReadMemory(&memory[9]));
  CHECK_EQ(2.0f, r.builder().ReadMemory(&memory[14]));
}

TEST(RunWasmTurbofan_F32x4ShuffleForSplatRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<float, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory =
      r.builder().AddMemoryElems<float>(kWasmPageSize / sizeof(float));
  constexpr Shuffle splat_shuffle = {8, 9, 10, 11, 8, 9, 10, 11,
                                     8, 9, 10, 11, 8, 9, 10, 11};
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmF32);
  uint8_t temp6 = r.AllocateLocal(kWasmF32);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone());
    // Add a F32x8 vector to a splat shuffle vector and store the result to
    // memory.
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2))),
             WASM_LOCAL_SET(temp3,
                            WASM_SIMD_I8x16_SHUFFLE_OP(
                                kExprI8x16Shuffle, splat_shuffle,
                                WASM_LOCAL_GET(temp2), WASM_LOCAL_GET(temp2))),
             WASM_LOCAL_SET(
                 temp4, WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp3))),
             WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset, WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(
                 temp2, WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp3))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp4)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp2)),
             WASM_LOCAL_SET(temp5,
                            WASM_SIMD_F32x4_EXTRACT_LANE(
                                0, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
             WASM_LOCAL_SET(temp6, WASM_SIMD_F32x4_EXTRACT_LANE(
                                       3, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param2)))),
             WASM_BINOP(kExprF32Add, WASM_LOCAL_GET(temp5),
                        WASM_LOCAL_GET(temp6))});
  }
  r.builder().WriteMemory(&memory[0], 1.0f);
  r.builder().WriteMemory(&memory[7], 2.0f);
  r.builder().WriteMemory(&memory[10], 10.0f);
  CHECK_EQ(23.0f, r.Call(0, 32));
}

TEST(RunWasmTurbofan_I32x4ShuffleSplatRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(kWasmPageSize / sizeof(int32_t));
  constexpr Shuffle shuffle = {4,  5,  6,  7,  0, 1, 2,  3,
                               12, 13, 14, 15, 8, 9, 10, 11};
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmI32);
  uint8_t temp6 = r.AllocateLocal(kWasmI32);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone());
    // Add a F32x8 vector to a splat shuffle vector and store the result to
    // memory.
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2))),
             WASM_LOCAL_SET(
                 temp3, WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                                   WASM_LOCAL_GET(temp2),
                                                   WASM_LOCAL_GET(temp2))),
             WASM_LOCAL_SET(
                 temp4, WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp3))),
             WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset, WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(
                 temp2, WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp3))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp4)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp2)),
             WASM_LOCAL_SET(temp5,
                            WASM_SIMD_I32x4_EXTRACT_LANE(
                                0, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
             WASM_LOCAL_SET(temp6, WASM_SIMD_I32x4_EXTRACT_LANE(
                                       3, WASM_SIMD_LOAD_MEM_OFFSET(
                                              offset, WASM_LOCAL_GET(param2)))),
             WASM_BINOP(kExprI32Add, WASM_LOCAL_GET(temp5),
                        WASM_LOCAL_GET(temp6))});
  }
  r.builder().WriteMemory(&memory[0], 1);
  r.builder().WriteMemory(&memory[7], 2);
  r.builder().WriteMemory(&memory[9], 10);
  r.builder().WriteMemory(&memory[10], 10);
  CHECK_EQ(23, r.Call(0, 32));
}

TEST(RunWasmTurbofan_I64x2ShuffleForSplatRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int64_t> r(
      TestExecutionTier::kTurbofan);
  int64_t* memory = r.builder().AddMemoryElems<int64_t>(6);
  constexpr Shuffle splat_shuffle = {8, 9, 10, 11, 12, 13, 14, 15,
                                     8, 9, 10, 11, 12, 13, 14, 15};
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone(),
                                  TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
                                  ExpectedResult::kPass);
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(temp2,
                            WASM_SIMD_I8x16_SHUFFLE_OP(
                                kExprI8x16Shuffle, splat_shuffle,
                                WASM_LOCAL_GET(temp1), WASM_LOCAL_GET(temp1))),
             WASM_LOCAL_SET(temp3,
                            WASM_SIMD_BINOP(
                                kExprI64x2Add, WASM_LOCAL_GET(temp2),
                                WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(param3)))),
             WASM_LOCAL_SET(temp4,
                            WASM_SIMD_BINOP(
                                kExprI64x2Add, WASM_LOCAL_GET(temp2),
                                WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(param3)))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp3)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp4)),
             WASM_ONE});
  }

  FOR_INT64_INPUTS(x) {
    r.builder().WriteMemory(&memory[0], x);
    r.builder().WriteMemory(&memory[1], x);
    FOR_INT64_INPUTS(y) {
      r.Call(0, 16, y);
      for (int i = 0; i < 4; i++) {
        CHECK_EQ(x + y, r.builder().ReadMemory(&memory[i + 2]));
      }
    }
  }
}

TEST(RunWasmTurbofan_ShuffleVpshufd) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  int32_t* memory;
  auto build_fn = [&memory](WasmRunner<int32_t>& r,
                            const std::array<int8_t, 16>& shuffle,
                            enum ExpectedResult result) {
    memory = r.builder().AddMemoryElems<int32_t>(16);

    uint8_t temp1 = r.AllocateLocal(kWasmS128);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);

    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpcode<
            compiler::turboshaft::Opcode::kSimd256Shufd>,
        result);

    BUILD_AND_CHECK_REVEC_NODE(
        r, compiler::IrOpcode::kI8x32Shuffle,
        WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
        WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(16, WASM_ZERO)),

        WASM_SIMD_STORE_MEM_OFFSET(
            16 * 2, WASM_ZERO,
            WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                       WASM_LOCAL_GET(temp1),
                                       WASM_LOCAL_GET(temp1))),
        WASM_SIMD_STORE_MEM_OFFSET(
            16 * 3, WASM_ZERO,
            WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                       WASM_LOCAL_GET(temp2),
                                       WASM_LOCAL_GET(temp2))),
        WASM_ONE);
  };

  auto init_memory = [&memory](WasmRunner<int32_t>& r,
                               const std::array<int32_t, 8>& input) {
    for (int i = 0; i < 8; ++i) {
      r.builder().WriteMemory(&memory[i], input[i]);
    }
  };

  auto check_results = [&memory](WasmRunner<int32_t>& r,
                                 const std::array<int32_t, 8>& expected) {
    for (int i = 0; i < 8; ++i) {
      CHECK_EQ(expected[i], r.builder().ReadMemory(&memory[i + 8]));
    }
  };

  {
    constexpr std::array<int8_t, 16> shuffle = {4,  5,  6,  7,  8, 9, 10, 11,
                                                12, 13, 14, 15, 0, 1, 2,  3};
    constexpr std::array<int32_t, 8> input = {1, 2, 3, 4, 5, 6, 7, 8};
    constexpr std::array<int32_t, 8> expected = {2, 3, 4, 1, 6, 7, 8, 5};

    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    build_fn(r, shuffle, ExpectedResult::kPass);
    init_memory(r, input);
    r.Call();
    check_results(r, expected);
  }

  {
    constexpr std::array<int8_t, 16> shuffle = {4,  5,  6,  7,  8, 9, 10, 11,
                                                12, 13, 14, 15, 0, 0, 0,  0};
    constexpr std::array<int32_t, 8> input = {1, 2, 3, 4, 5, 6, 7, 8};
    constexpr std::array<int32_t, 8> expected = {2, 3, 4, 0x1010101,
                                                 6, 7, 8, 0x5050505};
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    build_fn(r, shuffle, ExpectedResult::kFail);
    init_memory(r, input);
    r.Call();
    check_results(r, expected);
  }
}

// Can't merge Shuffle(a, a) and shuffle(b,b)
// if a and b have different opcodes
TEST(RunWasmTurbofan_ShuffleVpshufdExpectFail) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(16);
  // I32x4, shuffle=[1,2,3,0]
  constexpr std::array<int8_t, 16> shuffle = {4,  5,  6,  7,  8, 9, 10, 11,
                                              12, 13, 14, 15, 0, 1, 2,  3};
  std::array<uint8_t, kSimd128Size> const_buffer = {5, 0, 0, 0, 6, 0, 0, 0,
                                                    7, 0, 0, 0, 8, 0, 0, 0};

  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpcode<
            compiler::turboshaft::Opcode::kSimd256Shufd>,
        ExpectedResult::kFail);

    BUILD_AND_CHECK_REVEC_NODE(
        r, compiler::IrOpcode::kI8x32Shuffle,
        WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
        WASM_LOCAL_SET(temp2, WASM_SIMD_CONSTANT(const_buffer)),

        WASM_SIMD_STORE_MEM_OFFSET(
            16 * 2, WASM_ZERO,
            WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                       WASM_LOCAL_GET(temp1),
                                       WASM_LOCAL_GET(temp1))),
        WASM_SIMD_STORE_MEM_OFFSET(
            16 * 3, WASM_ZERO,
            WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                       WASM_LOCAL_GET(temp2),
                                       WASM_LOCAL_GET(temp2))),
        WASM_ONE);
  }
  std::pair<std::vector<int>, std::vector<int>> test_case = {
      {1, 2, 3, 4, 5, 6, 7, 8}, {2, 3, 4, 1, 6, 7, 8, 5}};

  auto input = test_case.first;
  auto expected_output = test_case.second;

  for (int i = 0; i < 8; ++i) {
    r.builder().WriteMemory(&memory[i], input[i]);
  }

  r.Call();

  for (int i = 0; i < 8; ++i) {
    CHECK_EQ(expected_output[i], r.builder().ReadMemory(&memory[i + 8]));
  }
}

TEST(RunWasmTurbofan_I8x32ShuffleShufps) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(24);
  constexpr std::array<int8_t, 16> shuffle = {0,  1,  2,  3,  8,  9,  10, 11,
                                              16, 17, 18, 19, 24, 25, 26, 27};
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Shufps>);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
         WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(16, WASM_ZERO)),
         WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM_OFFSET(16 * 2, WASM_ZERO)),
         WASM_LOCAL_SET(temp4, WASM_SIMD_LOAD_MEM_OFFSET(16 * 3, WASM_ZERO)),

         WASM_SIMD_STORE_MEM_OFFSET(
             16 * 4, WASM_ZERO,
             WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                        WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp3))),

         WASM_SIMD_STORE_MEM_OFFSET(
             16 * 5, WASM_ZERO,
             WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                        WASM_LOCAL_GET(temp2),
                                        WASM_LOCAL_GET(temp4))),
         WASM_ONE});
  }
  std::vector<std::pair<std::vector<int>, std::vector<int>>> test_cases = {
      {{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {0, 2, 8, 10, 4, 6, 12, 14}}}};

  for (auto pair : test_cases) {
    auto input = pair.first;
    auto expected_output = pair.second;
    for (int i = 0; i < 16; ++i) {
      r.builder().WriteMemory(&memory[i], input[i]);
    }
    r.Call();
    for (int i = 0; i < 8; ++i) {
      CHECK_EQ(expected_output[i], r.builder().ReadMemory(&memory[i + 16]));
    }
  }
}

TEST(RunWasmTurbofan_I8x32ShuffleS32x8UnpackLow) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(24);
  // shuffle32x4 [0,4,1,5]
  constexpr std::array<int8_t, 16> shuffle = {0, 1, 2, 3, 16, 17, 18, 19,
                                              4, 5, 6, 7, 20, 21, 22, 23};
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unpack>);

    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
         WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(16, WASM_ZERO)),
         WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM_OFFSET(16 * 2, WASM_ZERO)),
         WASM_LOCAL_SET(temp4, WASM_SIMD_LOAD_MEM_OFFSET(16 * 3, WASM_ZERO)),

         WASM_SIMD_STORE_MEM_OFFSET(
             16 * 4, WASM_ZERO,
             WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                        WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp3))),
         WASM_SIMD_STORE_MEM_OFFSET(
             16 * 5, WASM_ZERO,
             WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                        WASM_LOCAL_GET(temp2),
                                        WASM_LOCAL_GET(temp4))),
         WASM_ONE});
  }
  std::vector<std::pair<std::vector<int>, std::vector<int>>> test_cases = {
      {{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {0, 8, 1, 9, 4, 12, 5, 13}}}};

  for (auto pair : test_cases) {
    auto input = pair.first;
    auto expected_output = pair.second;
    for (int i = 0; i < 16; ++i) {
      r.builder().WriteMemory(&memory[i], input[i]);
    }
    r.Call();
    for (int i = 0; i < 8; ++i) {
      CHECK_EQ(expected_output[i], r.builder().ReadMemory(&memory[i + 16]));
    }
  }
}

TEST(RunWasmTurbofan_I8x32ShuffleS32x8UnpackHigh) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(24);
  // shuffle32x4 [2,6,3,7]
  constexpr std::array<int8_t, 16> shuffle = {8,  9,  10, 11, 24, 25, 26, 27,
                                              12, 13, 14, 15, 28, 29, 30, 31};
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimd256Unpack>);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
         WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(16, WASM_ZERO)),
         WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM_OFFSET(16 * 2, WASM_ZERO)),
         WASM_LOCAL_SET(temp4, WASM_SIMD_LOAD_MEM_OFFSET(16 * 3, WASM_ZERO)),

         WASM_SIMD_STORE_MEM_OFFSET(
             16 * 4, WASM_ZERO,
             WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                        WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp3))),
         WASM_SIMD_STORE_MEM_OFFSET(
             16 * 5, WASM_ZERO,
             WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle,
                                        WASM_LOCAL_GET(temp2),
                                        WASM_LOCAL_GET(temp4))),
         WASM_ONE});
  }
  std::vector<std::pair<std::vector<int>, std::vector<int>>> test_cases = {
      {{{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
        {2, 10, 3, 11, 6, 14, 7, 15}}}};

  for (auto pair : test_cases) {
    auto input = pair.first;
    auto expected_output = pair.second;
    for (int i = 0; i < 16; ++i) {
      r.builder().WriteMemory(&memory[i], input[i]);
    }
    r.Call();
    for (int i = 0; i < 8; ++i) {
      CHECK_EQ(expected_output[i], r.builder().ReadMemory(&memory[i + 16]));
    }
  }
}

TEST(RunWasmTurbofan_ShuffleToS256Load8x8U) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(40);

  constexpr std::array<int8_t, 16> shuffle0 = {16, 1, 2,  3,  17, 5,  6,  7,
                                               18, 9, 10, 11, 19, 13, 14, 15};
  constexpr std::array<int8_t, 16> shuffle1 = {4, 17, 18, 19, 5, 21, 22, 23,
                                               6, 25, 26, 27, 7, 29, 30, 31};
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  std::array<uint8_t, kSimd128Size> all_zero = {0};

  {
    auto verify_s256load8x8u = [](const compiler::turboshaft::Graph& graph) {
      for (const compiler::turboshaft::Operation& op : graph.AllOperations()) {
        if (const compiler::turboshaft::Simd256LoadTransformOp* load_op =
                op.TryCast<compiler::turboshaft::Simd256LoadTransformOp>()) {
          if (load_op->transform_kind ==
              compiler::turboshaft::Simd256LoadTransformOp::TransformKind::
                  k8x8U) {
            return true;
          }
        }
      }
      return false;
    };

    TSSimd256VerifyScope ts_scope(r.zone(), verify_s256load8x8u);
    r.Build({WASM_LOCAL_SET(temp1,
                            WASM_SIMD_LOAD_OP(kExprS128Load64Zero, WASM_ZERO)),
             WASM_SIMD_STORE_MEM_OFFSET(
                 8, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle0,
                                            WASM_SIMD_CONSTANT(all_zero),
                                            WASM_LOCAL_GET(temp1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 24, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle1,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_SIMD_CONSTANT(all_zero))),
             WASM_ONE});
  }
  std::pair<std::vector<int8_t>, std::vector<int32_t>> test_case = {
      {0, 1, 2, 3, 4, 5, 6, -1}, {0, 1, 2, 3, 4, 5, 6, 255}};
  auto input = test_case.first;
  auto expected_output = test_case.second;
  for (int i = 0; i < 8; ++i) {
    r.builder().WriteMemory(&memory[i], input[i]);
  }
  r.Call();
  int32_t* memory_int32_t = reinterpret_cast<int32_t*>(memory);
  for (int i = 0; i < 8; ++i) {
    CHECK_EQ(expected_output[i],
             r.builder().ReadMemory(&memory_int32_t[i + 2]));
  }
}

// ShuffleToS256Load8x8U try to math the following pattern:
// a = S128Load64Zero(memory);
// b = S128Zero;
// c = S128Shuffle(a, b, s1);
// d = S128Shuffle(a, b, s2);
// where
// s1 = {0,x,x,x,  1,x,x,x,  2,x,x,x,  3,x,x,x};
// s2 = {4,x,x,x,  5,x,x,x,  6,x,x,x,  7,x,x,x};
// and x >= 16.
//
// All the conditions need to be met.
// ShuffleToS256Load8x8UExpectFail1 to ShuffleToS256Load8x8UExpectFail5 are the
// cases where the conditions are not met.

// Shuffle with same input e, shuffle(e, e, x).
TEST(RunWasmTurbofan_ShuffleToS256Load8x8UExpectFail1) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(48);

  constexpr std::array<int8_t, 16> shuffle0 = {4,  5,  6,  7,  8, 9, 10, 11,
                                               12, 13, 14, 15, 0, 1, 2,  3};
  constexpr std::array<int8_t, 16> shuffle1 = {8, 9, 10, 11, 12, 13, 14, 15,
                                               0, 1, 2,  3,  4,  5,  6,  7};
  uint8_t temp1 = r.AllocateLocal(kWasmS128);

  {
    TSSimd256VerifyScope ts_scope(r.zone(),
                                  TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
                                  ExpectedResult::kFail);
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle0,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_LOCAL_GET(temp1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 32, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle1,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_LOCAL_GET(temp1))),
             WASM_ONE});
  }
  for (int8_t i = 0; i < 16; ++i) {
    r.builder().WriteMemory(&memory[i], i);
  }
  r.Call();
  for (int i = 0; i < 16; ++i) {
    CHECK_EQ(shuffle0[i], r.builder().ReadMemory(&memory[16 + i]));
    CHECK_EQ(shuffle1[i], r.builder().ReadMemory(&memory[32 + i]));
  }
}

// Not the same left, c.left_idx != d.left_idx.
TEST(RunWasmTurbofan_ShuffleToS256Load8x8UExpectFail2) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(64);

  constexpr std::array<int8_t, 16> shuffle0 = {4,  5,  6,  17, 8, 9, 10, 11,
                                               22, 13, 14, 25, 0, 1, 2,  23};
  constexpr std::array<int8_t, 16> shuffle1 = {8,  9, 10, 11, 18, 13, 14, 15,
                                               20, 1, 2,  3,  4,  27, 6,  17};

  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  std::array<uint8_t, kSimd128Size> all_zero = {0};

  {
    TSSimd256VerifyScope ts_scope(r.zone(),
                                  TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
                                  ExpectedResult::kFail);
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(16, WASM_ZERO)),
             WASM_SIMD_STORE_MEM_OFFSET(
                 32, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle0,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_SIMD_CONSTANT(all_zero))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 48, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle1,
                                            WASM_LOCAL_GET(temp2),
                                            WASM_SIMD_CONSTANT(all_zero))),
             WASM_ONE});
  }
  for (int8_t i = 0; i < 32; ++i) {
    r.builder().WriteMemory(&memory[i], i);
  }
  r.Call();
  for (int i = 0; i < 16; ++i) {
    CHECK_EQ(shuffle0[i] >= 16 ? 0 : shuffle0[i],
             r.builder().ReadMemory(&memory[32 + i]));
    CHECK_EQ(shuffle1[i] >= 16 ? 0 : shuffle1[i] + 16,
             r.builder().ReadMemory(&memory[48 + i]));
  }
}

// Shuffle left is not Simd128LoadTransformOp, a != S128Load64Zero(memory).
TEST(RunWasmTurbofan_ShuffleToS256Load8x8UExpectFail3) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(48);

  constexpr std::array<int8_t, 16> shuffle0 = {4,  5,  6,  17, 8, 9, 10, 11,
                                               22, 13, 14, 25, 0, 1, 2,  23};
  constexpr std::array<int8_t, 16> shuffle1 = {8,  9, 10, 11, 18, 13, 14, 15,
                                               20, 1, 2,  3,  4,  27, 6,  17};
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  std::array<uint8_t, kSimd128Size> all_zero = {0};

  {
    TSSimd256VerifyScope ts_scope(r.zone(),
                                  TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
                                  ExpectedResult::kFail);
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle0,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_SIMD_CONSTANT(all_zero))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 32, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle1,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_SIMD_CONSTANT(all_zero))),
             WASM_ONE});
  }
  for (int8_t i = 0; i < 16; ++i) {
    r.builder().WriteMemory(&memory[i], i);
  }
  r.Call();
  for (int i = 0; i < 16; ++i) {
    CHECK_EQ(shuffle0[i] >= 16 ? all_zero[shuffle0[i] - 16] : shuffle0[i],
             r.builder().ReadMemory(&memory[16 + i]));
    CHECK_EQ(shuffle1[i] >= 16 ? all_zero[shuffle1[i] - 16] : shuffle1[i],
             r.builder().ReadMemory(&memory[32 + i]));
  }
}

// a = S128Load32Zero(memory), not S128Load64Zero(memory).
TEST(RunWasmTurbofan_ShuffleToS256Load8x8UExpectFail4) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(48);

  constexpr std::array<int8_t, 16> shuffle0 = {4,  5,  6,  17, 8, 9, 10, 11,
                                               22, 13, 14, 25, 0, 1, 2,  23};
  constexpr std::array<int8_t, 16> shuffle1 = {8,  9, 10, 11, 18, 13, 14, 15,
                                               20, 1, 2,  3,  4,  27, 6,  17};
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  std::array<uint8_t, kSimd128Size> all_zero = {0};

  {
    TSSimd256VerifyScope ts_scope(r.zone(),
                                  TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
                                  ExpectedResult::kFail);
    r.Build({WASM_LOCAL_SET(temp1,
                            WASM_SIMD_LOAD_OP(kExprS128Load32Zero, WASM_ZERO)),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle0,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_SIMD_CONSTANT(all_zero))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 32, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle1,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_SIMD_CONSTANT(all_zero))),
             WASM_ONE});
  }
  for (int8_t i = 0; i < 16; ++i) {
    r.builder().WriteMemory(&memory[i], i);
  }
  r.Call();
  for (int i = 0; i < 16; ++i) {
    CHECK_EQ(shuffle0[i] >= 4 ? 0 : shuffle0[i],
             r.builder().ReadMemory(&memory[16 + i]));
    CHECK_EQ(shuffle1[i] >= 4 ? 0 : shuffle1[i],
             r.builder().ReadMemory(&memory[32 + i]));
  }
}

// Shuffle indices s1/s2 don't met the conditions, or
// b != S128Zero.
TEST(RunWasmTurbofan_ShuffleToS256Load8x8UExpectFail5) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;

  int8_t* memory;
  auto build_fn = [&memory](WasmRunner<int8_t>& r,
                            const std::array<uint8_t, 16>& shuffle0,
                            const std::array<uint8_t, 16>& shuffle1,
                            const std::array<uint8_t, 16>& const_buf) {
    memory = r.builder().AddMemoryElems<int8_t>(48);
    uint8_t temp1 = r.AllocateLocal(kWasmS128);
    TSSimd256VerifyScope ts_scope(r.zone(),
                                  TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
                                  ExpectedResult::kFail);
    r.Build({WASM_LOCAL_SET(temp1,
                            WASM_SIMD_LOAD_OP(kExprS128Load64Zero, WASM_ZERO)),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle0,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_SIMD_CONSTANT(const_buf))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 32, WASM_ZERO,
                 WASM_SIMD_I8x16_SHUFFLE_OP(kExprI8x16Shuffle, shuffle1,
                                            WASM_LOCAL_GET(temp1),
                                            WASM_SIMD_CONSTANT(const_buf))),
             WASM_ONE});
  };

  auto init_memory = [&memory](WasmRunner<int8_t>& r) {
    for (int8_t i = 0; i < 16; ++i) {
      r.builder().WriteMemory(&memory[i], i);
    }
  };

  auto check_results = [&memory](WasmRunner<int8_t>& r,
                                 const std::array<uint8_t, 16>& shuffle0,
                                 const std::array<uint8_t, 16>& shuffle1,
                                 const std::array<uint8_t, 16>& const_buf) {
    for (int i = 0; i < 16; ++i) {
      CHECK_EQ(shuffle0[i] >= 16  ? const_buf[shuffle0[i] - 16]
               : shuffle0[i] >= 8 ? 0
                                  : memory[shuffle0[i]],
               r.builder().ReadMemory(&memory[16 + i]));
      CHECK_EQ(shuffle1[i] >= 16  ? const_buf[shuffle1[i] - 16]
               : shuffle1[i] >= 8 ? 0
                                  : memory[shuffle1[i]],
               r.builder().ReadMemory(&memory[32 + i]));
    }
  };

  {
    // shuffle[i] < 16, is_swizzle.
    constexpr std::array<uint8_t, 16> shuffle0 = {4,  5,  6,  7,  8, 9, 10, 11,
                                                  12, 13, 14, 15, 0, 1, 2,  3};
    constexpr std::array<uint8_t, 16> shuffle1 = {8, 9, 10, 11, 12, 13, 14, 15,
                                                  0, 1, 2,  3,  4,  5,  6,  7};
    constexpr std::array<uint8_t, kSimd128Size> const_buf = {0};

    WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
    build_fn(r, shuffle0, shuffle1, const_buf);
    init_memory(r);
    r.Call();
    check_results(r, shuffle0, shuffle1, const_buf);
  }

  {
    constexpr std::array<uint8_t, 16> shuffle0 = {1, 17, 18, 19, 1, 21, 22, 23,
                                                  2, 25, 26, 27, 3, 29, 30, 31};
    constexpr std::array<uint8_t, 16> shuffle1 = {5, 17, 18, 19, 5, 21, 22, 23,
                                                  6, 25, 26, 27, 7, 29, 30, 31};
    constexpr std::array<uint8_t, kSimd128Size> const_buf = {0};

    WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
    build_fn(r, shuffle0, shuffle1, const_buf);
    init_memory(r);
    r.Call();
    check_results(r, shuffle0, shuffle1, const_buf);
  }

  {
    constexpr std::array<uint8_t, 16> shuffle0 = {0, 1,  18, 19, 1, 21, 22, 23,
                                                  2, 25, 26, 27, 3, 29, 30, 31};
    constexpr std::array<uint8_t, 16> shuffle1 = {4, 1,  18, 19, 5, 21, 22, 23,
                                                  6, 25, 26, 27, 7, 29, 30, 31};
    constexpr std::array<uint8_t, kSimd128Size> const_buf = {0};

    WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
    build_fn(r, shuffle0, shuffle1, const_buf);
    init_memory(r);
    r.Call();
    check_results(r, shuffle0, shuffle1, const_buf);
  }

  {
    constexpr std::array<uint8_t, 16> shuffle0 = {4,  5,  6,  17, 8, 9, 10, 11,
                                                  22, 13, 14, 25, 0, 1, 2,  23};
    constexpr std::array<uint8_t, 16> shuffle1 = {
        8, 9, 10, 11, 18, 13, 14, 15, 20, 1, 2, 3, 4, 27, 6, 17};
    // b != S128Zero.
    constexpr std::array<uint8_t, kSimd128Size> const_buf = {1};

    WasmRunner<int8_t> r(TestExecutionTier::kTurbofan);
    build_fn(r, shuffle0, shuffle1, const_buf);
    init_memory(r);
    r.Call();
    check_results(r, shuffle0, shuffle1, const_buf);
  }
}

template <typename T, bool use_memory64 = false>
void RunLoadSplatRevecTest(WasmOpcode op, WasmOpcode bin_op,
                           compiler::IrOpcode::Value revec_opcode,
                           T (*expected_op)(T, T)) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  constexpr int lanes = 16 / sizeof(T);
  constexpr int mem_index = 64;  // LoadSplat from mem index 64 (bytes).
  constexpr uint8_t offset = 16;

  using index_type = std::conditional_t<use_memory64, int64_t, int32_t>;
  wasm::AddressType address_type =
      use_memory64 ? wasm::AddressType::kI64 : wasm::AddressType::kI32;
  T* memory = nullptr;
#define BUILD_LOADSPLAT(TYPE)                                                  \
  memory = r.builder().template AddMemoryElems<T>(kWasmPageSize / sizeof(T),   \
                                                  address_type);               \
  uint8_t temp1 = r.AllocateLocal(kWasmS128);                                  \
  uint8_t temp2 = r.AllocateLocal(kWasmS128);                                  \
  uint8_t temp3 = r.AllocateLocal(kWasmS128);                                  \
                                                                               \
  BUILD_AND_CHECK_REVEC_NODE(                                                  \
      r, revec_opcode,                                                         \
      WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_OP(op, WASM_LOCAL_GET(0))),         \
      WASM_LOCAL_SET(temp2,                                                    \
                     WASM_SIMD_BINOP(bin_op, WASM_SIMD_LOAD_MEM(TYPE(0)),      \
                                     WASM_LOCAL_GET(temp1))),                  \
      WASM_LOCAL_SET(                                                          \
          temp3,                                                               \
          WASM_SIMD_BINOP(bin_op, WASM_SIMD_LOAD_MEM_OFFSET(offset, TYPE(0)),  \
                          WASM_LOCAL_GET(temp1))),                             \
                                                                               \
      /* Store the result to the 32-th byte, which is 2*lanes-th element (size \
         T) of memory */                                                       \
      WASM_SIMD_STORE_MEM(TYPE(32), WASM_LOCAL_GET(temp2)),                    \
      WASM_SIMD_STORE_MEM_OFFSET(offset, TYPE(32), WASM_LOCAL_GET(temp3)),     \
      WASM_ONE);                                                               \
                                                                               \
  r.builder().WriteMemory(&memory[1], T(1));                                   \
  r.builder().WriteMemory(&memory[lanes + 1], T(1));

  {
    WasmRunner<int32_t, index_type> r(TestExecutionTier::kTurbofan);
    TSSimd256VerifyScope ts_scope(r.zone());
    if (use_memory64) {
      BUILD_LOADSPLAT(WASM_I64V)
    } else {
      BUILD_LOADSPLAT(WASM_I32V)
    }

    for (T x : compiler::ValueHelper::GetVector<T>()) {
      // 64-th byte in memory is 4*lanes-th element (size T) of memory.
      r.builder().WriteMemory(&memory[4 * lanes], x);
      r.Call(mem_index);
      T expected = expected_op(1, x);
      CHECK_EQ(expected, memory[2 * lanes + 1]);
      CHECK_EQ(expected, memory[3 * lanes + 1]);
    }
  }

  // Test for OOB.
  {
    WasmRunner<int32_t, index_type> r(TestExecutionTier::kTurbofan);
    TSSimd256VerifyScope ts_scope(r.zone());
    if (use_memory64) {
      BUILD_LOADSPLAT(WASM_I64V)
    } else {
      BUILD_LOADSPLAT(WASM_I32V)
    }

    // Load splats load sizeof(T) bytes.
    for (uint32_t load_offset = kWasmPageSize - (sizeof(T) - 1);
         load_offset < kWasmPageSize; ++load_offset) {
      CHECK_TRAP(r.Call(load_offset));
    }
  }
#undef BUILD_LOADSPLAT
}

TEST(RunWasmTurbofan_S256Load8Splat) {
  RunLoadSplatRevecTest<int8_t>(kExprS128Load8Splat, kExprI8x16Add,
                                compiler::IrOpcode::kI8x32Add,
                                base::AddWithWraparound);
}

TEST(RunWasmTurbofan_S256Load16Splat) {
  RunLoadSplatRevecTest<int16_t>(kExprS128Load16Splat, kExprI16x8Add,
                                 compiler::IrOpcode::kI16x16Add,
                                 base::AddWithWraparound);
}

TEST(RunWasmTurbofan_S256Load32Splat) {
  RunLoadSplatRevecTest<int32_t>(kExprS128Load32Splat, kExprI32x4Add,
                                 compiler::IrOpcode::kI32x8Add,
                                 base::AddWithWraparound);
}

TEST(RunWasmTurbofan_S256Load64Splat) {
  RunLoadSplatRevecTest<int64_t>(kExprS128Load64Splat, kExprI64x2Add,
                                 compiler::IrOpcode::kI64x4Add,
                                 base::AddWithWraparound);
}

TEST(RunWasmTurbofan_S256Load8SplatMemory64) {
  RunLoadSplatRevecTest<int8_t, true>(kExprS128Load8Splat, kExprI8x16Add,
                                      compiler::IrOpcode::kI8x32Add,
                                      base::AddWithWraparound);
}

TEST(RunWasmTurbofan_S256Load16SplatMemory64) {
  RunLoadSplatRevecTest<int16_t, true>(kExprS128Load16Splat, kExprI16x8Add,
                                       compiler::IrOpcode::kI16x16Add,
                                       base::AddWithWraparound);
}

TEST(RunWasmTurbofan_S256Load32SplatMemory64) {
  RunLoadSplatRevecTest<int32_t, true>(kExprS128Load32Splat, kExprI32x4Add,
                                       compiler::IrOpcode::kI32x8Add,
                                       base::AddWithWraparound);
}

TEST(RunWasmTurbofan_S256Load64SplatMemory64) {
  RunLoadSplatRevecTest<int64_t, true>(kExprS128Load64Splat, kExprI64x2Add,
                                       compiler::IrOpcode::kI64x4Add,
                                       base::AddWithWraparound);
}

template <typename S, typename T>
void RunLoadExtendRevecTest(WasmOpcode op) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  static_assert(sizeof(S) < sizeof(T),
                "load extend should go from smaller to larger type");
  constexpr int lanes_s = 16 / sizeof(S);
  constexpr int lanes_t = 16 / sizeof(T);
  constexpr uint8_t offset_s = 8;  // Load extend accesses 8 bytes value.
  constexpr uint8_t offset = 16;
  constexpr int mem_index = 0;  // Load from mem index 0 (bytes).

#define BUILD_LOADEXTEND(get_op, index)                                      \
  uint8_t temp1 = r.AllocateLocal(kWasmS128);                                \
  uint8_t temp2 = r.AllocateLocal(kWasmS128);                                \
                                                                             \
  BUILD_AND_CHECK_REVEC_NODE(                                                \
      r, compiler::IrOpcode::kStore,                                         \
      WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_OP(op, get_op(index))),           \
      WASM_LOCAL_SET(temp2,                                                  \
                     WASM_SIMD_LOAD_OP_OFFSET(op, get_op(index), offset_s)), \
                                                                             \
      /* Store the result to the 16-th byte, which is lanes-th element (size \
         S) of memory. */                                                    \
      WASM_SIMD_STORE_MEM(WASM_I32V(16), WASM_LOCAL_GET(temp1)),             \
      WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_I32V(16),                      \
                                 WASM_LOCAL_GET(temp2)),                     \
      WASM_ONE);

  {
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    TSSimd256VerifyScope ts_scope(r.zone());
    S* memory = r.builder().AddMemoryElems<S>(kWasmPageSize / sizeof(S));
    BUILD_LOADEXTEND(WASM_I32V, mem_index)

    for (S x : compiler::ValueHelper::GetVector<S>()) {
      for (int i = 0; i < lanes_s; i++) {
        r.builder().WriteMemory(&memory[i], x);
      }
      r.Call();
      for (int i = 0; i < 2 * lanes_t; i++) {
        CHECK_EQ(static_cast<T>(x), reinterpret_cast<T*>(&memory[lanes_s])[i]);
      }
    }
  }

  // Test for OOB.
  {
    WasmRunner<int32_t, uint32_t> r(TestExecutionTier::kTurbofan);
    TSSimd256VerifyScope ts_scope(r.zone());
    r.builder().AddMemoryElems<S>(kWasmPageSize / sizeof(S));
    BUILD_LOADEXTEND(WASM_LOCAL_GET, 0)

    // Load extends load 8 bytes, so should trap from -7.
    for (uint32_t load_offset = kWasmPageSize - 7; load_offset < kWasmPageSize;
         ++load_offset) {
      CHECK_TRAP(r.Call(load_offset));
    }
  }
}

TEST(S128Load8x8U) {
  RunLoadExtendRevecTest<uint8_t, uint16_t>(kExprS128Load8x8U);
}

TEST(S128Load8x8S) {
  RunLoadExtendRevecTest<int8_t, int16_t>(kExprS128Load8x8S);
}

TEST(S128Load16x4U) {
  RunLoadExtendRevecTest<uint16_t, uint32_t>(kExprS128Load16x4U);
}

TEST(S128Load16x4S) {
  RunLoadExtendRevecTest<int16_t, int32_t>(kExprS128Load16x4S);
}

TEST(S128Load32x2U) {
  RunLoadExtendRevecTest<uint32_t, uint64_t>(kExprS128Load32x2U);
}

TEST(S128Load32x2S) {
  RunLoadExtendRevecTest<int32_t, int64_t>(kExprS128Load32x2S);
}

TEST(RunWasmTurbofan_I8x32Splat) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int8_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(32);
  int8_t param1 = 0;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256SplatOp,
                      compiler::turboshaft::Simd256SplatOp::Kind::kI8x32>);
    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO,
                                 WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_ONE});
  }
  FOR_INT8_INPUTS(x) {
    r.Call(x);
    for (int i = 0; i < 32; ++i) {
      CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
    }
  }
}

TEST(RunWasmTurbofan_I16x16Splat) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int16_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(16);
  int16_t param1 = 0;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256SplatOp,
                      compiler::turboshaft::Simd256SplatOp::Kind::kI16x16>);
    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO,
                                 WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO, WASM_SIMD_I16x8_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_ONE});
  }
  FOR_INT16_INPUTS(x) {
    r.Call(x);
    for (int i = 0; i < 16; ++i) {
      CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
    }
  }
}

TEST(RunWasmTurbofan_I32x8Splat) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(8);
  int32_t param1 = 0;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256SplatOp,
                      compiler::turboshaft::Simd256SplatOp::Kind::kI32x8>);
    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO,
                                 WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_ONE});
  }

  FOR_INT32_INPUTS(x) {
    r.Call(x);
    for (int i = 0; i < 8; ++i) {
      CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
    }
  }
}

TEST(RunWasmTurbofan_I32x8SplatConst) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(8);
  constexpr int32_t x = 5;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256SplatOp,
                      compiler::turboshaft::Simd256SplatOp::Kind::kI32x8>);
    r.Build(
        {WASM_SIMD_STORE_MEM(WASM_ZERO, WASM_SIMD_I32x4_SPLAT(WASM_I32V(x))),
         WASM_SIMD_STORE_MEM_OFFSET(16, WASM_ZERO,
                                    WASM_SIMD_I32x4_SPLAT(WASM_I32V(x))),
         WASM_ONE});
  }

  r.Call();
  for (int i = 0; i < 8; ++i) {
    CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
  }
}

TEST(RunWasmTurbofan_I64x4Splat) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int64_t> r(TestExecutionTier::kTurbofan);
  int64_t* memory = r.builder().AddMemoryElems<int64_t>(4);
  int64_t param1 = 0;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256SplatOp,
                      compiler::turboshaft::Simd256SplatOp::Kind::kI64x4>);
    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO,
                                 WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO, WASM_SIMD_I64x2_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_ONE});
  }

  FOR_INT64_INPUTS(x) {
    r.Call(x);
    for (int i = 0; i < 4; ++i) {
      CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
    }
  }
}

TEST(RunWasmTurbofan_F32x8Splat) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, float> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(8);
  float param1 = 0;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256SplatOp,
                      compiler::turboshaft::Simd256SplatOp::Kind::kF32x8>);
    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO,
                                 WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO, WASM_SIMD_F32x4_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_ONE});
  }

  FOR_FLOAT32_INPUTS(x) {
    r.Call(x);
    for (int i = 0; i < 8; ++i) {
      if (std::isnan(x)) {
        CHECK(std::isnan(r.builder().ReadMemory(&memory[i])));
      } else {
        CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
      }
    }
  }
}

TEST(RunWasmTurbofan_F64x4Splat) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, double> r(TestExecutionTier::kTurbofan);
  double* memory = r.builder().AddMemoryElems<double>(4);
  double param1 = 0;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256SplatOp,
                      compiler::turboshaft::Simd256SplatOp::Kind::kF64x4>);
    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO,
                                 WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO, WASM_SIMD_F64x2_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_ONE});
  }

  FOR_FLOAT64_INPUTS(x) {
    r.Call(x);
    for (int i = 0; i < 4; ++i) {
      if (std::isnan(x)) {
        CHECK(std::isnan(r.builder().ReadMemory(&memory[i])));
      } else {
        CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
      }
    }
  }
}

TEST(RunWasmTurbofan_Phi) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  constexpr int32_t iteration = 8;
  constexpr uint32_t lanes = kSimd128Size / sizeof(int32_t);
  constexpr int32_t count = 2 * iteration * lanes;
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(count);
  // Build fn perform add on 128 bit vectors a, store the result in b:
  // int32_t func(simd128* a, simd128* b) {
  //   simd128 sum1 = sum2 = 0;
  //   for (int i = 0; i < 8; i++) {
  //     sum1 += *a;
  //     sum2 += *(a+1);
  //     a += 2;
  //   }
  //   *b = sum1;
  //   *(b+1) = sum2;
  // }
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t index = r.AllocateLocal(kWasmI32);
  uint8_t sum1 = r.AllocateLocal(kWasmS128);
  uint8_t sum2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone());
    BUILD_AND_CHECK_REVEC_NODE(
        r, compiler::IrOpcode::kPhi, WASM_LOCAL_SET(index, WASM_I32V(0)),
        WASM_LOCAL_SET(sum1, WASM_SIMD_I32x4_SPLAT(WASM_I32V(0))),
        WASM_LOCAL_SET(sum2, WASM_LOCAL_GET(sum1)),
        WASM_LOOP(
            WASM_LOCAL_SET(
                sum1,
                WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(sum1),
                                WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)))),
            WASM_LOCAL_SET(
                sum2, WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(sum2),
                                      WASM_SIMD_LOAD_MEM_OFFSET(
                                          offset, WASM_LOCAL_GET(param1)))),
            WASM_IF(WASM_I32_LTS(WASM_INC_LOCAL(index), WASM_I32V(iteration)),
                    WASM_BR(1))),
        WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(sum1)),
        WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                   WASM_LOCAL_GET(sum2)),
        WASM_ONE);
  }
  for (int32_t x : compiler::ValueHelper::GetVector<int32_t>()) {
    for (int32_t y : compiler::ValueHelper::GetVector<int32_t>()) {
      for (int32_t i = 0; i < iteration; i++) {
        for (uint32_t j = 0; j < lanes; j++) {
          r.builder().WriteMemory(&memory[i * 2 * lanes + j], x);
          r.builder().WriteMemory(&memory[i * 2 * lanes + j + lanes], y);
        }
      }
      r.Call(0, iteration * 2 * kSimd128Size);
      int32_t* output = reinterpret_cast<int32_t*>(memory + count);
      for (uint32_t i = 0; i < lanes; i++) {
        CHECK_EQ(x * iteration, output[i]);
        CHECK_EQ(y * iteration, output[i + lanes]);
      }
    }
  }
}

TEST(RunWasmTurbofan_ForcePackIdenticalLoad) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(16);
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    // Load from [0:15], the two loads are indentical.
    r.Build({WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
             WASM_LOCAL_SET(
                 temp1, WASM_SIMD_UNOP(kExprI32x4Abs,
                                       WASM_SIMD_UNOP(kExprS128Not,
                                                      WASM_LOCAL_GET(temp3)))),
             WASM_LOCAL_SET(
                 temp2, WASM_SIMD_UNOP(kExprI32x4Abs,
                                       WASM_SIMD_UNOP(kExprS128Not,
                                                      WASM_LOCAL_GET(temp3)))),

             WASM_SIMD_STORE_MEM_OFFSET(16, WASM_ZERO, WASM_LOCAL_GET(temp1)),
             WASM_SIMD_STORE_MEM_OFFSET(32, WASM_ZERO, WASM_LOCAL_GET(temp2)),

             WASM_ONE});
  }
  FOR_INT32_INPUTS(x) {
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[13], x);
    r.Call();
    int32_t expected = std::abs(~x);
    CHECK_EQ(expected, memory[5]);
    CHECK_EQ(expected, memory[9]);
  }
}

TEST(RunWasmTurbofan_ForcePackLoadsAtSameAddr) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(16);
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    // Load from [0:15], the two loads are identical.
    r.Build({WASM_LOCAL_SET(
                 temp1,
                 WASM_SIMD_UNOP(kExprI32x4Abs,
                                WASM_SIMD_UNOP(kExprS128Not,
                                               WASM_SIMD_LOAD_MEM(WASM_ZERO)))),
             WASM_LOCAL_SET(
                 temp2,
                 WASM_SIMD_UNOP(kExprI32x4Abs,
                                WASM_SIMD_UNOP(kExprS128Not,
                                               WASM_SIMD_LOAD_MEM(WASM_ZERO)))),

             WASM_SIMD_STORE_MEM_OFFSET(16, WASM_ZERO, WASM_LOCAL_GET(temp1)),
             WASM_SIMD_STORE_MEM_OFFSET(32, WASM_ZERO, WASM_LOCAL_GET(temp2)),

             WASM_ONE});
  }
  FOR_INT32_INPUTS(x) {
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[13], x);
    r.Call();
    int32_t expected = std::abs(~x);
    CHECK_EQ(expected, memory[5]);
    CHECK_EQ(expected, memory[9]);
  }
}

TEST(RunWasmTurbofan_ForcePackInContinuousLoad) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(16);
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    // Load from [0:15] and [48:63] which are incontinuous, calculate the data
    // by Not and Abs and stores the results to [16:31] and [32:47] which are
    // continuous. By force-packing the incontinuous loads, we still revectorize
    // all the operations.
    //   simd128 *a,*b;
    //   simd128 temp1 = abs(!(*a));
    //   simd128 temp2 = abs(!(*(a + 3)));
    //   *b = temp1;
    //   *(b+1) = temp2;
    r.Build({WASM_LOCAL_SET(
                 temp1,
                 WASM_SIMD_UNOP(kExprI32x4Abs,
                                WASM_SIMD_UNOP(kExprS128Not,
                                               WASM_SIMD_LOAD_MEM(WASM_ZERO)))),
             WASM_LOCAL_SET(
                 temp2, WASM_SIMD_UNOP(kExprI32x4Abs,
                                       WASM_SIMD_UNOP(kExprS128Not,
                                                      WASM_SIMD_LOAD_MEM_OFFSET(
                                                          48, WASM_ZERO)))),

             WASM_SIMD_STORE_MEM_OFFSET(16, WASM_ZERO, WASM_LOCAL_GET(temp1)),
             WASM_SIMD_STORE_MEM_OFFSET(32, WASM_ZERO, WASM_LOCAL_GET(temp2)),

             WASM_ONE});
  }
  FOR_INT32_INPUTS(x) {
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[13], 2 * x);
    r.Call();
    CHECK_EQ(std::abs(~x), memory[5]);
    CHECK_EQ(std::abs(~(2 * x)), memory[9]);
  }
}

TEST(RunWasmTurbofan_ForcePackIncontinuousLoadsReversed) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(16);
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    // Loads from [48:63] and [0:15] which are incontinuous, calculate the data
    // by Not and Abs and stores the results in reversed order to [16:31] and
    // [32:47] which are continuous. By force-packing the incontinuous loads, we
    // still revectorize all the operations.
    //   simd128 *a,*b;
    //   simd128 temp1 = abs(!(*(a + 3)));
    //   simd128 temp2 = abs(!(*a));
    //   *b = temp2;
    //   *(b+1) = temp1;
    r.Build({WASM_LOCAL_SET(
                 temp1, WASM_SIMD_UNOP(kExprI32x4Abs,
                                       WASM_SIMD_UNOP(kExprS128Not,
                                                      WASM_SIMD_LOAD_MEM_OFFSET(
                                                          48, WASM_ZERO)))),
             WASM_LOCAL_SET(
                 temp2,
                 WASM_SIMD_UNOP(kExprI32x4Abs,
                                WASM_SIMD_UNOP(kExprS128Not,
                                               WASM_SIMD_LOAD_MEM(WASM_ZERO)))),
             WASM_SIMD_STORE_MEM_OFFSET(16, WASM_ZERO, WASM_LOCAL_GET(temp2)),
             WASM_SIMD_STORE_MEM_OFFSET(32, WASM_ZERO, WASM_LOCAL_GET(temp1)),
             WASM_ONE});
  }
  FOR_INT32_INPUTS(x) {
    r.builder().WriteMemory(&memory[1], x);
    r.builder().WriteMemory(&memory[14], 2 * x);
    r.Call();
    CHECK_EQ(std::abs(~x), memory[5]);
    CHECK_EQ(std::abs(~(2 * x)), memory[10]);
  }
}

TEST(RunWasmTurbofan_RevecReduce) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int64_t, int32_t> r(TestExecutionTier::kTurbofan);
  uint32_t count = 8;
  int64_t* memory = r.builder().AddMemoryElems<int64_t>(count);
  // Build fn perform sum up 128 bit vectors a, return the result:
  // int64_t sum(simd128* a) {
  //   simd128 sum128 = a[0] + a[1] + a[2] + a[3];
  //   return LANE(sum128, 0) + LANE(sum128, 1);
  // }
  uint8_t param1 = 0;
  uint8_t sum1 = r.AllocateLocal(kWasmS128);
  uint8_t sum2 = r.AllocateLocal(kWasmS128);
  uint8_t sum = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone());
    r.Build(
        {WASM_LOCAL_SET(
             sum1, WASM_SIMD_BINOP(kExprI64x2Add,
                                   WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                                   WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset * 2, WASM_LOCAL_GET(param1)))),
         WASM_LOCAL_SET(
             sum2, WASM_SIMD_BINOP(kExprI64x2Add,
                                   WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset, WASM_LOCAL_GET(param1)),
                                   WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset * 3, WASM_LOCAL_GET(param1)))),
         WASM_LOCAL_SET(sum,
                        WASM_SIMD_BINOP(kExprI64x2Add, WASM_LOCAL_GET(sum1),
                                        WASM_LOCAL_GET(sum2))),
         WASM_I64_ADD(WASM_SIMD_I64x2_EXTRACT_LANE(0, WASM_LOCAL_GET(sum)),
                      WASM_SIMD_I64x2_EXTRACT_LANE(1, WASM_LOCAL_GET(sum)))});
  }
  for (int64_t x : compiler::ValueHelper::GetVector<int64_t>()) {
    for (uint32_t i = 0; i < count; i++) {
      r.builder().WriteMemory(&memory[i], x);
    }
    int64_t expected = count * x;
    CHECK_EQ(r.Call(0), expected);
  }
}

TEST(RunWasmTurbofan_ForcePackLoadSplat) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  // Use Load32Splat for the force packing test.

  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(10);
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    r.Build({WASM_LOCAL_SET(
                 temp1, WASM_SIMD_UNOP(kExprI32x4Abs,
                                       WASM_SIMD_UNOP(kExprS128Not,
                                                      WASM_SIMD_LOAD_OP(
                                                          kExprS128Load32Splat,
                                                          WASM_ZERO)))),
             WASM_LOCAL_SET(
                 temp2, WASM_SIMD_UNOP(kExprI32x4Abs,
                                       WASM_SIMD_UNOP(kExprS128Not,
                                                      WASM_SIMD_LOAD_OP_OFFSET(
                                                          kExprS128Load32Splat,
                                                          WASM_ZERO, 4)))),

             WASM_SIMD_STORE_MEM_OFFSET(8, WASM_ZERO, WASM_LOCAL_GET(temp1)),
             WASM_SIMD_STORE_MEM_OFFSET(24, WASM_ZERO, WASM_LOCAL_GET(temp2)),

             WASM_ONE});
  }

  FOR_INT32_INPUTS(x) {
    FOR_INT32_INPUTS(y) {
      r.builder().WriteMemory(&memory[0], x);
      r.builder().WriteMemory(&memory[1], y);
      r.Call();
      int expected_x = std::abs(~x);
      int expected_y = std::abs(~y);
      for (int i = 0; i < 4; ++i) {
        CHECK_EQ(expected_x, memory[i + 2]);
        CHECK_EQ(expected_y, memory[i + 6]);
      }
    }
  }
}

TEST(RunWasmTurbofan_ForcePackLoadExtend) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  // Use load32x2_s for the force packing test.
  {
    // Test ForcePackType::kSplat
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(10);
    uint8_t temp1 = r.AllocateLocal(kWasmS128);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);
    {
      TSSimd256VerifyScope ts_scope(
          r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                        compiler::turboshaft::Opcode::kSimdPack128To256>);
      r.Build(
          {WASM_LOCAL_SET(
               temp1, WASM_SIMD_SHIFT_OP(
                          kExprI64x2Shl,
                          WASM_SIMD_UNOP(
                              kExprS128Not,
                              WASM_SIMD_LOAD_OP(kExprS128Load32x2S, WASM_ZERO)),
                          WASM_I32V(1))),
           WASM_LOCAL_SET(
               temp2, WASM_SIMD_SHIFT_OP(
                          kExprI64x2Shl,
                          WASM_SIMD_UNOP(
                              kExprS128Not,
                              WASM_SIMD_LOAD_OP(kExprS128Load32x2S, WASM_ZERO)),
                          WASM_I32V(1))),

           WASM_SIMD_STORE_MEM_OFFSET(8, WASM_ZERO, WASM_LOCAL_GET(temp1)),
           WASM_SIMD_STORE_MEM_OFFSET(24, WASM_ZERO, WASM_LOCAL_GET(temp2)),

           WASM_ONE});
    }

    FOR_INT32_INPUTS(x) {
      FOR_INT32_INPUTS(y) {
        r.builder().WriteMemory(&memory[0], x);
        r.builder().WriteMemory(&memory[1], y);
        r.Call();
        const int64_t expected_x =
            LogicalShiftLeft(~static_cast<int64_t>(x), 1);
        const int64_t expected_y =
            LogicalShiftLeft(~static_cast<int64_t>(y), 1);
        const int64_t* const output_mem =
            reinterpret_cast<const int64_t*>(&memory[2]);
        for (int i = 0; i < 2; ++i) {
          const int64_t actual_x = output_mem[i * 2];
          const int64_t actual_y = output_mem[i * 2 + 1];
          CHECK_EQ(expected_x, actual_x);
          CHECK_EQ(expected_y, actual_y);
        }
      }
    }
  }

  {
    // Test ForcePackType::kGeneral
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(12);
    uint8_t temp1 = r.AllocateLocal(kWasmS128);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);
    {
      // incontinuous load32x2_s
      TSSimd256VerifyScope ts_scope(
          r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                        compiler::turboshaft::Opcode::kSimdPack128To256>);
      r.Build(
          {WASM_LOCAL_SET(
               temp1, WASM_SIMD_SHIFT_OP(
                          kExprI64x2ShrU,
                          WASM_SIMD_UNOP(
                              kExprS128Not,
                              WASM_SIMD_LOAD_OP(kExprS128Load32x2S, WASM_ZERO)),
                          WASM_I32V(1))),
           WASM_LOCAL_SET(
               temp2, WASM_SIMD_SHIFT_OP(
                          kExprI64x2ShrU,
                          WASM_SIMD_UNOP(kExprS128Not, WASM_SIMD_LOAD_OP_OFFSET(
                                                           kExprS128Load32x2S,
                                                           WASM_ZERO, 40)),
                          WASM_I32V(1))),

           WASM_SIMD_STORE_MEM_OFFSET(8, WASM_ZERO, WASM_LOCAL_GET(temp1)),
           WASM_SIMD_STORE_MEM_OFFSET(24, WASM_ZERO, WASM_LOCAL_GET(temp2)),

           WASM_ONE});
    }
    FOR_INT32_INPUTS(a) {
      FOR_INT32_INPUTS(b) {
        // Don't loop over setting c and d, because an O(n^4) test takes too
        // much time.
        int32_t c = a + b;
        int32_t d = a - b;
        r.builder().WriteMemory(&memory[0], a);
        r.builder().WriteMemory(&memory[1], b);
        r.builder().WriteMemory(&memory[10], c);
        r.builder().WriteMemory(&memory[11], d);
        r.Call();
        const int64_t expected_a =
            LogicalShiftRight(~static_cast<int64_t>(a), 1);
        const int64_t expected_b =
            LogicalShiftRight(~static_cast<int64_t>(b), 1);
        const int64_t expected_c =
            LogicalShiftRight(~static_cast<int64_t>(c), 1);
        const int64_t expected_d =
            LogicalShiftRight(~static_cast<int64_t>(d), 1);
        const int64_t* const output_mem =
            reinterpret_cast<const int64_t*>(&memory[2]);
        const int64_t actual_a = output_mem[0];
        const int64_t actual_b = output_mem[1];
        const int64_t actual_c = output_mem[2];
        const int64_t actual_d = output_mem[3];
        CHECK_EQ(expected_a, actual_a);
        CHECK_EQ(expected_b, actual_b);
        CHECK_EQ(expected_c, actual_c);
        CHECK_EQ(expected_d, actual_d);
      }
    }
  }
}

namespace {

bool IsLowHalfExtensionOp(WasmOpcode opcode) {
  switch (opcode) {
    case kExprI16x8UConvertI8x16Low:
    case kExprI16x8SConvertI8x16Low:
    case kExprI32x4UConvertI16x8Low:
    case kExprI32x4SConvertI16x8Low:
    case kExprI64x2UConvertI32x4Low:
    case kExprI64x2SConvertI32x4Low:
      return true;
    case kExprI16x8UConvertI8x16High:
    case kExprI16x8SConvertI8x16High:
    case kExprI32x4UConvertI16x8High:
    case kExprI32x4SConvertI16x8High:
    case kExprI64x2UConvertI32x4High:
    case kExprI64x2SConvertI32x4High:
      return false;
    default:
      UNREACHABLE();
  }
}

}  // namespace

template <typename S, typename T>
void RunIntToIntExtensionRevecForcePack(
    WasmOpcode opcode1, WasmOpcode opcode2,
    ExpectedResult revec_result = ExpectedResult::kPass) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  static_assert(sizeof(T) == 2 * sizeof(S),
                "the element size of dst vector must be twice of src vector in "
                "integer to integer extension");
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);

  // v128 a = v128.load(param1);
  // v128 b = v128.not(v128.not(opcode1(a));
  // v128 c = v128.not(v128.not(opcode2(a));
  // v128.store(param2, b);
  // v128.store(param2 + 16, c);
  // Where opcode1 and opcode2 are int to int extension opcodes, the two
  // v128.not are used to make sure revec is beneficial in revec cost estimation
  // steps.
  uint32_t count = 3 * kSimd128Size / sizeof(S);
  S* memory = r.builder().AddMemoryElems<S>(count);

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpcode<
            compiler::turboshaft::Opcode::kSimdPack128To256>,
        revec_result);
    r.Build(
        {WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             temp1, WASM_SIMD_UNOP(
                        kExprS128Not,
                        WASM_SIMD_UNOP(
                            kExprS128Not,
                            WASM_SIMD_UNOP(opcode1, WASM_LOCAL_GET(temp3))))),
         WASM_LOCAL_SET(
             temp2, WASM_SIMD_UNOP(
                        kExprS128Not,
                        WASM_SIMD_UNOP(
                            kExprS128Not,
                            WASM_SIMD_UNOP(opcode2, WASM_LOCAL_GET(temp3))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp2)),
         WASM_ONE});
  }

  constexpr uint32_t lanes = kSimd128Size / sizeof(S);
  for (S x : compiler::ValueHelper::GetVector<S>()) {
    for (S y : compiler::ValueHelper::GetVector<S>()) {
      for (uint32_t i = 0; i < lanes / 2; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + lanes / 2], y);
      }
      r.Call(0, 16);
      T expected_low = static_cast<T>(x);
      T expected_high = static_cast<T>(y);
      T* output = reinterpret_cast<T*>(memory + lanes);
      for (uint32_t i = 0; i < lanes / 2; i++) {
        CHECK_EQ(IsLowHalfExtensionOp(opcode1) ? expected_low : expected_high,
                 output[i]);
        CHECK_EQ(IsLowHalfExtensionOp(opcode2) ? expected_low : expected_high,
                 output[lanes / 2 + i]);
      }
    }
  }
}

// (low, low) unsign extend, revec succeed.
// (low, low) sign extend, revec succeed.
// (high, high) unsign extend, revec succeed.
// (high, high) sign extend, revec succeed.
// (high, low) unsign extend, revec failed, not supported yet.
// (high, low) sign extend, revec failed, not supported yet.
TEST(RunWasmTurbofan_ForcePackIntToIntExtension) {
  // Extend 8 bits to 16 bits.
  RunIntToIntExtensionRevecForcePack<uint8_t, uint16_t>(
      kExprI16x8UConvertI8x16Low, kExprI16x8UConvertI8x16Low);
  RunIntToIntExtensionRevecForcePack<int8_t, int16_t>(
      kExprI16x8SConvertI8x16Low, kExprI16x8SConvertI8x16Low);
  RunIntToIntExtensionRevecForcePack<uint8_t, uint16_t>(
      kExprI16x8UConvertI8x16High, kExprI16x8UConvertI8x16High);
  RunIntToIntExtensionRevecForcePack<int8_t, int16_t>(
      kExprI16x8SConvertI8x16High, kExprI16x8SConvertI8x16High);
  RunIntToIntExtensionRevecForcePack<uint8_t, uint16_t>(
      kExprI16x8UConvertI8x16High, kExprI16x8UConvertI8x16Low,
      ExpectedResult::kFail);
  RunIntToIntExtensionRevecForcePack<int8_t, int16_t>(
      kExprI16x8SConvertI8x16High, kExprI16x8SConvertI8x16Low,
      ExpectedResult::kFail);

  // Extend 16 bits to 32 bits.
  RunIntToIntExtensionRevecForcePack<uint16_t, uint32_t>(
      kExprI32x4UConvertI16x8Low, kExprI32x4UConvertI16x8Low);
  RunIntToIntExtensionRevecForcePack<int16_t, int32_t>(
      kExprI32x4SConvertI16x8Low, kExprI32x4SConvertI16x8Low);
  RunIntToIntExtensionRevecForcePack<uint16_t, uint32_t>(
      kExprI32x4UConvertI16x8High, kExprI32x4UConvertI16x8High);
  RunIntToIntExtensionRevecForcePack<int16_t, int32_t>(
      kExprI32x4SConvertI16x8High, kExprI32x4SConvertI16x8High);
  RunIntToIntExtensionRevecForcePack<uint16_t, uint32_t>(
      kExprI32x4UConvertI16x8High, kExprI32x4UConvertI16x8Low,
      ExpectedResult::kFail);
  RunIntToIntExtensionRevecForcePack<int16_t, int32_t>(
      kExprI32x4SConvertI16x8High, kExprI32x4SConvertI16x8Low,
      ExpectedResult::kFail);

  // Extend 32 bits to 64 bits.
  RunIntToIntExtensionRevecForcePack<uint32_t, uint64_t>(
      kExprI64x2UConvertI32x4Low, kExprI64x2UConvertI32x4Low);
  RunIntToIntExtensionRevecForcePack<int32_t, int64_t>(
      kExprI64x2SConvertI32x4Low, kExprI64x2SConvertI32x4Low);
  RunIntToIntExtensionRevecForcePack<uint32_t, uint64_t>(
      kExprI64x2UConvertI32x4High, kExprI64x2UConvertI32x4High);
  RunIntToIntExtensionRevecForcePack<int32_t, int64_t>(
      kExprI64x2SConvertI32x4High, kExprI64x2SConvertI32x4High);
  RunIntToIntExtensionRevecForcePack<uint32_t, uint64_t>(
      kExprI64x2UConvertI32x4High, kExprI64x2UConvertI32x4Low,
      ExpectedResult::kFail);
  RunIntToIntExtensionRevecForcePack<int32_t, int64_t>(
      kExprI64x2SConvertI32x4High, kExprI64x2SConvertI32x4Low,
      ExpectedResult::kFail);
}

// Similar with RunIntToIntExtensionRevecForcePack, but two stores share an int
// to int extension op.
template <typename S, typename T>
void RunIntToIntExtensionRevecForcePackSplat(WasmOpcode opcode) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  static_assert(sizeof(T) == 2 * sizeof(S),
                "the element size of dst vector must be twice of src vector in "
                "integer to integer extension");
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);

  // v128 a = v128.load(param1);
  // v128 b = opcode1(a);
  // v128 c = v128.not(v128.not(b));
  // v128 d = v128.not(v128.not(b));
  // v128.store(param2, c);
  // v128.store(param2 + 16, d);
  // Where opcode1 is an int to int extension opcode, the two
  // v128.not are used to make sure revec is beneficial in revec cost estimation
  // steps.
  uint32_t count = 3 * kSimd128Size / sizeof(S);
  S* memory = r.builder().AddMemoryElems<S>(count);

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpcode<
            compiler::turboshaft::Opcode::kSimdPack128To256>,
        ExpectedResult::kPass);
    r.Build(
        {WASM_LOCAL_SET(
             temp3, WASM_SIMD_UNOP(opcode,
                                   WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)))),
         WASM_LOCAL_SET(
             temp1, WASM_SIMD_UNOP(
                        kExprS128Not,
                        WASM_SIMD_UNOP(kExprS128Not, WASM_LOCAL_GET(temp3)))),
         WASM_LOCAL_SET(
             temp2, WASM_SIMD_UNOP(
                        kExprS128Not,
                        WASM_SIMD_UNOP(kExprS128Not, WASM_LOCAL_GET(temp3)))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp2)),
         WASM_ONE});
  }

  constexpr uint32_t lanes = kSimd128Size / sizeof(S);
  for (S x : compiler::ValueHelper::GetVector<S>()) {
    for (S y : compiler::ValueHelper::GetVector<S>()) {
      for (uint32_t i = 0; i < lanes / 2; i++) {
        r.builder().WriteMemory(&memory[i], x);
        r.builder().WriteMemory(&memory[i + lanes / 2], y);
      }
      r.Call(0, 16);
      T expected_low = static_cast<T>(x);
      T expected_high = static_cast<T>(y);
      T* output = reinterpret_cast<T*>(memory + lanes);
      for (uint32_t i = 0; i < lanes; i++) {
        CHECK_EQ(IsLowHalfExtensionOp(opcode) ? expected_low : expected_high,
                 output[i]);
      }
    }
  }
}

TEST(RunWasmTurbofan_ForcePackIntToIntExtensionSplat) {
  // Extend 8 bits to 16 bits.
  RunIntToIntExtensionRevecForcePackSplat<uint8_t, uint16_t>(
      kExprI16x8UConvertI8x16Low);
  RunIntToIntExtensionRevecForcePackSplat<int8_t, int16_t>(
      kExprI16x8SConvertI8x16Low);
  RunIntToIntExtensionRevecForcePackSplat<uint8_t, uint16_t>(
      kExprI16x8UConvertI8x16High);
  RunIntToIntExtensionRevecForcePackSplat<int8_t, int16_t>(
      kExprI16x8SConvertI8x16High);

  // Extend 16 bits to 32 bits.
  RunIntToIntExtensionRevecForcePackSplat<uint16_t, uint32_t>(
      kExprI32x4UConvertI16x8Low);
  RunIntToIntExtensionRevecForcePackSplat<int16_t, int32_t>(
      kExprI32x4SConvertI16x8Low);
  RunIntToIntExtensionRevecForcePackSplat<uint16_t, uint32_t>(
      kExprI32x4UConvertI16x8High);
  RunIntToIntExtensionRevecForcePackSplat<int16_t, int32_t>(
      kExprI32x4SConvertI16x8High);

  // Extend 32 bits to 64 bits.
  RunIntToIntExtensionRevecForcePackSplat<uint32_t, uint64_t>(
      kExprI64x2UConvertI32x4Low);
  RunIntToIntExtensionRevecForcePackSplat<int32_t, int64_t>(
      kExprI64x2SConvertI32x4Low);
  RunIntToIntExtensionRevecForcePackSplat<uint32_t, uint64_t>(
      kExprI64x2UConvertI32x4High);
  RunIntToIntExtensionRevecForcePackSplat<int32_t, int64_t>(
      kExprI64x2SConvertI32x4High);
}

TEST(RunWasmTurbofan_ForcePackI16x16ConvertI8x16ExpectFail) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  r.builder().AddMemoryElems<int8_t>(48);
  uint8_t param1 = 0;
  uint8_t param2 = 1;

  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpcode<
            compiler::turboshaft::Opcode::kSimdPack128To256>,
        ExpectedResult::kFail);
    // ExprI16x8SConvertI8x16Low use the result of another
    // ExprI16x8SConvertI8x16Low so the force pack should fail.
    r.Build({WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(
                 temp1,
                 WASM_SIMD_UNOP(
                     kExprI16x8Neg,
                     WASM_SIMD_UNOP(kExprS128Not,
                                    WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                                   WASM_LOCAL_GET(temp3))))),
             WASM_LOCAL_SET(
                 temp2,
                 WASM_SIMD_UNOP(
                     kExprI16x8Neg,
                     WASM_SIMD_UNOP(kExprS128Not,
                                    WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                                   WASM_LOCAL_GET(temp1))))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp2)),
             WASM_ONE});
  }
}

TEST(RunWasmTurbofan_ForcePackInternalI16x16ConvertI8x16) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(64);
  uint8_t param1 = 0;
  uint8_t param2 = 1;

  // Load a i16x8 vector from memory, convert it to i8x16, and add the result
  // back to the original vector. This means that kExprI16x8SConvertI8x16Low
  // will be in an internal packed node, whose inputs are also packed nodes. In
  // this case we should properly handle the inputs by Simd256Extract128Lane.
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmS128);
  uint8_t temp6 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    r.Build(
        {WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             temp4, WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             temp1, WASM_SIMD_UNOP(kExprI16x8Neg,
                                   WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                                  WASM_LOCAL_GET(temp3)))),
         WASM_LOCAL_SET(
             temp2, WASM_SIMD_UNOP(kExprI16x8Neg,
                                   WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                                  WASM_LOCAL_GET(temp3)))),
         WASM_LOCAL_SET(temp5,
                        WASM_SIMD_BINOP(kExprI16x8Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp3))),
         WASM_LOCAL_SET(temp6,
                        WASM_SIMD_BINOP(kExprI16x8Add, WASM_LOCAL_GET(temp2),
                                        WASM_LOCAL_GET(temp4))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp5)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp6)),
         WASM_ONE});
  }
  FOR_INT8_INPUTS(x) {
    for (int i = 0; i < 16; i++) {
      r.builder().WriteMemory(&memory[i], x);
      r.builder().WriteMemory(&memory[i + 16], x);
    }
    r.Call(0, 32);
    int16_t extended_x = static_cast<int16_t>(x);
    int16_t expected_signed =
        -extended_x + ((extended_x << 8) + (extended_x & 0xFF));
    int16_t* out_memory = reinterpret_cast<int16_t*>(memory);
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(expected_signed, out_memory[16 + i]);
      CHECK_EQ(expected_signed, out_memory[24 + i]);
    }
  }
}

TEST(RunWasmTurbofan_ForcePackLoadZero) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  // Use load32_zero for the force packing test.
  {
    // Test ForcePackType::kSplat
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(9);
    uint8_t temp1 = r.AllocateLocal(kWasmS128);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);
    {
      TSSimd256VerifyScope ts_scope(
          r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                        compiler::turboshaft::Opcode::kSimdPack128To256>);
      r.Build({WASM_LOCAL_SET(
                   temp1, WASM_SIMD_UNOP(kExprS128Not,
                                         WASM_SIMD_LOAD_OP(kExprS128Load32Zero,
                                                           WASM_ZERO))),
               WASM_LOCAL_SET(
                   temp2, WASM_SIMD_UNOP(kExprS128Not,
                                         WASM_SIMD_LOAD_OP(kExprS128Load32Zero,
                                                           WASM_ZERO))),

               WASM_SIMD_STORE_MEM_OFFSET(20, WASM_ZERO, WASM_LOCAL_GET(temp2)),
               WASM_SIMD_STORE_MEM_OFFSET(4, WASM_ZERO, WASM_LOCAL_GET(temp1)),

               WASM_ONE});
    }

    FOR_INT32_INPUTS(a) {
      int32_t expected_a = ~a;
      constexpr int32_t expected_padding = ~0;
      r.builder().WriteMemory(&memory[0], a);
      r.Call();
      CHECK_EQ(memory[1], expected_a);
      CHECK_EQ(memory[2], expected_padding);
      CHECK_EQ(memory[3], expected_padding);
      CHECK_EQ(memory[4], expected_padding);
      CHECK_EQ(memory[5], expected_a);
      CHECK_EQ(memory[6], expected_padding);
      CHECK_EQ(memory[7], expected_padding);
      CHECK_EQ(memory[8], expected_padding);
    }
  }

  {
    // Test ForcePackType::kGeneral
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    int32_t* memory = r.builder().AddMemoryElems<int32_t>(10);
    uint8_t temp1 = r.AllocateLocal(kWasmS128);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);
    {
      TSSimd256VerifyScope ts_scope(
          r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                        compiler::turboshaft::Opcode::kSimdPack128To256>);
      r.Build({WASM_LOCAL_SET(
                   temp1, WASM_SIMD_UNOP(kExprS128Not,
                                         WASM_SIMD_LOAD_OP(kExprS128Load32Zero,
                                                           WASM_ZERO))),
               WASM_LOCAL_SET(
                   temp2, WASM_SIMD_UNOP(kExprS128Not, WASM_SIMD_LOAD_OP_OFFSET(
                                                           kExprS128Load32Zero,
                                                           WASM_ZERO, 4))),

               WASM_SIMD_STORE_MEM_OFFSET(24, WASM_ZERO, WASM_LOCAL_GET(temp2)),
               WASM_SIMD_STORE_MEM_OFFSET(8, WASM_ZERO, WASM_LOCAL_GET(temp1)),
               WASM_ONE});
    }

    FOR_INT32_INPUTS(x) {
      FOR_INT32_INPUTS(y) {
        r.builder().WriteMemory(&memory[0], x);
        r.builder().WriteMemory(&memory[1], y);
        r.Call();
        int expected_x = ~x;
        int expected_y = ~y;
        constexpr int32_t expected_padding = ~0;
        CHECK_EQ(memory[2], expected_x);
        CHECK_EQ(memory[3], expected_padding);
        CHECK_EQ(memory[4], expected_padding);
        CHECK_EQ(memory[5], expected_padding);
        CHECK_EQ(memory[6], expected_y);
        CHECK_EQ(memory[7], expected_padding);
        CHECK_EQ(memory[8], expected_padding);
        CHECK_EQ(memory[8], expected_padding);
      }
    }
  }
}

TEST(RunWasmTurbofan_ForcePackInputWithSideEffect) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(48);
  r.builder().SetMemoryShared();

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmI32);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpcode<
            compiler::turboshaft::Opcode::kSimdPack128To256>,
        ExpectedResult::kFail);

    // Use I16x8SConvertI8x16Low for the force packing and test revectorization
    // failed due to side effect in the input tree.
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(
                 temp2,
                 WASM_SIMD_UNOP(
                     kExprI16x8Abs,
                     WASM_SIMD_UNOP(kExprI16x8Neg,
                                    WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                                   WASM_LOCAL_GET(temp1))))),

             WASM_LOCAL_SET(
                 temp3, WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad,
                                             WASM_I32V_3(kWasmPageSize),
                                             MachineRepresentation::kWord32)),
             WASM_LOCAL_SET(temp4, WASM_SIMD_SHIFT_OP(kExprI32x4ShrU,
                                                      WASM_LOCAL_GET(temp1),
                                                      WASM_LOCAL_GET(temp3))),
             WASM_LOCAL_SET(
                 temp5,
                 WASM_SIMD_UNOP(
                     kExprI16x8Abs,
                     WASM_SIMD_UNOP(kExprI16x8Neg,
                                    WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                                   WASM_LOCAL_GET(temp4))))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp2)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp5)),
             WASM_ONE});
  }

  FOR_INT8_INPUTS(x) {
    for (int i = 0; i < 16; i++) {
      r.builder().WriteMemory(&memory[i], x);
    }
    CHECK_TRAP(r.Call(0, 16));
  }
}

TEST(RunWasmTurbofan_ForcePackWithForcePackedInputs) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(4);
  uint8_t param1 = 0;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);

    // Force-pack temp4 with a I32x4ConvertI16x8Low node and reduced by
    // I32x4Add. Build another SLP tree that will force-pack two
    // I32x4SConvertI16x8Low nodes, one of which will use temp4 as an input.
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_I32V(1))),
         WASM_LOCAL_SET(
             temp2, WASM_SIMD_UNOP(
                        kExprI32x4Neg,
                        WASM_SIMD_UNOP(
                            kExprS128Not,
                            WASM_SIMD_UNOP(
                                kExprI32x4Abs,
                                WASM_SIMD_UNOP(
                                    kExprI32x4Neg,
                                    WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                                   WASM_LOCAL_GET(temp1))))))),
         WASM_LOCAL_SET(temp3, WASM_SIMD_I16x8_SPLAT(WASM_I32V(2))),
         WASM_LOCAL_SET(temp4, WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                              WASM_LOCAL_GET(temp1))),
         WASM_LOCAL_SET(
             temp3, WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(temp4),
                                    WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                                   WASM_LOCAL_GET(temp3)))),
         WASM_LOCAL_SET(
             temp4, WASM_SIMD_UNOP(
                        kExprI32x4Neg,
                        WASM_SIMD_UNOP(
                            kExprS128Not,
                            WASM_SIMD_UNOP(
                                kExprI32x4Abs,
                                WASM_SIMD_UNOP(
                                    kExprI32x4Neg,
                                    WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                                   WASM_LOCAL_GET(temp4))))))),
         WASM_LOCAL_SET(
             temp3, WASM_SIMD_BINOP(
                        kExprI32x4Add,
                        WASM_SIMD_BINOP(kExprI32x4Add, WASM_LOCAL_GET(temp2),
                                        WASM_LOCAL_GET(temp4)),
                        WASM_LOCAL_GET(temp3))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param1), WASM_LOCAL_GET(temp3)),
         WASM_ONE});
  }

  r.Call(0);
  auto func = [](int32_t a) { return -(~std::abs(-a)); };
  int32_t expected_signed[2] = {func(1) + func(1) + (1 + 2),
                                func(1) + func(0) + (1 + 2)};
  for (int i = 0; i < 4; i++) {
    CHECK_EQ(expected_signed[i % 2], memory[i]);
  }
}

TEST(RunWasmTurbofan_ForcePackInputsExpectFail) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  r.builder().AddMemoryElems<int8_t>(64);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmI32);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmS128);
  uint8_t temp6 = r.AllocateLocal(kWasmS128);
  uint8_t temp7 = r.AllocateLocal(kWasmS128);
  uint8_t temp8 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpcode<
            compiler::turboshaft::Opcode::kSimdPack128To256>,
        ExpectedResult::kFail);

    // Force-pack two I32x4ConvertI16x8Low nodes, the one with bigger OpIndex
    // has an input from temp4. Later in the same SLP tree, we will try to
    // force-pack temp4 with temp7 from another tree branch. We should forbid
    // this force-packing since it will cause unchecked reordering of temp3
    // (input of temp7) before temp4.
    r.Build({WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(
                                       offset, WASM_LOCAL_GET(param1))),
             WASM_LOCAL_SET(
                 temp1,
                 WASM_SIMD_UNOP(
                     kExprI16x8Neg,
                     WASM_SIMD_UNOP(kExprI16x8Abs,
                                    WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                                   WASM_LOCAL_GET(temp1))))),
             WASM_LOCAL_SET(
                 temp3, WASM_ATOMICS_LOAD_OP(kExprI32AtomicLoad,
                                             WASM_I32V_3(kWasmPageSize),
                                             MachineRepresentation::kWord32)),
             WASM_LOCAL_SET(temp4, WASM_SIMD_I16x8_REPLACE_LANE(
                                       0, WASM_LOCAL_GET(temp2), WASM_I32V(1))),
             WASM_LOCAL_SET(
                 temp5, WASM_SIMD_BINOP(kExprI16x8Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp4))),
             WASM_LOCAL_SET(
                 temp6,
                 WASM_SIMD_UNOP(
                     kExprI16x8Neg,
                     WASM_SIMD_UNOP(kExprI16x8Abs,
                                    WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                                   WASM_LOCAL_GET(temp4))))),
             WASM_LOCAL_SET(
                 temp7, WASM_SIMD_I16x8_REPLACE_LANE(1, WASM_LOCAL_GET(temp2),
                                                     WASM_LOCAL_GET(temp3))),
             WASM_LOCAL_SET(
                 temp8, WASM_SIMD_BINOP(kExprI16x8Add, WASM_LOCAL_GET(temp6),
                                        WASM_LOCAL_GET(temp7))),
             WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp5)),
             WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                        WASM_LOCAL_GET(temp8)),
             WASM_ONE});
  }
}

TEST(RunWasmTurbofan_TwoForcePackExpectFail) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  r.builder().AddMemoryElems<int8_t>(64);
  uint8_t param1 = 0;
  uint8_t param2 = 1;

  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpcode<
            compiler::turboshaft::Opcode::kSimdPack128To256>,
        ExpectedResult::kFail);
    // ExprI16x8SConvertI8x16Low will use the result of another
    // ExprI16x8SConvertI8x16Low so the force pack should fail due to input
    // dependency. ForcePack another two ExprI16x8SConvertI8x16Low nodes that
    // will verify empty of shared hash-set and fail eventually due to higher
    // cost.
    r.Build(
        {WASM_LOCAL_SET(temp3, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             temp1,
             WASM_SIMD_UNOP(
                 kExprI16x8Neg,
                 WASM_SIMD_UNOP(kExprS128Not,
                                WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                               WASM_LOCAL_GET(temp3))))),
         WASM_LOCAL_SET(
             temp2,
             WASM_SIMD_UNOP(
                 kExprI16x8Neg,
                 WASM_SIMD_UNOP(kExprS128Not,
                                WASM_SIMD_UNOP(kExprI16x8SConvertI8x16Low,
                                               WASM_LOCAL_GET(temp1))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp2)),
         WASM_LOCAL_SET(temp1, WASM_SIMD_I16x8_SPLAT(WASM_I32V(1))),
         WASM_LOCAL_SET(
             temp2, WASM_SIMD_BINOP(kExprI32x4Add,
                                    WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                                   WASM_LOCAL_GET(temp1)),
                                    WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                                   WASM_LOCAL_GET(temp1)))),
         WASM_SIMD_STORE_MEM_OFFSET(2 * offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp2)),
         WASM_ONE});
  }
}

template <bool inputs_swapped = false>
void RunForcePackF32x4ReplaceLaneIntersectTest() {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(16);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  uint8_t add1, add2, add3, add4;
  if constexpr (inputs_swapped) {
    add1 = temp3;
    add2 = temp2;
    add3 = temp4;
    add4 = temp3;
  } else {
    add1 = temp2;
    add2 = temp3;
    add3 = temp3;
    add4 = temp4;
  }

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    // Test force-packing two f32x4 replace_lanes(2, 3) or (3, 4) in
    // ForcePackNode, and intersected replace_lanes(3, 4) or (2, 3) in
    // IntersectPackNode. Reduce the ForcePackNode and IntersectPackNode in
    // different order.
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_F32(3.14f))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_F32x4_REPLACE_LANE(
                                   0, WASM_LOCAL_GET(temp1), WASM_F32(0.0f))),
         WASM_LOCAL_SET(temp3, WASM_SIMD_F32x4_REPLACE_LANE(
                                   1, WASM_LOCAL_GET(temp1), WASM_F32(1.0f))),
         WASM_LOCAL_SET(temp4, WASM_SIMD_F32x4_REPLACE_LANE(
                                   2, WASM_LOCAL_GET(temp1), WASM_F32(2.0f))),
         WASM_LOCAL_SET(
             temp5,
             WASM_SIMD_BINOP(
                 kExprF32x4Mul, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                 WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(add1),
                                 WASM_LOCAL_GET(add2)))),
         WASM_LOCAL_SET(
             temp4,
             WASM_SIMD_BINOP(
                 kExprF32x4Mul,
                 WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                 WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(add3),
                                 WASM_LOCAL_GET(add4)))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp5)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp4)),
         WASM_ONE});
  }

  for (int i = 0; i < 8; i++) {
    r.builder().WriteMemory(&memory[i], 2.0f);
  }
  r.Call(0, 32);
  CHECK_EQ(Mul(Add(3.14f, 0.0f), 2.0f), memory[8]);
  CHECK_EQ(Mul(Add(3.14f, 1.0f), 2.0f), memory[9]);
  CHECK_EQ(Mul(Add(3.14f, 1.0f), 2.0f), memory[13]);
  CHECK_EQ(Mul(Add(3.14f, 2.0f), 2.0f), memory[14]);
}

TEST(RunWasmTurbofan_ForcePackF32x4ReplaceLaneIntersect1) {
  RunForcePackF32x4ReplaceLaneIntersectTest<false>();
}

TEST(RunWasmTurbofan_ForcePackF32x4ReplaceLaneIntersect2) {
  RunForcePackF32x4ReplaceLaneIntersectTest<true>();
}

TEST(RunWasmTurbofan_IntersectPackNodeMerge1) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(24);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmS128);
  uint8_t temp6 = r.AllocateLocal(kWasmS128);
  uint8_t temp7 = r.AllocateLocal(kWasmS128);
  uint8_t temp8 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  // Build an SLPTree with default, ForcePackNode and IntersectPackNode. Build
  // another SLPTree that will merge with the default and IntersectPackNode.
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_F32(3.14f))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_F32x4_REPLACE_LANE(
                                   0, WASM_LOCAL_GET(temp1), WASM_F32(0.0f))),
         WASM_LOCAL_SET(temp3, WASM_SIMD_F32x4_REPLACE_LANE(
                                   1, WASM_LOCAL_GET(temp1), WASM_F32(1.0f))),
         WASM_LOCAL_SET(temp4, WASM_SIMD_F32x4_REPLACE_LANE(
                                   2, WASM_LOCAL_GET(temp1), WASM_F32(2.0f))),
         WASM_LOCAL_SET(temp5, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
         WASM_LOCAL_SET(temp6, WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_ZERO)),
         WASM_LOCAL_SET(
             temp7, WASM_SIMD_BINOP(
                        kExprF32x4Add, WASM_LOCAL_GET(temp5),
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp2),
                                        WASM_LOCAL_GET(temp3)))),
         WASM_LOCAL_SET(
             temp8, WASM_SIMD_BINOP(
                        kExprF32x4Add, WASM_LOCAL_GET(temp6),
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp3),
                                        WASM_LOCAL_GET(temp4)))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param1), WASM_LOCAL_GET(temp7)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param1),
                                    WASM_LOCAL_GET(temp8)),
         WASM_LOCAL_SET(
             temp7, WASM_SIMD_BINOP(
                        kExprF32x4Add, WASM_LOCAL_GET(temp5),
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp2),
                                        WASM_LOCAL_GET(temp3)))),
         WASM_LOCAL_SET(
             temp8, WASM_SIMD_BINOP(
                        kExprF32x4Add, WASM_LOCAL_GET(temp6),
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp4),
                                        WASM_LOCAL_GET(temp4)))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp7)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp8)),
         WASM_ONE});
  }
  for (int i = 0; i < 8; i++) {
    r.builder().WriteMemory(&memory[i], 2.0f);
  }

  r.Call(32, 64);
  CHECK_EQ(Add(Add(3.14f, 0.0f), 2.0f), memory[8]);
  CHECK_EQ(Add(Add(3.14f, 1.0f), 2.0f), memory[9]);
  CHECK_EQ(Add(Add(3.14f, 1.0f), 2.0f), memory[13]);
  CHECK_EQ(Add(Add(3.14f, 2.0f), 2.0f), memory[14]);
  CHECK_EQ(Add(Add(3.14f, 0.0f), 2.0f), memory[16]);
  CHECK_EQ(Add(Add(3.14f, 1.0f), 2.0f), memory[17]);
  CHECK_EQ(Add(Add(2.0f, 2.0f), 2.0f), memory[22]);
}

TEST(RunWasmTurbofan_IntersectPackNodeMerge2) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(24);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmS128);
  uint8_t temp6 = r.AllocateLocal(kWasmS128);
  uint8_t temp7 = r.AllocateLocal(kWasmS128);
  uint8_t temp8 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  // Build an SLPTree with default, ForcePackNode(2, 3) and IntersectPackNode(3,
  // 4). Build another SLPTree that will create new IntersectPackNode(1, 3) and
  // (4, 4) and expand the existing revetorizable_intersect_node map entries.
  // This test will ensure no missing IntersectPackNode after the merge.
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_F32(3.14f))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_F32x4_REPLACE_LANE(
                                   0, WASM_LOCAL_GET(temp1), WASM_F32(0.0f))),
         WASM_LOCAL_SET(temp3, WASM_SIMD_F32x4_REPLACE_LANE(
                                   1, WASM_LOCAL_GET(temp1), WASM_F32(1.0f))),
         WASM_LOCAL_SET(temp4, WASM_SIMD_F32x4_REPLACE_LANE(
                                   2, WASM_LOCAL_GET(temp1), WASM_F32(2.0f))),
         WASM_LOCAL_SET(temp5, WASM_SIMD_LOAD_MEM(WASM_ZERO)),
         WASM_LOCAL_SET(temp6, WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_ZERO)),
         WASM_LOCAL_SET(
             temp7, WASM_SIMD_BINOP(
                        kExprF32x4Add, WASM_LOCAL_GET(temp5),
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp2),
                                        WASM_LOCAL_GET(temp3)))),
         WASM_LOCAL_SET(
             temp8, WASM_SIMD_BINOP(
                        kExprF32x4Add, WASM_LOCAL_GET(temp6),
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp3),
                                        WASM_LOCAL_GET(temp4)))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param1), WASM_LOCAL_GET(temp7)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param1),
                                    WASM_LOCAL_GET(temp8)),
         WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_REPLACE_LANE(
                                   3, WASM_LOCAL_GET(temp1), WASM_F32(3.0f))),
         WASM_LOCAL_SET(
             temp7, WASM_SIMD_BINOP(
                        kExprF32x4Add, WASM_LOCAL_GET(temp5),
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp1),
                                        WASM_LOCAL_GET(temp4)))),
         WASM_LOCAL_SET(
             temp8, WASM_SIMD_BINOP(
                        kExprF32x4Add, WASM_LOCAL_GET(temp6),
                        WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp3),
                                        WASM_LOCAL_GET(temp4)))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp7)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp8)),
         WASM_ONE});
  }
  for (int i = 0; i < 8; i++) {
    r.builder().WriteMemory(&memory[i], 2.0f);
  }

  r.Call(32, 64);
  CHECK_EQ(Add(Add(3.14f, 0.0f), 2.0f), memory[8]);
  CHECK_EQ(Add(Add(3.14f, 1.0f), 2.0f), memory[9]);
  CHECK_EQ(Add(Add(3.14f, 1.0f), 2.0f), memory[13]);
  CHECK_EQ(Add(Add(3.14f, 2.0f), 2.0f), memory[14]);
  CHECK_EQ(Add(Add(3.14f, 2.0f), 2.0f), memory[18]);
  CHECK_EQ(Add(Add(3.14f, 3.0f), 2.0f), memory[19]);
  CHECK_EQ(Add(Add(3.14f, 1.0f), 2.0f), memory[21]);
  CHECK_EQ(Add(Add(3.14f, 2.0f), 2.0f), memory[22]);
}

TEST(RunWasmTurbofan_ForcePackExtractInputsTest) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(20);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  uint8_t temp4 = r.AllocateLocal(kWasmS128);
  uint8_t temp5 = r.AllocateLocal(kWasmS128);
  uint8_t temp6 = r.AllocateLocal(kWasmS128);
  uint8_t temp7 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;

  uint8_t c[kSimd128Size];
  for (size_t i = 0; i < kSimd128Size / sizeof(float); i++) {
    WriteLittleEndianValue<float>(reinterpret_cast<float*>(c) + i, 0.1f);
  }

  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpcode<
                      compiler::turboshaft::Opcode::kSimdPack128To256>);
    // Test force-packing two f32x4 replace_lanes(2, 6), while input of node 6:
    // node 5 will also be packed and emit additional Extract128 node due to
    // used by unpacked nodes.
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_F32(3.14f))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_F32x4_REPLACE_LANE(
                                   0, WASM_LOCAL_GET(temp1), WASM_F32(0.0f))),
         WASM_LOCAL_SET(temp3, WASM_SIMD_CONSTANT(c)),
         WASM_LOCAL_SET(
             temp4,
             WASM_SIMD_BINOP(
                 kExprF32x4Mul, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1)),
                 WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp3),
                                 WASM_LOCAL_GET(temp2)))),
         WASM_LOCAL_SET(temp5, WASM_SIMD_CONSTANT(c)),
         WASM_LOCAL_SET(temp6, WASM_SIMD_F32x4_REPLACE_LANE(
                                   2, WASM_LOCAL_GET(temp5), WASM_F32(2.0f))),
         WASM_LOCAL_SET(
             temp7,
             WASM_SIMD_BINOP(
                 kExprF32x4Mul,
                 WASM_SIMD_LOAD_MEM_OFFSET(offset, WASM_LOCAL_GET(param1)),
                 WASM_SIMD_BINOP(kExprF32x4Add, WASM_LOCAL_GET(temp5),
                                 WASM_LOCAL_GET(temp6)))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(temp4)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp7)),
         WASM_SIMD_STORE_MEM_OFFSET(offset * 2, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(temp5)),
         WASM_ONE});
  }

  for (int i = 0; i < 8; i++) {
    r.builder().WriteMemory(&memory[i], 2.0f);
  }
  r.Call(0, 32);
  CHECK_EQ(Mul(Add(0.0f, 0.1f), 2.0f), memory[8]);
  CHECK_EQ(Mul(Add(3.14f, 0.1f), 2.0f), memory[9]);
  CHECK_EQ(Mul(Add(0.1f, 0.1f), 2.0f), memory[13]);
  CHECK_EQ(Mul(Add(2.0f, 0.1f), 2.0f), memory[14]);
  CHECK_EQ(0.1f, memory[16]);
}

TEST(RunWasmTurbofan_RevecCommutativeOp) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(
      TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(16);
  // Add int variable a to each element of 256 bit vectors b, store the result
  // in c
  //   int32_t a,
  //   simd128 *b,*c;
  //   *c = splat(a) + *b;
  //   *(c+1) = *(b+1) + splat(a);
  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t param3 = 2;

  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);
  uint8_t temp3 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<
                      compiler::turboshaft::Simd256BinopOp,
                      compiler::turboshaft::Simd256BinopOp::Kind::kI32x8Add>);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_BINOP(
                                   kExprI32x4Add, WASM_LOCAL_GET(temp1),
                                   WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param2)))),
         WASM_LOCAL_SET(temp3,
                        WASM_SIMD_BINOP(kExprI32x4Add,
                                        WASM_SIMD_LOAD_MEM_OFFSET(
                                            offset, WASM_LOCAL_GET(param2)),
                                        WASM_LOCAL_GET(temp1))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param3), WASM_LOCAL_GET(temp2)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param3),
                                    WASM_LOCAL_GET(temp3)),
         WASM_ONE});
  }

  for (int32_t x : compiler::ValueHelper::GetVector<int32_t>()) {
    for (int32_t y : compiler::ValueHelper::GetVector<int32_t>()) {
      for (int i = 0; i < 8; i++) {
        r.builder().WriteMemory(&memory[i], y);
      }
      int64_t expected = base::AddWithWraparound(x, y);
      CHECK_EQ(r.Call(x, 0, 32), 1);
      for (int i = 0; i < 8; i++) {
        CHECK_EQ(expected, memory[i + 8]);
      }
    }
  }
}

TEST(RunWasmTurbofan_I16x16SConvertI32x8) {
  RunIntToIntNarrowingRevecTest<int32_t, int16_t>(
      kExprI16x8SConvertI32x4, compiler::IrOpcode::kI16x16SConvertI32x8);
}

TEST(RunWasmTurbofan_I16x16UConvertI32x8) {
  RunIntToIntNarrowingRevecTest<int32_t, uint16_t>(
      kExprI16x8UConvertI32x4, compiler::IrOpcode::kI16x16UConvertI32x8);
}

TEST(RunWasmTurbofan_I8x32SConvertI16x16) {
  RunIntToIntNarrowingRevecTest<int16_t, int8_t>(
      kExprI8x16SConvertI16x8, compiler::IrOpcode::kI8x32SConvertI16x16);
}

TEST(RunWasmTurbofan_I8x32UConvertI16x16) {
  RunIntToIntNarrowingRevecTest<int16_t, uint8_t>(
      kExprI8x16UConvertI16x8, compiler::IrOpcode::kI8x32UConvertI16x16);
}

#define RunExtendIntToF32x4RevecTest(format, sign, convert_opcode,             \
                                     convert_sign, param_type, extract_type,   \
                                     convert_type)                             \
  TEST(RunWasmTurbofan_Extend##format##sign##ConvertF32x8##convert_sign) {     \
    EXPERIMENTAL_FLAG_SCOPE(revectorize);                                      \
    if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2))     \
      return;                                                                  \
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);     \
    param_type* memory =                                                       \
        r.builder().AddMemoryElems<param_type>(48 / sizeof(param_type));       \
    uint8_t param1 = 0;                                                        \
    uint8_t param2 = 1;                                                        \
    uint8_t input = r.AllocateLocal(kWasmS128);                                \
    uint8_t output1 = r.AllocateLocal(kWasmS128);                              \
    uint8_t output2 = r.AllocateLocal(kWasmS128);                              \
    constexpr uint8_t offset = 16;                                             \
    {                                                                          \
      TSSimd256VerifyScope ts_scope(                                           \
          r.zone(), TSSimd256VerifyScope::VerifyHaveOpWithKind<                \
                        compiler::turboshaft::Simd256UnaryOp,                  \
                        compiler::turboshaft::Simd256UnaryOp::Kind::           \
                            kF32x8##convert_sign##ConvertI32x8>);              \
      r.Build(                                                                 \
          {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),  \
           WASM_LOCAL_SET(                                                     \
               output1,                                                        \
               WASM_SIMD_F32x4_SPLAT(WASM_UNOP(                                \
                   convert_opcode, WASM_SIMD_##format##_EXTRACT_LANE##sign(    \
                                       0, WASM_LOCAL_GET(input))))),           \
           WASM_LOCAL_SET(                                                     \
               output1, WASM_SIMD_F32x4_REPLACE_LANE(                          \
                            1, WASM_LOCAL_GET(output1),                        \
                            WASM_UNOP(convert_opcode,                          \
                                      WASM_SIMD_##format##_EXTRACT_LANE##sign( \
                                          1, WASM_LOCAL_GET(input))))),        \
           WASM_LOCAL_SET(                                                     \
               output1, WASM_SIMD_F32x4_REPLACE_LANE(                          \
                            2, WASM_LOCAL_GET(output1),                        \
                            WASM_UNOP(convert_opcode,                          \
                                      WASM_SIMD_##format##_EXTRACT_LANE##sign( \
                                          2, WASM_LOCAL_GET(input))))),        \
           WASM_LOCAL_SET(                                                     \
               output1, WASM_SIMD_F32x4_REPLACE_LANE(                          \
                            3, WASM_LOCAL_GET(output1),                        \
                            WASM_UNOP(convert_opcode,                          \
                                      WASM_SIMD_##format##_EXTRACT_LANE##sign( \
                                          3, WASM_LOCAL_GET(input))))),        \
           WASM_LOCAL_SET(                                                     \
               output2,                                                        \
               WASM_SIMD_F32x4_SPLAT(WASM_UNOP(                                \
                   convert_opcode, WASM_SIMD_##format##_EXTRACT_LANE##sign(    \
                                       4, WASM_LOCAL_GET(input))))),           \
           WASM_LOCAL_SET(                                                     \
               output2, WASM_SIMD_F32x4_REPLACE_LANE(                          \
                            1, WASM_LOCAL_GET(output2),                        \
                            WASM_UNOP(convert_opcode,                          \
                                      WASM_SIMD_##format##_EXTRACT_LANE##sign( \
                                          5, WASM_LOCAL_GET(input))))),        \
           WASM_LOCAL_SET(                                                     \
               output2, WASM_SIMD_F32x4_REPLACE_LANE(                          \
                            2, WASM_LOCAL_GET(output2),                        \
                            WASM_UNOP(convert_opcode,                          \
                                      WASM_SIMD_##format##_EXTRACT_LANE##sign( \
                                          6, WASM_LOCAL_GET(input))))),        \
           WASM_LOCAL_SET(                                                     \
               output2, WASM_SIMD_F32x4_REPLACE_LANE(                          \
                            3, WASM_LOCAL_GET(output2),                        \
                            WASM_UNOP(convert_opcode,                          \
                                      WASM_SIMD_##format##_EXTRACT_LANE##sign( \
                                          7, WASM_LOCAL_GET(input))))),        \
           WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2),                         \
                               WASM_LOCAL_GET(output1)),                       \
           WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),          \
                                      WASM_LOCAL_GET(output2)),                \
           WASM_ONE});                                                         \
    }                                                                          \
                                                                               \
    constexpr uint32_t lanes = kSimd128Size / sizeof(param_type);              \
    auto values = compiler::ValueHelper::GetVector<param_type>();              \
    float* output = reinterpret_cast<float*>(memory + lanes);                  \
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {                    \
      for (uint32_t j = 0; j < lanes; j++) {                                   \
        r.builder().WriteMemory(&memory[j], values[i + j]);                    \
      }                                                                        \
      r.Call(0, 16);                                                           \
                                                                               \
      /* Only lane0 to lane7 are processed*/                                   \
      for (uint32_t j = 0; j < 7; j++) {                                       \
        float expected = static_cast<float>(static_cast<convert_type>(         \
            static_cast<extract_type>(values[i + j])));                        \
        CHECK_EQ(output[j], expected);                                         \
      }                                                                        \
    }                                                                          \
  }

// clang-format off
RunExtendIntToF32x4RevecTest(I8x16, _U, kExprF32UConvertI32, U, uint8_t,
                           uint32_t, uint32_t)

RunExtendIntToF32x4RevecTest(I8x16, _U, kExprF32SConvertI32, S, uint8_t,
                           uint32_t, int32_t)

RunExtendIntToF32x4RevecTest(I8x16, , kExprF32UConvertI32, U, int8_t,
                           int32_t, uint32_t)

RunExtendIntToF32x4RevecTest(I8x16, , kExprF32SConvertI32, S, int8_t,
                           int32_t, int32_t)

RunExtendIntToF32x4RevecTest(I16x8, _U, kExprF32UConvertI32, U, uint16_t,
                           uint32_t, uint32_t)

RunExtendIntToF32x4RevecTest(I16x8, _U, kExprF32SConvertI32, S, uint16_t,
                           uint32_t, int32_t)

RunExtendIntToF32x4RevecTest(I16x8, , kExprF32UConvertI32, U, int16_t,
                           int32_t, uint32_t)

RunExtendIntToF32x4RevecTest(I16x8, , kExprF32SConvertI32, S, int16_t,
                           int32_t, int32_t)

#undef RunExtendIntToF32x4RevecTest

// ExtendIntToF32x4Revec try to match the following pattern:
// load 128-bit from memory into a, extract 8 continuous i8/i16 lanes(i to
// i+7) from a and sign extended or zero (unsigned) extended each lane to
// i32, then signed/unsigned covert each i32 to f32,  and finally combine
// f32 into two f32x4 vectors.
//
// Pseudo code snippet:
// v128 a = load(mem);
//
// Extract 8 continuous i8/i16 lanes from a, sign/unsign extend each lane to
// i32.
// int32_t b = extract_lane(a, i);
// int32_t c = extract_lane(a, i+1);
// int32_t d = extract_lane(a, i+2);
// int32_t e = extract_lane(a, i+3);
//
// int32_t f = extract_lane(a, i+4);
// int32_t g = extract_lane(a, i+5);
// int32_t h = extract_lane(a, i+6);
// int32_t k = extract_lane(a, i+7);
//
// Sign/unsign covert i32 to f32.
// float m = f32.convert_i32(b);
// float n = f32.convert_i32(c);
// float p = f32.convert_i32(d);
// float q = f32.convert_i32(e);
//
// float r = f32.convert_i32(f);
// float s = f32.convert_i32(g);
// float t = f32.convert_i32(h);
// float u = f32.convert_i32(k);
//
// Combine four scalar f32 into f32x4
// f32x4 v0 = f32x4.splat(m);
// v1 = f32x4.replace_lane(v0, 1, n);
// v2 = f32x4.replace_lane(v1, 2, p);
// v3 = f32x4.replace_lane(v2, 3, q);
//
// f32x4 w0 = f32x4.splat(r);
// w1 = f32x4.replace_lane(w0, 1, s);
// w2 = f32x4.replace_lane(w1, 2, t);
// w3 = f32x4.replace_lane(w2, 3, u);

// All the conditions need to be met.
// ExtendIntToF32x4RevecExpectedFail1 to ExtendIntToF32x4RevecExpectedFail11
// are the cases where the conditions are not met.

// Data type is f64x2, not f32x4.
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail1) {
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  int64_t* memory = r.builder().AddMemoryElems<int64_t>(4);
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);

  constexpr int32_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_I64x2_SPLAT(WASM_I64V(0))),
         WASM_LOCAL_SET(temp1, WASM_SIMD_I64x2_REPLACE_LANE(
                                   1, WASM_LOCAL_GET(temp1), WASM_I64V(1))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_I64x2_SPLAT(WASM_I64V(2))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_I64x2_REPLACE_LANE(
                                   1, WASM_LOCAL_GET(temp2), WASM_I64V(3))),
         WASM_SIMD_STORE_MEM(WASM_ZERO, WASM_LOCAL_GET(temp1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_ZERO, WASM_LOCAL_GET(temp2)),
         WASM_ONE});
  }

  r.Call();
  for (int i = 0; i < 4; ++i) {
    CHECK_EQ(i, r.builder().ReadMemory(&memory[i]));
  }
}

// clang-format on
// Convert i32 constant to f32, not extracted from v128.
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail2) {
  WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
  float* memory = r.builder().AddMemoryElems<float>(8);
  uint8_t temp1 = r.AllocateLocal(kWasmS128);
  uint8_t temp2 = r.AllocateLocal(kWasmS128);

  constexpr int32_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_SPLAT(WASM_F32(0.0f))),
         WASM_LOCAL_SET(temp1, WASM_SIMD_F32x4_REPLACE_LANE(
                                   1, WASM_LOCAL_GET(temp1),
                                   WASM_F32_SCONVERT_I32(WASM_I32V(1)))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_F32x4_SPLAT(WASM_F32(2.0f))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_F32x4_REPLACE_LANE(
                                   1, WASM_LOCAL_GET(temp2),
                                   WASM_F32_SCONVERT_I32(WASM_I32V(3)))),

         WASM_SIMD_STORE_MEM(WASM_ZERO, WASM_LOCAL_GET(temp1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_ZERO, WASM_LOCAL_GET(temp2)),
         WASM_ONE});
  }

  r.Call();
  for (int i = 0; i < 4; ++i) {
    float expected1 = (i == 1 ? 1.0f : 0.0f);
    float expected2 = (i == 1 ? 3.0f : 2.0f);
    CHECK_EQ(expected1, r.builder().ReadMemory(&memory[i]));
    CHECK_EQ(expected2, r.builder().ReadMemory(&memory[i + 4]));
  }
}

// v0/w0 is constructed from a directly, extract_lane is not used.
// v0 = f32x4.convert_i32x4_s(i32x4.extend_low_i16x8_s(a));
// w0 = f32x4.convert_i32x4_s(i32x4.extend_high_i16x8_s(a));
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail3) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(48 / sizeof(int16_t));

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t input = r.AllocateLocal(kWasmS128);
  uint8_t output1 = r.AllocateLocal(kWasmS128);
  uint8_t output2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             output1, WASM_SIMD_UNOP(kExprF32x4SConvertI32x4,
                                     WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                                    WASM_LOCAL_GET(input)))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   1, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   2, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   3, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2, WASM_SIMD_UNOP(kExprF32x4SConvertI32x4,
                                     WASM_SIMD_UNOP(kExprI32x4SConvertI16x8High,
                                                    WASM_LOCAL_GET(input)))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   5, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   6, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   7, WASM_LOCAL_GET(input))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  }
  constexpr uint32_t lanes = kSimd128Size / sizeof(int16_t);
  auto values = compiler::ValueHelper::GetVector<int16_t>();
  float* output = reinterpret_cast<float*>(memory + lanes);
  for (uint32_t i = 0; i + lanes <= values.size(); i++) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[i + j]);
    }
    r.Call(0, 16);

    // Only lane0 to lane7 are processed.
    for (uint32_t j = 0; j < 7; j++) {
      float expected = static_cast<float>(static_cast<int32_t>(values[i + j]));
      CHECK_EQ(output[j], expected);
    }
  }
}

// Extend a to i32x4 vector directly, not extracting i16 scalar from a and
// convert to f32.
// v0 = f32x4.splat(
//        f32x4.extract_lane(f32x4.convert_i32x4_s(i32x4.extend_low_i16x8_s(a)),0));
// w0 = f32x4.splat(
//        f32x4.extract_lane(f32x4.convert_i32x4_s(i32x4.extend_high_i16x8_s(a)),0));
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail4) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(48 / sizeof(int16_t));

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t input = r.AllocateLocal(kWasmS128);
  uint8_t output1 = r.AllocateLocal(kWasmS128);
  uint8_t output2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             output1,
             WASM_SIMD_F32x4_SPLAT(WASM_SIMD_F32x4_EXTRACT_LANE(
                 0, WASM_SIMD_UNOP(kExprF32x4SConvertI32x4,
                                   WASM_SIMD_UNOP(kExprI32x4SConvertI16x8Low,
                                                  WASM_LOCAL_GET(input)))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   1, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   2, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   3, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2,
             WASM_SIMD_F32x4_SPLAT(WASM_SIMD_F32x4_EXTRACT_LANE(
                 0, WASM_SIMD_UNOP(kExprF32x4SConvertI32x4,
                                   WASM_SIMD_UNOP(kExprI32x4SConvertI16x8High,
                                                  WASM_LOCAL_GET(input)))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   5, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   6, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   7, WASM_LOCAL_GET(input))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  }
  constexpr uint32_t lanes = kSimd128Size / sizeof(int16_t);
  auto values = compiler::ValueHelper::GetVector<int16_t>();
  float* output = reinterpret_cast<float*>(memory + lanes);
  for (uint32_t i = 0; i + lanes <= values.size(); i++) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[i + j]);
    }
    r.Call(0, 16);

    // Only lane0 to lane7 are processed.
    for (uint32_t j = 0; j < 7; j++) {
      float expected = static_cast<float>(static_cast<int32_t>(values[i + j]));
      CHECK_EQ(output[j], expected);
    }
  }
}

// A additional load is used to construct v0/w0, a is not used.
// v0 = f32x4.splat(f32.convert_i32_s(I32LoadMem16S(mem)));
// w0 = f32x4.splat(f32.convert_i32_s(I32LoadMem16S(mem+8)));
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail5) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(48 / sizeof(int16_t));

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t input = r.AllocateLocal(kWasmS128);
  uint8_t output1 = r.AllocateLocal(kWasmS128);
  uint8_t output2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                                     kExprF32SConvertI32,
                                     WASM_LOAD_MEM(MachineType::Int16(),
                                                   WASM_LOCAL_GET(param1))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   1, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   2, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   3, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2,
                        WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                            kExprF32SConvertI32,
                            WASM_LOAD_MEM_OFFSET(MachineType::Int16(), 8,
                                                 WASM_LOCAL_GET(param1))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   5, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   6, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   7, WASM_LOCAL_GET(input))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  }
  constexpr uint32_t lanes = kSimd128Size / sizeof(int16_t);
  auto values = compiler::ValueHelper::GetVector<int16_t>();
  float* output = reinterpret_cast<float*>(memory + lanes);
  for (uint32_t i = 0; i + lanes <= values.size(); i++) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[i + j]);
    }
    r.Call(0, 16);

    // Only lane0 to lane7 are processed.
    for (uint32_t j = 0; j < 7; j++) {
      float expected = static_cast<float>(static_cast<int32_t>(values[i + j]));
      CHECK_EQ(output[j], expected);
    }
  }
}

// Mixed use of sign and unsign covert i32 to f32:
// Unsign convert is used in lane 0 2 4 6 of a.
// Sign convert is used in lane 1 3 5 7 of a.
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail6) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(48 / sizeof(int16_t));

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t input = r.AllocateLocal(kWasmS128);
  uint8_t output1 = r.AllocateLocal(kWasmS128);
  uint8_t output2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             output1, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32UConvertI32, WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   0, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   1, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32UConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   2, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   3, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32UConvertI32, WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   4, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   5, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32UConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   6, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   7, WASM_LOCAL_GET(input))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  }
  constexpr uint32_t lanes = kSimd128Size / sizeof(int16_t);
  auto values = compiler::ValueHelper::GetVector<int16_t>();
  float* output = reinterpret_cast<float*>(memory + lanes);
  for (uint32_t i = 0; i + lanes <= values.size(); i++) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[i + j]);
    }
    r.Call(0, 16);

    float expected;
    // Only lane0 to lane7 are processed.
    for (uint32_t j = 0; j < 7; j++) {
      if ((j & 1) == 0) {
        expected = static_cast<float>(
            static_cast<uint32_t>(static_cast<int32_t>(values[i + j])));
      } else {
        expected = static_cast<float>(static_cast<int32_t>(values[i + j]));
      }
      CHECK_EQ(output[j], expected);
    }
  }
}

// Mixed use of sign and unsign covert i32 to f32:
// Unsign convert is used in lane 0 1 2 3 of a.
// Sign convert is used in lane 4 5 6 7 of a.
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail7) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(48 / sizeof(int16_t));

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t input = r.AllocateLocal(kWasmS128);
  uint8_t output1 = r.AllocateLocal(kWasmS128);
  uint8_t output2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             output1, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32UConvertI32, WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   0, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32UConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   1, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32UConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   2, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32UConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   3, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32SConvertI32, WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   4, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   5, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   6, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   7, WASM_LOCAL_GET(input))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  }
  constexpr uint32_t lanes = kSimd128Size / sizeof(int16_t);
  auto values = compiler::ValueHelper::GetVector<int16_t>();
  float* output = reinterpret_cast<float*>(memory + lanes);
  for (uint32_t i = 0; i + lanes <= values.size(); i++) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[i + j]);
    }
    r.Call(0, 16);

    float expected;
    // Only lane0 to lane7 are processed.
    for (uint32_t j = 0; j < 7; j++) {
      if (j < 4) {
        expected = static_cast<float>(
            static_cast<uint32_t>(static_cast<int32_t>(values[i + j])));
      } else {
        expected = static_cast<float>(static_cast<int32_t>(values[i + j]));
      }
      CHECK_EQ(output[j], expected);
    }
  }
}

// Mixed use of sign and unsign extract_lane:
// Unsign extract_lane is used in lane 0 2 4 6 of a.
// Sign extract_lane is used in lane 1 3 5 7 of a.
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail8) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(48 / sizeof(int16_t));

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t input = r.AllocateLocal(kWasmS128);
  uint8_t output1 = r.AllocateLocal(kWasmS128);
  uint8_t output2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             output1, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32SConvertI32, WASM_SIMD_I16x8_EXTRACT_LANE_U(
                                                   0, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   1, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE_U(
                                                   2, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output1),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   3, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32SConvertI32, WASM_SIMD_I16x8_EXTRACT_LANE_U(
                                                   4, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     1, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   5, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     2, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE_U(
                                                   6, WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_REPLACE_LANE(
                                     3, WASM_LOCAL_GET(output2),
                                     WASM_UNOP(kExprF32SConvertI32,
                                               WASM_SIMD_I16x8_EXTRACT_LANE(
                                                   7, WASM_LOCAL_GET(input))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  }
  constexpr uint32_t lanes = kSimd128Size / sizeof(int16_t);
  auto values = compiler::ValueHelper::GetVector<int16_t>();
  float* output = reinterpret_cast<float*>(memory + lanes);
  for (uint32_t i = 0; i + lanes <= values.size(); i++) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[i + j]);
    }
    r.Call(0, 16);

    float expected;
    // Only lane0 to lane7 are processed.
    for (uint32_t j = 0; j < 7; j++) {
      if ((j & 1) == 0) {
        expected = static_cast<float>(
            static_cast<int32_t>(static_cast<uint16_t>(values[i + j])));
      } else {
        expected = static_cast<float>(static_cast<int32_t>(values[i + j]));
      }
      CHECK_EQ(output[j], expected);
    }
  }
}

// Two different inputs are used:
// a0 = S128Load64Zero(mem);
// a1 = S128Load64Zero(mem+8);
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail9) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int16_t* memory = r.builder().AddMemoryElems<int16_t>(48 / sizeof(int16_t));

  uint8_t param1 = 0;
  uint8_t param2 = 1;
  uint8_t input1 = r.AllocateLocal(kWasmS128);
  uint8_t input2 = r.AllocateLocal(kWasmS128);
  uint8_t output1 = r.AllocateLocal(kWasmS128);
  uint8_t output2 = r.AllocateLocal(kWasmS128);
  constexpr uint8_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        ExpectedResult::kFail);
    r.Build(
        {WASM_LOCAL_SET(input1, WASM_SIMD_LOAD_OP(kExprS128Load64Zero,
                                                  WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(input2,
                        WASM_SIMD_LOAD_OP_OFFSET(kExprS128Load64Zero,
                                                 WASM_LOCAL_GET(param1), 8)),

         WASM_LOCAL_SET(output1, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                                     kExprF32SConvertI32,
                                     WASM_SIMD_I16x8_EXTRACT_LANE(
                                         0, WASM_LOCAL_GET(input1))))),
         WASM_LOCAL_SET(output1,
                        WASM_SIMD_F32x4_REPLACE_LANE(
                            1, WASM_LOCAL_GET(output1),
                            WASM_UNOP(kExprF32SConvertI32,
                                      WASM_SIMD_I16x8_EXTRACT_LANE(
                                          0, WASM_LOCAL_GET(input2))))),
         WASM_LOCAL_SET(output1,
                        WASM_SIMD_F32x4_REPLACE_LANE(
                            2, WASM_LOCAL_GET(output1),
                            WASM_UNOP(kExprF32SConvertI32,
                                      WASM_SIMD_I16x8_EXTRACT_LANE(
                                          1, WASM_LOCAL_GET(input1))))),
         WASM_LOCAL_SET(output1,
                        WASM_SIMD_F32x4_REPLACE_LANE(
                            3, WASM_LOCAL_GET(output1),
                            WASM_UNOP(kExprF32SConvertI32,
                                      WASM_SIMD_I16x8_EXTRACT_LANE(
                                          1, WASM_LOCAL_GET(input2))))),
         WASM_LOCAL_SET(output2, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                                     kExprF32SConvertI32,
                                     WASM_SIMD_I16x8_EXTRACT_LANE(
                                         2, WASM_LOCAL_GET(input1))))),
         WASM_LOCAL_SET(output2,
                        WASM_SIMD_F32x4_REPLACE_LANE(
                            1, WASM_LOCAL_GET(output2),
                            WASM_UNOP(kExprF32SConvertI32,
                                      WASM_SIMD_I16x8_EXTRACT_LANE(
                                          2, WASM_LOCAL_GET(input2))))),
         WASM_LOCAL_SET(output2,
                        WASM_SIMD_F32x4_REPLACE_LANE(
                            2, WASM_LOCAL_GET(output2),
                            WASM_UNOP(kExprF32SConvertI32,
                                      WASM_SIMD_I16x8_EXTRACT_LANE(
                                          3, WASM_LOCAL_GET(input1))))),
         WASM_LOCAL_SET(output2,
                        WASM_SIMD_F32x4_REPLACE_LANE(
                            3, WASM_LOCAL_GET(output2),
                            WASM_UNOP(kExprF32SConvertI32,
                                      WASM_SIMD_I16x8_EXTRACT_LANE(
                                          3, WASM_LOCAL_GET(input2))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  }
  constexpr uint32_t lanes = kSimd128Size / sizeof(int16_t);
  auto values = compiler::ValueHelper::GetVector<int16_t>();
  float* output = reinterpret_cast<float*>(memory + lanes);
  for (uint32_t i = 0; i + lanes <= values.size(); i++) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[i + j]);
    }
    r.Call(0, 16);

    float expected;
    constexpr std::array<uint8_t, 8> input_to_output = {0, 4, 1, 5, 2, 6, 3, 7};
    // Only lane0 to lane7 are processed.
    for (uint32_t j = 0; j < 7; j++) {
      expected = static_cast<float>(
          static_cast<int32_t>(values[i + input_to_output[j]]));
      CHECK_EQ(output[j], expected);
    }
  }
}

// Unsign extract i8x16 lane to i32 and unsign convert i32 to float.
// Lane indices are not continuous or the minimal index is not 0/8.
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail10) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  uint8_t* memory;
  constexpr uint32_t lanes = kSimd128Size / sizeof(uint8_t);

  auto build_fn = [&memory](WasmRunner<int32_t, int32_t, int32_t>& r,
                            const std::array<uint8_t, 8>& extract_lane_index,
                            const std::array<uint8_t, 8>& replace_lane_index,
                            ExpectedResult result) {
    memory = r.builder().AddMemoryElems<uint8_t>(48 / sizeof(uint8_t));
    uint8_t param1 = 0;
    uint8_t param2 = 1;
    uint8_t input = r.AllocateLocal(kWasmS128);
    uint8_t output1 = r.AllocateLocal(kWasmS128);
    uint8_t output2 = r.AllocateLocal(kWasmS128);
    constexpr uint8_t offset = 16;
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8UConvertI32x8>,
        result);
    r.Build(
        {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             output1, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32UConvertI32,
                          WASM_SIMD_I8x16_EXTRACT_LANE_U(
                              extract_lane_index[0], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output1,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[1], WASM_LOCAL_GET(output1),
                 WASM_UNOP(kExprF32UConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE_U(
                               extract_lane_index[1], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output1,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[2], WASM_LOCAL_GET(output1),
                 WASM_UNOP(kExprF32UConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE_U(
                               extract_lane_index[2], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output1,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[3], WASM_LOCAL_GET(output1),
                 WASM_UNOP(kExprF32UConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE_U(
                               extract_lane_index[3], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32UConvertI32,
                          WASM_SIMD_I8x16_EXTRACT_LANE_U(
                              extract_lane_index[4], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[5], WASM_LOCAL_GET(output2),
                 WASM_UNOP(kExprF32UConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE_U(
                               extract_lane_index[5], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[6], WASM_LOCAL_GET(output2),
                 WASM_UNOP(kExprF32UConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE_U(
                               extract_lane_index[6], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[7], WASM_LOCAL_GET(output2),
                 WASM_UNOP(kExprF32UConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE_U(
                               extract_lane_index[7], WASM_LOCAL_GET(input))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  };

  auto init_memory = [&memory](WasmRunner<int32_t, int32_t, int32_t>& r,
                               const base::Vector<const uint8_t>& values,
                               uint32_t index) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[index + j]);
    }
  };

  auto check_results =
      [&memory](WasmRunner<int32_t, int32_t, int32_t>& r,
                const base::Vector<const uint8_t>& values, uint32_t index,
                const std::array<uint8_t, 8>& extract_lane_index,
                const std::array<uint8_t, 8>& replace_lane_index) {
        float* output = reinterpret_cast<float*>(memory + lanes);
        // Only lane0 to lane7 are processed.
        for (uint32_t j = 0; j < 7; j++) {
          float expected = static_cast<float>(
              static_cast<uint32_t>(values[index + extract_lane_index[j]]));
          if (j < 4) {
            CHECK_EQ(output[replace_lane_index[j]], expected);
          } else {
            CHECK_EQ(output[4 + replace_lane_index[j]], expected);
          }
        }
      };

  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {0, 1, 2, 3,
                                                           4, 5, 6, 7};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kPass);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {8,  9,  10, 11,
                                                           12, 13, 14, 15};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kPass);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  // extract_lane_index is continuous, but not starting from 0 or 8.
  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {4, 5, 6,  7,
                                                           8, 9, 10, 11};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kFail);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  // extract_lane_index is not continuous.
  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {0, 1, 2,  3,
                                                           8, 9, 10, 11};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kFail);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  // extract_lane_index is not continuous.
  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {0, 2,  4,  6,
                                                           8, 10, 12, 14};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kFail);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  // The order of replace_lane_index is reversed.
  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {0, 1, 2,  3,
                                                           8, 9, 10, 11};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 3, 2, 1,
                                                           0, 3, 2, 1};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kFail);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }
}

// Similar to ExtendIntToF32x4RevecExpectedFail10,
// but sign extract i8x16 lane to i32 and sign convert i32 to float.
// Lane indices are not continuous or the minimal index is not 0/8.
TEST(RunWasmTurbofan_ExtendIntToF32x4RevecExpectedFail11) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  uint8_t* memory;
  constexpr uint32_t lanes = kSimd128Size / sizeof(uint8_t);

  auto build_fn = [&memory](WasmRunner<int32_t, int32_t, int32_t>& r,
                            const std::array<uint8_t, 8>& extract_lane_index,
                            const std::array<uint8_t, 8>& replace_lane_index,
                            ExpectedResult result) {
    memory = r.builder().AddMemoryElems<uint8_t>(48 / sizeof(uint8_t));
    uint8_t param1 = 0;
    uint8_t param2 = 1;
    uint8_t input = r.AllocateLocal(kWasmS128);
    uint8_t output1 = r.AllocateLocal(kWasmS128);
    uint8_t output2 = r.AllocateLocal(kWasmS128);
    constexpr uint8_t offset = 16;
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256UnaryOp,
            compiler::turboshaft::Simd256UnaryOp::Kind::kF32x8SConvertI32x8>,
        result);
    r.Build(
        {WASM_LOCAL_SET(input, WASM_SIMD_LOAD_MEM(WASM_LOCAL_GET(param1))),
         WASM_LOCAL_SET(
             output1, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32SConvertI32,
                          WASM_SIMD_I8x16_EXTRACT_LANE(
                              extract_lane_index[0], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output1,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[1], WASM_LOCAL_GET(output1),
                 WASM_UNOP(kExprF32SConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE(
                               extract_lane_index[1], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output1,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[2], WASM_LOCAL_GET(output1),
                 WASM_UNOP(kExprF32SConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE(
                               extract_lane_index[2], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output1,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[3], WASM_LOCAL_GET(output1),
                 WASM_UNOP(kExprF32SConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE(
                               extract_lane_index[3], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2, WASM_SIMD_F32x4_SPLAT(WASM_UNOP(
                          kExprF32SConvertI32,
                          WASM_SIMD_I8x16_EXTRACT_LANE(
                              extract_lane_index[4], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[5], WASM_LOCAL_GET(output2),
                 WASM_UNOP(kExprF32SConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE(
                               extract_lane_index[5], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[6], WASM_LOCAL_GET(output2),
                 WASM_UNOP(kExprF32SConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE(
                               extract_lane_index[6], WASM_LOCAL_GET(input))))),
         WASM_LOCAL_SET(
             output2,
             WASM_SIMD_F32x4_REPLACE_LANE(
                 replace_lane_index[7], WASM_LOCAL_GET(output2),
                 WASM_UNOP(kExprF32SConvertI32,
                           WASM_SIMD_I8x16_EXTRACT_LANE(
                               extract_lane_index[7], WASM_LOCAL_GET(input))))),
         WASM_SIMD_STORE_MEM(WASM_LOCAL_GET(param2), WASM_LOCAL_GET(output1)),
         WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_LOCAL_GET(param2),
                                    WASM_LOCAL_GET(output2)),
         WASM_ONE});
  };

  auto init_memory = [&memory](WasmRunner<int32_t, int32_t, int32_t>& r,
                               const base::Vector<const uint8_t>& values,
                               uint32_t index) {
    for (uint32_t j = 0; j < lanes; j++) {
      r.builder().WriteMemory(&memory[j], values[index + j]);
    }
  };

  auto check_results =
      [&memory](WasmRunner<int32_t, int32_t, int32_t>& r,
                const base::Vector<const uint8_t>& values, uint32_t index,
                const std::array<uint8_t, 8>& extract_lane_index,
                const std::array<uint8_t, 8>& replace_lane_index) {
        float* output = reinterpret_cast<float*>(memory + lanes);
        // Only lane0 to lane7 are processed.
        for (uint32_t j = 0; j < 7; j++) {
          float expected = static_cast<float>(static_cast<int32_t>(
              static_cast<int8_t>(values[index + extract_lane_index[j]])));
          if (j < 4) {
            CHECK_EQ(output[replace_lane_index[j]], expected);
          } else {
            CHECK_EQ(output[4 + replace_lane_index[j]], expected);
          }
        }
      };

  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {0, 1, 2, 3,
                                                           4, 5, 6, 7};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kPass);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {8,  9,  10, 11,
                                                           12, 13, 14, 15};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kPass);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  // extract_lane_index is continuous, but not starting from 0 or 8.
  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {4, 5, 6,  7,
                                                           8, 9, 10, 11};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kFail);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  // extract_lane_index is not continuous.
  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {0, 1, 2,  3,
                                                           8, 9, 10, 11};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kFail);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  // extract_lane_index is not continuous.
  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {0, 2,  4,  6,
                                                           8, 10, 12, 14};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 1, 2, 3,
                                                           0, 1, 2, 3};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kFail);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }

  // The order of replace_lane_index is reversed.
  {
    WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
    constexpr std::array<uint8_t, 8> extract_lane_index = {0, 1, 2,  3,
                                                           8, 9, 10, 11};
    constexpr std::array<uint8_t, 8> replace_lane_index = {0, 3, 2, 1,
                                                           0, 3, 2, 1};
    build_fn(r, extract_lane_index, replace_lane_index, ExpectedResult::kFail);

    auto values = compiler::ValueHelper::GetVector<uint8_t>();
    for (uint32_t i = 0; i + lanes <= values.size(); i++) {
      init_memory(r, values, i);
      r.Call(0, 16);
      check_results(r, values, i, extract_lane_index, replace_lane_index);
    }
  }
}

TEST(RunWasmTurbofan_ChangeIndexFromI32ToI64ExpectFail) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory =
      r.builder().AddMemoryElems<int32_t>(8, wasm::AddressType::kI64);
  int32_t param1 = 0;
  constexpr int32_t offset = 16;
  {
    TSSimd256VerifyScope ts_scope(r.zone(),
                                  TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
                                  ExpectedResult::kFail);
    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO64,
                                 WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 offset, WASM_I64_SCONVERT_I32(WASM_ZERO),
                 WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_ONE});
  }

  FOR_INT32_INPUTS(x) {
    r.Call(x);
    for (int i = 0; i < 8; ++i) {
      CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
    }
  }
}

// Two splat have same constant value, may be revectorized.
TEST(RunWasmTurbofan_ConstSplatRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  int32_t* memory;
  auto build_and_check_results = [&memory](WasmRunner<int32_t>& r, int32_t x,
                                           int32_t y, uint8_t offset,
                                           enum ExpectedResult expect) {
    memory = r.builder().AddMemoryElems<int32_t>(12);
    {
      TSSimd256VerifyScope ts_scope(
          r.zone(),
          TSSimd256VerifyScope::VerifyHaveOpWithKind<
              compiler::turboshaft::Simd256SplatOp,
              compiler::turboshaft::Simd256SplatOp::Kind::kI32x8>,
          expect);

      r.Build(
          {WASM_SIMD_STORE_MEM(WASM_ZERO, WASM_SIMD_I32x4_SPLAT(WASM_I32V(x))),
           WASM_SIMD_STORE_MEM_OFFSET(offset, WASM_ZERO,
                                      WASM_SIMD_I32x4_SPLAT(WASM_I32V(y))),
           WASM_ONE});
    }
    r.Call();
    for (int i = 0; i < 4; ++i) {
      CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
      CHECK_EQ(y,
               r.builder().ReadMemory(&memory[i + offset / sizeof(int32_t)]));
    }
  };

  {
    // Splat from same constant(1)
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    build_and_check_results(r, 1, 1, 16, ExpectedResult::kPass);
  }

  {
    // Splat from different constant(0 and 1)
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    build_and_check_results(r, 0, 1, 16, ExpectedResult::kFail);
  }

  {
    // Non-continuous store
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    build_and_check_results(r, 1, 1, 32, ExpectedResult::kFail);
  }
}

// Two Splat from different value, not constant
TEST(RunWasmTurbofan_I32x4SplatRevecExpectFail) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int32_t, int32_t> r(TestExecutionTier::kTurbofan);
  int32_t* memory = r.builder().AddMemoryElems<int32_t>(8);
  int32_t param1 = 0;
  int32_t param2 = 1;
  {
    TSSimd256VerifyScope ts_scope(
        r.zone(),
        TSSimd256VerifyScope::VerifyHaveOpWithKind<
            compiler::turboshaft::Simd256SplatOp,
            compiler::turboshaft::Simd256SplatOp::Kind::kI32x8>,
        ExpectedResult::kFail);

    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO,
                                 WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO, WASM_SIMD_I32x4_SPLAT(WASM_LOCAL_GET(param2))),
             WASM_ONE});
  }

  FOR_INT32_INPUTS(x) {
    FOR_INT32_INPUTS(y) {
      r.Call(x, y);
      for (int i = 0; i < 4; ++i) {
        CHECK_EQ(x, r.builder().ReadMemory(&memory[i]));
        CHECK_EQ(y, r.builder().ReadMemory(&memory[i + 4]));
      }
    }
  }
}

// Can't merge different opcode, I32x4Splat(i32) and S128Const
TEST(RunWasmTurbofan_DifferentOpcodeRevecExpectFail) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  WasmRunner<int32_t, int8_t> r(TestExecutionTier::kTurbofan);
  int8_t* memory = r.builder().AddMemoryElems<int8_t>(32);
  int8_t param1 = 0;
  std::array<uint8_t, kSimd128Size> expected = {0};
  // Test for generic constant
  for (int i = 0; i < kSimd128Size; i++) {
    expected[i] = i;
  }

  {
    TSSimd256VerifyScope ts_scope(r.zone(),
                                  TSSimd256VerifyScope::VerifyHaveAnySimd256Op,
                                  ExpectedResult::kFail);
    r.Build({WASM_SIMD_STORE_MEM(WASM_ZERO, WASM_SIMD_CONSTANT(expected)),
             WASM_SIMD_STORE_MEM_OFFSET(
                 16, WASM_ZERO, WASM_SIMD_I8x16_SPLAT(WASM_LOCAL_GET(param1))),
             WASM_ONE});
  }
  FOR_INT8_INPUTS(x) {
    r.Call(x);
    for (int i = 0; i < kSimd128Size; ++i) {
      CHECK_EQ(expected[i], r.builder().ReadMemory(&memory[i]));
      CHECK_EQ(x, r.builder().ReadMemory(&memory[i + 16]));
    }
  }
}

// Signed overflow in offset + index.
TEST(RunWasmTurbofan_OffsetAddIndexMayOverflowRevec) {
  EXPERIMENTAL_FLAG_SCOPE(revectorize);
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  uint32_t mem_size = (max_mem32_pages() - 1) * kWasmPageSize;
  uint8_t* memory;
  auto build_fn = [mem_size, &memory](WasmRunner<int32_t>& r, uint32_t index,
                                      enum ExpectedResult expect) {
    memory = r.builder().AddMemory(mem_size);
    TSSimd256VerifyScope ts_scope(
        r.zone(), TSSimd256VerifyScope::VerifyHaveAnySimd256Op, expect);

    uint8_t temp1 = r.AllocateLocal(kWasmS128);
    uint8_t temp2 = r.AllocateLocal(kWasmS128);
    r.Build(
        {WASM_LOCAL_SET(temp1, WASM_SIMD_LOAD_MEM_OFFSET(16, WASM_I32V(index))),
         WASM_LOCAL_SET(temp2, WASM_SIMD_LOAD_MEM_OFFSET(32, WASM_I32V(index))),
         WASM_SIMD_STORE_MEM(
             WASM_ZERO, WASM_SIMD_UNOP(kExprS128Not, WASM_LOCAL_GET(temp1))),
         WASM_SIMD_STORE_MEM_OFFSET(
             16, WASM_ZERO,
             WASM_SIMD_UNOP(kExprS128Not, WASM_LOCAL_GET(temp2))),
         WASM_ONE});
  };

  auto init_memory = [&memory](WasmRunner<int32_t>& r, uint32_t index) {
    for (uint32_t i = 0; i < kSimd128Size * 2; ++i) {
      memory[i + 16 + index] = i;
    }
  };

  auto check_results = [&memory](uint32_t index) {
    for (uint32_t i = 0; i < kSimd128Size * 2; ++i) {
      CHECK_EQ(~memory[i + 16 + index] & 0xFF, memory[i]);
    }
  };

  {
    constexpr uint32_t index = std::numeric_limits<int32_t>::max();
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    build_fn(r, index, ExpectedResult::kPass);
    if (index < mem_size - 48) {
      init_memory(r, index);
      r.Call();
      check_results(index);
    } else {
      CHECK_TRAP(r.Call());
    }
  }

  {
    constexpr uint32_t index = std::numeric_limits<uint32_t>::max();
    WasmRunner<int32_t> r(TestExecutionTier::kTurbofan);
    build_fn(r, index, ExpectedResult::kFail);
    if (index < mem_size - 48) {
      init_memory(r, index);
      r.Call();
      check_results(index);
    } else {
      CHECK_TRAP(r.Call());
    }
  }
}

#endif  // V8_ENABLE_WASM_SIMD256_REVEC

#undef WASM_SIMD_CHECK_LANE_S
#undef WASM_SIMD_CHECK_LANE_U
#undef TO_BYTE
#undef WASM_SIMD_OP
#undef WASM_SIMD_SPLAT
#undef WASM_SIMD_UNOP
#undef WASM_SIMD_BINOP
#undef WASM_SIMD_SHIFT_OP
#undef WASM_SIMD_CONCAT_OP
#undef WASM_SIMD_SELECT
#undef WASM_SIMD_F64x2_SPLAT
#undef WASM_SIMD_F64x2_EXTRACT_LANE
#undef WASM_SIMD_F64x2_REPLACE_LANE
#undef WASM_SIMD_F32x4_SPLAT
#undef WASM_SIMD_F32x4_EXTRACT_LANE
#undef WASM_SIMD_F32x4_REPLACE_LANE
#undef WASM_SIMD_I64x2_SPLAT
#undef WASM_SIMD_I64x2_EXTRACT_LANE
#undef WASM_SIMD_I64x2_REPLACE_LANE
#undef WASM_SIMD_I32x4_SPLAT
#undef WASM_SIMD_I32x4_EXTRACT_LANE
#undef WASM_SIMD_I32x4_REPLACE_LANE
#undef WASM_SIMD_I16x8_SPLAT
#undef WASM_SIMD_I16x8_EXTRACT_LANE
#undef WASM_SIMD_I16x8_EXTRACT_LANE_U
#undef WASM_SIMD_I16x8_REPLACE_LANE
#undef WASM_SIMD_I8x16_SPLAT
#undef WASM_SIMD_I8x16_EXTRACT_LANE
#undef WASM_SIMD_I8x16_EXTRACT_LANE_U
#undef WASM_SIMD_I8x16_REPLACE_LANE
#undef WASM_SIMD_I8x16_SHUFFLE_OP
#undef WASM_SIMD_LOAD_MEM
#undef WASM_SIMD_LOAD_MEM_OFFSET
#undef WASM_SIMD_STORE_MEM
#undef WASM_SIMD_STORE_MEM_OFFSET
#undef WASM_SIMD_SELECT_TEST
#undef WASM_SIMD_NON_CANONICAL_SELECT_TEST
#undef WASM_SIMD_BOOL_REDUCTION_TEST
#undef WASM_SIMD_ANYTRUE_TEST
#undef WASM_SIMD_ALLTRUE_TEST
#undef WASM_SIMD_F64x2_QFMA
#undef WASM_SIMD_F64x2_QFMS
#undef WASM_SIMD_F32x4_QFMA
#undef WASM_SIMD_F32x4_QFMS
#undef WASM_SIMD_LOAD_OP
#undef WASM_SIMD_LOAD_OP_OFFSET
#undef WASM_SIMD_LOAD_OP_ALIGNMENT

}  // namespace test_run_wasm_simd
}  // namespace wasm
}  // namespace internal
}  // namespace v8
