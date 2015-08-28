// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-array-builder.h"

namespace v8 {
namespace internal {
namespace interpreter {

BytecodeArrayBuilder::BytecodeArrayBuilder(Isolate* isolate, Zone* zone)
    : isolate_(isolate),
      bytecodes_(zone),
      bytecode_generated_(false),
      constants_map_(isolate->heap(), zone),
      constants_(zone),
      parameter_count_(-1),
      local_register_count_(-1),
      temporary_register_count_(0),
      temporary_register_next_(0) {}


void BytecodeArrayBuilder::set_locals_count(int number_of_locals) {
  local_register_count_ = number_of_locals;
  temporary_register_next_ = local_register_count_;
}


int BytecodeArrayBuilder::locals_count() const { return local_register_count_; }


void BytecodeArrayBuilder::set_parameter_count(int number_of_parameters) {
  parameter_count_ = number_of_parameters;
}


int BytecodeArrayBuilder::parameter_count() const { return parameter_count_; }


Register BytecodeArrayBuilder::Parameter(int param_index) {
  DCHECK_GE(param_index, 0);
  DCHECK_LT(param_index, parameter_count_);
  return Register(kLastParamRegisterIndex - parameter_count_ + param_index + 1);
}


Handle<BytecodeArray> BytecodeArrayBuilder::ToBytecodeArray() {
  DCHECK_EQ(bytecode_generated_, false);
  DCHECK_GE(parameter_count_, 0);
  DCHECK_GE(local_register_count_, 0);
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


BytecodeArrayBuilder& BytecodeArrayBuilder::BinaryOperation(Token::Value binop,
                                                            Register reg) {
  Output(BytecodeForBinaryOperation(binop), reg.ToOperand());
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
  if (entry <= 255) {
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


BytecodeArrayBuilder& BytecodeArrayBuilder::Return() {
  Output(Bytecode::kReturn);
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
                                          uint8_t operand_value) const {
  OperandType operand_type = Bytecodes::GetOperandType(bytecode, operand_index);
  switch (operand_type) {
    case OperandType::kNone:
      return false;
    case OperandType::kImm8:
      return true;
    case OperandType::kIdx:
      return operand_value < constants_.size();
    case OperandType::kReg: {
      int reg_index = Register::FromOperand(operand_value).index();
      return (reg_index >= 0 && reg_index < temporary_register_next_) ||
             (reg_index <= kLastParamRegisterIndex &&
              reg_index > kLastParamRegisterIndex - parameter_count_);
    }
  }
  UNREACHABLE();
  return false;
}


void BytecodeArrayBuilder::Output(Bytecode bytecode, uint8_t operand0,
                                  uint8_t operand1, uint8_t operand2) {
  DCHECK_EQ(Bytecodes::NumberOfOperands(bytecode), 3);
  DCHECK(OperandIsValid(bytecode, 0, operand0) &&
         OperandIsValid(bytecode, 1, operand1) &&
         OperandIsValid(bytecode, 2, operand2));
  bytecodes_.push_back(Bytecodes::ToByte(bytecode));
  bytecodes_.push_back(operand0);
  bytecodes_.push_back(operand1);
  bytecodes_.push_back(operand2);
}


void BytecodeArrayBuilder::Output(Bytecode bytecode, uint8_t operand0,
                                  uint8_t operand1) {
  DCHECK_EQ(Bytecodes::NumberOfOperands(bytecode), 2);
  DCHECK(OperandIsValid(bytecode, 0, operand0) &&
         OperandIsValid(bytecode, 1, operand1));
  bytecodes_.push_back(Bytecodes::ToByte(bytecode));
  bytecodes_.push_back(operand0);
  bytecodes_.push_back(operand1);
}


void BytecodeArrayBuilder::Output(Bytecode bytecode, uint8_t operand0) {
  DCHECK_EQ(Bytecodes::NumberOfOperands(bytecode), 1);
  DCHECK(OperandIsValid(bytecode, 0, operand0));
  bytecodes_.push_back(Bytecodes::ToByte(bytecode));
  bytecodes_.push_back(operand0);
}


void BytecodeArrayBuilder::Output(Bytecode bytecode) {
  DCHECK_EQ(Bytecodes::NumberOfOperands(bytecode), 0);
  bytecodes_.push_back(Bytecodes::ToByte(bytecode));
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
    default:
      UNIMPLEMENTED();
      return static_cast<Bytecode>(-1);
  }
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
