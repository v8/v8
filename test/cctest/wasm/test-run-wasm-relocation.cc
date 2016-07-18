// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/v8.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/c-signature.h"
#include "test/cctest/wasm/wasm-run-utils.h"

using namespace v8::internal;
using namespace v8::internal::compiler;

#define FOREACH_TYPE(TEST_BODY)             \
  TEST_BODY(int8_t, Int8, WASM_I32_ADD)     \
  TEST_BODY(uint8_t, Uint8, WASM_I32_ADD)   \
  TEST_BODY(int16_t, Int16, WASM_I32_ADD)   \
  TEST_BODY(uint16_t, Uint16, WASM_I32_ADD) \
  TEST_BODY(int32_t, Int32, WASM_I32_ADD)   \
  TEST_BODY(uint32_t, Uint32, WASM_I32_ADD) \
  TEST_BODY(float, Float32, WASM_F32_ADD)   \
  TEST_BODY(double, Float64, WASM_F64_ADD)

#define LOAD_STORE_GLOBAL_TEST_BODY(C_TYPE, MACHINE_TYPE, ADD)                \
  TEST(WasmRelocateGlobal##MACHINE_TYPE) {                                    \
    TestingModule module(kExecuteCompiled);                                   \
    module.AddGlobal<int32_t>(MachineType::MACHINE_TYPE());                   \
    module.AddGlobal<int32_t>(MachineType::MACHINE_TYPE());                   \
                                                                              \
    WasmRunner<C_TYPE> r(&module, MachineType::MACHINE_TYPE());               \
                                                                              \
    /* global = global + p0 */                                                \
    BUILD(r,                                                                  \
          WASM_STORE_GLOBAL(1, ADD(WASM_LOAD_GLOBAL(0), WASM_GET_LOCAL(0)))); \
    CHECK_EQ(1, module.instance->function_code.size());                       \
                                                                              \
    int filter = 1 << RelocInfo::WASM_GLOBAL_REFERENCE;                       \
                                                                              \
    Handle<Code> code = module.instance->function_code[0];                    \
                                                                              \
    Address old_start = module.instance->globals_start;                       \
    Address new_start = old_start + 1;                                        \
                                                                              \
    Address old_addresses[2];                                                 \
    uint32_t address_index = 0U;                                              \
    for (RelocIterator it(*code, filter); !it.done(); it.next()) {            \
      old_addresses[address_index] = it.rinfo()->wasm_global_reference();     \
      it.rinfo()->update_wasm_global_reference(old_start, new_start);         \
      ++address_index;                                                        \
    }                                                                         \
    CHECK_EQ(2U, address_index);                                              \
                                                                              \
    address_index = 0U;                                                       \
    for (RelocIterator it(*code, filter); !it.done(); it.next()) {            \
      CHECK_EQ(old_addresses[address_index] + 1,                              \
               it.rinfo()->wasm_global_reference());                          \
      ++address_index;                                                        \
    }                                                                         \
    CHECK_EQ(2U, address_index);                                              \
  }

FOREACH_TYPE(LOAD_STORE_GLOBAL_TEST_BODY)
