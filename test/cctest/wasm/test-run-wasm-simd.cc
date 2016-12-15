// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-macro-gen.h"

#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/wasm-run-utils.h"

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

// TODO(gdeepti): These are tests using sample values to verify functional
// correctness of opcodes, add more tests for a range of values and macroize
// tests.

#define WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lane_value, lane_index) \
  WASM_IF(WASM_##LANE_TYPE##_NE(WASM_GET_LOCAL(lane_value),                  \
                                WASM_SIMD_##TYPE##_EXTRACT_LANE(             \
                                    lane_index, WASM_GET_LOCAL(value))),     \
          WASM_RETURN1(WASM_ZERO))

#define WASM_SIMD_CHECK4(TYPE, value, LANE_TYPE, lv0, lv1, lv2, lv3) \
  WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv0, 0)               \
  , WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv1, 1),            \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv2, 2),          \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv3, 3)

#define WASM_SIMD_CHECK_SPLAT4(TYPE, value, LANE_TYPE, lv) \
  WASM_SIMD_CHECK4(TYPE, value, LANE_TYPE, lv, lv, lv, lv)

WASM_EXEC_TEST(I32x4Splat) {
  FLAG_wasm_simd_prototype = true;

  // Store SIMD value in a local variable, use extract lane to check lane values
  // This test is not a test for ExtractLane as Splat does not create
  // interesting SIMD values.
  //
  // SetLocal(1, I32x4Splat(Local(0)));
  // For each lane index
  // if(Local(0) != I32x4ExtractLane(Local(1), index)
  //   return 0
  //
  // return 1
  WasmRunner<int32_t> r(kExecuteCompiled, MachineType::Int32());
  byte lane_val = 0;
  byte simd = r.AllocateLocal(kAstS128);
  BUILD(r, WASM_BLOCK(WASM_SET_LOCAL(simd, WASM_SIMD_I32x4_SPLAT(
                                               WASM_GET_LOCAL(lane_val))),
                      WASM_SIMD_CHECK_SPLAT4(I32x4, simd, I32, lane_val),
                      WASM_RETURN1(WASM_ONE)));

  FOR_INT32_INPUTS(i) { CHECK_EQ(1, r.Call(*i)); }
}

WASM_EXEC_TEST(I32x4ReplaceLane) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled, MachineType::Int32(),
                        MachineType::Int32());
  byte old_val = 0;
  byte new_val = 1;
  byte simd = r.AllocateLocal(kAstS128);
  BUILD(r, WASM_BLOCK(
               WASM_SET_LOCAL(simd,
                              WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(old_val))),
               WASM_SET_LOCAL(
                   simd, WASM_SIMD_I32x4_REPLACE_LANE(0, WASM_GET_LOCAL(simd),
                                                      WASM_GET_LOCAL(new_val))),
               WASM_SIMD_CHECK4(I32x4, simd, I32, new_val, old_val, old_val,
                                old_val),
               WASM_SET_LOCAL(
                   simd, WASM_SIMD_I32x4_REPLACE_LANE(1, WASM_GET_LOCAL(simd),
                                                      WASM_GET_LOCAL(new_val))),
               WASM_SIMD_CHECK4(I32x4, simd, I32, new_val, new_val, old_val,
                                old_val),
               WASM_SET_LOCAL(
                   simd, WASM_SIMD_I32x4_REPLACE_LANE(2, WASM_GET_LOCAL(simd),
                                                      WASM_GET_LOCAL(new_val))),
               WASM_SIMD_CHECK4(I32x4, simd, I32, new_val, new_val, new_val,
                                old_val),
               WASM_SET_LOCAL(
                   simd, WASM_SIMD_I32x4_REPLACE_LANE(3, WASM_GET_LOCAL(simd),
                                                      WASM_GET_LOCAL(new_val))),
               WASM_SIMD_CHECK_SPLAT4(I32x4, simd, I32, new_val),
               WASM_RETURN1(WASM_ONE)));

  CHECK_EQ(1, r.Call(1, 2));
}

WASM_EXEC_TEST(I32x4Add) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled, MachineType::Int32(),
                        MachineType::Int32(), MachineType::Int32());
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kAstS128);
  byte simd1 = r.AllocateLocal(kAstS128);
  BUILD(r,
        WASM_BLOCK(
            WASM_SET_LOCAL(simd0, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(a))),
            WASM_SET_LOCAL(simd1, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(b))),
            WASM_SET_LOCAL(simd1, WASM_SIMD_I32x4_ADD(WASM_GET_LOCAL(simd0),
                                                      WASM_GET_LOCAL(simd1))),
            WASM_SIMD_CHECK_SPLAT4(I32x4, simd1, I32, expected),
            WASM_RETURN1(WASM_ONE)));

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) { CHECK_EQ(1, r.Call(*i, *j, *i + *j)); }
  }
}

WASM_EXEC_TEST(I32x4Sub) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled, MachineType::Int32(),
                        MachineType::Int32(), MachineType::Int32());
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kAstS128);
  byte simd1 = r.AllocateLocal(kAstS128);
  BUILD(r,
        WASM_BLOCK(
            WASM_SET_LOCAL(simd0, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(a))),
            WASM_SET_LOCAL(simd1, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(b))),
            WASM_SET_LOCAL(simd1, WASM_SIMD_I32x4_SUB(WASM_GET_LOCAL(simd0),
                                                      WASM_GET_LOCAL(simd1))),
            WASM_SIMD_CHECK_SPLAT4(I32x4, simd1, I32, expected),
            WASM_RETURN1(WASM_ONE)));

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) { CHECK_EQ(1, r.Call(*i, *j, *i - *j)); }
  }
}
