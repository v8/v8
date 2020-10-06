// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {

namespace {

TEST(RunWasmTurbofan_ExportSameSig) {
  WasmRunner<int32_t> r1(TestExecutionTier::kTurbofan);
  BUILD(r1, kExprI32Const, 0);

  WasmRunner<int32_t> r2(TestExecutionTier::kTurbofan);
  BUILD(r2, kExprI32Const, 1);

  Handle<JSFunction> f1 = r1.builder().WrapCode(0);
  auto shared = f1->shared();
  auto wasm_exported_function_data = shared.wasm_exported_function_data();
  Code code1 = wasm_exported_function_data.wrapper_code();

  Handle<JSFunction> f2 = r2.builder().WrapCode(0);
  shared = f2->shared();
  wasm_exported_function_data = shared.wasm_exported_function_data();
  Code code2 = wasm_exported_function_data.wrapper_code();

  CHECK_EQ(code1, code2);
}

}  // namespace

}  // namespace wasm
}  // namespace internal
}  // namespace v8
