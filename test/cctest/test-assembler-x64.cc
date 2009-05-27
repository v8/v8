// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include "v8.h"

#include "macro-assembler.h"
#include "factory.h"
#include "platform.h"
#include "serialize.h"
#include "cctest.h"

using v8::internal::byte;
using v8::internal::OS;
using v8::internal::Assembler;
using v8::internal::Operand;
using v8::internal::Label;
using v8::internal::rax;
using v8::internal::rsi;
using v8::internal::rdi;
using v8::internal::rbp;
using v8::internal::rsp;
using v8::internal::FUNCTION_CAST;
using v8::internal::CodeDesc;


// Test the x64 assembler by compiling some simple functions into
// a buffer and executing them.  These tests do not initialize the
// V8 library, create a context, or use any V8 objects.
// The AMD64 calling convention is used, with the first five arguments
// in RSI, RDI, RDX, RCX, R8, and R9, and floating point arguments in
// the XMM registers.  The return value is in RAX.
// This calling convention is used on Linux, with GCC, and on Mac OS,
// with GCC.  A different convention is used on 64-bit windows.

typedef int (*F0)();
typedef int (*F1)(int x);
typedef int (*F2)(int x, int y);

#define __ assm.


TEST(AssemblerX64ReturnOperation) {
  // Allocate an executable page of memory.
  size_t actual_size;
  byte* buffer = static_cast<byte*>(OS::Allocate(Assembler::kMinimalBufferSize,
                                                 &actual_size,
                                                 true));
  CHECK(buffer);
  Assembler assm(buffer, actual_size);

  // Assemble a simple function that copies argument 2 and returns it.
  __ mov(rax, rsi);
  __ nop();
  __ ret(0);

  CodeDesc desc;
  assm.GetCode(&desc);
  // Call the function from C++.
  int result =  FUNCTION_CAST<F2>(buffer)(3, 2);
  CHECK_EQ(2, result);
}

TEST(AssemblerX64StackOperations) {
  // Allocate an executable page of memory.
  size_t actual_size;
  byte* buffer = static_cast<byte*>(OS::Allocate(Assembler::kMinimalBufferSize,
                                                 &actual_size,
                                                 true));
  CHECK(buffer);
  Assembler assm(buffer, actual_size);

  // Assemble a simple function that copies argument 2 and returns it.
  // We compile without stack frame pointers, so the gdb debugger shows
  // incorrect stack frames when debugging this function (which has them).
  __ push(rbp);
  __ mov(rbp, rsp);
  __ push(rsi);  // Value at (rbp - 8)
  __ push(rsi);  // Value at (rbp - 16)
  __ push(rdi);  // Value at (rbp - 24)
  __ pop(rax);
  __ pop(rax);
  __ pop(rax);
  __ pop(rbp);
  __ nop();
  __ ret(0);

  CodeDesc desc;
  assm.GetCode(&desc);
  // Call the function from C++.
  int result =  FUNCTION_CAST<F2>(buffer)(3, 2);
  CHECK_EQ(2, result);
}

TEST(AssemblerX64ArithmeticOperations) {
  // Allocate an executable page of memory.
  size_t actual_size;
  byte* buffer = static_cast<byte*>(OS::Allocate(Assembler::kMinimalBufferSize,
                                                 &actual_size,
                                                 true));
  CHECK(buffer);
  Assembler assm(buffer, actual_size);

  // Assemble a simple function that copies argument 2 and returns it.
  __ mov(rax, rsi);
  __ add(rax, rdi);
  __ ret(0);

  CodeDesc desc;
  assm.GetCode(&desc);
  // Call the function from C++.
  int result =  FUNCTION_CAST<F2>(buffer)(3, 2);
  CHECK_EQ(5, result);
}

TEST(AssemblerX64MemoryOperands) {
  // Allocate an executable page of memory.
  size_t actual_size;
  byte* buffer = static_cast<byte*>(OS::Allocate(Assembler::kMinimalBufferSize,
                                                 &actual_size,
                                                 true));
  CHECK(buffer);
  Assembler assm(buffer, actual_size);

  // Assemble a simple function that copies argument 2 and returns it.
  __ push(rbp);
  __ mov(rbp, rsp);
  __ push(rsi);  // Value at (rbp - 8)
  __ push(rsi);  // Value at (rbp - 16)
  __ push(rdi);  // Value at (rbp - 24)
  const int kStackElementSize = 8;
  __ mov(rax, Operand(rbp, -3 * kStackElementSize));
  __ pop(rsi);
  __ pop(rsi);
  __ pop(rsi);
  __ pop(rbp);
  __ nop();
  __ ret(0);

  CodeDesc desc;
  assm.GetCode(&desc);
  // Call the function from C++.
  int result =  FUNCTION_CAST<F2>(buffer)(3, 2);
  CHECK_EQ(3, result);
}

TEST(AssemblerX64ControlFlow) {
  // Allocate an executable page of memory.
  size_t actual_size;
  byte* buffer = static_cast<byte*>(OS::Allocate(Assembler::kMinimalBufferSize,
                                                 &actual_size,
                                                 true));
  CHECK(buffer);
  Assembler assm(buffer, actual_size);

  // Assemble a simple function that copies argument 2 and returns it.
  __ push(rbp);
  __ mov(rbp, rsp);
  __ mov(rax, rdi);
  Label target;
  __ jmp(&target);
  __ mov(rax, rsi);
  __ bind(&target);
  __ pop(rbp);
  __ ret(0);

  CodeDesc desc;
  assm.GetCode(&desc);
  // Call the function from C++.
  int result =  FUNCTION_CAST<F2>(buffer)(3, 2);
  CHECK_EQ(3, result);
}

#undef __
