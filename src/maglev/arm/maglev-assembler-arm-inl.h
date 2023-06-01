// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_ARM_MAGLEV_ASSEMBLER_ARM_INL_H_
#define V8_MAGLEV_ARM_MAGLEV_ASSEMBLER_ARM_INL_H_

#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/maglev/maglev-assembler.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-code-gen-state.h"

#define MAGLEV_NOT_IMPLEMENTED()                            \
  do {                                                      \
    PrintF("Maglev: Not yet implemented '%s'\n", __func__); \
    failed_ = true;                                         \
  } while (false)

namespace v8 {
namespace internal {
namespace maglev {

constexpr Condition ConditionFor(Operation operation) {
  switch (operation) {
    case Operation::kEqual:
    case Operation::kStrictEqual:
      return eq;
    case Operation::kLessThan:
      return lt;
    case Operation::kLessThanOrEqual:
      return le;
    case Operation::kGreaterThan:
      return gt;
    case Operation::kGreaterThanOrEqual:
      return ge;
    default:
      UNREACHABLE();
  }
}

constexpr Condition ConditionForFloat64(Operation operation) {
  return ConditionFor(operation);
}

constexpr Condition ConditionForNaN() { return vs; }

inline int ShiftFromScale(int n) {
  switch (n) {
    case 1:
      return 0;
    case 2:
      return 1;
    case 4:
      return 2;
    default:
      UNREACHABLE();
  }
}

class MaglevAssembler::ScratchRegisterScope {
 public:
  explicit ScratchRegisterScope(MaglevAssembler* masm)
      : wrapped_scope_(masm),
        masm_(masm),
        prev_scope_(masm->scratch_register_scope_) {
    masm_->scratch_register_scope_ = this;
  }

  ~ScratchRegisterScope() { masm_->scratch_register_scope_ = prev_scope_; }

  Register Acquire() { return wrapped_scope_.Acquire(); }
  void Include(Register reg) { wrapped_scope_.Include(reg); }
  void Include(const RegList list) { wrapped_scope_.Include(list); }

  DoubleRegister AcquireDouble() { return wrapped_scope_.AcquireD(); }
  void IncludeDouble(const DoubleRegList list) {}

  RegList Available() { return wrapped_scope_.Available(); }
  void SetAvailable(RegList list) { wrapped_scope_.SetAvailable(list); }

  DoubleRegList AvailableDouble() { return {}; }
  void SetAvailableDouble(DoubleRegList list) {}

