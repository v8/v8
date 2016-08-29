// Copyright 2015 the V8 project authors. All rights reserved.
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

// TODO(jpp): WASM_EXEC_TEST(TryCatch)

// TODO(jpp): Move these macros to src/wasm/wasm-macro-gen.h once zero cost
// exceptions are added to the spec.
#define WASM_TRY_FINALLY(...) kExprTryFinally, __VA_ARGS__, kExprEnd
#define WASM_FINALLY(...) kExprFinally, __VA_ARGS__

WASM_EXEC_TEST(TryFinally_single) {
  if (execution_mode == kExecuteInterpreted) {
    // TODO(jpp): implement eh support in the interpreter.
    return;
  }

  FLAG_wasm_eh_prototype = true;
  WasmRunner<int32_t> r(execution_mode, MachineType::Int32(),
                        MachineType::Int32());
  // r(i32 p, i32 q) -> i32 {
  //   try {
  //     if (q) {
  //       break;
  //     }
  //     p += 0x0f0;
  //   } finally {
  //     p += 0x00f;
  //   }
  //   p += 0xf00
  //   return p;
  // }
  BUILD(r, WASM_TRY_FINALLY(
               WASM_IF(WASM_GET_LOCAL(1), WASM_BREAK(0)),
               WASM_SET_LOCAL(
                   0, WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_I32V_2(0xf0))),
               WASM_FINALLY(WASM_SET_LOCAL(
                   0, WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_I32V_1(0x0f))))),
        WASM_SET_LOCAL(0, WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_I32V(0xf00))),
        WASM_GET_LOCAL(0));

  CHECK_EQ(0xFFFF, r.Call(0xF000, 0));
  CHECK_EQ(0xFF0F, r.Call(0xF000, 1));
}

WASM_EXEC_TEST(TryFinally_double) {
  if (execution_mode == kExecuteInterpreted) {
    // TODO(jpp): implement eh support in the interpreter.
    return;
  }

  FLAG_wasm_eh_prototype = true;
  WasmRunner<int32_t> r(execution_mode, MachineType::Int32(),
                        MachineType::Int32());
  // r(i32 p, i32 q) -> i32 {
  //   a: try {
  //     b: try {
  //       if (q == 40) {
  //         break a;
  //       } else {
  //         if (q == 1) {
  //           break b;
  //         }
  //       }
  //       p += 0x00000f;
  //     } finally {
  //       p += 0x0000f0;
  //     }
  //     p += 0x000f00;
  //   } finally {
  //     p += 0x00f000;
  //   }
  //   return p;
  // }
  BUILD(
      r,
      WASM_TRY_FINALLY(
          WASM_TRY_FINALLY(
              WASM_IF_ELSE(WASM_I32_EQ(WASM_GET_LOCAL(1), WASM_I32V(40)),
                           WASM_BREAK(1),
                           WASM_IF(WASM_I32_EQ(WASM_GET_LOCAL(1), WASM_I32V(1)),
                                   WASM_BREAK(1))),
              WASM_SET_LOCAL(
                  0, WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_I32V(0x00000f))),
              WASM_FINALLY(WASM_SET_LOCAL(
                  0, WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_I32V(0x0000f0))))),
          WASM_SET_LOCAL(0,
                         WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_I32V(0x000f00))),
          WASM_FINALLY(WASM_SET_LOCAL(
              0, WASM_I32_ADD(WASM_GET_LOCAL(0), WASM_I32V(0x00f000))))),
      WASM_GET_LOCAL(0));

  CHECK_EQ(0x7000ffff, r.Call(0x70000000, 2));
  CHECK_EQ(0x7000fff0, r.Call(0x70000000, 1));
  CHECK_EQ(0x7000f0f0, r.Call(0x70000000, 40));
}

