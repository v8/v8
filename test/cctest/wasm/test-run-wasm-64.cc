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
#if !V8_TARGET_ARCH_MIPS && !V8_TARGET_ARCH_X87
TEST(Run_WasmI64Shl) {
  {
    WasmRunner<int64_t> r(MachineType::Int64(), MachineType::Int64());
    BUILD(r, WASM_I64_SHL(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
    FOR_UINT64_INPUTS(i) {
      for (int64_t j = 1; j < 64; j++) {
        CHECK_EQ(*i << j, r.Call(*i, j));
      }
    }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SHL(WASM_GET_LOCAL(0), WASM_I64V_1(0)));
    FOR_UINT64_INPUTS(i) { CHECK_EQ(*i << 0, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SHL(WASM_GET_LOCAL(0), WASM_I64V_1(32)));
    FOR_UINT64_INPUTS(i) { CHECK_EQ(*i << 32, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SHL(WASM_GET_LOCAL(0), WASM_I64V_1(20)));
    FOR_UINT64_INPUTS(i) { CHECK_EQ(*i << 20, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SHL(WASM_GET_LOCAL(0), WASM_I64V_1(40)));
    FOR_UINT64_INPUTS(i) { CHECK_EQ(*i << 40, r.Call(*i)); }
  }
}
#endif
// kExprI64ShrU:
#if !V8_TARGET_ARCH_MIPS && !V8_TARGET_ARCH_X87 && !V8_TARGET_ARCH_ARM
TEST(Run_WasmI64ShrU) {
  {
    WasmRunner<int64_t> r(MachineType::Int64(), MachineType::Int64());
    BUILD(r, WASM_I64_SHR(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
    FOR_UINT64_INPUTS(i) {
      for (int64_t j = 1; j < 64; j++) {
        CHECK_EQ(*i >> j, r.Call(*i, j));
      }
    }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SHR(WASM_GET_LOCAL(0), WASM_I64V_1(0)));
    FOR_UINT64_INPUTS(i) { CHECK_EQ(*i >> 0, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SHR(WASM_GET_LOCAL(0), WASM_I64V_1(32)));
    FOR_UINT64_INPUTS(i) { CHECK_EQ(*i >> 32, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SHR(WASM_GET_LOCAL(0), WASM_I64V_1(20)));
    FOR_UINT64_INPUTS(i) { CHECK_EQ(*i >> 20, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SHR(WASM_GET_LOCAL(0), WASM_I64V_1(40)));
    FOR_UINT64_INPUTS(i) { CHECK_EQ(*i >> 40, r.Call(*i)); }
  }
}
#endif
// kExprI64ShrS:
#if !V8_TARGET_ARCH_MIPS && !V8_TARGET_ARCH_X87 && !V8_TARGET_ARCH_ARM
TEST(Run_WasmI64ShrS) {
  {
    WasmRunner<int64_t> r(MachineType::Int64(), MachineType::Int64());
    BUILD(r, WASM_I64_SAR(WASM_GET_LOCAL(0), WASM_GET_LOCAL(1)));
    FOR_INT64_INPUTS(i) {
      for (int64_t j = 1; j < 64; j++) {
        CHECK_EQ(*i >> j, r.Call(*i, j));
      }
    }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SAR(WASM_GET_LOCAL(0), WASM_I64V_1(0)));
    FOR_INT64_INPUTS(i) { CHECK_EQ(*i >> 0, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SAR(WASM_GET_LOCAL(0), WASM_I64V_1(32)));
    FOR_INT64_INPUTS(i) { CHECK_EQ(*i >> 32, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SAR(WASM_GET_LOCAL(0), WASM_I64V_1(20)));
    FOR_INT64_INPUTS(i) { CHECK_EQ(*i >> 20, r.Call(*i)); }
  }
  {
    WasmRunner<int64_t> r(MachineType::Int64());
    BUILD(r, WASM_I64_SAR(WASM_GET_LOCAL(0), WASM_I64V_1(40)));
    FOR_INT64_INPUTS(i) { CHECK_EQ(*i >> 40, r.Call(*i)); }
  }
}
#endif
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
TEST(Run_WasmI64SConvertI32) {
  WasmRunner<int64_t> r(MachineType::Int32());
  BUILD(r, WASM_I64_SCONVERT_I32(WASM_GET_LOCAL(0)));
  FOR_INT32_INPUTS(i) { CHECK_EQ(static_cast<int64_t>(*i), r.Call(*i)); }
}

// kExprI64UConvertI32:
TEST(Run_WasmI64UConvertI32) {
  WasmRunner<int64_t> r(MachineType::Uint32());
  BUILD(r, WASM_I64_UCONVERT_I32(WASM_GET_LOCAL(0)));
  FOR_UINT32_INPUTS(i) { CHECK_EQ(static_cast<uint64_t>(*i), r.Call(*i)); }
}

// kExprF64ReinterpretI64:
// kExprI64ReinterpretF64:

// kExprI64Clz:
// kExprI64Ctz:
// kExprI64Popcnt:

// kExprF32SConvertI64:
TEST(Run_WasmF32SConvertI64) {
  WasmRunner<float> r(MachineType::Int64());
  BUILD(r, WASM_F32_SCONVERT_I64(WASM_GET_LOCAL(0)));
  FOR_INT64_INPUTS(i) { CHECK_EQ(static_cast<float>(*i), r.Call(*i)); }
}
// kExprF32UConvertI64:
TEST(Run_WasmF32UConvertI64) {
  struct {
    uint64_t input;
    uint32_t expected;
  } values[] = {{0x0, 0x0},
                {0x1, 0x3f800000},
                {0xffffffff, 0x4f800000},
                {0x1b09788b, 0x4dd84bc4},
                {0x4c5fce8, 0x4c98bf9d},
                {0xcc0de5bf, 0x4f4c0de6},
                {0x2, 0x40000000},
                {0x3, 0x40400000},
                {0x4, 0x40800000},
                {0x5, 0x40a00000},
                {0x8, 0x41000000},
                {0x9, 0x41100000},
                {0xffffffffffffffff, 0x5f800000},
                {0xfffffffffffffffe, 0x5f800000},
                {0xfffffffffffffffd, 0x5f800000},
                {0x0, 0x0},
                {0x100000000, 0x4f800000},
                {0xffffffff00000000, 0x5f800000},
                {0x1b09788b00000000, 0x5dd84bc4},
                {0x4c5fce800000000, 0x5c98bf9d},
                {0xcc0de5bf00000000, 0x5f4c0de6},
                {0x200000000, 0x50000000},
                {0x300000000, 0x50400000},
                {0x400000000, 0x50800000},
                {0x500000000, 0x50a00000},
                {0x800000000, 0x51000000},
                {0x900000000, 0x51100000},
                {0x273a798e187937a3, 0x5e1ce9e6},
                {0xece3af835495a16b, 0x5f6ce3b0},
                {0xb668ecc11223344, 0x5d3668ed},
                {0x9e, 0x431e0000},
                {0x43, 0x42860000},
                {0xaf73, 0x472f7300},
                {0x116b, 0x458b5800},
                {0x658ecc, 0x4acb1d98},
                {0x2b3b4c, 0x4a2ced30},
                {0x88776655, 0x4f087766},
                {0x70000000, 0x4ee00000},
                {0x7200000, 0x4ce40000},
                {0x7fffffff, 0x4f000000},
                {0x56123761, 0x4eac246f},
                {0x7fffff00, 0x4efffffe},
                {0x761c4761eeeeeeee, 0x5eec388f},
                {0x80000000eeeeeeee, 0x5f000000},
                {0x88888888dddddddd, 0x5f088889},
                {0xa0000000dddddddd, 0x5f200000},
                {0xddddddddaaaaaaaa, 0x5f5dddde},
                {0xe0000000aaaaaaaa, 0x5f600000},
                {0xeeeeeeeeeeeeeeee, 0x5f6eeeef},
                {0xfffffffdeeeeeeee, 0x5f800000},
                {0xf0000000dddddddd, 0x5f700000},
                {0x7fffffdddddddd, 0x5b000000},
                {0x3fffffaaaaaaaa, 0x5a7fffff},
                {0x1fffffaaaaaaaa, 0x59fffffd},
                {0xfffff, 0x497ffff0},
                {0x7ffff, 0x48ffffe0},
                {0x3ffff, 0x487fffc0},
                {0x1ffff, 0x47ffff80},
                {0xffff, 0x477fff00},
                {0x7fff, 0x46fffe00},
                {0x3fff, 0x467ffc00},
                {0x1fff, 0x45fff800},
                {0xfff, 0x457ff000},
                {0x7ff, 0x44ffe000},
                {0x3ff, 0x447fc000},
                {0x1ff, 0x43ff8000},
                {0x3fffffffffff, 0x56800000},
                {0x1fffffffffff, 0x56000000},
                {0xfffffffffff, 0x55800000},
                {0x7ffffffffff, 0x55000000},
                {0x3ffffffffff, 0x54800000},
                {0x1ffffffffff, 0x54000000},
                {0x8000008000000000, 0x5f000000},
                {0x8000008000000001, 0x5f000001},
                {0x8000000000000400, 0x5f000000},
                {0x8000000000000401, 0x5f000000}};
  WasmRunner<float> r(MachineType::Uint64());
  BUILD(r, WASM_F32_UCONVERT_I64(WASM_GET_LOCAL(0)));
  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(bit_cast<float>(values[i].expected), r.Call(values[i].input));
  }
}
// kExprF64SConvertI64:
TEST(Run_WasmF64SConvertI64) {
  WasmRunner<double> r(MachineType::Int64());
  BUILD(r, WASM_F64_SCONVERT_I64(WASM_GET_LOCAL(0)));
  FOR_INT64_INPUTS(i) { CHECK_EQ(static_cast<double>(*i), r.Call(*i)); }
}
// kExprF64UConvertI64:
TEST(Run_Wasm_F64UConvertI64) {
  struct {
    uint64_t input;
    uint64_t expected;
  } values[] = {{0x0, 0x0},
                {0x1, 0x3ff0000000000000},
                {0xffffffff, 0x41efffffffe00000},
                {0x1b09788b, 0x41bb09788b000000},
                {0x4c5fce8, 0x419317f3a0000000},
                {0xcc0de5bf, 0x41e981bcb7e00000},
                {0x2, 0x4000000000000000},
                {0x3, 0x4008000000000000},
                {0x4, 0x4010000000000000},
                {0x5, 0x4014000000000000},
                {0x8, 0x4020000000000000},
                {0x9, 0x4022000000000000},
                {0xffffffffffffffff, 0x43f0000000000000},
                {0xfffffffffffffffe, 0x43f0000000000000},
                {0xfffffffffffffffd, 0x43f0000000000000},
                {0x100000000, 0x41f0000000000000},
                {0xffffffff00000000, 0x43efffffffe00000},
                {0x1b09788b00000000, 0x43bb09788b000000},
                {0x4c5fce800000000, 0x439317f3a0000000},
                {0xcc0de5bf00000000, 0x43e981bcb7e00000},
                {0x200000000, 0x4200000000000000},
                {0x300000000, 0x4208000000000000},
                {0x400000000, 0x4210000000000000},
                {0x500000000, 0x4214000000000000},
                {0x800000000, 0x4220000000000000},
                {0x900000000, 0x4222000000000000},
                {0x273a798e187937a3, 0x43c39d3cc70c3c9c},
                {0xece3af835495a16b, 0x43ed9c75f06a92b4},
                {0xb668ecc11223344, 0x43a6cd1d98224467},
                {0x9e, 0x4063c00000000000},
                {0x43, 0x4050c00000000000},
                {0xaf73, 0x40e5ee6000000000},
                {0x116b, 0x40b16b0000000000},
                {0x658ecc, 0x415963b300000000},
                {0x2b3b4c, 0x41459da600000000},
                {0x88776655, 0x41e10eeccaa00000},
                {0x70000000, 0x41dc000000000000},
                {0x7200000, 0x419c800000000000},
                {0x7fffffff, 0x41dfffffffc00000},
                {0x56123761, 0x41d5848dd8400000},
                {0x7fffff00, 0x41dfffffc0000000},
                {0x761c4761eeeeeeee, 0x43dd8711d87bbbbc},
                {0x80000000eeeeeeee, 0x43e00000001dddde},
                {0x88888888dddddddd, 0x43e11111111bbbbc},
                {0xa0000000dddddddd, 0x43e40000001bbbbc},
                {0xddddddddaaaaaaaa, 0x43ebbbbbbbb55555},
                {0xe0000000aaaaaaaa, 0x43ec000000155555},
                {0xeeeeeeeeeeeeeeee, 0x43edddddddddddde},
                {0xfffffffdeeeeeeee, 0x43efffffffbdddde},
                {0xf0000000dddddddd, 0x43ee0000001bbbbc},
                {0x7fffffdddddddd, 0x435ffffff7777777},
                {0x3fffffaaaaaaaa, 0x434fffffd5555555},
                {0x1fffffaaaaaaaa, 0x433fffffaaaaaaaa},
                {0xfffff, 0x412ffffe00000000},
                {0x7ffff, 0x411ffffc00000000},
                {0x3ffff, 0x410ffff800000000},
                {0x1ffff, 0x40fffff000000000},
                {0xffff, 0x40efffe000000000},
                {0x7fff, 0x40dfffc000000000},
                {0x3fff, 0x40cfff8000000000},
                {0x1fff, 0x40bfff0000000000},
                {0xfff, 0x40affe0000000000},
                {0x7ff, 0x409ffc0000000000},
                {0x3ff, 0x408ff80000000000},
                {0x1ff, 0x407ff00000000000},
                {0x3fffffffffff, 0x42cfffffffffff80},
                {0x1fffffffffff, 0x42bfffffffffff00},
                {0xfffffffffff, 0x42affffffffffe00},
                {0x7ffffffffff, 0x429ffffffffffc00},
                {0x3ffffffffff, 0x428ffffffffff800},
                {0x1ffffffffff, 0x427ffffffffff000},
                {0x8000008000000000, 0x43e0000010000000},
                {0x8000008000000001, 0x43e0000010000000},
                {0x8000000000000400, 0x43e0000000000000},
                {0x8000000000000401, 0x43e0000000000001}};
  WasmRunner<double> r(MachineType::Uint64());
  BUILD(r, WASM_F64_UCONVERT_I64(WASM_GET_LOCAL(0)));
  for (size_t i = 0; i < arraysize(values); i++) {
    CHECK_EQ(bit_cast<double>(values[i].expected), r.Call(values[i].input));
  }
}
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
    WasmRunner<int32_t> r(&module);
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
