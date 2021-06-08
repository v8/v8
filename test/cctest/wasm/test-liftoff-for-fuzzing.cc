// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// #include "src/api/api-inl.h"
// #include "test/cctest/wasm/wasm-atomics-utils.h"
#include "test/cctest/cctest.h"
#include "test/cctest/wasm/wasm-run-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace test_liftoff_for_fuzzing {

TEST(MaxSteps) {
  WasmRunner<uint32_t> r(TestExecutionTier::kLiftoffForFuzzing);

  BUILD(r, WASM_LOOP(WASM_BR(0)), WASM_I32V(23));
  r.SetMaxSteps(10);
  r.CheckCallViaJSTraps();
}

}  // namespace test_liftoff_for_fuzzing
}  // namespace wasm
}  // namespace internal
}  // namespace v8
