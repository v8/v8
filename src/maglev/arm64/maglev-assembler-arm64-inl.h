// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_
#define V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_

#include "src/codegen/macro-assembler-inl.h"
#include "src/maglev/maglev-assembler.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-code-gen-state.h"

namespace v8 {
namespace internal {
namespace maglev {

constexpr Register kScratchRegister = x16;
constexpr DoubleRegister kScratchDoubleReg = d30;

inline MemOperand MaglevAssembler::StackSlotOperand(StackSlot slot) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
  return MemOperand();
}

inline MemOperand MaglevAssembler::GetStackSlot(
    const compiler::AllocatedOperand& operand) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
  return MemOperand();
}

inline MemOperand MaglevAssembler::ToMemOperand(
    const compiler::InstructionOperand& operand) {
  return GetStackSlot(compiler::AllocatedOperand::cast(operand));
}

inline MemOperand MaglevAssembler::ToMemOperand(const ValueLocation& location) {
  return ToMemOperand(location.operand());
}

inline void MaglevAssembler::Move(StackSlot dst, Register src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(StackSlot dst, DoubleRegister src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, StackSlot src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(DoubleRegister dst, StackSlot src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(MemOperand dst, Register src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(MemOperand dst, DoubleRegister src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, MemOperand src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(DoubleRegister dst, MemOperand src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, Smi src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Register src) {
  MacroAssembler::Move(dst, src);
}
inline void MaglevAssembler::Move(Register dst, Immediate i) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(DoubleRegister dst, double n) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, Handle<HeapObject> obj) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}

inline void MaglevAssembler::AssertStackSizeCorrect() {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}

inline void MaglevAssembler::MaterialiseValueNode(Register dst,
                                                  ValueNode* value) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_
