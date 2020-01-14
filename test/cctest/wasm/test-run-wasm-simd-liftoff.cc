// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains tests that run only on Liftoff, and each test verifies
// that the code was compiled by Liftoff. The default behavior is that each
// function is first attempted to be compiled by Liftoff, and if it fails, fall
// back to TurboFan. However we want to enforce that Liftoff is the tier that
// compiles these functions, in order to verify correctness of SIMD
// implementation in Liftoff.

#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_simd_liftoff {

#define WASM_SIMD_LIFTOFF_TEST(name) \
  void RunWasm_##name##_Impl();      \
  TEST(RunWasm_##name##_liftoff) {   \
    EXPERIMENTAL_FLAG_SCOPE(simd);   \
    RunWasm_##name##_Impl();         \
  }                                  \
  void RunWasm_##name##_Impl()

WASM_SIMD_LIFTOFF_TEST(S128Local) {
  WasmRunner<int32_t> r(ExecutionTier::kLiftoff, kNoLowerSimd);
  byte temp1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(temp1, WASM_GET_LOCAL(temp1)), WASM_ONE);
  CHECK_EQ(1, r.Call());
  r.CheckUsedExecutionTier(ExecutionTier::kLiftoff);
}

WASM_SIMD_LIFTOFF_TEST(S128Global) {
  WasmRunner<int32_t> r(ExecutionTier::kLiftoff, kNoLowerSimd);

  int32_t* g0 = r.builder().AddGlobal<int32_t>(kWasmS128);
  int32_t* g1 = r.builder().AddGlobal<int32_t>(kWasmS128);
  BUILD(r, WASM_SET_GLOBAL(1, WASM_GET_GLOBAL(0)), WASM_ONE);

  int32_t expected = 0x1234;
  for (int i = 0; i < 4; i++) {
    WriteLittleEndianValue<int32_t>(&g0[i], expected);
  }
  r.Call();
  for (int i = 0; i < 4; i++) {
    int32_t actual = ReadLittleEndianValue<int32_t>(&g1[i]);
    CHECK_EQ(actual, expected);
  }
}

#undef WASM_SIMD_LIFTOFF_TEST

}  // namespace test_run_wasm_simd_liftoff
}  // namespace wasm
}  // namespace internal
}  // namespace v8
