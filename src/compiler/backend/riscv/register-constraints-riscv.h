// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_RISCV_REGISTER_CONSTRAINTS_RISCV_H_
#define V8_COMPILER_BACKEND_RISCV_REGISTER_CONSTRAINTS_RISCV_H_

namespace v8 {
namespace internal {
namespace compiler {

enum class RiscvRegisterConstraint {
  kNone = 0,
  // Destination and source operands of vector operations are not allowed to
  // overlap.
  kNoDestinationSourceOverlap,
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_RISCV_REGISTER_CONSTRAINTS_RISCV_H_
