// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-array-builder.h"

#include "src/compiler.h"
#include "src/interpreter/bytecode-array-writer.h"
#include "src/interpreter/bytecode-label.h"
#include "src/interpreter/bytecode-peephole-optimizer.h"
#include "src/interpreter/bytecode-register-optimizer.h"
#include "src/interpreter/interpreter-intrinsics.h"

namespace v8 {
namespace internal {
namespace interpreter {

BytecodeArrayBuilder::BytecodeArrayBuilder(Isolate* isolate, Zone* zone,
                                           int parameter_count,
                                           int context_count, int locals_count,
                                           FunctionLiteral* literal)
    : isolate_(isolate),
      zone_(zone),
      bytecode_generated_(false),
      constant_array_builder_(isolate, zone),
      handler_table_builder_(isolate, zone),
      return_seen_in_block_(false),
      parameter_count_(parameter_count),
      local_register_count_(locals_count),
      context_register_count_(context_count),
      temporary_allocator_(zone, fixed_register_count()),
      bytecode_array_writer_(isolate, zone, &constant_array_builder_),
      pipeline_(&bytecode_array_writer_) {
  DCHECK_GE(parameter_count_, 0);
  DCHECK_GE(context_register_count_, 0);
  DCHECK_GE(local_register_count_, 0);

  if (FLAG_ignition_peephole) {
    pipeline_ = new (zone)
        BytecodePeepholeOptimizer(&constant_array_builder_, pipeline_);
  }

  if (FLAG_ignition_reo) {
    pipeline_ = new (zone) BytecodeRegisterOptimizer(
        zone, &temporary_allocator_, parameter_count, pipeline_);
  }

  return_position_ =
      literal ? std::max(literal->start_position(), literal->end_position() - 1)
              : RelocInfo::kNoPosition;
}

Register BytecodeArrayBuilder::first_context_register() const {
  DCHECK_GT(context_register_count_, 0);
  return Register(local_register_count_);
}

Register BytecodeArrayBuilder::last_context_register() const {
  DCHECK_GT(context_register_count_, 0);
  return Register(local_register_count_ + context_register_count_ - 1);
}

Register BytecodeArrayBuilder::Parameter(int parameter_index) const {
  DCHECK_GE(parameter_index, 0);
  return Register::FromParameterIndex(parameter_index, parameter_count());
}

bool BytecodeArrayBuilder::RegisterIsParameterOrLocal(Register reg) const {
  return reg.is_parameter() || reg.index() < locals_count();
}

Handle<BytecodeArray> BytecodeArrayBuilder::ToBytecodeArray() {
  DCHECK(return_seen_in_block_);
  DCHECK(!bytecode_generated_);
  bytecode_generated_ = true;

  Handle<FixedArray> handler_table = handler_table_builder()->ToHandlerTable();
  return pipeline_->ToBytecodeArray(fixed_register_count(), parameter_count(),
                                    handler_table);
}

namespace {

static bool ExpressionPositionIsNeeded(Bytecode bytecode) {
  // An expression position is always needed if filtering is turned
  // off. Otherwise an expression is only needed if the bytecode has
  // external side effects.
  return !FLAG_ignition_filter_expression_positions ||
         !Bytecodes::IsWithoutExternalSideEffects(bytecode);
}

}  // namespace

void BytecodeArrayBuilder::AttachSourceInfo(BytecodeNode* node) {
  if (latest_source_info_.is_valid()) {
    // Statement positions need to be emitted immediately.  Expression
    // positions can be pushed back until a bytecode is found that can
    // throw. Hence we only invalidate the existing source position
    // information if it is used.
    if (latest_source_info_.is_statement() ||
        ExpressionPositionIsNeeded(node->bytecode())) {
      node->source_info() = latest_source_info_;
      latest_source_info_.set_invalid();
    }
  }
}

void BytecodeArrayBuilder::Output(Bytecode bytecode) {
  BytecodeNode node(bytecode);
  AttachSourceInfo(&node);
  pipeline()->Write(&node);
}

void BytecodeArrayBuilder::OutputScaled(Bytecode bytecode,
                                        OperandScale operand_scale,
                                        uint32_t operand0, uint32_t operand1,
                                        uint32_t operand2, uint32_t operand3) {
  DCHECK(OperandIsValid(bytecode, operand_scale, 0, operand0));
  DCHECK(OperandIsValid(bytecode, operand_scale, 1, operand1));
  DCHECK(OperandIsValid(bytecode, operand_scale, 2, operand2));
  DCHECK(OperandIsValid(bytecode, operand_scale, 3, operand3));
  BytecodeNode node(bytecode, operand0, operand1, operand2, operand3,
                    operand_scale);
  AttachSourceInfo(&node);
  pipeline()->Write(&node);
}

void BytecodeArrayBuilder::OutputScaled(Bytecode bytecode,
                                        OperandScale operand_scale,
                                        uint32_t operand0, uint32_t operand1,
                                        uint32_t operand2) {
  DCHECK(OperandIsValid(bytecode, operand_scale, 0, operand0));
  DCHECK(OperandIsValid(bytecode, operand_scale, 1, operand1));
  DCHECK(OperandIsValid(bytecode, operand_scale, 2, operand2));
  BytecodeNode node(bytecode, operand0, operand1, operand2, operand_scale);
  AttachSourceInfo(&node);
  pipeline()->Write(&node);
}

void BytecodeArrayBuilder::OutputScaled(Bytecode bytecode,
                                        OperandScale operand_scale,
                                        uint32_t operand0, uint32_t operand1) {
  DCHECK(OperandIsValid(bytecode, operand_scale, 0, operand0));
  DCHECK(OperandIsValid(bytecode, operand_scale, 1, operand1));
  BytecodeNode node(bytecode, operand0, operand1, operand_scale);
  AttachSourceInfo(&node);
  pipeline()->Write(&node);
}

void BytecodeArrayBuilder::OutputScaled(Bytecode bytecode,
                                        OperandScale operand_scale,
                                        uint32_t operand0) {
  DCHECK(OperandIsValid(bytecode, operand_scale, 0, operand0));
  BytecodeNode node(bytecode, operand0, operand_scale);
  AttachSourceInfo(&node);
  pipeline()->Write(&node);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::BinaryOperation(Token::Value op,
                                                            Register reg) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(reg.SizeOfOperand());
  OutputScaled(BytecodeForBinaryOperation(op), operand_scale,
               RegisterOperand(reg));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CountOperation(Token::Value op) {
  Output(BytecodeForCountOperation(op));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LogicalNot() {
  Output(Bytecode::kToBooleanLogicalNot);
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::TypeOf() {
  Output(Bytecode::kTypeOf);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CompareOperation(Token::Value op,
                                                             Register reg) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(reg.SizeOfOperand());
  OutputScaled(BytecodeForCompareOperation(op), operand_scale,
               RegisterOperand(reg));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLiteral(
    v8::internal::Smi* smi) {
  int32_t raw_smi = smi->value();
  if (raw_smi == 0) {
    Output(Bytecode::kLdaZero);
  } else {
    OperandSize operand_size = Bytecodes::SizeForSignedOperand(raw_smi);
    OperandScale operand_scale = Bytecodes::OperandSizesToScale(operand_size);
    OutputScaled(Bytecode::kLdaSmi, operand_scale,
                 SignedOperand(raw_smi, operand_size));
  }
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLiteral(Handle<Object> object) {
  size_t entry = GetConstantPoolEntry(object);
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(Bytecodes::SizeForUnsignedOperand(entry));
  OutputScaled(Bytecode::kLdaConstant, operand_scale, UnsignedOperand(entry));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadUndefined() {
  Output(Bytecode::kLdaUndefined);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadNull() {
  Output(Bytecode::kLdaNull);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadTheHole() {
  Output(Bytecode::kLdaTheHole);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadTrue() {
  Output(Bytecode::kLdaTrue);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadFalse() {
  Output(Bytecode::kLdaFalse);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadAccumulatorWithRegister(
    Register reg) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(reg.SizeOfOperand());
  OutputScaled(Bytecode::kLdar, operand_scale, RegisterOperand(reg));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreAccumulatorInRegister(
    Register reg) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(reg.SizeOfOperand());
  OutputScaled(Bytecode::kStar, operand_scale, RegisterOperand(reg));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::MoveRegister(Register from,
                                                         Register to) {
  DCHECK(from != to);
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(from.SizeOfOperand(), to.SizeOfOperand());
  OutputScaled(Bytecode::kMov, operand_scale, RegisterOperand(from),
               RegisterOperand(to));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadGlobal(
    const Handle<String> name, int feedback_slot, TypeofMode typeof_mode) {
  // TODO(rmcilroy): Potentially store typeof information in an
  // operand rather than having extra bytecodes.
  Bytecode bytecode = BytecodeForLoadGlobal(typeof_mode);
  size_t name_index = GetConstantPoolEntry(name);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      Bytecodes::SizeForUnsignedOperand(name_index),
      Bytecodes::SizeForUnsignedOperand(feedback_slot));
  OutputScaled(bytecode, operand_scale, UnsignedOperand(name_index),
               UnsignedOperand(feedback_slot));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreGlobal(
    const Handle<String> name, int feedback_slot, LanguageMode language_mode) {
  Bytecode bytecode = BytecodeForStoreGlobal(language_mode);
  size_t name_index = GetConstantPoolEntry(name);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      Bytecodes::SizeForUnsignedOperand(name_index),
      Bytecodes::SizeForUnsignedOperand(feedback_slot));
  OutputScaled(bytecode, operand_scale, UnsignedOperand(name_index),
               UnsignedOperand(feedback_slot));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadContextSlot(Register context,
                                                            int slot_index) {
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      context.SizeOfOperand(), Bytecodes::SizeForUnsignedOperand(slot_index));
  OutputScaled(Bytecode::kLdaContextSlot, operand_scale,
               RegisterOperand(context), UnsignedOperand(slot_index));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreContextSlot(Register context,
                                                             int slot_index) {
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      context.SizeOfOperand(), Bytecodes::SizeForUnsignedOperand(slot_index));
  OutputScaled(Bytecode::kStaContextSlot, operand_scale,
               RegisterOperand(context), UnsignedOperand(slot_index));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLookupSlot(
    const Handle<String> name, TypeofMode typeof_mode) {
  Bytecode bytecode = (typeof_mode == INSIDE_TYPEOF)
                          ? Bytecode::kLdaLookupSlotInsideTypeof
                          : Bytecode::kLdaLookupSlot;
  size_t name_index = GetConstantPoolEntry(name);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      Bytecodes::SizeForUnsignedOperand(name_index));
  OutputScaled(bytecode, operand_scale, UnsignedOperand(name_index));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreLookupSlot(
    const Handle<String> name, LanguageMode language_mode) {
  Bytecode bytecode = BytecodeForStoreLookupSlot(language_mode);
  size_t name_index = GetConstantPoolEntry(name);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      Bytecodes::SizeForUnsignedOperand(name_index));
  OutputScaled(bytecode, operand_scale, UnsignedOperand(name_index));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadNamedProperty(
    Register object, const Handle<Name> name, int feedback_slot) {
  size_t name_index = GetConstantPoolEntry(name);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      object.SizeOfOperand(), Bytecodes::SizeForUnsignedOperand(name_index),
      Bytecodes::SizeForUnsignedOperand(feedback_slot));
  OutputScaled(Bytecode::kLdaNamedProperty, operand_scale,
               RegisterOperand(object), UnsignedOperand(name_index),
               UnsignedOperand(feedback_slot));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::LoadKeyedProperty(
    Register object, int feedback_slot) {
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      object.SizeOfOperand(), Bytecodes::SizeForUnsignedOperand(feedback_slot));
  OutputScaled(Bytecode::kLdaKeyedProperty, operand_scale,
               RegisterOperand(object), UnsignedOperand(feedback_slot));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreNamedProperty(
    Register object, const Handle<Name> name, int feedback_slot,
    LanguageMode language_mode) {
  Bytecode bytecode = BytecodeForStoreNamedProperty(language_mode);
  size_t name_index = GetConstantPoolEntry(name);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      object.SizeOfOperand(), Bytecodes::SizeForUnsignedOperand(name_index),
      Bytecodes::SizeForUnsignedOperand(feedback_slot));
  OutputScaled(bytecode, operand_scale, RegisterOperand(object),
               UnsignedOperand(name_index), UnsignedOperand(feedback_slot));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StoreKeyedProperty(
    Register object, Register key, int feedback_slot,
    LanguageMode language_mode) {
  Bytecode bytecode = BytecodeForStoreKeyedProperty(language_mode);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      object.SizeOfOperand(), key.SizeOfOperand(),
      Bytecodes::SizeForUnsignedOperand(feedback_slot));
  OutputScaled(bytecode, operand_scale, RegisterOperand(object),
               RegisterOperand(key), UnsignedOperand(feedback_slot));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateClosure(
    Handle<SharedFunctionInfo> shared_info, PretenureFlag tenured) {
  size_t entry = GetConstantPoolEntry(shared_info);
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(Bytecodes::SizeForUnsignedOperand(entry));
  OutputScaled(Bytecode::kCreateClosure, operand_scale, UnsignedOperand(entry),
               UnsignedOperand(static_cast<size_t>(tenured)));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateArguments(
    CreateArgumentsType type) {
  // TODO(rmcilroy): Consider passing the type as a bytecode operand rather
  // than having two different bytecodes once we have better support for
  // branches in the InterpreterAssembler.
  Bytecode bytecode = BytecodeForCreateArguments(type);
  Output(bytecode);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateRegExpLiteral(
    Handle<String> pattern, int literal_index, int flags) {
  size_t pattern_entry = GetConstantPoolEntry(pattern);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      Bytecodes::SizeForUnsignedOperand(pattern_entry),
      Bytecodes::SizeForUnsignedOperand(literal_index),
      Bytecodes::SizeForUnsignedOperand(flags));
  OutputScaled(Bytecode::kCreateRegExpLiteral, operand_scale,
               UnsignedOperand(pattern_entry), UnsignedOperand(literal_index),
               UnsignedOperand(flags));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateArrayLiteral(
    Handle<FixedArray> constant_elements, int literal_index, int flags) {
  size_t constant_elements_entry = GetConstantPoolEntry(constant_elements);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      Bytecodes::SizeForUnsignedOperand(constant_elements_entry),
      Bytecodes::SizeForUnsignedOperand(literal_index),
      Bytecodes::SizeForUnsignedOperand(flags));
  OutputScaled(Bytecode::kCreateArrayLiteral, operand_scale,
               UnsignedOperand(constant_elements_entry),
               UnsignedOperand(literal_index), UnsignedOperand(flags));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CreateObjectLiteral(
    Handle<FixedArray> constant_properties, int literal_index, int flags) {
  size_t constant_properties_entry = GetConstantPoolEntry(constant_properties);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      Bytecodes::SizeForUnsignedOperand(constant_properties_entry),
      Bytecodes::SizeForUnsignedOperand(literal_index),
      Bytecodes::SizeForUnsignedOperand(flags));
  OutputScaled(Bytecode::kCreateObjectLiteral, operand_scale,
               UnsignedOperand(constant_properties_entry),
               UnsignedOperand(literal_index), UnsignedOperand(flags));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::PushContext(Register context) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(context.SizeOfOperand());
  OutputScaled(Bytecode::kPushContext, operand_scale, RegisterOperand(context));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::PopContext(Register context) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(context.SizeOfOperand());
  OutputScaled(Bytecode::kPopContext, operand_scale, RegisterOperand(context));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CastAccumulatorToJSObject() {
  Output(Bytecode::kToObject);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CastAccumulatorToName() {
  Output(Bytecode::kToName);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CastAccumulatorToNumber() {
  Output(Bytecode::kToNumber);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Bind(BytecodeLabel* label) {
  pipeline_->BindLabel(label);
  LeaveBasicBlock();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Bind(const BytecodeLabel& target,
                                                 BytecodeLabel* label) {
  pipeline_->BindLabel(target, label);
  LeaveBasicBlock();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::OutputJump(Bytecode jump_bytecode,
                                                       BytecodeLabel* label) {
  BytecodeNode node(jump_bytecode, 0, OperandScale::kSingle);
  AttachSourceInfo(&node);
  pipeline_->WriteJump(&node, label);
  LeaveBasicBlock();
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Jump(BytecodeLabel* label) {
  return OutputJump(Bytecode::kJump, label);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfTrue(BytecodeLabel* label) {
  // The peephole optimizer attempts to simplify JumpIfToBooleanTrue
  // to JumpIfTrue.
  return OutputJump(Bytecode::kJumpIfToBooleanTrue, label);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfFalse(BytecodeLabel* label) {
  // The peephole optimizer attempts to simplify JumpIfToBooleanFalse
  // to JumpIfFalse.
  return OutputJump(Bytecode::kJumpIfToBooleanFalse, label);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfNull(BytecodeLabel* label) {
  return OutputJump(Bytecode::kJumpIfNull, label);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfUndefined(
    BytecodeLabel* label) {
  return OutputJump(Bytecode::kJumpIfUndefined, label);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfNotHole(
    BytecodeLabel* label) {
  return OutputJump(Bytecode::kJumpIfNotHole, label);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::StackCheck(int position) {
  if (position != RelocInfo::kNoPosition) {
    // We need to attach a non-breakable source position to a stack check,
    // so we simply add it as expression position.
    latest_source_info_ = {position, false};
  }
  Output(Bytecode::kStackCheck);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Throw() {
  Output(Bytecode::kThrow);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ReThrow() {
  Output(Bytecode::kReThrow);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Return() {
  Output(Bytecode::kReturn);
  return_seen_in_block_ = true;
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Debugger() {
  Output(Bytecode::kDebugger);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ForInPrepare(
    Register cache_info_triple) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(cache_info_triple.SizeOfOperand());
  OutputScaled(Bytecode::kForInPrepare, operand_scale,
               RegisterOperand(cache_info_triple));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ForInDone(Register index,
                                                      Register cache_length) {
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      index.SizeOfOperand(), cache_length.SizeOfOperand());
  OutputScaled(Bytecode::kForInDone, operand_scale, RegisterOperand(index),
               RegisterOperand(cache_length));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ForInNext(
    Register receiver, Register index, Register cache_type_array_pair,
    int feedback_slot) {
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      receiver.SizeOfOperand(), index.SizeOfOperand(),
      cache_type_array_pair.SizeOfOperand(),
      Bytecodes::SizeForUnsignedOperand(feedback_slot));
  OutputScaled(Bytecode::kForInNext, operand_scale, RegisterOperand(receiver),
               RegisterOperand(index), RegisterOperand(cache_type_array_pair),
               UnsignedOperand(feedback_slot));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ForInStep(Register index) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(index.SizeOfOperand());
  OutputScaled(Bytecode::kForInStep, operand_scale, RegisterOperand(index));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::SuspendGenerator(
    Register generator) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(generator.SizeOfOperand());
  OutputScaled(Bytecode::kSuspendGenerator, operand_scale,
               RegisterOperand(generator));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::ResumeGenerator(
    Register generator) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(generator.SizeOfOperand());
  OutputScaled(Bytecode::kResumeGenerator, operand_scale,
               RegisterOperand(generator));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::MarkHandler(int handler_id,
                                                        bool will_catch) {
  BytecodeLabel handler;
  Bind(&handler);
  handler_table_builder()->SetHandlerTarget(handler_id, handler.offset());
  handler_table_builder()->SetPrediction(handler_id, will_catch);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::MarkTryBegin(int handler_id,
                                                         Register context) {
  BytecodeLabel try_begin;
  Bind(&try_begin);
  handler_table_builder()->SetTryRegionStart(handler_id, try_begin.offset());
  handler_table_builder()->SetContextRegister(handler_id, context);
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::MarkTryEnd(int handler_id) {
  BytecodeLabel try_end;
  Bind(&try_end);
  handler_table_builder()->SetTryRegionEnd(handler_id, try_end.offset());
  return *this;
}

void BytecodeArrayBuilder::EnsureReturn() {
  if (!return_seen_in_block_) {
    LoadUndefined();
    SetReturnPosition();
    Return();
  }
  DCHECK(return_seen_in_block_);
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Call(Register callable,
                                                 Register receiver_args,
                                                 size_t receiver_args_count,
                                                 int feedback_slot,
                                                 TailCallMode tail_call_mode) {
  Bytecode bytecode = BytecodeForCall(tail_call_mode);
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      callable.SizeOfOperand(), receiver_args.SizeOfOperand(),
      Bytecodes::SizeForUnsignedOperand(receiver_args_count),
      Bytecodes::SizeForUnsignedOperand(feedback_slot));
  OutputScaled(bytecode, operand_scale, RegisterOperand(callable),
               RegisterOperand(receiver_args),
               UnsignedOperand(receiver_args_count),
               UnsignedOperand(feedback_slot));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::New(Register constructor,
                                                Register first_arg,
                                                size_t arg_count) {
  if (!first_arg.is_valid()) {
    DCHECK_EQ(0u, arg_count);
    first_arg = Register(0);
  }
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      constructor.SizeOfOperand(), first_arg.SizeOfOperand(),
      Bytecodes::SizeForUnsignedOperand(arg_count));
  OutputScaled(Bytecode::kNew, operand_scale, RegisterOperand(constructor),
               RegisterOperand(first_arg), UnsignedOperand(arg_count));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallRuntime(
    Runtime::FunctionId function_id, Register first_arg, size_t arg_count) {
  DCHECK_EQ(1, Runtime::FunctionForId(function_id)->result_size);
  DCHECK(Bytecodes::SizeForUnsignedOperand(function_id) <= OperandSize::kShort);
  if (!first_arg.is_valid()) {
    DCHECK_EQ(0u, arg_count);
    first_arg = Register(0);
  }
  Bytecode bytecode = IntrinsicsHelper::IsSupported(function_id)
                          ? Bytecode::kInvokeIntrinsic
                          : Bytecode::kCallRuntime;
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      first_arg.SizeOfOperand(), Bytecodes::SizeForUnsignedOperand(arg_count));
  OutputScaled(bytecode, operand_scale, static_cast<uint16_t>(function_id),
               RegisterOperand(first_arg), UnsignedOperand(arg_count));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallRuntimeForPair(
    Runtime::FunctionId function_id, Register first_arg, size_t arg_count,
    Register first_return) {
  DCHECK_EQ(2, Runtime::FunctionForId(function_id)->result_size);
  DCHECK(Bytecodes::SizeForUnsignedOperand(function_id) <= OperandSize::kShort);
  if (!first_arg.is_valid()) {
    DCHECK_EQ(0u, arg_count);
    first_arg = Register(0);
  }
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      first_arg.SizeOfOperand(), Bytecodes::SizeForUnsignedOperand(arg_count),
      first_return.SizeOfOperand());
  OutputScaled(Bytecode::kCallRuntimeForPair, operand_scale,
               static_cast<uint16_t>(function_id), RegisterOperand(first_arg),
               UnsignedOperand(arg_count), RegisterOperand(first_return));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::CallJSRuntime(
    int context_index, Register receiver_args, size_t receiver_args_count) {
  OperandScale operand_scale = Bytecodes::OperandSizesToScale(
      Bytecodes::SizeForUnsignedOperand(context_index),
      receiver_args.SizeOfOperand(),
      Bytecodes::SizeForUnsignedOperand(receiver_args_count));
  OutputScaled(Bytecode::kCallJSRuntime, operand_scale,
               UnsignedOperand(context_index), RegisterOperand(receiver_args),
               UnsignedOperand(receiver_args_count));
  return *this;
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Delete(Register object,
                                                   LanguageMode language_mode) {
  OperandScale operand_scale =
      Bytecodes::OperandSizesToScale(object.SizeOfOperand());
  OutputScaled(BytecodeForDelete(language_mode), operand_scale,
               RegisterOperand(object));
  return *this;
}

size_t BytecodeArrayBuilder::GetConstantPoolEntry(Handle<Object> object) {
  return constant_array_builder()->Insert(object);
}

void BytecodeArrayBuilder::SetReturnPosition() {
  if (return_position_ == RelocInfo::kNoPosition) return;
  latest_source_info_.Update({return_position_, true});
}

void BytecodeArrayBuilder::SetStatementPosition(Statement* stmt) {
  if (stmt->position() == RelocInfo::kNoPosition) return;
  latest_source_info_.Update({stmt->position(), true});
}

void BytecodeArrayBuilder::SetExpressionPosition(Expression* expr) {
  if (expr->position() == RelocInfo::kNoPosition) return;
  if (latest_source_info_.is_expression()) {
    // Ensure the current expression position is overwritten with the
    // latest value.
    //
    // TODO(oth): Clean-up BytecodeSourceInfo to have three states and
    // simplify the update logic, taking care to ensure position
    // information is not lost.
    latest_source_info_.set_invalid();
  }
  latest_source_info_.Update({expr->position(), false});
}

void BytecodeArrayBuilder::SetExpressionAsStatementPosition(Expression* expr) {
  if (expr->position() == RelocInfo::kNoPosition) return;
  latest_source_info_.Update({expr->position(), true});
}

bool BytecodeArrayBuilder::TemporaryRegisterIsLive(Register reg) const {
  return temporary_register_allocator()->RegisterIsLive(reg);
}

bool BytecodeArrayBuilder::OperandIsValid(Bytecode bytecode,
                                          OperandScale operand_scale,
                                          int operand_index,
                                          uint32_t operand_value) const {
  OperandSize operand_size =
      Bytecodes::GetOperandSize(bytecode, operand_index, operand_scale);
  OperandType operand_type = Bytecodes::GetOperandType(bytecode, operand_index);
  switch (operand_type) {
    case OperandType::kNone:
      return false;
    case OperandType::kRegCount: {
      if (operand_index > 0) {
        OperandType previous_operand_type =
            Bytecodes::GetOperandType(bytecode, operand_index - 1);
        if (previous_operand_type != OperandType::kMaybeReg &&
            previous_operand_type != OperandType::kReg) {
          return false;
        }
      }
    }  // Fall-through
    case OperandType::kFlag8:
    case OperandType::kIdx:
    case OperandType::kRuntimeId:
    case OperandType::kImm: {
      size_t unsigned_value = static_cast<size_t>(operand_value);
      return Bytecodes::SizeForUnsignedOperand(unsigned_value) <= operand_size;
    }
    case OperandType::kMaybeReg:
      if (RegisterFromOperand(operand_value) == Register(0)) {
        return true;
      }
    // Fall-through to kReg case.
    case OperandType::kReg:
    case OperandType::kRegOut: {
      Register reg = RegisterFromOperand(operand_value);
      return RegisterIsValid(reg, operand_size);
    }
    case OperandType::kRegOutPair:
    case OperandType::kRegPair: {
      Register reg0 = RegisterFromOperand(operand_value);
      Register reg1 = Register(reg0.index() + 1);
      // The size of reg1 is immaterial.
      return RegisterIsValid(reg0, operand_size) &&
             RegisterIsValid(reg1, OperandSize::kQuad);
    }
    case OperandType::kRegOutTriple: {
      Register reg0 = RegisterFromOperand(operand_value);
      Register reg1 = Register(reg0.index() + 1);
      Register reg2 = Register(reg0.index() + 2);
      // The size of reg1 and reg2 is immaterial.
      return RegisterIsValid(reg0, operand_size) &&
             RegisterIsValid(reg1, OperandSize::kQuad) &&
             RegisterIsValid(reg2, OperandSize::kQuad);
    }
  }
  UNREACHABLE();
  return false;
}

bool BytecodeArrayBuilder::RegisterIsValid(Register reg,
                                           OperandSize reg_size) const {
  if (!reg.is_valid()) {
    return false;
  }

  if (reg.SizeOfOperand() > reg_size) {
    return false;
  }

  if (reg.is_current_context() || reg.is_function_closure() ||
      reg.is_new_target()) {
    return true;
  } else if (reg.is_parameter()) {
    int parameter_index = reg.ToParameterIndex(parameter_count());
    return parameter_index >= 0 && parameter_index < parameter_count();
  } else if (reg.index() < fixed_register_count()) {
    return true;
  } else {
    return TemporaryRegisterIsLive(reg);
  }
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForBinaryOperation(Token::Value op) {
  switch (op) {
    case Token::Value::ADD:
      return Bytecode::kAdd;
    case Token::Value::SUB:
      return Bytecode::kSub;
    case Token::Value::MUL:
      return Bytecode::kMul;
    case Token::Value::DIV:
      return Bytecode::kDiv;
    case Token::Value::MOD:
      return Bytecode::kMod;
    case Token::Value::BIT_OR:
      return Bytecode::kBitwiseOr;
    case Token::Value::BIT_XOR:
      return Bytecode::kBitwiseXor;
    case Token::Value::BIT_AND:
      return Bytecode::kBitwiseAnd;
    case Token::Value::SHL:
      return Bytecode::kShiftLeft;
    case Token::Value::SAR:
      return Bytecode::kShiftRight;
    case Token::Value::SHR:
      return Bytecode::kShiftRightLogical;
    default:
      UNREACHABLE();
      return Bytecode::kIllegal;
  }
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForCountOperation(Token::Value op) {
  switch (op) {
    case Token::Value::ADD:
      return Bytecode::kInc;
    case Token::Value::SUB:
      return Bytecode::kDec;
    default:
      UNREACHABLE();
      return Bytecode::kIllegal;
  }
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForCompareOperation(Token::Value op) {
  switch (op) {
    case Token::Value::EQ:
      return Bytecode::kTestEqual;
    case Token::Value::NE:
      return Bytecode::kTestNotEqual;
    case Token::Value::EQ_STRICT:
      return Bytecode::kTestEqualStrict;
    case Token::Value::LT:
      return Bytecode::kTestLessThan;
    case Token::Value::GT:
      return Bytecode::kTestGreaterThan;
    case Token::Value::LTE:
      return Bytecode::kTestLessThanOrEqual;
    case Token::Value::GTE:
      return Bytecode::kTestGreaterThanOrEqual;
    case Token::Value::INSTANCEOF:
      return Bytecode::kTestInstanceOf;
    case Token::Value::IN:
      return Bytecode::kTestIn;
    default:
      UNREACHABLE();
      return Bytecode::kIllegal;
  }
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForStoreNamedProperty(
    LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kStaNamedPropertySloppy;
    case STRICT:
      return Bytecode::kStaNamedPropertyStrict;
    default:
      UNREACHABLE();
  }
  return Bytecode::kIllegal;
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForStoreKeyedProperty(
    LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kStaKeyedPropertySloppy;
    case STRICT:
      return Bytecode::kStaKeyedPropertyStrict;
    default:
      UNREACHABLE();
  }
  return Bytecode::kIllegal;
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForLoadGlobal(TypeofMode typeof_mode) {
  return typeof_mode == INSIDE_TYPEOF ? Bytecode::kLdaGlobalInsideTypeof
                                      : Bytecode::kLdaGlobal;
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForStoreGlobal(
    LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kStaGlobalSloppy;
    case STRICT:
      return Bytecode::kStaGlobalStrict;
    default:
      UNREACHABLE();
  }
  return Bytecode::kIllegal;
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForStoreLookupSlot(
    LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kStaLookupSlotSloppy;
    case STRICT:
      return Bytecode::kStaLookupSlotStrict;
    default:
      UNREACHABLE();
  }
  return Bytecode::kIllegal;
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForCreateArguments(
    CreateArgumentsType type) {
  switch (type) {
    case CreateArgumentsType::kMappedArguments:
      return Bytecode::kCreateMappedArguments;
    case CreateArgumentsType::kUnmappedArguments:
      return Bytecode::kCreateUnmappedArguments;
    case CreateArgumentsType::kRestParameter:
      return Bytecode::kCreateRestParameter;
  }
  UNREACHABLE();
  return Bytecode::kIllegal;
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForDelete(LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kDeletePropertySloppy;
    case STRICT:
      return Bytecode::kDeletePropertyStrict;
    default:
      UNREACHABLE();
  }
  return Bytecode::kIllegal;
}

// static
Bytecode BytecodeArrayBuilder::BytecodeForCall(TailCallMode tail_call_mode) {
  switch (tail_call_mode) {
    case TailCallMode::kDisallow:
      return Bytecode::kCall;
    case TailCallMode::kAllow:
      return Bytecode::kTailCall;
    default:
      UNREACHABLE();
  }
  return Bytecode::kIllegal;
}

uint32_t BytecodeArrayBuilder::RegisterOperand(Register reg) {
  return static_cast<uint32_t>(reg.ToOperand());
}

Register BytecodeArrayBuilder::RegisterFromOperand(uint32_t operand) {
  return Register::FromOperand(static_cast<int32_t>(operand));
}

uint32_t BytecodeArrayBuilder::SignedOperand(int value, OperandSize size) {
  switch (size) {
    case OperandSize::kByte:
      return static_cast<uint8_t>(value & 0xff);
    case OperandSize::kShort:
      return static_cast<uint16_t>(value & 0xffff);
    case OperandSize::kQuad:
      return static_cast<uint32_t>(value);
    case OperandSize::kNone:
      UNREACHABLE();
  }
  return 0;
}

uint32_t BytecodeArrayBuilder::UnsignedOperand(int value) {
  DCHECK_GE(value, 0);
  return static_cast<uint32_t>(value);
}

uint32_t BytecodeArrayBuilder::UnsignedOperand(size_t value) {
  DCHECK_LE(value, kMaxUInt32);
  return static_cast<uint32_t>(value);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
