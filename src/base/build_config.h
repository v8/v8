// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_BUILD_CONFIG_H_
#define V8_BASE_BUILD_CONFIG_H_

#include "../../include/v8config.h"

// Processor architecture detection.  For more info on what's defined, see:
//   http://msdn.microsoft.com/en-us/library/b0084kay.aspx
//   http://www.agner.org/optimize/calling_conventions.pdf
//   or with gcc, run: "echo | gcc -E -dM -"
#if defined(_M_X64) || defined(__x86_64__)
#if defined(__native_client__)
// For Native Client builds of V8, use V8_TARGET_ARCH_ARM, so that V8
// generates ARM machine code, together with a portable ARM simulator
// compiled for the host architecture in question.
//
// Since Native Client is ILP-32 on all architectures we use
// V8_HOST_ARCH_IA32 on both 32- and 64-bit x86.
#define V8_HOST_ARCH_IA32 1
#define V8_HOST_ARCH_32_BIT 1
#define V8_HOST_CAN_READ_UNALIGNED 1
#else
#define V8_HOST_ARCH_X64 1
#define V8_HOST_ARCH_64_BIT 1
#define V8_HOST_CAN_READ_UNALIGNED 1
#endif  // __native_client__
#elif defined(_M_IX86) || defined(__i386__)
#define V8_HOST_ARCH_IA32 1
#define V8_HOST_ARCH_32_BIT 1
#define V8_HOST_CAN_READ_UNALIGNED 1
#elif defined(__AARCH64EL__)
#define V8_HOST_ARCH_ARM64 1
#define V8_HOST_ARCH_64_BIT 1
#define V8_HOST_CAN_READ_UNALIGNED 1
#elif defined(__ARMEL__)
#define V8_HOST_ARCH_ARM 1
#define V8_HOST_ARCH_32_BIT 1
#elif defined(__MIPSEB__) || defined(__MIPSEL__)
#define V8_HOST_ARCH_MIPS 1
#define V8_HOST_ARCH_32_BIT 1
#else
#error "Host architecture was not detected as supported by v8"
#endif

#if defined(__ARM_ARCH_7A__) || \
    defined(__ARM_ARCH_7R__) || \
    defined(__ARM_ARCH_7__)
# define CAN_USE_ARMV7_INSTRUCTIONS 1
# ifndef CAN_USE_VFP3_INSTRUCTIONS
#  define CAN_USE_VFP3_INSTRUCTIONS
# endif
#endif


// Target architecture detection. This may be set externally. If not, detect
// in the same way as the host architecture, that is, target the native
// environment as presented by the compiler.
#if !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_IA32 && !V8_TARGET_ARCH_X87 &&\
    !V8_TARGET_ARCH_ARM && !V8_TARGET_ARCH_ARM64 && !V8_TARGET_ARCH_MIPS
#if defined(_M_X64) || defined(__x86_64__)
#define V8_TARGET_ARCH_X64 1
#elif defined(_M_IX86) || defined(__i386__)
#define V8_TARGET_ARCH_IA32 1
#elif defined(__AARCH64EL__)
#define V8_TARGET_ARCH_ARM64 1
#elif defined(__ARMEL__)
#define V8_TARGET_ARCH_ARM 1
#elif defined(__MIPSEB__) || defined(__MIPSEL__)
#define V8_TARGET_ARCH_MIPS 1
#else
#error Target architecture was not detected as supported by v8
#endif
#endif

// Check for supported combinations of host and target architectures.
#if V8_TARGET_ARCH_IA32 && !V8_HOST_ARCH_IA32
#error Target architecture ia32 is only supported on ia32 host
#endif
#if V8_TARGET_ARCH_X64 && !V8_HOST_ARCH_X64
#error Target architecture x64 is only supported on x64 host
#endif
#if (V8_TARGET_ARCH_ARM && !(V8_HOST_ARCH_IA32 || V8_HOST_ARCH_ARM))
#error Target architecture arm is only supported on arm and ia32 host
#endif
#if (V8_TARGET_ARCH_ARM64 && !(V8_HOST_ARCH_X64 || V8_HOST_ARCH_ARM64))
#error Target architecture arm64 is only supported on arm64 and x64 host
#endif
#if (V8_TARGET_ARCH_MIPS && !(V8_HOST_ARCH_IA32 || V8_HOST_ARCH_MIPS))
#error Target architecture mips is only supported on mips and ia32 host
#endif

// Determine architecture endianness.
#if V8_TARGET_ARCH_IA32
#define V8_TARGET_LITTLE_ENDIAN 1
#elif V8_TARGET_ARCH_X64
#define V8_TARGET_LITTLE_ENDIAN 1
#elif V8_TARGET_ARCH_ARM
#define V8_TARGET_LITTLE_ENDIAN 1
#elif V8_TARGET_ARCH_ARM64
#define V8_TARGET_LITTLE_ENDIAN 1
#elif V8_TARGET_ARCH_MIPS
#if defined(__MIPSEB__)
#define V8_TARGET_BIG_ENDIAN 1
#else
#define V8_TARGET_LITTLE_ENDIAN 1
#endif
#elif V8_TARGET_ARCH_X87
#define V8_TARGET_LITTLE_ENDIAN 1
#else
#error Unknown target architecture endianness
#endif

#if V8_OS_MACOSX || defined(__FreeBSD__) || defined(__OpenBSD__)
#define USING_BSD_ABI
#endif

#endif  // V8_BASE_BUILD_CONFIG_H_
