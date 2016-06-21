// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-peephole-optimizer.h"

#include "src/interpreter/constant-array-builder.h"
#include "src/objects-inl.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace interpreter {

BytecodePeepholeOptimizer::BytecodePeepholeOptimizer(
    ConstantArrayBuilder* constant_array_builder,
    BytecodePipelineStage* next_stage)
    : constant_array_builder_(constant_array_builder), next_stage_(next_stage) {
  InvalidateLast();
}

// override
Handle<BytecodeArray> BytecodePeepholeOptimizer::ToBytecodeArray(
    int fixed_register_count, int parameter_count,
    Handle<FixedArray> handler_table) {
  Flush();
  return next_stage_->ToBytecodeArray(fixed_register_count, parameter_count,
                                      handler_table);
}

// override
void BytecodePeepholeOptimizer::Write(BytecodeNode* node) {
  node = OptimizeAndEmitLast(node);
  if (node != nullptr) {
    SetLast(node);
  }
}

// override
void BytecodePeepholeOptimizer::WriteJump(BytecodeNode* node,
                                          BytecodeLabel* label) {
  node = OptimizeAndEmitLast(node);
  next_stage_->WriteJump(node, label);
}

// override
void BytecodePeepholeOptimizer::BindLabel(BytecodeLabel* label) {
  Flush();
  next_stage_->BindLabel(label);
}

// override
void BytecodePeepholeOptimizer::BindLabel(const BytecodeLabel& target,
                                          BytecodeLabel* label) {
  // There is no need to flush here, it will have been flushed when |target|
  // was bound.
  next_stage_->BindLabel(target, label);
}

void BytecodePeepholeOptimizer::Flush() {
  // TODO(oth/rmcilroy): We could check CanElideLast() here to potentially
  // eliminate last rather than writing it.
  if (LastIsValid()) {
    next_stage_->Write(&last_);
    InvalidateLast();
  }
}

void BytecodePeepholeOptimizer::InvalidateLast() {
  last_.set_bytecode(Bytecode::kIllegal);
}

bool BytecodePeepholeOptimizer::LastIsValid() const {
  return last_.bytecode() != Bytecode::kIllegal;
}

void BytecodePeepholeOptimizer::SetLast(const BytecodeNode* const node) {
  last_.Clone(node);
}

Handle<Object> BytecodePeepholeOptimizer::GetConstantForIndexOperand(
    const BytecodeNode* const node, int index) const {
  DCHECK_LE(index, node->operand_count());
  DCHECK_EQ(Bytecodes::GetOperandType(node->bytecode(), 0), OperandType::kIdx);
  uint32_t index_operand = node->operand(0);
  return constant_array_builder_->At(index_operand);
}

bool BytecodePeepholeOptimizer::LastBytecodePutsNameInAccumulator() const {
  DCHECK(LastIsValid());
  return (last_.bytecode() == Bytecode::kTypeOf ||
          last_.bytecode() == Bytecode::kToName ||
          (last_.bytecode() == Bytecode::kLdaConstant &&
           GetConstantForIndexOperand(&last_, 0)->IsName()));
}

void BytecodePeepholeOptimizer::TryToRemoveLastExpressionPosition(
    const BytecodeNode* const current) {
  if (current->source_info().is_valid() &&
      last_.source_info().is_expression() &&
      Bytecodes::IsWithoutExternalSideEffects(last_.bytecode())) {
    // The last bytecode has been marked as expression. It has no
    // external effects so can't throw and the current bytecode is a
    // source position. Remove the expression position on the last
    // bytecode to open up potential peephole optimizations and to
    // save the memory and perf cost of storing the unneeded
    // expression position.
    last_.source_info().set_invalid();
  }
}