 private:
  UseScratchRegisterScope wrapped_scope_;
  MaglevAssembler* masm_;
  ScratchRegisterScope* prev_scope_;
};

template <typename... T>
void MaglevAssembler::Push(T... vals) {
  MAGLEV_NOT_IMPLEMENTED();
}

template <typename... T>
void MaglevAssembler::PushReverse(T... vals) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::BindJumpTarget(Label* label) { bind(label); }

inline void MaglevAssembler::BindBlock(BasicBlock* block) {
  bind(block->label());
}

inline void MaglevAssembler::DoubleToInt64Repr(Register dst,
                                               DoubleRegister src) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::SmiTagInt32(Register obj, Label* fail) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline Condition MaglevAssembler::IsInt64Constant(Register reg,
                                                  int64_t constant) {
  MAGLEV_NOT_IMPLEMENTED();
  return eq;
}

inline Condition MaglevAssembler::IsRootConstant(Input input,
                                                 RootIndex root_index) {
  MAGLEV_NOT_IMPLEMENTED();
  return eq;
}

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

inline void MaglevAssembler::BuildTypedArrayDataPointer(Register data_pointer,
                                                        Register object) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::LoadTaggedFieldByIndex(Register result,
                                                    Register object,
                                                    Register index, int scale,
                                                    int offset) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::LoadBoundedSizeFromObject(Register result,
                                                       Register object,
                                                       int offset) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::LoadExternalPointerField(Register result,
                                                      MemOperand operand) {
#ifdef V8_ENABLE_SANDBOX
  LoadSandboxedPointerField(result, operand);
#else
  Move(result, operand);
#endif
}

void MaglevAssembler::LoadFixedArrayElement(Register result, Register array,
                                            Register index) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::LoadFixedArrayElementWithoutDecompressing(
    Register result, Register array, Register index) {
  MAGLEV_NOT_IMPLEMENTED();
}

void MaglevAssembler::LoadFixedDoubleArrayElement(DoubleRegister result,
                                                  Register array,
                                                  Register index) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::LoadSignedField(Register result,
                                             MemOperand operand, int size) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::LoadUnsignedField(Register result,
                                               MemOperand operand, int size) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::StoreTaggedSignedField(Register object, int offset,
                                                    Register value) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::StoreTaggedSignedField(Register object, int offset,
                                                    Smi value) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::StoreField(MemOperand operand, Register value,
                                        int size) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::ReverseByteOrder(Register value, int size) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::IncrementInt32(Register reg) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::Move(StackSlot dst, Register src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(StackSlot dst, DoubleRegister src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(Register dst, StackSlot src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(DoubleRegister dst, StackSlot src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(MemOperand dst, Register src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(MemOperand dst, DoubleRegister src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(Register dst, MemOperand src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(DoubleRegister dst, MemOperand src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(DoubleRegister dst, DoubleRegister src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(Register dst, Smi src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(Register dst, ExternalReference src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(Register dst, Register src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(Register dst, TaggedIndex i) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(Register dst, int32_t i) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(DoubleRegister dst, double n) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(DoubleRegister dst, Float64 n) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::Move(Register dst, Handle<HeapObject> obj) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::SignExtend32To64Bits(Register dst, Register src) {
  MAGLEV_NOT_IMPLEMENTED();
}
inline void MaglevAssembler::NegateInt32(Register val) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::ToUint8Clamped(Register result,
                                            DoubleRegister value, Label* min,
                                            Label* max, Label* done) {
  MAGLEV_NOT_IMPLEMENTED();
}

template <typename NodeT>
inline void MaglevAssembler::DeoptIfBufferDetached(Register array,
                                                   Register scratch,
                                                   NodeT* node) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::LoadByte(Register dst, MemOperand src) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::IsObjectType(Register heap_object,
                                          InstanceType type) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareObjectType(Register heap_object,
                                               InstanceType type) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  CompareObjectType(heap_object, type, scratch);
}

inline void MaglevAssembler::JumpIfJSAnyIsNotPrimitive(
    Register heap_object, Label* target, Label::Distance distance) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareObjectType(Register heap_object,
                                               InstanceType type,
                                               Register scratch) {
  LoadMap(scratch, heap_object);
  CompareInstanceType(scratch, scratch, type);
}

inline void MaglevAssembler::CompareObjectTypeRange(Register heap_object,
                                                    InstanceType lower_limit,
                                                    InstanceType higher_limit) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  LoadMap(scratch, heap_object);
  CompareInstanceTypeRange(scratch, scratch, lower_limit, higher_limit);
}

inline void MaglevAssembler::CompareMapWithRoot(Register object,
                                                RootIndex index,
                                                Register scratch) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareInstanceTypeRange(
    Register map, InstanceType lower_limit, InstanceType higher_limit) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  CompareInstanceTypeRange(map, scratch, lower_limit, higher_limit);
}

inline void MaglevAssembler::CompareInstanceTypeRange(
    Register map, Register instance_type_out, InstanceType lower_limit,
    InstanceType higher_limit) {
  MacroAssembler::CompareInstanceTypeRange(map, instance_type_out, lower_limit,
                                           higher_limit);
}

inline void MaglevAssembler::CompareTagged(Register reg, Smi smi) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareTagged(Register reg,
                                           Handle<HeapObject> obj) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareTagged(Register src1, Register src2) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareInt32(Register reg, int32_t imm) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareInt32(Register src1, Register src2) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareFloat64(DoubleRegister src1,
                                            DoubleRegister src2) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CallSelf() {
  DCHECK(code_gen_state()->entry_label()->is_bound());
  bl(code_gen_state()->entry_label());
}

inline void MaglevAssembler::Jump(Label* target, Label::Distance) { b(target); }

inline void MaglevAssembler::JumpIf(Condition cond, Label* target,
                                    Label::Distance) {
  b(target, cond);
}

inline void MaglevAssembler::JumpIfRoot(Register with, RootIndex index,
                                        Label* if_equal,
                                        Label::Distance distance) {
  MacroAssembler::JumpIfRoot(with, index, if_equal);
}

inline void MaglevAssembler::JumpIfNotRoot(Register with, RootIndex index,
                                           Label* if_not_equal,
                                           Label::Distance distance) {
  MacroAssembler::JumpIfNotRoot(with, index, if_not_equal);
}

inline void MaglevAssembler::JumpIfSmi(Register src, Label* on_smi,
                                       Label::Distance distance) {
  MacroAssembler::JumpIfSmi(src, on_smi);
}

void MaglevAssembler::JumpIfByte(Condition cc, Register value, int32_t byte,
                                 Label* target, Label::Distance) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareInt32AndJumpIf(Register r1, Register r2,
                                                   Condition cond,
                                                   Label* target,
                                                   Label::Distance distance) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareInt32AndJumpIf(Register r1, int32_t value,
                                                   Condition cond,
                                                   Label* target,
                                                   Label::Distance distance) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareSmiAndJumpIf(Register r1, Smi value,
                                                 Condition cond, Label* target,
                                                 Label::Distance distance) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::CompareTaggedAndJumpIf(Register r1, Smi value,
                                                    Condition cond,
                                                    Label* target,
                                                    Label::Distance distance) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::TestInt32AndJumpIfAnySet(
    Register r1, int32_t mask, Label* target, Label::Distance distance) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::TestInt32AndJumpIfAllClear(
    Register r1, int32_t mask, Label* target, Label::Distance distance) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::LoadHeapNumberValue(DoubleRegister result,
                                                 Register heap_number) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::Int32ToDouble(DoubleRegister result, Register n) {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::Pop(Register dst) { MAGLEV_NOT_IMPLEMENTED(); }

inline void MaglevAssembler::AssertStackSizeCorrect() {
  MAGLEV_NOT_IMPLEMENTED();
}

inline void MaglevAssembler::FinishCode() { MAGLEV_NOT_IMPLEMENTED(); }

template <typename NodeT>
inline void MaglevAssembler::EmitEagerDeoptIfNotEqual(DeoptimizeReason reason,
                                                      NodeT* node) {
  EmitEagerDeoptIf(ne, reason, node);
}

inline void MaglevAssembler::MaterialiseValueNode(Register dst,
                                                  ValueNode* value) {
  MAGLEV_NOT_IMPLEMENTED();
}

template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      Register src) {
  MAGLEV_NOT_IMPLEMENTED();
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr, Register dst,
                                      MemOperand src) {
  MAGLEV_NOT_IMPLEMENTED();
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr,
                                      MemOperand dst, Register src) {
  MAGLEV_NOT_IMPLEMENTED();
}
template <>
inline void MaglevAssembler::MoveRepr(MachineRepresentation repr,
                                      MemOperand dst, MemOperand src) {
  ScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MoveRepr(repr, scratch, src);
  MoveRepr(repr, dst, scratch);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_ARM_MAGLEV_ASSEMBLER_ARM_INL_H_
