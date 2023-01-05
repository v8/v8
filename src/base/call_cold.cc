// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/call_cold.h"

namespace v8::base {

#if V8_HOST_ARCH_X64 && (defined(__clang__) || defined(__GNUC__))
asm(".globl v8_base_call_cold\n"
    "v8_base_call_cold:\n"
    "  push %rbp\n"
    "  mov %rsp, %rbp\n"
    // Push all non-clobbered registers, except for callee-saved ones. The
    // compiler does not even know that it is executing a call, so we can not
    // clobber any register, not the registers holding the function address or
    // the arguments.
    "  push %rax\n"
    "  push %rcx\n"
    "  push %rdx\n"
#ifndef V8_OS_WIN
    // %rsi and %rdi are callee-saved on Windows.
    "  push %rsi\n"
    "  push %rdi\n"
#endif  // !V8_OS_WIN
    "  push %r8\n"
    "  push %r9\n"
    "  push %r10\n"
    "  push %r11\n"
    // Save %rsp to %r15 (after pushing it) and align %rsp to 16 bytes.
    // %r15 is callee-saved, so the value will still be there after the call.
    "  push %r15\n"
    "  mov %rsp, %r15\n"
    "  and $-16, %rsp\n"
    // Now execute the actual call.
    "  call *%rax\n"
    // Restore the potentially unaligned %rsp.
    "  mov %r15, %rsp\n"
    // Pop the previously pushed registers. We have no return value, so we do
    // not need to preserve %rax.
    "  pop %r15\n"
    "  pop %r11\n"
    "  pop %r10\n"
    "  pop %r9\n"
    "  pop %r8\n"
#ifndef V8_OS_WIN
    "  pop %rdi\n"
    "  pop %rsi\n"
#endif  // !V8_OS_WIN
    "  pop %rdx\n"
    "  pop %rcx\n"
    "  pop %rax\n"
    // Leave the frame and return.
    "  pop %rbp\n"
    "  ret");
#endif

}  // namespace v8::base