bool BytecodePeepholeOptimizer::CanElideCurrent(
    const BytecodeNode* const current) const {
  if (Bytecodes::IsLdarOrStar(last_.bytecode()) &&
      Bytecodes::IsLdarOrStar(current->bytecode()) &&
      current->operand(0) == last_.operand(0)) {
    // Ldar and Star make the accumulator and register hold equivalent
    // values. Only the first bytecode is needed if there's a sequence
    // of back-to-back Ldar and Star bytecodes with the same operand.
    return true;
  } else if (current->bytecode() == Bytecode::kToName &&
             LastBytecodePutsNameInAccumulator()) {
    // If the previous bytecode ensured a name was in the accumulator,
    // the type coercion ToName() can be elided.
    return true;
  } else {
    // Additional candidates for eliding current:
    // (i) ToNumber if the last puts a number in the accumulator.
    return false;
  }
}

bool BytecodePeepholeOptimizer::CanElideLastBasedOnSourcePosition(
    const BytecodeNode* const current) const {
  //
  // The rules for allowing the elision of the last bytecode based
  // on source position are:
  //
  //                     C U R R E N T
  //              +--------+--------+--------+
  //              |  None  |  Expr  |  Stmt  |
  //  L  +--------+--------+--------+--------+
  //     |  None  |  YES   |  YES   |  YES   |
  //  A  +--------+--------+--------+--------+
  //     |  Expr  |  YES   | MAYBE  |  MAYBE |
  //  S  +--------+--------+--------+--------+
  //     |  Stmt  |  YES   |   NO   |   NO   |
  //  T  +--------+--------+--------+--------+
  //
  // The goal is not lose any statement positions and not lose useful
  // expression positions. Whenever the last bytecode is elided it's
  // source position information is applied to the current node
  // updating it if necessary.
  //
  // The last bytecode can be elided for the MAYBE cases if the last
  // bytecode is known not to throw. If it throws, the system would
  // not have correct stack trace information. The appropriate check
  // for this would be Bytecodes::IsWithoutExternalSideEffects(),
  // which is checked in
  // BytecodePeepholeOptimizer::TransformLastAndCurrentBytecodes() to
  // keep the check here simple.
  //
  // In rare cases, bytecode generation produces consecutive bytecodes
  // with the same expression positions. In principle, the latter of
  // these can be elided, but would make this function more expensive.
  //
  return (!last_.source_info().is_valid() ||
          !current->source_info().is_valid());
}

namespace {

void TransformLdaStarToLdrLdar(Bytecode new_bytecode, BytecodeNode* const last,
                               BytecodeNode* const current) {
  DCHECK_EQ(current->bytecode(), Bytecode::kStar);

  //
  // An example transformation here would be:
  //
  //   LdaGlobal i0, i1  ____\  LdrGlobal i0, i1, R
  //   Star R            ====/  Ldar R
  //
  // which loads a global value into both a register and the
  // accumulator. However, in the second form the Ldar can often be
  // peephole optimized away unlike the Star in the first form.
  //
  last->Transform(new_bytecode, current->operand(0));
  current->set_bytecode(Bytecode::kLdar, current->operand(0));
}

}  // namespace

bool BytecodePeepholeOptimizer::TransformLastAndCurrentBytecodes(
    BytecodeNode* const current) {
  if (current->bytecode() == Bytecode::kStar &&
      !current->source_info().is_statement()) {
    // Note: If the Star is tagged with a statement position, we can't
    // perform this transform as the store to the register will
    // have the wrong ordering for stepping in the debugger.
    switch (last_.bytecode()) {
      case Bytecode::kLdaNamedProperty:
        TransformLdaStarToLdrLdar(Bytecode::kLdrNamedProperty, &last_, current);
        return true;
      case Bytecode::kLdaKeyedProperty:
        TransformLdaStarToLdrLdar(Bytecode::kLdrKeyedProperty, &last_, current);
        return true;
      case Bytecode::kLdaGlobal:
        TransformLdaStarToLdrLdar(Bytecode::kLdrGlobal, &last_, current);
        return true;
      case Bytecode::kLdaContextSlot:
        TransformLdaStarToLdrLdar(Bytecode::kLdrContextSlot, &last_, current);
        return true;
      case Bytecode::kLdaUndefined:
        TransformLdaStarToLdrLdar(Bytecode::kLdrUndefined, &last_, current);
        return true;
      default:
        break;
    }
  }
  return false;
}

