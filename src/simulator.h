// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SIMULATOR_H_
#define V8_SIMULATOR_H_

#include "src/globals.h"

#if V8_TARGET_ARCH_IA32
#include "src/ia32/simulator-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/x64/simulator-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/arm64/simulator-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/arm/simulator-arm.h"
#elif V8_TARGET_ARCH_PPC
#include "src/ppc/simulator-ppc.h"
#elif V8_TARGET_ARCH_MIPS
#include "src/mips/simulator-mips.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/mips64/simulator-mips64.h"
#elif V8_TARGET_ARCH_S390
#include "src/s390/simulator-s390.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

#if defined(USE_SIMULATOR)
// Running with a simulator.

// The simulator has its own stack. Thus it has a different stack limit from
// the C-based native code.  The JS-based limit normally points near the end of
// the simulator stack.  When the C-based limit is exhausted we reflect that by
// lowering the JS-based limit as well, to make stack checks trigger.
class SimulatorStack : public v8::internal::AllStatic {
 public:
  static inline uintptr_t JsLimitFromCLimit(v8::internal::Isolate* isolate,
                                            uintptr_t c_limit) {
    return Simulator::current(isolate)->StackLimit(c_limit);
  }

  static inline uintptr_t RegisterCTryCatch(v8::internal::Isolate* isolate,
                                            uintptr_t try_catch_address) {
    return Simulator::current(isolate)->PushAddress(try_catch_address);
  }

  static inline void UnregisterCTryCatch(v8::internal::Isolate* isolate) {
    Simulator::current(isolate)->PopAddress();
  }
};

#else  // defined(USE_SIMULATOR)
// Running without a simulator on a native platform.

// The stack limit beyond which we will throw stack overflow errors in
// generated code. Because generated code uses the C stack, we just use
// the C stack limit.
class SimulatorStack : public v8::internal::AllStatic {
 public:
  static inline uintptr_t JsLimitFromCLimit(v8::internal::Isolate* isolate,
                                            uintptr_t c_limit) {
    USE(isolate);
    return c_limit;
  }

  static inline uintptr_t RegisterCTryCatch(v8::internal::Isolate* isolate,
                                            uintptr_t try_catch_address) {
    USE(isolate);
    return try_catch_address;
  }

  static inline void UnregisterCTryCatch(v8::internal::Isolate* isolate) {
    USE(isolate);
  }
};

// When running without a simulator we call the entry directly.
#define CALL_GENERATED_CODE(isolate, entry, p0, p1, p2, p3, p4) \
  (entry(p0, p1, p2, p3, p4))

typedef int (*regexp_matcher)(String*, int, const byte*, const byte*, int*, int,
                              Address, int, Isolate*);

// Call the generated regexp code directly. The code at the entry address
// should act as a function matching the type {regexp_matcher} above.
#define CALL_GENERATED_REGEXP_CODE(isolate, entry, p0, p1, p2, p3, p4, p5, p6, \
                                   p7, p8)                                     \
  (FUNCTION_CAST<regexp_matcher>(entry)(p0, p1, p2, p3, p4, p5, p6, p7, p8))

#endif  // defined(USE_SIMULATOR)

}  // namespace internal
}  // namespace v8

#endif  // V8_SIMULATOR_H_
