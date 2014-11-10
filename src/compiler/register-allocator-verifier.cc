// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/instruction.h"
#include "src/compiler/register-allocator-verifier.h"

namespace v8 {
namespace internal {
namespace compiler {

static size_t OperandCount(const Instruction* instr) {
  return instr->InputCount() + instr->OutputCount() + instr->TempCount();
}


RegisterAllocatorVerifier::RegisterAllocatorVerifier(
    Zone* zone, const InstructionSequence* sequence)
    : sequence_(sequence), constraints_(zone) {
  constraints_.reserve(sequence->instructions().size());
  for (const auto* instr : sequence->instructions()) {
    const size_t operand_count = OperandCount(instr);
    auto* op_constraints =
        zone->NewArray<OperandConstraint>(static_cast<int>(operand_count));
    // Construct OperandConstraints for all InstructionOperands, eliminating
    // kSameAsFirst along the way.
    size_t count = 0;
    for (size_t i = 0; i < instr->InputCount(); ++i, ++count) {
      BuildConstraint(instr->InputAt(i), &op_constraints[count]);
      CHECK_NE(kSameAsFirst, op_constraints[count].type_);
    }
    for (size_t i = 0; i < instr->OutputCount(); ++i, ++count) {
      BuildConstraint(instr->OutputAt(i), &op_constraints[count]);
      if (op_constraints[count].type_ == kSameAsFirst) {
        CHECK(instr->InputCount() > 0);
        op_constraints[count] = op_constraints[0];
      }
    }
    for (size_t i = 0; i < instr->TempCount(); ++i, ++count) {
      BuildConstraint(instr->TempAt(i), &op_constraints[count]);
      CHECK_NE(kSameAsFirst, op_constraints[count].type_);
    }
    // All gaps should be totally unallocated at this point.
    if (instr->IsGapMoves()) {
      const auto* gap = GapInstruction::cast(instr);
      for (int i = GapInstruction::FIRST_INNER_POSITION;
           i <= GapInstruction::LAST_INNER_POSITION; i++) {
        GapInstruction::InnerPosition inner_pos =
            static_cast<GapInstruction::InnerPosition>(i);
        CHECK_EQ(NULL, gap->GetParallelMove(inner_pos));
      }
    }
    InstructionConstraint instr_constraint = {instr, operand_count,
                                              op_constraints};
    constraints()->push_back(instr_constraint);
  }
}


void RegisterAllocatorVerifier::VerifyAssignment() {
  CHECK(sequence()->instructions().size() == constraints()->size());
  auto instr_it = sequence()->begin();
  for (const auto& instr_constraint : *constraints()) {
    const auto* instr = instr_constraint.instruction_;
    const size_t operand_count = instr_constraint.operand_constaints_size_;
    const auto* op_constraints = instr_constraint.operand_constraints_;
    CHECK_EQ(instr, *instr_it);
    CHECK(operand_count == OperandCount(instr));
    size_t count = 0;
    for (size_t i = 0; i < instr->InputCount(); ++i, ++count) {
      CheckConstraint(instr->InputAt(i), &op_constraints[count]);
    }
    for (size_t i = 0; i < instr->OutputCount(); ++i, ++count) {
      CheckConstraint(instr->OutputAt(i), &op_constraints[count]);
    }
    for (size_t i = 0; i < instr->TempCount(); ++i, ++count) {
      CheckConstraint(instr->TempAt(i), &op_constraints[count]);
    }
    ++instr_it;
  }
}


void RegisterAllocatorVerifier::BuildConstraint(const InstructionOperand* op,
                                                OperandConstraint* constraint) {
  constraint->value_ = kMinInt;
  if (op->IsConstant()) {
    constraint->type_ = kConstant;
    constraint->value_ = ConstantOperand::cast(op)->index();
  } else if (op->IsImmediate()) {
    constraint->type_ = kImmediate;
    constraint->value_ = ImmediateOperand::cast(op)->index();
  } else {
    CHECK(op->IsUnallocated());
    const auto* unallocated = UnallocatedOperand::cast(op);
    int vreg = unallocated->virtual_register();
    if (unallocated->basic_policy() == UnallocatedOperand::FIXED_SLOT) {
      constraint->type_ = kFixedSlot;
      constraint->value_ = unallocated->fixed_slot_index();
    } else {
      switch (unallocated->extended_policy()) {
        case UnallocatedOperand::ANY:
          CHECK(false);
          break;
        case UnallocatedOperand::NONE:
          if (sequence()->IsDouble(vreg)) {
            constraint->type_ = kNoneDouble;
          } else {
            constraint->type_ = kNone;
          }
          break;
        case UnallocatedOperand::FIXED_REGISTER:
          constraint->type_ = kFixedRegister;
          constraint->value_ = unallocated->fixed_register_index();
          break;
        case UnallocatedOperand::FIXED_DOUBLE_REGISTER:
          constraint->type_ = kFixedDoubleRegister;
          constraint->value_ = unallocated->fixed_register_index();
          break;
        case UnallocatedOperand::MUST_HAVE_REGISTER:
          if (sequence()->IsDouble(vreg)) {
            constraint->type_ = kDoubleRegister;
          } else {
            constraint->type_ = kRegister;
          }
          break;
        case UnallocatedOperand::SAME_AS_FIRST_INPUT:
          constraint->type_ = kSameAsFirst;
          break;
      }
    }
  }
}


void RegisterAllocatorVerifier::CheckConstraint(
    const InstructionOperand* op, const OperandConstraint* constraint) {
  switch (constraint->type_) {
    case kConstant:
      CHECK(op->IsConstant());
      CHECK_EQ(op->index(), constraint->value_);
      return;
    case kImmediate:
      CHECK(op->IsImmediate());
      CHECK_EQ(op->index(), constraint->value_);
      return;
    case kRegister:
      CHECK(op->IsRegister());
      return;
    case kFixedRegister:
      CHECK(op->IsRegister());
      CHECK_EQ(op->index(), constraint->value_);
      return;
    case kDoubleRegister:
      CHECK(op->IsDoubleRegister());
      return;
    case kFixedDoubleRegister:
      CHECK(op->IsDoubleRegister());
      CHECK_EQ(op->index(), constraint->value_);
      return;
    case kFixedSlot:
      CHECK(op->IsStackSlot());
      CHECK_EQ(op->index(), constraint->value_);
      return;
    case kNone:
      CHECK(op->IsRegister() || op->IsStackSlot());
      return;
    case kNoneDouble:
      CHECK(op->IsDoubleRegister() || op->IsDoubleStackSlot());
      return;
    case kSameAsFirst:
      CHECK(false);
      return;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
