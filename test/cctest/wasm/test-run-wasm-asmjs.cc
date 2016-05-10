// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/base/platform/elapsed-timer.h"

#include "src/wasm/wasm-macro-gen.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/test-signatures.h"
#include "test/cctest/wasm/wasm-run-utils.h"

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

// for even shorter tests.
#define B2(a, b) kExprBlock, a, b, kExprEnd
#define B1(a) kExprBlock, a, kExprEnd
#define RET(x) x, kExprReturn, 1
#define RET_I8(x) kExprI8Const, x, kExprReturn, 1

TEST(Run_WASM_Int32AsmjsDivS) {
  WasmRunner<int32_t> r(MachineType::Int32(), MachineType::Int32());
  BUILD(r, WASM_BINOP(kExprI32AsmjsDivS, WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_EQ(0, r.Call(0, 100));
  CHECK_EQ(0, r.Call(100, 0));
  CHECK_EQ(0, r.Call(-1001, 0));
  CHECK_EQ(kMin, r.Call(kMin, -1));
  CHECK_EQ(0, r.Call(kMin, 0));
}

TEST(Run_WASM_Int32AsmjsRemS) {
  WasmRunner<int32_t> r(MachineType::Int32(), MachineType::Int32());
  BUILD(r, WASM_BINOP(kExprI32AsmjsRemS, WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_EQ(33, r.Call(133, 100));
  CHECK_EQ(0, r.Call(kMin, -1));
  CHECK_EQ(0, r.Call(100, 0));
  CHECK_EQ(0, r.Call(-1001, 0));
  CHECK_EQ(0, r.Call(kMin, 0));
}

TEST(Run_WASM_Int32AsmjsDivU) {
  WasmRunner<int32_t> r(MachineType::Int32(), MachineType::Int32());
  BUILD(r, WASM_BINOP(kExprI32AsmjsDivU, WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_EQ(0, r.Call(0, 100));
  CHECK_EQ(0, r.Call(kMin, -1));
  CHECK_EQ(0, r.Call(100, 0));
  CHECK_EQ(0, r.Call(-1001, 0));
  CHECK_EQ(0, r.Call(kMin, 0));
}

TEST(Run_WASM_Int32AsmjsRemU) {
  WasmRunner<int32_t> r(MachineType::Int32(), MachineType::Int32());
  BUILD(r, WASM_BINOP(kExprI32AsmjsRemU, WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  const int32_t kMin = std::numeric_limits<int32_t>::min();
  CHECK_EQ(17, r.Call(217, 100));
  CHECK_EQ(0, r.Call(100, 0));
  CHECK_EQ(0, r.Call(-1001, 0));
  CHECK_EQ(0, r.Call(kMin, 0));
  CHECK_EQ(kMin, r.Call(kMin, -1));
}

TEST(Run_Wasm_LoadMemI32_oob_asm) {
  TestingModule module;
  module.origin = kAsmJsOrigin;
  int32_t* memory = module.AddMemoryElems<int32_t>(8);
  WasmRunner<int32_t> r(&module, MachineType::Uint32());
  module.RandomizeMemory(1112);

  BUILD(r, WASM_LOAD_MEM(MachineType::Int32(), WASM_GET_LOCAL(0)));

  memory[0] = 999999;
  CHECK_EQ(999999, r.Call(0u));
  // TODO(titzer): offset 29-31 should also be OOB.
  for (uint32_t offset = 32; offset < 40; offset++) {
    CHECK_EQ(0, r.Call(offset));
  }

  for (uint32_t offset = 0x80000000; offset < 0x80000010; offset++) {
    CHECK_EQ(0, r.Call(offset));
  }
}
