// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-opcodes-inl.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace wasm {

class WasmOpcodeTest : public TestWithZone {
 public:
  void CheckName(WasmOpcode opcode, const char* expected) {
    EXPECT_STREQ(expected, WasmOpcodes::OpcodeName(opcode));
  }
};

TEST_F(WasmOpcodeTest, AtomicNames) {
  // Reference:
  // https://webassembly.github.io/threads/core/text/instructions.html#atomic-memory-instructions
  CheckName(kExprAtomicNotify, "memory.atomic.notify");
  CheckName(kExprI32AtomicWait, "memory.atomic.wait32");
  CheckName(kExprI64AtomicWait, "memory.atomic.wait64");
  CheckName(kExprI32AtomicLoad, "i32.atomic.load");
  CheckName(kExprI64AtomicLoad, "i64.atomic.load");
  CheckName(kExprI32AtomicLoad8U, "i32.atomic.load8_u");
  CheckName(kExprI32AtomicLoad16U, "i32.atomic.load16_u");
  CheckName(kExprI64AtomicLoad8U, "i64.atomic.load8_u");
  CheckName(kExprI64AtomicLoad16U, "i64.atomic.load16_u");
  CheckName(kExprI64AtomicLoad32U, "i64.atomic.load32_u");
  CheckName(kExprI32AtomicStore, "i32.atomic.store");
  CheckName(kExprI64AtomicStore, "i64.atomic.store");
  CheckName(kExprI32AtomicStore8U, "i32.atomic.store8");
  CheckName(kExprI32AtomicStore16U, "i32.atomic.store16");
  CheckName(kExprI64AtomicStore8U, "i64.atomic.store8");
  CheckName(kExprI64AtomicStore16U, "i64.atomic.store16");
  CheckName(kExprI64AtomicStore32U, "i64.atomic.store32");
  CheckName(kExprI32AtomicAdd, "i32.atomic.rmw.add");
  CheckName(kExprI64AtomicAdd, "i64.atomic.rmw.add");
  CheckName(kExprI32AtomicAdd8U, "i32.atomic.rmw8.add_u");
  CheckName(kExprI32AtomicAdd16U, "i32.atomic.rmw16.add_u");
  CheckName(kExprI64AtomicAdd8U, "i64.atomic.rmw8.add_u");
  CheckName(kExprI64AtomicAdd16U, "i64.atomic.rmw16.add_u");
  CheckName(kExprI64AtomicAdd32U, "i64.atomic.rmw32.add_u");
  CheckName(kExprI32AtomicSub, "i32.atomic.rmw.sub");
  CheckName(kExprI64AtomicSub, "i64.atomic.rmw.sub");
  CheckName(kExprI32AtomicSub8U, "i32.atomic.rmw8.sub_u");
  CheckName(kExprI32AtomicSub16U, "i32.atomic.rmw16.sub_u");
  CheckName(kExprI64AtomicSub8U, "i64.atomic.rmw8.sub_u");
  CheckName(kExprI64AtomicSub16U, "i64.atomic.rmw16.sub_u");
  CheckName(kExprI64AtomicSub32U, "i64.atomic.rmw32.sub_u");
  CheckName(kExprI32AtomicAnd, "i32.atomic.rmw.and");
  CheckName(kExprI64AtomicAnd, "i64.atomic.rmw.and");
  CheckName(kExprI32AtomicAnd8U, "i32.atomic.rmw8.and_u");
  CheckName(kExprI32AtomicAnd16U, "i32.atomic.rmw16.and_u");
  CheckName(kExprI64AtomicAnd8U, "i64.atomic.rmw8.and_u");
  CheckName(kExprI64AtomicAnd16U, "i64.atomic.rmw16.and_u");
  CheckName(kExprI64AtomicAnd32U, "i64.atomic.rmw32.and_u");
  CheckName(kExprI32AtomicOr, "i32.atomic.rmw.or");
  CheckName(kExprI64AtomicOr, "i64.atomic.rmw.or");
  CheckName(kExprI32AtomicOr8U, "i32.atomic.rmw8.or_u");
  CheckName(kExprI32AtomicOr16U, "i32.atomic.rmw16.or_u");
  CheckName(kExprI64AtomicOr8U, "i64.atomic.rmw8.or_u");
  CheckName(kExprI64AtomicOr16U, "i64.atomic.rmw16.or_u");
  CheckName(kExprI64AtomicOr32U, "i64.atomic.rmw32.or_u");
  CheckName(kExprI32AtomicXor, "i32.atomic.rmw.xor");
  CheckName(kExprI64AtomicXor, "i64.atomic.rmw.xor");
  CheckName(kExprI32AtomicXor8U, "i32.atomic.rmw8.xor_u");
  CheckName(kExprI32AtomicXor16U, "i32.atomic.rmw16.xor_u");
  CheckName(kExprI64AtomicXor8U, "i64.atomic.rmw8.xor_u");
  CheckName(kExprI64AtomicXor16U, "i64.atomic.rmw16.xor_u");
  CheckName(kExprI64AtomicXor32U, "i64.atomic.rmw32.xor_u");
  CheckName(kExprI32AtomicExchange, "i32.atomic.rmw.xchg");
  CheckName(kExprI64AtomicExchange, "i64.atomic.rmw.xchg");
  CheckName(kExprI32AtomicExchange8U, "i32.atomic.rmw8.xchg_u");
  CheckName(kExprI32AtomicExchange16U, "i32.atomic.rmw16.xchg_u");
  CheckName(kExprI64AtomicExchange8U, "i64.atomic.rmw8.xchg_u");
  CheckName(kExprI64AtomicExchange16U, "i64.atomic.rmw16.xchg_u");
  CheckName(kExprI64AtomicExchange32U, "i64.atomic.rmw32.xchg_u");
  CheckName(kExprI32AtomicCompareExchange, "i32.atomic.rmw.cmpxchg");
  CheckName(kExprI64AtomicCompareExchange, "i64.atomic.rmw.cmpxchg");
  CheckName(kExprI32AtomicCompareExchange8U, "i32.atomic.rmw8.cmpxchg_u");
  CheckName(kExprI32AtomicCompareExchange16U, "i32.atomic.rmw16.cmpxchg_u");
  CheckName(kExprI64AtomicCompareExchange8U, "i64.atomic.rmw8.cmpxchg_u");
  CheckName(kExprI64AtomicCompareExchange16U, "i64.atomic.rmw16.cmpxchg_u");
  CheckName(kExprI64AtomicCompareExchange32U, "i64.atomic.rmw32.cmpxchg_u");
  // https://github.com/WebAssembly/threads/blob/main/proposals/threads/Overview.md#fence-operator
  CheckName(kExprAtomicFence, "atomic.fence");
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
