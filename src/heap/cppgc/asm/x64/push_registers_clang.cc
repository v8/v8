// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HAVE_TARGET_OS
#error "File assumes V8_TARGET_OS_* defines are present"
#endif  // V8_HAVE_TARGET_OS

// Push all callee-saved registers to get them on the stack for conservative
// stack scanning.
//
// Do not add any C code to the function. The function is naked to avoid
// emitting a prologue/epilogue that could violate alginment computations.
extern "C" __attribute__((naked, noinline)) void
PushAllRegistersAndIterateStack(void* /* {Stack*} */,
                                void* /* {StackVisitor*} */,
                                void* /* {IterateStackCallback} */) {
#ifdef V8_TARGET_OS_WIN

  // We maintain 16-byte alignment at calls. There is an 8-byte return address
  // on the stack and we push 72 bytes which maintains 16-byte stack alignment
  // at the call.
  // Source: https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention
  asm volatile(
      // rbp is callee-saved. Maintain proper frame pointer for debugging.
      " push %rbp           \n"
      " mov %rsp, %rbp      \n"
      // Dummy for alignment.
      " push $0xCDCDCD      \n"
      " push %rsi           \n"
      " push %rdi           \n"
      " push %rbx           \n"
      " push %r12           \n"
      " push %r13           \n"
      " push %r14           \n"
      " push %r15           \n"
      // Pass 1st parameter (rcx) unchanged (Stack*).
      // Pass 2nd parameter (rdx) unchanged (StackVisitor*).
      // Save 3rd parameter (r8; IterateStackCallback)
      " mov %r8, %r9        \n"
      // Pass 3rd parameter as rsp (stack pointer).
      " mov %rsp, %r8       \n"
      // Call the callback.
      " call *%r9           \n"
      // Pop the callee-saved registers.
      " add $64, %rsp       \n"
      // Restore rbp as it was used as frame pointer.
      " pop %rbp            \n"
      " ret                 \n");

#else  // !V8_TARGET_OS_WIN

  // We maintain 16-byte alignment at calls. There is an 8-byte return address
  // on the stack and we push 56 bytes which maintains 16-byte stack alignment
  // at the call.
  // Source: https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-1.0.pdf
  asm volatile(
      // rbp is callee-saved. Maintain proper frame pointer for debugging.
      " push %rbp           \n"
      " mov %rsp, %rbp      \n"
      // Dummy for alignment.
      " push $0xCDCDCD      \n"
      " push %rbx           \n"
      " push %r12           \n"
      " push %r13           \n"
      " push %r14           \n"
      " push %r15           \n"
      // Pass 1st parameter (rdi) unchanged (Stack*).
      // Pass 2nd parameter (rsi) unchanged (StackVisitor*).
      // Save 3rd parameter (rdx; IterateStackCallback)
      " mov %rdx, %r8       \n"
      // Pass 3rd parameter as rsp (stack pointer).
      " mov %rsp, %rdx      \n"
      // Call the callback.
      " call *%r8           \n"
      // Pop the callee-saved registers.
      " add $48, %rsp       \n"
      // Restore rbp as it was used as frame pointer.
      " pop %rbp            \n"
      " ret                 \n");

#endif  // !V8_TARGET_OS_WIN
}
