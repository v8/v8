// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-array-builder.h"

namespace v8 {
namespace internal {
namespace interpreter {

BytecodeArrayBuilder::BytecodeArrayBuilder(Isolate* isolate, Zone* zone)
    : isolate_(isolate),
      zone_(zone),
      bytecodes_(zone),
      bytecode_generated_(false),
      last_block_end_(0),
      last_bytecode_start_(~0),
      return_seen_in_block_(false),
      constants_map_(isolate->heap(), zone),
      constants_(zone),
      parameter_count_(-1),
      local_register_count_(-1),
      context_register_count_(-1),
      temporary_register_count_(0),
      temporary_register_next_(0) {}


void BytecodeArrayBuilder::set_locals_count(int number_of_locals) {
  local_register_count_ = number_of_locals;
  DCHECK_LE(context_register_count_, 0);
  temporary_register_next_ = local_register_count_;
}


int BytecodeArrayBuilder::locals_count() const { return local_register_count_; }


void BytecodeArrayBuilder::set_parameter_count(int number_of_parameters) {
  parameter_count_ = number_of_parameters;
}


int BytecodeArrayBuilder::parameter_count() const { return parameter_count_; }


void BytecodeArrayBuilder::set_context_count(int number_of_contexts) {
  context_register_count_ = number_of_contexts;
  DCHECK_GE(local_register_count_, 0);
  temporary_register_next_ = local_register_count_ + context_register_count_;
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
  DCHECK_LT(parameter_index, parameter_count_);
  return Register::FromParameterIndex(parameter_index, parameter_count_);
}


Handle<BytecodeArray> BytecodeArrayBuilder::ToBytecodeArray() {
  DCHECK_EQ(bytecode_generated_, false);
  DCHECK_GE(parameter_count_, 0);
  DCHECK_GE(local_register_count_, 0);

  EnsureReturn();

  int bytecode_size = static_cast<int>(bytecodes_.size());
  int register_count = local_register_count_ + temporary_register_count_;
  int frame_size = register_count * kPointerSize;

  Factory* factory = isolate_->factory();
  int constants_count = static_cast<int>(constants_.size());
  Handle<FixedArray> constant_pool =
      factory->NewFixedArray(constants_count, TENURED);
  for (int i = 0; i < constants_count; i++) {
    constant_pool->set(i, *constants_[i]);
  }

  Handle<BytecodeArray> output =
      factory->NewBytecodeArray(bytecode_size, &bytecodes_.front(), frame_size,
                                parameter_count_, constant_pool);
  bytecode_generated_ = true;
  return output;
}


template <size_t N>
void BytecodeArrayBuilder::Output(Bytecode bytecode, uint32_t(&operands)[N]) {
  DCHECK_EQ(Bytecodes::NumberOfOperands(bytecode), static_cast<int>(N));
  last_bytecode_start_ = bytecodes()->size();
  bytecodes()->push_back(Bytecodes::ToByte(bytecode));
  for (int i = 0; i < static_cast<int>(N); i++) {
    DCHECK(OperandIsValid(bytecode, i, operands[i]));
    switch (Bytecodes::GetOperandSize(bytecode, i)) {
      case OperandSize::kNone:
        UNREACHABLE();
      case OperandSize::kByte:
        bytecodes()->push_back(static_cast<uint8_t>(operands[i]));
        break;
      case OperandSize::kShort: {
        uint8_t operand_bytes[2];
        Bytecodes::ShortOperandToBytes(operands[i], operand_bytes);
        bytecodes()->insert(bytecodes()->end(), operand_bytes,
                            operand_bytes + 2);
        break;
      }
    }
  }
}


void BytecodeArrayBuilder::Output(Bytecode bytecode, uint32_t operand0,
                                  uint32_t operand1, uint32_t operand2) {
  uint32_t operands[] = {operand0, operand1, operand2};
  Output(bytecode, operands);
}


void BytecodeArrayBuilder::Output(Bytecode bytecode, uint32_t operand0,
                                  uint32_t operand1) {
  uint32_t operands[] = {operand0, operand1};
  Output(bytecode, operands);
}


void BytecodeArrayBuilder::Output(Bytecode bytecode, uint32_t operand0) {
  uint32_t operands[] = {operand0};
  Output(bytecode, operands);
}


void BytecodeArrayBuilder::Output(Bytecode bytecode) {
  DCHECK_EQ(Bytecodes::NumberOfOperands(bytecode), 0);
  last_bytecode_start_ = bytecodes()->size();
  bytecodes()->push_back(Bytecodes::ToByte(bytecode));
}


BytecodeArrayBuilder& BytecodeArrayBuilder::BinaryOperation(Token::Value op,
                                                            Register reg,
                                                            Strength strength) {
  if (is_strong(strength)) {
    UNIMPLEMENTED();
  }

  Output(BytecodeForBinaryOperation(op), reg.ToOperand());
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::LogicalNot() {
  Output(Bytecode::kLogicalNot);
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::TypeOf() {
  Output(Bytecode::kTypeOf);
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::CompareOperation(
    Token::Value op, Register reg, Strength strength) {
  if (is_strong(strength)) {
    UNIMPLEMENTED();
  }

  Output(BytecodeForCompareOperation(op), reg.ToOperand());
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLiteral(
    v8::internal::Smi* smi) {
  int32_t raw_smi = smi->value();
  if (raw_smi == 0) {
    Output(Bytecode::kLdaZero);
  } else if (raw_smi >= -128 && raw_smi <= 127) {
    Output(Bytecode::kLdaSmi8, static_cast<uint8_t>(raw_smi));
  } else {
    LoadLiteral(Handle<Object>(smi, isolate_));
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::LoadLiteral(Handle<Object> object) {
  size_t entry = GetConstantPoolEntry(object);
  if (FitsInIdx8Operand(entry)) {
    Output(Bytecode::kLdaConstant, static_cast<uint8_t>(entry));
  } else {
    UNIMPLEMENTED();
  }
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
  Output(Bytecode::kLdar, reg.ToOperand());
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::StoreAccumulatorInRegister(
    Register reg) {
  Output(Bytecode::kStar, reg.ToOperand());
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::LoadGlobal(int slot_index) {
  DCHECK(slot_index >= 0);
  if (FitsInIdx8Operand(slot_index)) {
    Output(Bytecode::kLdaGlobal, static_cast<uint8_t>(slot_index));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::StoreGlobal(
    int slot_index, LanguageMode language_mode) {
  if (!is_sloppy(language_mode)) {
    UNIMPLEMENTED();
  }
  DCHECK(slot_index >= 0);
  if (FitsInIdx8Operand(slot_index)) {
    Output(Bytecode::kStaGlobal, static_cast<uint8_t>(slot_index));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::LoadContextSlot(Register context,
                                                            int slot_index) {
  DCHECK(slot_index >= 0);
  if (FitsInIdx8Operand(slot_index)) {
    Output(Bytecode::kLdaContextSlot, context.ToOperand(),
           static_cast<uint8_t>(slot_index));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::LoadNamedProperty(
    Register object, int feedback_slot, LanguageMode language_mode) {
  Bytecode bytecode = BytecodeForLoadIC(language_mode);
  if (FitsInIdx8Operand(feedback_slot)) {
    Output(bytecode, object.ToOperand(), static_cast<uint8_t>(feedback_slot));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::LoadKeyedProperty(
    Register object, int feedback_slot, LanguageMode language_mode) {
  Bytecode bytecode = BytecodeForKeyedLoadIC(language_mode);
  if (FitsInIdx8Operand(feedback_slot)) {
    Output(bytecode, object.ToOperand(), static_cast<uint8_t>(feedback_slot));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::StoreNamedProperty(
    Register object, Register name, int feedback_slot,
    LanguageMode language_mode) {
  Bytecode bytecode = BytecodeForStoreIC(language_mode);
  if (FitsInIdx8Operand(feedback_slot)) {
    Output(bytecode, object.ToOperand(), name.ToOperand(),
           static_cast<uint8_t>(feedback_slot));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::StoreKeyedProperty(
    Register object, Register key, int feedback_slot,
    LanguageMode language_mode) {
  Bytecode bytecode = BytecodeForKeyedStoreIC(language_mode);
  if (FitsInIdx8Operand(feedback_slot)) {
    Output(bytecode, object.ToOperand(), key.ToOperand(),
           static_cast<uint8_t>(feedback_slot));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::GenericStoreKeyedProperty(
    Register object, Register key) {
  Output(Bytecode::kKeyedStoreICGeneric, object.ToOperand(), key.ToOperand());
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::CreateClosure(
    PretenureFlag tenured) {
  DCHECK(FitsInImm8Operand(tenured));
  Output(Bytecode::kCreateClosure, static_cast<uint8_t>(tenured));
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::CreateArrayLiteral(
    int literal_index, int flags) {
  DCHECK(FitsInImm8Operand(flags));  // Flags should fit in 8 bytes.
  if (FitsInIdx8Operand(literal_index)) {
    Output(Bytecode::kCreateArrayLiteral, static_cast<uint8_t>(literal_index),
           static_cast<uint8_t>(flags));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::CreateObjectLiteral(
    int literal_index, int flags) {
  DCHECK(FitsInImm8Operand(flags));  // Flags should fit in 8 bytes.
  if (FitsInIdx8Operand(literal_index)) {
    Output(Bytecode::kCreateObjectLiteral, static_cast<uint8_t>(literal_index),
           static_cast<uint8_t>(flags));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::PushContext(Register context) {
  Output(Bytecode::kPushContext, context.ToOperand());
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::PopContext(Register context) {
  Output(Bytecode::kPopContext, context.ToOperand());
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::CastAccumulatorToBoolean() {
  if (LastBytecodeInSameBlock()) {
    // If the previous bytecode puts a boolean in the accumulator
    // there is no need to emit an instruction.
    switch (Bytecodes::FromByte(bytecodes()->at(last_bytecode_start_))) {
      case Bytecode::kToBoolean:
        UNREACHABLE();
      case Bytecode::kLdaTrue:
      case Bytecode::kLdaFalse:
      case Bytecode::kLogicalNot:
      case Bytecode::kTestEqual:
      case Bytecode::kTestNotEqual:
      case Bytecode::kTestEqualStrict:
      case Bytecode::kTestNotEqualStrict:
      case Bytecode::kTestLessThan:
      case Bytecode::kTestLessThanOrEqual:
      case Bytecode::kTestGreaterThan:
      case Bytecode::kTestGreaterThanOrEqual:
      case Bytecode::kTestInstanceOf:
      case Bytecode::kTestIn:
        return *this;
      default:
        // Fall through to output kToBoolean.
        break;
    }
  }
  Output(Bytecode::kToBoolean);
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::CastAccumulatorToName() {
  Output(Bytecode::kToName);
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::Bind(BytecodeLabel* label) {
  if (label->is_forward_target()) {
    // An earlier jump instruction refers to this label. Update it's location.
    PatchJump(bytecodes()->end(), bytecodes()->begin() + label->offset());
    // Now treat as if the label will only be back referred to.
  }
  label->bind_to(bytecodes()->size());
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::Bind(const BytecodeLabel& target,
                                                 BytecodeLabel* label) {
  DCHECK_EQ(label->is_bound(), false);
  DCHECK_EQ(target.is_bound(), true);
  PatchJump(bytecodes()->begin() + target.offset(),
            bytecodes()->begin() + label->offset());
  label->bind_to(target.offset());
  return *this;
}


// static
Bytecode BytecodeArrayBuilder::GetJumpWithConstantOperand(
    Bytecode jump_bytecode) {
  switch (jump_bytecode) {
    case Bytecode::kJump:
      return Bytecode::kJumpConstant;
    case Bytecode::kJumpIfTrue:
      return Bytecode::kJumpIfTrueConstant;
    case Bytecode::kJumpIfFalse:
      return Bytecode::kJumpIfFalseConstant;
    default:
      UNREACHABLE();
      return Bytecode::kJumpConstant;
  }
}


void BytecodeArrayBuilder::PatchJump(
    const ZoneVector<uint8_t>::iterator& jump_target,
    ZoneVector<uint8_t>::iterator jump_location) {
  Bytecode jump_bytecode = Bytecodes::FromByte(*jump_location);
  int delta = static_cast<int>(jump_target - jump_location);

  DCHECK(Bytecodes::IsJump(jump_bytecode));
  DCHECK_EQ(Bytecodes::Size(jump_bytecode), 2);
  DCHECK_NE(delta, 0);

  if (FitsInImm8Operand(delta)) {
    // Just update the operand
    jump_location++;
    *jump_location = static_cast<uint8_t>(delta);
  } else {
    // Update the jump type and operand
    size_t entry = GetConstantPoolEntry(handle(Smi::FromInt(delta), isolate()));
    if (FitsInIdx8Operand(entry)) {
      jump_bytecode = GetJumpWithConstantOperand(jump_bytecode);
      *jump_location++ = Bytecodes::ToByte(jump_bytecode);
      *jump_location = static_cast<uint8_t>(entry);
    } else {
      // TODO(oth): OutputJump should reserve a constant pool entry
      // when jump is written. The reservation should be used here if
      // needed, or cancelled if not. This is due to the patch needing
      // to match the size of the code it's replacing. In future,
      // there will probably be a jump with 32-bit operand for cases
      // when constant pool is full, but that needs to be emitted in
      // OutputJump too.
      UNIMPLEMENTED();
    }
  }
}


BytecodeArrayBuilder& BytecodeArrayBuilder::OutputJump(Bytecode jump_bytecode,
                                                       BytecodeLabel* label) {
  int delta;
  if (label->is_bound()) {
    // Label has been bound already so this is a backwards jump.
    CHECK_GE(bytecodes()->size(), label->offset());
    CHECK_LE(bytecodes()->size(), static_cast<size_t>(kMaxInt));
    size_t abs_delta = bytecodes()->size() - label->offset();
    delta = -static_cast<int>(abs_delta);
  } else {
    // Label has not yet been bound so this is a forward reference
    // that will be patched when the label is bound.
    label->set_referrer(bytecodes()->size());
    delta = 0;
  }

  if (FitsInImm8Operand(delta)) {
    Output(jump_bytecode, static_cast<uint8_t>(delta));
  } else {
    size_t entry = GetConstantPoolEntry(handle(Smi::FromInt(delta), isolate()));
    if (FitsInIdx8Operand(entry)) {
      Output(GetJumpWithConstantOperand(jump_bytecode),
             static_cast<uint8_t>(entry));
    } else {
      UNIMPLEMENTED();
    }
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::Jump(BytecodeLabel* label) {
  return OutputJump(Bytecode::kJump, label);
}


BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfTrue(BytecodeLabel* label) {
  return OutputJump(Bytecode::kJumpIfTrue, label);
}


BytecodeArrayBuilder& BytecodeArrayBuilder::JumpIfFalse(BytecodeLabel* label) {
  return OutputJump(Bytecode::kJumpIfFalse, label);
}


BytecodeArrayBuilder& BytecodeArrayBuilder::Return() {
  Output(Bytecode::kReturn);
  return_seen_in_block_ = true;
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::EnterBlock() { return *this; }


BytecodeArrayBuilder& BytecodeArrayBuilder::LeaveBlock() {
  last_block_end_ = bytecodes()->size();
  return_seen_in_block_ = false;
  return *this;
}


void BytecodeArrayBuilder::EnsureReturn() {
  if (!return_seen_in_block_) {
    LoadUndefined();
    Return();
  }
}

BytecodeArrayBuilder& BytecodeArrayBuilder::Call(Register callable,
                                                 Register receiver,
                                                 size_t arg_count) {
  if (FitsInIdx8Operand(arg_count)) {
    Output(Bytecode::kCall, callable.ToOperand(), receiver.ToOperand(),
           static_cast<uint8_t>(arg_count));
  } else {
    UNIMPLEMENTED();
  }
  return *this;
}


BytecodeArrayBuilder& BytecodeArrayBuilder::CallRuntime(
    Runtime::FunctionId function_id, Register first_arg, size_t arg_count) {
  DCHECK(FitsInIdx16Operand(function_id));
  DCHECK(FitsInIdx8Operand(arg_count));
  Output(Bytecode::kCallRuntime, static_cast<uint16_t>(function_id),
         first_arg.ToOperand(), static_cast<uint8_t>(arg_count));
  return *this;
}


size_t BytecodeArrayBuilder::GetConstantPoolEntry(Handle<Object> object) {
  // These constants shouldn't be added to the constant pool, the should use
  // specialzed bytecodes instead.
  DCHECK(!object.is_identical_to(isolate_->factory()->undefined_value()));
  DCHECK(!object.is_identical_to(isolate_->factory()->null_value()));
  DCHECK(!object.is_identical_to(isolate_->factory()->the_hole_value()));
  DCHECK(!object.is_identical_to(isolate_->factory()->true_value()));
  DCHECK(!object.is_identical_to(isolate_->factory()->false_value()));

  size_t* entry = constants_map_.Find(object);
  if (!entry) {
    entry = constants_map_.Get(object);
    *entry = constants_.size();
    constants_.push_back(object);
  }
  DCHECK(constants_[*entry].is_identical_to(object));
  return *entry;
}


int BytecodeArrayBuilder::BorrowTemporaryRegister() {
  DCHECK_GE(local_register_count_, 0);
  int temporary_reg_index = temporary_register_next_++;
  int count = temporary_register_next_ - local_register_count_;
  if (count > temporary_register_count_) {
    temporary_register_count_ = count;
  }
  return temporary_reg_index;
}


void BytecodeArrayBuilder::ReturnTemporaryRegister(int reg_index) {
  DCHECK_EQ(reg_index, temporary_register_next_ - 1);
  temporary_register_next_ = reg_index;
}


bool BytecodeArrayBuilder::OperandIsValid(Bytecode bytecode, int operand_index,
                                          uint32_t operand_value) const {
  OperandType operand_type = Bytecodes::GetOperandType(bytecode, operand_index);
  switch (operand_type) {
    case OperandType::kNone:
      return false;
    case OperandType::kIdx16:
      return static_cast<uint16_t>(operand_value) == operand_value;
    case OperandType::kCount8:
    case OperandType::kImm8:
    case OperandType::kIdx8:
      return static_cast<uint8_t>(operand_value) == operand_value;
    case OperandType::kReg8: {
      Register reg = Register::FromOperand(static_cast<uint8_t>(operand_value));
      if (reg.is_function_context() || reg.is_function_closure()) {
        return true;
      } else if (reg.is_parameter()) {
        int parameter_index = reg.ToParameterIndex(parameter_count_);
        return parameter_index >= 0 && parameter_index < parameter_count_;
      } else {
        return (reg.index() >= 0 && reg.index() < temporary_register_next_);
      }
    }
  }
  UNREACHABLE();
  return false;
}

bool BytecodeArrayBuilder::LastBytecodeInSameBlock() const {
  return last_bytecode_start_ < bytecodes()->size() &&
         last_bytecode_start_ >= last_block_end_;
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
      return static_cast<Bytecode>(-1);
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
    case Token::Value::NE_STRICT:
      return Bytecode::kTestNotEqualStrict;
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
      return static_cast<Bytecode>(-1);
  }
}


// static
Bytecode BytecodeArrayBuilder::BytecodeForLoadIC(LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kLoadICSloppy;
    case STRICT:
      return Bytecode::kLoadICStrict;
    case STRONG:
      UNIMPLEMENTED();
    default:
      UNREACHABLE();
  }
  return static_cast<Bytecode>(-1);
}


// static
Bytecode BytecodeArrayBuilder::BytecodeForKeyedLoadIC(
    LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kKeyedLoadICSloppy;
    case STRICT:
      return Bytecode::kKeyedLoadICStrict;
    case STRONG:
      UNIMPLEMENTED();
    default:
      UNREACHABLE();
  }
  return static_cast<Bytecode>(-1);
}


// static
Bytecode BytecodeArrayBuilder::BytecodeForStoreIC(LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kStoreICSloppy;
    case STRICT:
      return Bytecode::kStoreICStrict;
    case STRONG:
      UNIMPLEMENTED();
    default:
      UNREACHABLE();
  }
  return static_cast<Bytecode>(-1);
}


// static
Bytecode BytecodeArrayBuilder::BytecodeForKeyedStoreIC(
    LanguageMode language_mode) {
  switch (language_mode) {
    case SLOPPY:
      return Bytecode::kKeyedStoreICSloppy;
    case STRICT:
      return Bytecode::kKeyedStoreICStrict;
    case STRONG:
      UNIMPLEMENTED();
    default:
      UNREACHABLE();
  }
  return static_cast<Bytecode>(-1);
}


// static
bool BytecodeArrayBuilder::FitsInIdx8Operand(int value) {
  return kMinUInt8 <= value && value <= kMaxUInt8;
}


// static
bool BytecodeArrayBuilder::FitsInIdx8Operand(size_t value) {
  return value <= static_cast<size_t>(kMaxUInt8);
}


// static
bool BytecodeArrayBuilder::FitsInImm8Operand(int value) {
  return kMinInt8 <= value && value < kMaxInt8;
}


// static
bool BytecodeArrayBuilder::FitsInIdx16Operand(int value) {
  return kMinUInt16 <= value && value <= kMaxUInt16;
}


TemporaryRegisterScope::TemporaryRegisterScope(BytecodeArrayBuilder* builder)
    : builder_(builder), count_(0), last_register_index_(-1) {}


TemporaryRegisterScope::~TemporaryRegisterScope() {
  while (count_-- != 0) {
    builder_->ReturnTemporaryRegister(last_register_index_--);
  }
}


Register TemporaryRegisterScope::NewRegister() {
  count_++;
  last_register_index_ = builder_->BorrowTemporaryRegister();
  return Register(last_register_index_);
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
