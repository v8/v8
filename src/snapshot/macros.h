// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_MACROS_H_
#define V8_SNAPSHOT_MACROS_H_

// .byte portability macros.

#if defined(V8_OS_MACOSX)  // MACOSX
#define V8_ASM_MANGLE_LABEL "_"
#define V8_ASM_RODATA_SECTION ".const_data\n"
#define V8_ASM_TEXT_SECTION ".text\n"
#define V8_ASM_GLOBAL(NAME) ".globl " V8_ASM_MANGLE_LABEL NAME "\n"
#elif defined(V8_OS_WIN)  // WIN
#if defined(V8_TARGET_ARCH_X64)
#define V8_ASM_MANGLE_LABEL ""
#else
#define V8_ASM_MANGLE_LABEL "_"
#endif
#define V8_ASM_RODATA_SECTION ".section .rodata\n"
#define V8_ASM_TEXT_SECTION ".section .text\n"
#define V8_ASM_GLOBAL(NAME) ".global " V8_ASM_MANGLE_LABEL NAME "\n"
#else  // !MACOSX && !WIN
#define V8_ASM_MANGLE_LABEL ""
#define V8_ASM_RODATA_SECTION ".section .rodata\n"
#define V8_ASM_TEXT_SECTION ".section .text\n"
#define V8_ASM_GLOBAL(NAME) ".global " V8_ASM_MANGLE_LABEL NAME "\n"
#endif

#define V8_ASM_BALIGN16 ".balign 16\n"
#define V8_ASM_LABEL(NAME) V8_ASM_MANGLE_LABEL NAME ":\n"

#define V8_EMBEDDED_TEXT_HEADER(LABEL)              \
  __asm__(V8_ASM_TEXT_SECTION V8_ASM_GLOBAL(#LABEL) \
              V8_ASM_BALIGN16 V8_ASM_LABEL(#LABEL));

#define V8_EMBEDDED_RODATA_HEADER(LABEL)              \
  __asm__(V8_ASM_RODATA_SECTION V8_ASM_GLOBAL(#LABEL) \
              V8_ASM_BALIGN16 V8_ASM_LABEL(#LABEL));

#endif  // V8_SNAPSHOT_MACROS_H_
