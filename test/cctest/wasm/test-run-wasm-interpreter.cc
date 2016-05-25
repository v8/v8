// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/wasm/wasm-macro-gen.h"

#include "src/wasm/wasm-interpreter.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/test-signatures.h"
#include "test/cctest/wasm/wasm-run-utils.h"

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

namespace v8 {
namespace internal {
namespace wasm {

TEST(Run_WasmInt8Const_i) {
  WasmRunner<int32_t> r(kExecuteInterpreted);
  const byte kExpectedValue = 109;
  // return(kExpectedValue)
  BUILD(r, WASM_I8(kExpectedValue));
  CHECK_EQ(kExpectedValue, r.Call());
}

TEST(Run_WasmIfElse) {
  WasmRunner<int32_t> r(kExecuteInterpreted, MachineType::Int32());
  BUILD(r, WASM_IF_ELSE(WASM_GET_LOCAL(0), WASM_I8(9), WASM_I8(10)));
  CHECK_EQ(10, r.Call(0));
  CHECK_EQ(9, r.Call(1));
}

TEST(Run_WasmIfReturn) {
  WasmRunner<int32_t> r(kExecuteInterpreted, MachineType::Int32());
  BUILD(r, WASM_IF(WASM_GET_LOCAL(0), WASM_RETURN1(WASM_I8(77))), WASM_I8(65));
  CHECK_EQ(65, r.Call(0));
  CHECK_EQ(77, r.Call(1));
}

TEST(Run_WasmNopsN) {
  const int kMaxNops = 10;
  byte code[kMaxNops + 2];
  for (int nops = 0; nops < kMaxNops; nops++) {
    byte expected = static_cast<byte>(20 + nops);
    memset(code, kExprNop, sizeof(code));
    code[nops] = kExprI8Const;
    code[nops + 1] = expected;

    WasmRunner<int32_t> r(kExecuteInterpreted);
    r.Build(code, code + nops + 2);
    CHECK_EQ(expected, r.Call());
  }
}

TEST(Run_WasmConstsN) {
  const int kMaxConsts = 10;
  byte code[kMaxConsts * 2];
  for (int count = 1; count < kMaxConsts; count++) {
    for (int i = 0; i < count; i++) {
      code[i * 2] = kExprI8Const;
      code[i * 2 + 1] = static_cast<byte>(count * 10 + i);
    }
    byte expected = static_cast<byte>(count * 11 - 1);

    WasmRunner<int32_t> r(kExecuteInterpreted);
    r.Build(code, code + (count * 2));
    CHECK_EQ(expected, r.Call());
  }
}

TEST(Run_WasmBlocksN) {
  const int kMaxNops = 10;
  const int kExtra = 4;
  byte code[kMaxNops + kExtra];
  for (int nops = 0; nops < kMaxNops; nops++) {
    byte expected = static_cast<byte>(30 + nops);
    memset(code, kExprNop, sizeof(code));
    code[0] = kExprBlock;
    code[1 + nops] = kExprI8Const;
    code[1 + nops + 1] = expected;
    code[1 + nops + 2] = kExprEnd;

    WasmRunner<int32_t> r(kExecuteInterpreted);
    r.Build(code, code + nops + kExtra);
    CHECK_EQ(expected, r.Call());
  }
}

TEST(Run_WasmBlockBreakN) {
  const int kMaxNops = 10;
  const int kExtra = 6;
  byte code[kMaxNops + kExtra];
  for (int nops = 0; nops < kMaxNops; nops++) {
    // Place the break anywhere within the block.
    for (int index = 0; index < nops; index++) {
      memset(code, kExprNop, sizeof(code));
      code[0] = kExprBlock;
      code[sizeof(code) - 1] = kExprEnd;

      int expected = nops * 11 + index;
      code[1 + index + 0] = kExprI8Const;
      code[1 + index + 1] = static_cast<byte>(expected);
      code[1 + index + 2] = kExprBr;
      code[1 + index + 3] = ARITY_1;
      code[1 + index + 4] = 0;

      WasmRunner<int32_t> r(kExecuteInterpreted);
      r.Build(code, code + kMaxNops + kExtra);
      CHECK_EQ(expected, r.Call());
    }
  }
}

TEST(Run_Wasm_nested_ifs_i) {
  WasmRunner<int32_t> r(kExecuteInterpreted, MachineType::Int32(),
                        MachineType::Int32());

  BUILD(r, WASM_IF_ELSE(
               WASM_GET_LOCAL(0),
               WASM_IF_ELSE(WASM_GET_LOCAL(1), WASM_I8(11), WASM_I8(12)),
               WASM_IF_ELSE(WASM_GET_LOCAL(1), WASM_I8(13), WASM_I8(14))));

  CHECK_EQ(11, r.Call(1, 1));
  CHECK_EQ(12, r.Call(1, 0));
  CHECK_EQ(13, r.Call(0, 1));
  CHECK_EQ(14, r.Call(0, 0));
}

TEST(Step_I32Add) {
  WasmRunner<int32_t> r(kExecuteInterpreted, MachineType::Int32(),
                        MachineType::Int32());
  BUILD(r, WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));

  WasmInterpreter* interpreter = r.interpreter();
  interpreter->SetBreakpoint(r.function(), 0, true);

  r.Call(1, 1);
  interpreter->Run();
  CHECK_EQ(2, interpreter->GetThread(0).GetReturnValue().to<int32_t>());
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
