// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/cctest.h"

#include "src/handles-inl.h"
#include "src/isolate.h"
#include "src/macro-assembler.h"
#include "src/simulator.h"

// To generate the binary files for the test function (used in the IncbinInText
// below), enable this section and run GenerateTestFunctionData once on each
// arch.
#define GENERATE_TEST_FUNCTION_DATA false

// Arch-specific includes and defines.
#if V8_TARGET_ARCH_IA32
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-ia32.bin"
#include "src/ia32/assembler-ia32-inl.h"
#elif V8_TARGET_ARCH_X64
#ifdef _WIN64
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-x64-win.bin"
#else
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-x64.bin"
#endif
#include "src/x64/assembler-x64-inl.h"
#elif V8_TARGET_ARCH_ARM64
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-arm64.bin"
#include "src/arm64/assembler-arm64-inl.h"
#include "src/arm64/macro-assembler-arm64-inl.h"
#elif V8_TARGET_ARCH_ARM
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-arm.bin"
#include "src/arm/assembler-arm-inl.h"
#elif V8_TARGET_ARCH_PPC
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-ppc.bin"
#include "src/ppc/assembler-ppc-inl.h"
#elif V8_TARGET_ARCH_MIPS
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-mips.bin"
#include "src/mips/assembler-mips-inl.h"
#include "src/mips/macro-assembler-mips.h"
#elif V8_TARGET_ARCH_MIPS64
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-mips64.bin"
#include "src/mips64/assembler-mips64-inl.h"
#elif V8_TARGET_ARCH_S390
#define TEST_FUNCTION_FILE "test-isolate-independent-builtins-f-s390.bin"
#include "src/s390/assembler-s390-inl.h"
#else  // Unknown architecture.
#error "Unknown architecture."
#endif  // Target architecture.

#define __ masm.

namespace v8 {
namespace internal {
namespace test_isolate_independent_builtins {

// .incbin macros.
#if defined(V8_OS_MACOSX)
#define INCBIN_RODATA_SECTION ".const_data\n"
#define INCBIN_TEXT_SECTION ".text\n"
#define INCBIN_MANGLE "_"
#define INCBIN_GLOBAL(NAME) ".globl " INCBIN_MANGLE NAME "\n"
#elif defined(V8_OS_WIN)
#define INCBIN_RODATA_SECTION ".section .rodata\n"
#define INCBIN_TEXT_SECTION ".section .text\n"
#if defined(V8_TARGET_ARCH_X64)
#define INCBIN_MANGLE ""
#else
#define INCBIN_MANGLE "_"
#endif
#define INCBIN_GLOBAL(NAME) ".global " INCBIN_MANGLE NAME "\n"
#else
#define INCBIN_RODATA_SECTION ".section .rodata\n"
#define INCBIN_TEXT_SECTION ".section .text\n"
#define INCBIN_MANGLE ""
#define INCBIN_GLOBAL(NAME) ".global " INCBIN_MANGLE NAME "\n"
#endif

// clang-format off
#define INCBIN_RODATA(LABEL, FILE)              \
  __asm__(INCBIN_RODATA_SECTION                 \
          INCBIN_GLOBAL(#LABEL)                 \
          ".balign 16\n"                        \
          INCBIN_MANGLE #LABEL ":\n"            \
          ".incbin \"" FILE "\"\n");            \
  extern "C" V8_ALIGNED(16) const char LABEL[]

#define INCBIN_TEXT(LABEL, FILE)                \
  __asm__(INCBIN_TEXT_SECTION                   \
          INCBIN_GLOBAL(#LABEL)                 \
          ".balign 16\n"                        \
          INCBIN_MANGLE #LABEL ":\n"            \
          ".incbin \"" FILE "\"\n");            \
  extern "C" V8_ALIGNED(16) const char LABEL[]
// clang-format on

INCBIN_RODATA(test_string_bytes,
              "test/cctest/test-isolate-independent-builtins-string.bin");
INCBIN_TEXT(test_function_bytes, "test/cctest/" TEST_FUNCTION_FILE);

#if GENERATE_TEST_FUNCTION_DATA
TEST(GenerateTestFunctionData) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

#if V8_TARGET_ARCH_IA32
  v8::internal::byte buffer[256];
  Assembler masm(isolate, buffer, sizeof(buffer));

  __ mov(eax, Operand(esp, 4));
  __ add(eax, Operand(esp, 8));
  __ ret(0);
#elif V8_TARGET_ARCH_X64
  size_t allocated;
  byte* buffer = AllocateAssemblerBuffer(&allocated);
  Assembler masm(isolate, buffer, static_cast<int>(allocated));

#ifdef _WIN64
  static const Register arg1 = rcx;
  static const Register arg2 = rdx;
#else
  static const Register arg1 = rdi;
  static const Register arg2 = rsi;
#endif

  __ movq(rax, arg2);
  __ addq(rax, arg1);
  __ ret(0);
#elif V8_TARGET_ARCH_ARM64
  MacroAssembler masm(isolate, nullptr, 0,
                      v8::internal::CodeObjectRequired::kYes);

  __ Add(x0, x0, x1);
  __ Ret();
#elif V8_TARGET_ARCH_ARM
  Assembler masm(isolate, nullptr, 0);

  __ add(r0, r0, Operand(r1));
  __ mov(pc, Operand(lr));
#elif V8_TARGET_ARCH_PPC
  Assembler masm(isolate, nullptr, 0);

  __ function_descriptor();
  __ add(r3, r3, r4);
  __ blr();
#elif V8_TARGET_ARCH_MIPS
  MacroAssembler masm(isolate, nullptr, 0,
                      v8::internal::CodeObjectRequired::kYes);

  __ addu(v0, a0, a1);
  __ jr(ra);
  __ nop();
#elif V8_TARGET_ARCH_MIPS64
  MacroAssembler masm(isolate, nullptr, 0,
                      v8::internal::CodeObjectRequired::kYes);

  __ addu(v0, a0, a1);
  __ jr(ra);
  __ nop();
#elif V8_TARGET_ARCH_S390
  Assembler masm(isolate, nullptr, 0);

  __ lhi(r1, Operand(3));
  __ llilf(r2, Operand(4));
  __ lgr(r2, r2);
  __ ar(r2, r1);
  __ b(r14);
#else  // Unknown architecture.
#error "Unknown architecture."
#endif  // Target architecture.

  CodeDesc desc;
  masm.GetCode(isolate, &desc);

  std::ofstream of(TEST_FUNCTION_FILE, std::ios::out | std::ios::binary);
  of.write(reinterpret_cast<char*>(desc.buffer), desc.instr_size);
}
#endif  // GENERATE_TEST_FUNCTION_DATA

#undef __
#undef GENERATE_TEST_FUNCTION_DATA
#undef INCBIN_GLOBAL
#undef INCBIN_MANGLE
#undef INCBIN_RODATA
#undef INCBIN_RODATA_SECTION
#undef INCBIN_TEXT
#undef INCBIN_TEXT_SECTION
#undef TEST_FUNCTION_FILE

TEST(IncbinInRodata) {
  CHECK_EQ(0, std::strcmp("0123456789\n", test_string_bytes));
}

TEST(IncbinInText) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  auto f = GeneratedCode<int(int, int)>::FromAddress(
      isolate, const_cast<char*>(test_function_bytes));
  CHECK_EQ(7, f.Call(3, 4));
  CHECK_EQ(11, f.Call(5, 6));
}

}  // namespace test_isolate_independent_builtins
}  // namespace internal
}  // namespace v8