WASM_EXEC_TEST(TryFinally_multiple) {
  if (execution_mode == kExecuteInterpreted) {
    // TODO(jpp): implement eh support in the interpreter.
    return;
  }

  FLAG_wasm_eh_prototype = true;
  WasmRunner<int32_t> r(execution_mode, MachineType::Int32(),
                        MachineType::Int32());

// Handy-dandy shortcuts for recurring patterns for this test.
#define I32_IOR_LOCAL(local, value) \
  WASM_SET_LOCAL(local, WASM_I32_IOR(WASM_GET_LOCAL(local), WASM_I32V(value)))
#define IF_LOCAL_IS_BREAK_TO(local, value, depth)               \
  WASM_IF(WASM_I32_EQ(WASM_GET_LOCAL(local), WASM_I32V(value)), \
          WASM_BREAK(depth))

  // r(i32 p, i32 q) -> i32 {
  //   a: try {
  //     b: try {
  //       c: try {
  //         d: try {
  //           e: try {
  //             switch (q) {
  //               case 1: break e;
  //               case 2: break d;
  //               case 3: break c;
  //               case 4: break b;
  //               case 5: break a;
  //             }
  //             p |= 0x00000001;
  //           } finally {
  //             p |= 0x00000002;
  //           }
  //           switch (q) {
  //             case 6: break d;
  //             case 7: break c;
  //             case 8: break b;
  //             case 9: break a;
  //           }
  //           p |= 0x00000004;
  //         } finally {
  //           p |= 0x00000008;
  //         }
  //         switch (q) {
  //           case 10: break c;
  //           case 11: break b;
  //           case 12: break a;
  //         }
  //         p |= 0x00000010;
  //       } finally {
  //         p |= 0x00000020;
  //       }
  //       switch (q) {
  //         case 13: break b;
  //         case 14: break a;
  //       }
  //       p |= 0x00000040;
  //     } finally {
  //       p |= 0x00000080;
  //     }
  //     switch (q) {
  //       case 15: break a;
  //     }
  //     p |= 0x00000100;
  //   } finally {
  //     p |= 0x00000200;
  //   }
  //   return p;
  // }
  BUILD(
      r,
      WASM_TRY_FINALLY(
          WASM_TRY_FINALLY(
              WASM_TRY_FINALLY(
                  WASM_TRY_FINALLY(
                      WASM_TRY_FINALLY(
                          IF_LOCAL_IS_BREAK_TO(1, 1, 0),
                          IF_LOCAL_IS_BREAK_TO(1, 2, 1),
                          IF_LOCAL_IS_BREAK_TO(1, 3, 2),
                          IF_LOCAL_IS_BREAK_TO(1, 4, 3),
                          IF_LOCAL_IS_BREAK_TO(1, 5, 4),
                          I32_IOR_LOCAL(0, 0x00000001),
                          WASM_FINALLY(I32_IOR_LOCAL(0, 0x00000002))),
                      IF_LOCAL_IS_BREAK_TO(1, 6, 0),
                      IF_LOCAL_IS_BREAK_TO(1, 7, 1),
                      IF_LOCAL_IS_BREAK_TO(1, 8, 2),
                      IF_LOCAL_IS_BREAK_TO(1, 9, 3),
                      I32_IOR_LOCAL(0, 0x00000004),
                      WASM_FINALLY(I32_IOR_LOCAL(0, 0x00000008))),
                  IF_LOCAL_IS_BREAK_TO(1, 10, 0),
                  IF_LOCAL_IS_BREAK_TO(1, 11, 1),
                  IF_LOCAL_IS_BREAK_TO(1, 12, 2), I32_IOR_LOCAL(0, 0x00000010),
                  WASM_FINALLY(I32_IOR_LOCAL(0, 0x00000020))),
              IF_LOCAL_IS_BREAK_TO(1, 13, 0), IF_LOCAL_IS_BREAK_TO(1, 14, 1),
              I32_IOR_LOCAL(0, 0x00000040),
              WASM_FINALLY(I32_IOR_LOCAL(0, 0x00000080))),
          IF_LOCAL_IS_BREAK_TO(1, 15, 0), I32_IOR_LOCAL(0, 0x00000100),
          WASM_FINALLY(I32_IOR_LOCAL(0, 0x00000200))),
      WASM_GET_LOCAL(0));
#undef WASM_IF_LOCAL_IS_BREAK_TO
#undef WASM_I32_IOR_LOCAL

  const struct {
    uint32_t inputs[2];
    uint32_t expected_output;
  } kTests[] = {
      {{0x80000000u, 0}, 0x800003ffu},  {{0x80000000u, 1}, 0x800003feu},
      {{0x80000000u, 2}, 0x800003fau},  {{0x80000000u, 3}, 0x800003eau},
      {{0x80000000u, 4}, 0x800003aau},  {{0x80000000u, 5}, 0x800002aau},
      {{0x80000000u, 6}, 0x800003fbu},  {{0x80000000u, 7}, 0x800003ebu},
      {{0x80000000u, 8}, 0x800003abu},  {{0x80000000u, 9}, 0x800002abu},
      {{0x80000000u, 10}, 0x800003efu}, {{0x80000000u, 11}, 0x800003afu},
      {{0x80000000u, 12}, 0x800002afu}, {{0x80000000u, 13}, 0x800003bfu},
      {{0x80000000u, 14}, 0x800002bfu}, {{0x80000000u, 15}, 0x800002ffu},
  };

  for (uint32_t ii = 0; ii < arraysize(kTests); ++ii) {
    const auto& test_instance = kTests[ii];
    CHECK_EQ(test_instance.expected_output,
             static_cast<uint32_t>(
                 r.Call(test_instance.inputs[0], test_instance.inputs[1])));
  }
}