bool BytecodePeepholeOptimizer::RemoveToBooleanFromJump(
    BytecodeNode* const current) {
  bool can_remove = Bytecodes::IsJumpIfToBoolean(current->bytecode()) &&
                    Bytecodes::WritesBooleanToAccumulator(last_.bytecode());
  if (can_remove) {
    // Conditional jumps with boolean conditions are emiitted in
    // ToBoolean form by the bytecode array builder,
    // i.e. JumpIfToBooleanTrue rather JumpIfTrue. The ToBoolean
    // element can be removed if the previous bytecode put a boolean
    // value in the accumulator.
    Bytecode jump = Bytecodes::GetJumpWithoutToBoolean(current->bytecode());
    current->set_bytecode(jump, current->operand(0));
  }
  return can_remove;
}

bool BytecodePeepholeOptimizer::RemoveToBooleanFromLogicalNot(
    BytecodeNode* const current) {
  bool can_remove = current->bytecode() == Bytecode::kToBooleanLogicalNot &&
                    Bytecodes::WritesBooleanToAccumulator(last_.bytecode());
  if (can_remove) {
    // Logical-nots are emitted in ToBoolean form by the bytecode array
    // builder, The ToBoolean element can be removed if the previous bytecode
    // put a boolean value in the accumulator.
    current->set_bytecode(Bytecode::kLogicalNot);
  }
  return can_remove;
}

bool BytecodePeepholeOptimizer::TransformCurrentBytecode(
    BytecodeNode* const current) {
  return RemoveToBooleanFromJump(current) ||
         RemoveToBooleanFromLogicalNot(current);
}

bool BytecodePeepholeOptimizer::CanElideLast(
    const BytecodeNode* const current) const {
  if (last_.bytecode() == Bytecode::kNop) {
    // Nop are placeholders for holding source position information.
    return true;
  } else if (Bytecodes::IsAccumulatorLoadWithoutEffects(current->bytecode()) &&
             Bytecodes::IsAccumulatorLoadWithoutEffects(last_.bytecode())) {
    // The accumulator is invisible to the debugger. If there is a sequence of
    // consecutive accumulator loads (that don't have side effects) then only
    // the final load is potentially visible.
    return true;
  } else if (Bytecodes::GetAccumulatorUse(current->bytecode()) ==
                 AccumulatorUse::kWrite &&
             Bytecodes::IsAccumulatorLoadWithoutEffects(last_.bytecode())) {
    // The current instruction clobbers the accumulator without reading it. The
    // load in the last instruction can be elided as it has no effect.
    return true;
  } else {
    return false;
  }
}

BytecodeNode* BytecodePeepholeOptimizer::Optimize(BytecodeNode* current) {
  TryToRemoveLastExpressionPosition(current);

  if (TransformCurrentBytecode(current) ||
      TransformLastAndCurrentBytecodes(current)) {
    return current;
  }

  if (CanElideCurrent(current)) {
    if (current->source_info().is_valid()) {
      // Preserve the source information by replacing the current bytecode
      // with a no op bytecode.
      current->set_bytecode(Bytecode::kNop);
    } else {
      current = nullptr;
    }
    return current;
  }

  if (CanElideLast(current) && CanElideLastBasedOnSourcePosition(current)) {
    if (last_.source_info().is_valid()) {
      // Current can not be valid per CanElideLastBasedOnSourcePosition().
      current->source_info().Clone(last_.source_info());
    }
    InvalidateLast();
    return current;
  }

  return current;
}

BytecodeNode* BytecodePeepholeOptimizer::OptimizeAndEmitLast(
    BytecodeNode* current) {
  // Attempt optimization if there is an earlier node to optimize with.
  if (LastIsValid()) {
    current = Optimize(current);
    // Only output the last node if it wasn't invalidated by the optimization.
    if (LastIsValid()) {
      next_stage_->Write(&last_);
      InvalidateLast();
    }
  }
  return current;
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
