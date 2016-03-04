// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/wasm/wasm-macro-gen.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/wasm-run-utils.h"

// using namespace v8::base;
// using namespace v8::internal;
// using namespace v8::internal::compiler;
// using namespace v8::internal::wasm;

// todo(ahaas): I added a list of missing instructions here to make merging
// easier when I do them one by one.
// kExprI64Add:
// kExprI64Sub:
// kExprI64Mul:
// kExprI64DivS:
// kExprI64DivU:
// kExprI64RemS:
// kExprI64RemU:
// kExprI64And:
TEST(Run_WasmI64And) {
  WasmRunner<int64_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_AND(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ((*i) & (*j), r.Call(*i, *j)); }
  }
}
// kExprI64Ior:
TEST(Run_WasmI64Ior) {
  WasmRunner<int64_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_IOR(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ((*i) | (*j), r.Call(*i, *j)); }
  }
}
// kExprI64Xor:
TEST(Run_WasmI64Xor) {
  WasmRunner<int64_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_XOR(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ((*i) ^ (*j), r.Call(*i, *j)); }
  }
}
// kExprI64Shl:
// kExprI64ShrU:
// kExprI64ShrS:
// kExprI64Eq:
TEST(Run_WasmI64Eq) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_EQ(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(*i == *j ? 1 : 0, r.Call(*i, *j)); }
  }
}
// kExprI64Ne:
TEST(Run_WasmI64Ne) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_NE(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(*i != *j ? 1 : 0, r.Call(*i, *j)); }
  }
}
// kExprI64LtS:
TEST(Run_WasmI64LtS) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_LTS(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(*i < *j ? 1 : 0, r.Call(*i, *j)); }
  }
}
TEST(Run_WasmI64LeS) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_LES(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(*i <= *j ? 1 : 0, r.Call(*i, *j)); }
  }
}
TEST(Run_WasmI64LtU) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_LTU(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) { CHECK_EQ(*i < *j ? 1 : 0, r.Call(*i, *j)); }
  }
}
TEST(Run_WasmI64LeU) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_LEU(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) { CHECK_EQ(*i <= *j ? 1 : 0, r.Call(*i, *j)); }
  }
}
TEST(Run_WasmI64GtS) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_GTS(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(*i > *j ? 1 : 0, r.Call(*i, *j)); }
  }
}
TEST(Run_WasmI64GeS) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_GES(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_INT64_INPUTS(i) {
    FOR_INT64_INPUTS(j) { CHECK_EQ(*i >= *j ? 1 : 0, r.Call(*i, *j)); }
  }
}

TEST(Run_WasmI64GtU) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_GTU(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) { CHECK_EQ(*i > *j ? 1 : 0, r.Call(*i, *j)); }
  }
}

TEST(Run_WasmI64GeU) {
  WasmRunner<int32_t> r(MachineType::Int64(), MachineType::Int64());
  BUILD(r, WASM_I64_GEU(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
  FOR_UINT64_INPUTS(i) {
    FOR_UINT64_INPUTS(j) { CHECK_EQ(*i >= *j ? 1 : 0, r.Call(*i, *j)); }
  }
}
// kExprI32ConvertI64:
TEST(Run_WasmI32ConvertI64) {
  FOR_INT64_INPUTS(i) {
    WasmRunner<int32_t> r;
    BUILD(r, WASM_I32_CONVERT_I64(WASM_I64V(*i)));
    CHECK_EQ(static_cast<int32_t>(*i), r.Call());
  }
}
// kExprI64SConvertI32:
// kExprI64UConvertI32:

// kExprF64ReinterpretI64:
// kExprI64ReinterpretF64:

// kExprI64Clz:
// kExprI64Ctz:
// kExprI64Popcnt:

// kExprF32SConvertI64:
// kExprF32UConvertI64:
// kExprF64SConvertI64:
// kExprF64UConvertI64:
// kExprI64SConvertF32:
// kExprI64SConvertF64:
// kExprI64UConvertF32:
// kExprI64UConvertF64:

TEST(Run_WasmCallI64Parameter) {
  // Build the target function.
  LocalType param_types[20];
  for (int i = 0; i < 20; i++) param_types[i] = kAstI64;
  param_types[3] = kAstI32;
  param_types[4] = kAstI32;
  FunctionSig sig(1, 19, param_types);
  for (int i = 0; i < 19; i++) {
    TestingModule module;
    WasmFunctionCompiler t(&sig, &module);
    if (i == 2 || i == 3) {
      continue;
    } else {
      BUILD(t, WASM_GET_LOCAL(i));
    }
    uint32_t index = t.CompileAndAdd();

    // Build the calling function.
    WasmRunner<int32_t> r;
    r.env()->module = &module;
    BUILD(
        r,
        WASM_I32_CONVERT_I64(WASM_CALL_FUNCTION(
            index, WASM_I64V_9(0xbcd12340000000b),
            WASM_I64V_9(0xbcd12340000000c), WASM_I32V_1(0xd),
            WASM_I32_CONVERT_I64(WASM_I64V_9(0xbcd12340000000e)),
            WASM_I64V_9(0xbcd12340000000f), WASM_I64V_10(0xbcd1234000000010),
            WASM_I64V_10(0xbcd1234000000011), WASM_I64V_10(0xbcd1234000000012),
            WASM_I64V_10(0xbcd1234000000013), WASM_I64V_10(0xbcd1234000000014),
            WASM_I64V_10(0xbcd1234000000015), WASM_I64V_10(0xbcd1234000000016),
            WASM_I64V_10(0xbcd1234000000017), WASM_I64V_10(0xbcd1234000000018),
            WASM_I64V_10(0xbcd1234000000019), WASM_I64V_10(0xbcd123400000001a),
            WASM_I64V_10(0xbcd123400000001b), WASM_I64V_10(0xbcd123400000001c),
            WASM_I64V_10(0xbcd123400000001d))));

    CHECK_EQ(i + 0xb, r.Call());
  }
}
