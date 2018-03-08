// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_MACROS_H_
#define V8_SNAPSHOT_MACROS_H_

#include "include/v8config.h"

// .byte portability macros.

#if defined(V8_OS_MACOSX)  // MACOSX
#define V8_ASM_MANGLE_LABEL "_"
#define V8_ASM_RODATA_SECTION ".const_data\n"
#define V8_ASM_TEXT_SECTION ".text\n"
#define V8_ASM_LOCAL(NAME) ".private_extern " V8_ASM_MANGLE_LABEL NAME "\n"
#elif defined(V8_OS_WIN)  // WIN
#if defined(V8_TARGET_ARCH_X64)
#define V8_ASM_MANGLE_LABEL ""
#else
#define V8_ASM_MANGLE_LABEL "_"
#endif
#define V8_ASM_RODATA_SECTION ".section .rodata\n"
#define V8_ASM_TEXT_SECTION ".section .text\n"
#define V8_ASM_LOCAL(NAME)
#else  // !MACOSX && !WIN
#define V8_ASM_MANGLE_LABEL ""
#define V8_ASM_RODATA_SECTION ".section .rodata\n"
#define V8_ASM_TEXT_SECTION ".section .text\n"
#define V8_ASM_LOCAL(NAME) ".local " V8_ASM_MANGLE_LABEL NAME "\n"
#endif

// Align to kCodeAlignment.
#define V8_ASM_BALIGN32 ".balign 32\n"
#define V8_ASM_LABEL(NAME) V8_ASM_MANGLE_LABEL NAME ":\n"

// clang-format off
#define V8_EMBEDDED_TEXT_HEADER(LABEL) \
  __asm__(V8_ASM_TEXT_SECTION          \
          V8_ASM_LOCAL(#LABEL)         \
          V8_ASM_BALIGN32              \
          V8_ASM_LABEL(#LABEL));

#define V8_EMBEDDED_RODATA_HEADER(LABEL) \
  __asm__(V8_ASM_RODATA_SECTION          \
          V8_ASM_LOCAL(#LABEL)           \
          V8_ASM_BALIGN32                \
          V8_ASM_LABEL(#LABEL));
// clang-format off

#endif  // V8_SNAPSHOT_MACROS_H_
