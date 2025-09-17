// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_RISCV_EXTENSION_RISCV_ZIMOP_H_
#define V8_CODEGEN_RISCV_EXTENSION_RISCV_ZIMOP_H_
#include "src/codegen/assembler.h"
#include "src/codegen/riscv/base-assembler-riscv.h"
#include "src/codegen/riscv/register-riscv.h"

namespace v8 {

namespace internal {

class AssemblerRISCVZimop : public AssemblerRiscvBase {
 public:
  void mop_r(int n, Register rd, Register rs1);
  void mop_rr(int n, Register rd, Register rs1, Register rs2);
};

}  // namespace internal

}  // namespace v8
#endif  // V8_CODEGEN_RISCV_EXTENSION_RISCV_ZIMOP_H_
