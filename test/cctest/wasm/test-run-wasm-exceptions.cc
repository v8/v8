// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/wasm/wasm-atomics-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_run_wasm_exceptions {

WASM_EXEC_TEST(TryCatchThrow) {
  TestSignatures sigs;
  EXPERIMENTAL_FLAG_SCOPE(eh);
  WasmRunner<uint32_t, uint32_t> r(execution_tier);
  uint32_t except = r.builder().AddException(sigs.v_v());
  constexpr uint32_t kResult0 = 23;
  constexpr uint32_t kResult1 = 42;

  BUILD(r, WASM_TRY_CATCH_T(kWasmI32,
                            WASM_STMTS(WASM_I32V(kResult1),
                                       WASM_IF(WASM_I32_EQZ(WASM_GET_LOCAL(0)),
                                               WASM_THROW(except))),
                            WASM_STMTS(WASM_DROP, WASM_I32V(kResult0))));

  // Need to call through JS to allow for creation of stack traces.
  r.CheckCallViaJS(kResult0, 0);
  r.CheckCallViaJS(kResult1, 1);
}

}  // namespace test_run_wasm_exceptions
}  // namespace wasm
}  // namespace internal
}  // namespace v8
