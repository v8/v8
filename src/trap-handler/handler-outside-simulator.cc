// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8config.h"
#include "src/trap-handler/trap-handler-simulator.h"

#if !V8_OS_LINUX
#error "The inline assembly only works on Linux so far."
#endif

asm(
    // Define the ProbeMemory function declared in trap-handler-simulators.h.
    ".pushsection .text                             \n"
    ".globl ProbeMemory                             \n"
    ".type ProbeMemory, %function                   \n"
    ".globl v8_probe_memory_address                 \n"
    ".globl v8_probe_memory_continuation            \n"
    "ProbeMemory:                                   \n"
    // First parameter (address) passed in %rdi.
    // The second parameter (pc) is unused here. It is read by the trap handler
    // instead.
    "v8_probe_memory_address:                       \n"
    "  movb (%rdi), %al                             \n"
    // Return 0 on success.
    "  xorl %eax, %eax                              \n"
    "v8_probe_memory_continuation:                  \n"
    // If the trap handler continues here, it wrote the landing pad in %rax.
    "  ret                                          \n"
    ".popsection                                    \n");
