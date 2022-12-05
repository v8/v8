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
  return MemOperand(fp, slot.index);
}

// TODO(Victorgomes): Unify this to use StackSlot struct.
inline MemOperand MaglevAssembler::GetStackSlot(
    const compiler::AllocatedOperand& operand) {
  return MemOperand(fp, GetFramePointerOffsetForStackSlot(operand));
}

inline MemOperand MaglevAssembler::ToMemOperand(
    const compiler::InstructionOperand& operand) {
  return GetStackSlot(compiler::AllocatedOperand::cast(operand));
}

inline MemOperand MaglevAssembler::ToMemOperand(const ValueLocation& location) {
  return ToMemOperand(location.operand());
}

inline void MaglevAssembler::Move(StackSlot dst, Register src) {
  Str(src, StackSlotOperand(dst));
}
inline void MaglevAssembler::Move(StackSlot dst, DoubleRegister src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, StackSlot src) {
  Ldr(dst, StackSlotOperand(src));
}
inline void MaglevAssembler::Move(DoubleRegister dst, StackSlot src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(MemOperand dst, Register src) {
  Str(src, dst);
}
inline void MaglevAssembler::Move(MemOperand dst, DoubleRegister src) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, MemOperand src) {
  Ldr(dst, src);
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
inline void MaglevAssembler::Move(Register dst, Immediate i) { Mov(dst, i); }
inline void MaglevAssembler::Move(DoubleRegister dst, double n) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}
inline void MaglevAssembler::Move(Register dst, Handle<HeapObject> obj) {
  Mov(dst, Operand(obj));
}

inline void MaglevAssembler::Jump(Label* target) { B(target); }

inline void MaglevAssembler::JumpIf(Condition cond, Label* target) {
  b(target, cond);
}

// TODO(victorgomes): We should avoid dong a single push in arm64!
inline void MaglevAssembler::Push(Register src) { Push(src, padreg); }
inline void MaglevAssembler::Pop(Register dst) { Pop(padreg, dst); }

inline void MaglevAssembler::AssertStackSizeCorrect() {
  // TODO(v8:7700): Implement!
}

inline void MaglevAssembler::FinishCode() {
  ForceConstantPoolEmissionWithoutJump();
}

inline void MaglevAssembler::MaterialiseValueNode(Register dst,
                                                  ValueNode* value) {
  // TODO(v8:7700): Implement!
  UNREACHABLE();
}

template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      Register src) {
  Mov(dst, src);
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      MemOperand src) {
  switch (repr) {
    case MachineRepresentation::kWord32:
      return Ldr(dst.W(), src);
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTaggedSigned:
      return Ldr(dst, src);
    default:
      UNREACHABLE();
  }
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr,
                                      MemOperand dst, Register src) {
  switch (repr) {
    case MachineRepresentation::kWord32:
      return Str(src.W(), dst);
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTaggedSigned:
      return Str(src, dst);
    default:
      UNREACHABLE();
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_ARM64_MAGLEV_ASSEMBLER_ARM64_INL_H_
