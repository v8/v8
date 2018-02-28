// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/wasm/wasm-atomics-utils.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace atomics_64 {

void RunU64BinOp(WasmExecutionMode execution_mode, WasmOpcode wasm_op,
                 Uint64BinOp expected_op) {
  EXPERIMENTAL_FLAG_SCOPE(threads);
  WasmRunner<uint64_t, uint64_t> r(execution_mode);
  uint64_t* memory = r.builder().AddMemoryElems<uint64_t>(8);
  r.builder().SetHasSharedMemory();

  BUILD(r, WASM_ATOMICS_BINOP(wasm_op, WASM_I32V_1(0), WASM_GET_LOCAL(0),
                              MachineRepresentation::kWord64));

  FOR_UINT64_INPUTS(i) {
    uint64_t initial = *i;
    FOR_UINT64_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(*j));
      uint64_t expected = expected_op(*i, *j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

WASM_COMPILED_EXEC_TEST(I64AtomicAdd) {
  RunU64BinOp(execution_mode, kExprI64AtomicAdd, Add);
}
WASM_COMPILED_EXEC_TEST(I64AtomicSub) {
  RunU64BinOp(execution_mode, kExprI64AtomicSub, Sub);
}
WASM_COMPILED_EXEC_TEST(I64AtomicAnd) {
  RunU64BinOp(execution_mode, kExprI64AtomicAnd, And);
}
WASM_COMPILED_EXEC_TEST(I64AtomicOr) {
  RunU64BinOp(execution_mode, kExprI64AtomicOr, Or);
}
WASM_COMPILED_EXEC_TEST(I64AtomicXor) {
  RunU64BinOp(execution_mode, kExprI64AtomicXor, Xor);
}

void RunU32BinOp(WasmExecutionMode execution_mode, WasmOpcode wasm_op,
                 Uint32BinOp expected_op) {
  EXPERIMENTAL_FLAG_SCOPE(threads);
  WasmRunner<uint64_t, uint64_t> r(execution_mode);
  uint32_t* memory = r.builder().AddMemoryElems<uint32_t>(8);
  r.builder().SetHasSharedMemory();

  BUILD(r, WASM_ATOMICS_BINOP(wasm_op, WASM_I32V_1(0), WASM_GET_LOCAL(0),
                              MachineRepresentation::kWord32));

  FOR_UINT32_INPUTS(i) {
    uint32_t initial = *i;
    FOR_UINT32_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(*j));
      uint32_t expected = expected_op(*i, *j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

WASM_COMPILED_EXEC_TEST(I64AtomicAdd32U) {
  RunU32BinOp(execution_mode, kExprI64AtomicAdd32U, Add);
}
WASM_COMPILED_EXEC_TEST(I64AtomicSub32U) {
  RunU32BinOp(execution_mode, kExprI64AtomicSub32U, Sub);
}
WASM_COMPILED_EXEC_TEST(I64AtomicAnd32U) {
  RunU32BinOp(execution_mode, kExprI64AtomicAnd32U, And);
}
WASM_COMPILED_EXEC_TEST(I64AtomicOr32U) {
  RunU32BinOp(execution_mode, kExprI64AtomicOr32U, Or);
}
WASM_COMPILED_EXEC_TEST(I64AtomicXor32U) {
  RunU32BinOp(execution_mode, kExprI64AtomicXor32U, Xor);
}

void RunU16BinOp(WasmExecutionMode mode, WasmOpcode wasm_op,
                 Uint16BinOp expected_op) {
  EXPERIMENTAL_FLAG_SCOPE(threads);
  WasmRunner<uint64_t, uint64_t> r(mode);
  r.builder().SetHasSharedMemory();
  uint16_t* memory = r.builder().AddMemoryElems<uint16_t>(8);

  BUILD(r, WASM_ATOMICS_BINOP(wasm_op, WASM_I32V_1(0), WASM_GET_LOCAL(0),
                              MachineRepresentation::kWord16));

  FOR_UINT16_INPUTS(i) {
    uint16_t initial = *i;
    FOR_UINT16_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(*j));
      uint16_t expected = expected_op(*i, *j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

WASM_COMPILED_EXEC_TEST(I64AtomicAdd16U) {
  RunU16BinOp(execution_mode, kExprI64AtomicAdd16U, Add);
}
WASM_COMPILED_EXEC_TEST(I64AtomicSub16U) {
  RunU16BinOp(execution_mode, kExprI64AtomicSub16U, Sub);
}
WASM_COMPILED_EXEC_TEST(I64AtomicAnd16U) {
  RunU16BinOp(execution_mode, kExprI64AtomicAnd16U, And);
}
WASM_COMPILED_EXEC_TEST(I64AtomicOr16U) {
  RunU16BinOp(execution_mode, kExprI64AtomicOr16U, Or);
}
WASM_COMPILED_EXEC_TEST(I64AtomicXor16U) {
  RunU16BinOp(execution_mode, kExprI64AtomicXor16U, Xor);
}

void RunU8BinOp(WasmExecutionMode execution_mode, WasmOpcode wasm_op,
                Uint8BinOp expected_op) {
  EXPERIMENTAL_FLAG_SCOPE(threads);
  WasmRunner<uint64_t, uint64_t> r(execution_mode);
  r.builder().SetHasSharedMemory();
  uint8_t* memory = r.builder().AddMemoryElems<uint8_t>(8);

  BUILD(r, WASM_ATOMICS_BINOP(wasm_op, WASM_I32V_1(0), WASM_GET_LOCAL(0),
                              MachineRepresentation::kWord8));

  FOR_UINT8_INPUTS(i) {
    uint8_t initial = *i;
    FOR_UINT8_INPUTS(j) {
      r.builder().WriteMemory(&memory[0], initial);
      CHECK_EQ(initial, r.Call(*j));
      uint8_t expected = expected_op(*i, *j);
      CHECK_EQ(expected, r.builder().ReadMemory(&memory[0]));
    }
  }
}

WASM_COMPILED_EXEC_TEST(I64AtomicAdd8U) {
  RunU8BinOp(execution_mode, kExprI64AtomicAdd8U, Add);
}
WASM_COMPILED_EXEC_TEST(I64AtomicSub8U) {
  RunU8BinOp(execution_mode, kExprI64AtomicSub8U, Sub);
}
WASM_COMPILED_EXEC_TEST(I64AtomicAnd8U) {
  RunU8BinOp(execution_mode, kExprI64AtomicAnd8U, And);
}
WASM_COMPILED_EXEC_TEST(I64AtomicOr8U) {
  RunU8BinOp(execution_mode, kExprI64AtomicOr8U, Or);
}
WASM_COMPILED_EXEC_TEST(I64AtomicXor8U) {
  RunU8BinOp(execution_mode, kExprI64AtomicXor8U, Xor);
}

}  // namespace atomics_64
}  // namespace wasm
}  // namespace internal
}  // namespace v8
