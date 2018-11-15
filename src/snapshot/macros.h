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
#define V8_ASM_DECLARE(NAME) ".private_extern " V8_ASM_MANGLE_LABEL NAME "\n"
#elif defined(V8_OS_AIX)  // AIX
#define V8_ASM_RODATA_SECTION ".csect[RO]\n"
#define V8_ASM_TEXT_SECTION ".csect .text[PR]\n"
#define V8_ASM_MANGLE_LABEL ""
#define V8_ASM_DECLARE(NAME) ".globl " V8_ASM_MANGLE_LABEL NAME "\n"
#elif defined(V8_OS_WIN)  // WIN
#if defined(V8_TARGET_ARCH_X64) || defined(V8_TARGET_ARCH_ARM64)
#define V8_ASM_MANGLE_LABEL ""
#else
#define V8_ASM_MANGLE_LABEL "_"
#endif
#define V8_ASM_RODATA_SECTION ".section .rodata\n"
#define V8_ASM_TEXT_SECTION ".section .text\n"
#define V8_ASM_DECLARE(NAME)
#else  // !MACOSX && !WIN && !AIX
#define V8_ASM_MANGLE_LABEL ""
#define V8_ASM_RODATA_SECTION ".section .rodata\n"
#if defined(OS_CHROMEOS)  // ChromeOS
#define V8_ASM_TEXT_SECTION ".section .text.hot.embedded\n"
#else
#define V8_ASM_TEXT_SECTION ".section .text\n"
#endif
#if defined(V8_TARGET_ARCH_MIPS) || defined(V8_TARGET_ARCH_MIPS64)
#define V8_ASM_DECLARE(NAME) ".global " V8_ASM_MANGLE_LABEL NAME "\n"
#else
#define V8_ASM_DECLARE(NAME) ".local " V8_ASM_MANGLE_LABEL NAME "\n"
#endif
#endif

// Align to kCodeAlignment.
#define V8_ASM_BALIGN32 ".balign 32\n"
#define V8_ASM_LABEL(NAME) V8_ASM_MANGLE_LABEL NAME ":\n"

#if defined(V8_OS_WIN)
// The directives for inserting debugging information on Windows come
// from the PE (Portable Executable) and COFF (Common Object File Format)
// standards. Documented here:
// https://docs.microsoft.com/en-us/windows/desktop/debug/pe-format

#define V8_ASM_TYPE(NAME)                            \
  /* .scl 2 means StorageClass external. */          \
  /* .type 32 means Type Representation Function. */ \
  ".def " V8_ASM_MANGLE_LABEL NAME "; .scl 2; .type 32; .endef;\n"

#elif defined(V8_TARGET_ARCH_ARM) || defined(V8_TARGET_ARCH_ARM64)

// ELF format binaries on ARM use ".type <function name>, %function"
// to create a DWARF subprogram entry.
#define V8_ASM_TYPE(NAME) ".type " V8_ASM_MANGLE_LABEL NAME ", %function\n"

#else

// Other ELF Format binaries use ".type <function name>, @function"
// to create a DWARF subprogram entry.
#define V8_ASM_TYPE(NAME) ".type " V8_ASM_MANGLE_LABEL NAME ", @function\n"
#endif  // !V8_OS_WIN

#define V8_ASM_DECLARE_FUNCTION(NAME) \
  V8_ASM_LABEL(NAME)                  \
  V8_ASM_TYPE(NAME)

// clang-format off
#if defined(V8_OS_AIX)

#define V8_EMBEDDED_TEXT_HEADER(LABEL)         \
  __asm__(V8_ASM_DECLARE(#LABEL)               \
          ".csect " #LABEL "[DS]\n"            \
          #LABEL ":\n"                         \
          ".llong ." #LABEL ", TOC[tc0], 0\n"  \
          V8_ASM_TEXT_SECTION                  \
          "." #LABEL ":\n");

#define V8_EMBEDDED_RODATA_HEADER(LABEL)    \
  __asm__(V8_ASM_RODATA_SECTION             \
          V8_ASM_DECLARE(#LABEL)            \
          ".align 5\n"                      \
          V8_ASM_LABEL(#LABEL));

#else

#define V8_EMBEDDED_TEXT_HEADER(LABEL) \
  __asm__(V8_ASM_TEXT_SECTION          \
          V8_ASM_DECLARE(#LABEL)       \
          V8_ASM_BALIGN32              \
          V8_ASM_LABEL(#LABEL));

#define V8_EMBEDDED_RODATA_HEADER(LABEL) \
  __asm__(V8_ASM_RODATA_SECTION          \
          V8_ASM_DECLARE(#LABEL)         \
          V8_ASM_BALIGN32                \
          V8_ASM_LABEL(#LABEL));

#endif  // #if defined(V8_OS_AIX)
#endif  // V8_SNAPSHOT_MACROS_H_
