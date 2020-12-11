// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64

#include "src/codegen/ppc/constants-ppc.h"

namespace v8 {
namespace internal {

// These register names are defined in a way to match the native disassembler
// formatting. See for example the command "objdump -d <binary file>".
const char* Registers::names_[kNumRegisters] = {
    "r0",  "sp",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",  "r8",  "r9",  "r10",
    "r11", "ip",  "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21",
    "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29", "r30", "fp"};

const char* DoubleRegisters::names_[kNumDoubleRegisters] = {
    "d0",  "d1",  "d2",  "d3",  "d4",  "d5",  "d6",  "d7",  "d8",  "d9",  "d10",
    "d11", "d12", "d13", "d14", "d15", "d16", "d17", "d18", "d19", "d20", "d21",
    "d22", "d23", "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31"};

// PPC FP and Vector Register (VR and VSR) Layout.
// VR0 is VSR32 and goes all the way to VSR63 which is used by V8 Vector
// operations.
//
// VSR[0]0 - FPR[0]                     VSR[0]128
//   |
//   |
//   |
// VSR[31] - FPR[31]
// VSR[32] - VR[0]                      VR[0]128
//   |
//   |
//   |
//   V
// VSR[63] - VR[31]
const char* Simd128Registers::names_[kNumSimd128Registers] = {
    "v0",  "v1",   "v2",  "v3",  "v4",  "v5",  "v6",  "v7",
    "v8",  "v9",   "v10", "v11", "v12", "v13", "v14", "v15",
    "v16", "v17",  "v18", "v19", "v20", "v21", "v22", "v23",
    "v24", "vr25", "v26", "v27", "v28", "v29", "v30", "v31"};

int DoubleRegisters::Number(const char* name) {
  for (int i = 0; i < kNumDoubleRegisters; i++) {
    if (strcmp(names_[i], name) == 0) {
      return i;
    }
  }

  // No register with the requested name found.
  return kNoRegister;
}

int Registers::Number(const char* name) {
  // Look through the canonical names.
  for (int i = 0; i < kNumRegisters; i++) {
    if (strcmp(names_[i], name) == 0) {
      return i;
    }
  }

  // No register with the requested name found.
  return kNoRegister;
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