WASM_EXEC_TEST(TryFinally_break_within_finally) {
  if (execution_mode == kExecuteInterpreted) {
    // TODO(jpp): implement eh support in the interpreter.
    return;
  }

  FLAG_wasm_eh_prototype = true;
  WasmRunner<int32_t> r(execution_mode, MachineType::Int32(),
                        MachineType::Int32());

#define I32_IOR_LOCAL(local, value) \
  WASM_SET_LOCAL(local, WASM_I32_IOR(WASM_GET_LOCAL(local), WASM_I32V(value)))
#define IF_LOCAL_IS_BREAK_TO(local, value, depth)               \
  WASM_IF(WASM_I32_EQ(WASM_GET_LOCAL(local), WASM_I32V(value)), \
          WASM_BREAK(depth))

  // r(i32 p, i32 q) -> i32 {
  //   a: try {
  //   } finally {
  //     b: try {
  //       c: try {
  //       } finally {
  //         d: try {
  //           e: try {
  //           } finally {
  //             f: try {
  //             } finally {
  //               if (q == 1) {
  //                 break a;
  //               }
  //               p |= 0x00000001
  //             }
  //             p |= 0x00000002
  //           }
  //           p |= 0x00000004
  //         } finally {
  //           p |= 0x00000008 /* should run */
  //         }
  //         p |= 0x00000010
  //       }
  //       p |= 0x00000020
  //     } finally {
  //       p |= 0x00000040  /* should run */
  //     }
  //     p |= 0x00000080
  //   }
  //   return p;
  // }
  BUILD(r,
        WASM_TRY_FINALLY(  // a
            WASM_FINALLY(
                WASM_TRY_FINALLY(      // b
                    WASM_TRY_FINALLY(  // c
                        WASM_FINALLY(
                            WASM_TRY_FINALLY(      // d
                                WASM_TRY_FINALLY(  // e
                                    WASM_FINALLY(
                                        WASM_TRY_FINALLY(  // f
                                            WASM_FINALLY(
                                                IF_LOCAL_IS_BREAK_TO(1, 1, 5),
                                                I32_IOR_LOCAL(0, 0x00000001))),
                                        I32_IOR_LOCAL(0, 0x00000002))),
                                I32_IOR_LOCAL(0, 0x00000004),
                                WASM_FINALLY(I32_IOR_LOCAL(0, 0x00000008))),
                            I32_IOR_LOCAL(0, 0x00000010))),
                    I32_IOR_LOCAL(0, 0x00000020),
                    WASM_FINALLY(I32_IOR_LOCAL(0, 0x00000040))),
                I32_IOR_LOCAL(0, 0x00000080))),
        WASM_GET_LOCAL(0));

#undef WASM_IF_LOCAL_IS_BREAK_TO
#undef WASM_I32_IOR_LOCAL

  CHECK_EQ(0x40000048, r.Call(0x40000000, 1));
}

// TODO(jpp): WASM_EXEC_TEST(TryCatchFinally)
