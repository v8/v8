// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-tier.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_simd {

#define WASM_SIMD_TEST(name)                                         \
  void RunWasm_##name##_Impl(LowerSimd lower_simd,                   \
                             TestExecutionTier execution_tier);      \
  TEST(RunWasm_##name##_simd_lowered) {                              \
    EXPERIMENTAL_FLAG_SCOPE(simd);                                   \
    RunWasm_##name##_Impl(kLowerSimd, TestExecutionTier::kTurbofan); \
  }                                                                  \
  void RunWasm_##name##_Impl(LowerSimd lower_simd,                   \
                             TestExecutionTier execution_tier)

WASM_SIMD_TEST(I8x16ToF32x4) {
  WasmRunner<int32_t, int32_t> r(execution_tier, lower_simd);
  float* g = r.builder().AddGlobal<float>(kWasmS128);
  byte param1 = 0;
  BUILD(r,
        WASM_SET_GLOBAL(
            0, WASM_SIMD_UNOP(kExprF32x4Sqrt,
                              WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(param1)))),
        WASM_ONE);

  // Arbitrary pattern that doesn't end up creating a NaN.
  r.Call(0x5b);
  float f = bit_cast<float>(0x5b5b5b5b);
  float actual = ReadLittleEndianValue<float>(&g[0]);
  float expected = std::sqrt(f);
  CHECK_EQ(expected, actual);
}

WASM_SIMD_TEST(F32x4) {
  // Check that functions that return F32x4 are correctly lowered into 4 int32
  // nodes. The signature of such functions are always lowered to 4 Word32, and
  // if the last operation before the return was a f32x4, it will need to be
  // bitcasted from float to int.
  TestSignatures sigs;
  WasmRunner<uint32_t, uint32_t> r(execution_tier, lower_simd);

  // A simple function that just calls f32x4.neg on the param.
  WasmFunctionCompiler& fn = r.NewFunction(sigs.s_s());
  BUILD(fn, WASM_SIMD_UNOP(kExprF32x4Neg, WASM_GET_LOCAL(0)));

  // TODO(v8:10507)
  // Use i32x4 splat since scalar lowering has a problem with f32x4 as a param
  // to a function call, the lowering is not correct yet.
  BUILD(r,
        WASM_SIMD_I32x4_EXTRACT_LANE(
            0, WASM_CALL_FUNCTION(fn.function_index(),
                                  WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(0)))));
  CHECK_EQ(0x80000001, r.Call(1));
}

}  // namespace test_run_wasm_simd
}  // namespace wasm
}  // namespace internal
}  // namespace v8
