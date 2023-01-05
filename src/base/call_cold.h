// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_CALL_COLD_H_
#define V8_BASE_CALL_COLD_H_

#include <type_traits>

#include "include/v8config.h"

namespace v8::base {

// Use {call_cold} for calls in hot paths that are unlikely to be executed. The
// compiler will not know that this executes a call, so it will not clobber any
// registers (i.e. this behaves like a custom calling convention where all
// registers are callee-save).
// Executing the call will be significantly slower then without going through
// {call_cold}, as all register will have to be spilled and an indirect call is
// being executed.

// As a start, we added support for GCC and clang on x64. Other platforms can
// be added later, as needed.

template <typename Fn, typename... Ps>
constexpr bool IsValidForCallCold =
    // The callable object must be convertible to a function pointer (e.g. a
    // capture-less lambda).
    std::is_convertible_v<Fn, void (*)(Ps...)> &&
    // All parameters must be integral (support for floating-point arguments is
    // not implemented).
    (... && (std::is_integral_v<Ps> || std::is_pointer_v<Ps>));

// Do not use V8_CC_GNU, as this is not defined for clang on Windows. Explicitly
// check for GCC or clang.
#if V8_HOST_ARCH_X64 && (defined(__clang__) || defined(__GNUC__))

// Define the parameter registers. Windows uses a different calling convention
// than other OSes.
#define REG_FN "rax"
#ifdef V8_OS_WIN
#define REG_P1 "rcx"
#define REG_P2 "rdx"
#define REG_P3 "r8"
#else
#define REG_P1 "rdi"
#define REG_P2 "rsi"
#define REG_P3 "rdx"
#endif
// We clobber all xmm registers so we do not have to spill and reload them.
#define CLOBBER                                                              \
  "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",    \
      "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14",   \
      "xmm15", "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6", "st", \
      "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"
#define V8_CALL_COLD_ASM                                                  \
  "sub $128, %%rsp\n"        /* Bump %rsp by 128, beyond the red zone. */ \
  "call v8_base_call_cold\n" /* Call our trampoline. */                   \
  "add $128, %%rsp"          /* Restore previous %rsp. */

// 1 Parameter, no result.
template <typename P1, typename Fn>
V8_INLINE void call_cold(const Fn& fn, P1 p1) {
  static_assert(IsValidForCallCold<Fn, P1>);
  using FnPtr = void (*)(P1);
  register FnPtr fn_reg asm(REG_FN) = fn;
  register P1 p1_reg asm(REG_P1) = p1;
  asm(V8_CALL_COLD_ASM : : "r"(fn_reg), "r"(p1_reg) : CLOBBER);
}

// 3 Parameters, no result.
template <typename P1, typename P2, typename P3, typename Fn>
V8_INLINE void call_cold(const Fn& fn, P1 p1, P2 p2, P3 p3) {
  static_assert(IsValidForCallCold<Fn, P1, P2, P3>);
  using FnPtr = void (*)(P1, P2, P3);
  register FnPtr fn_reg asm(REG_FN) = fn;
  register P1 p1_reg asm(REG_P1) = p1;
  register P2 p2_reg asm(REG_P2) = p2;
  register P3 p3_reg asm(REG_P3) = p3;
  asm(V8_CALL_COLD_ASM
      :
      : "r"(fn_reg), "r"(p1_reg), "r"(p2_reg), "r"(p3_reg)
      : CLOBBER);
}

#else
// Architectures without special support just execute the call directly.
template <typename... Ps, typename Fn>
V8_INLINE void call_cold(const Fn& fn, Ps... ps) {
  static_assert(IsValidForCallCold<Fn, Ps...>);
  fn(ps...);
}
#endif

#undef REG_P1
#undef REG_P2
#undef REG_P3
#undef CLOBBER
#undef V8_CALL_COLD_ASM

}  // namespace v8::base

#endif  // V8_BASE_CALL_COLD_H_
