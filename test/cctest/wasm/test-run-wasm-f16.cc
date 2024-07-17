// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/overflowing-math.h"
#include "src/codegen/assembler-inl.h"
#include "src/wasm/wasm-opcodes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/cctest/wasm/wasm-simd-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_f16 {

TEST(F16Load) {
  i::v8_flags.experimental_wasm_fp16 = true;
  WasmRunner<float> r(TestExecutionTier::kLiftoff);
  uint16_t* memory = r.builder().AddMemoryElems<uint16_t>(4);
  r.Build({WASM_F16_LOAD_MEM(WASM_I32V_1(4))});
  r.builder().WriteMemory(&memory[2], fp16_ieee_from_fp32_value(2.75));
  CHECK_EQ(2.75f, r.Call());
}

TEST(F16Store) {
  i::v8_flags.experimental_wasm_fp16 = true;
  WasmRunner<int32_t> r(TestExecutionTier::kLiftoff);
  uint16_t* memory = r.builder().AddMemoryElems<uint16_t>(4);
  r.Build({WASM_F16_STORE_MEM(WASM_I32V_1(4), WASM_F32(2.75)), WASM_ZERO});
  r.Call();
  CHECK_EQ(r.builder().ReadMemory(&memory[2]), fp16_ieee_from_fp32_value(2.75));
}

}  // namespace test_run_wasm_f16
}  // namespace wasm
}  // namespace internal
}  // namespace v8
