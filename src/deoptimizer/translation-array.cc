// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/translation-array.h"

#include "src/objects/fixed-array-inl.h"

namespace v8 {
namespace internal {

TranslationArrayIterator::TranslationArrayIterator(TranslationArray buffer,
                                                   int index)
    : buffer_(buffer), index_(index) {
  DCHECK(index >= 0 && index < buffer.length());
}

int32_t TranslationArrayIterator::Next() {
  // Run through the bytes until we reach one with a least significant
  // bit of zero (marks the end).
  uint32_t bits = 0;
  for (int i = 0; true; i += 7) {
    DCHECK(HasNext());
    uint8_t next = buffer_.get(index_++);
    bits |= (next >> 1) << i;
    if ((next & 1) == 0) break;
  }
  // The bits encode the sign in the least significant bit.
  bool is_negative = (bits & 1) == 1;
  int32_t result = bits >> 1;
  return is_negative ? -result : result;
}

bool TranslationArrayIterator::HasNext() const {
  return index_ < buffer_.length();
}

void TranslationArrayBuilder::Add(int32_t value) {
  // This wouldn't handle kMinInt correctly if it ever encountered it.
  DCHECK_NE(value, kMinInt);
  // Encode the sign bit in the least significant bit.
  bool is_negative = (value < 0);
  uint32_t bits = (static_cast<uint32_t>(is_negative ? -value : value) << 1) |
                  static_cast<uint32_t>(is_negative);
  // Encode the individual bytes using the least significant bit of
  // each byte to indicate whether or not more bytes follow.
  do {
    uint32_t next = bits >> 7;
    contents_.push_back(((bits << 1) & 0xFF) | (next != 0));
    bits = next;
  } while (bits != 0);
}

Handle<TranslationArray> TranslationArrayBuilder::ToTranslationArray(
    Factory* factory) {
  Handle<TranslationArray> result =
      factory->NewByteArray(Size(), AllocationType::kOld);
  contents_.CopyTo(result->GetDataStartAddress());
  return result;
}

void TranslationArrayBuilder::BeginBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::BUILTIN_CONTINUATION_FRAME;
  Add(opcode);
  Add(bytecode_offset.ToInt());
  Add(literal_id);
  Add(height);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 3);
}

void TranslationArrayBuilder::BeginJavaScriptBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::JAVA_SCRIPT_BUILTIN_CONTINUATION_FRAME;
  Add(opcode);
  Add(bytecode_offset.ToInt());
  Add(literal_id);
  Add(height);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 3);
}

void TranslationArrayBuilder::BeginJavaScriptBuiltinContinuationWithCatchFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode =
      TranslationOpcode::JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME;
  Add(opcode);
  Add(bytecode_offset.ToInt());
  Add(literal_id);
  Add(height);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 3);
}

void TranslationArrayBuilder::BeginConstructStubFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  auto opcode = TranslationOpcode::CONSTRUCT_STUB_FRAME;
  Add(opcode);
  Add(bytecode_offset.ToInt());
  Add(literal_id);
  Add(height);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 3);
}

void TranslationArrayBuilder::BeginArgumentsAdaptorFrame(int literal_id,
                                                         unsigned height) {
  auto opcode = TranslationOpcode::ARGUMENTS_ADAPTOR_FRAME;
  Add(opcode);
  Add(literal_id);
  Add(height);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 2);
}

void TranslationArrayBuilder::BeginInterpretedFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height,
    int return_value_offset, int return_value_count) {
  auto opcode = TranslationOpcode::INTERPRETED_FRAME;
  Add(opcode);
  Add(bytecode_offset.ToInt());
  Add(literal_id);
  Add(height);
  Add(return_value_offset);
  Add(return_value_count);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 5);
}

void TranslationArrayBuilder::ArgumentsElements(CreateArgumentsType type) {
  auto opcode = TranslationOpcode::ARGUMENTS_ELEMENTS;
  Add(opcode);
  Add(static_cast<uint8_t>(type));
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::ArgumentsLength() {
  auto opcode = TranslationOpcode::ARGUMENTS_LENGTH;
  Add(opcode);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 0);
}

void TranslationArrayBuilder::BeginCapturedObject(int length) {
  auto opcode = TranslationOpcode::CAPTURED_OBJECT;
  Add(opcode);
  Add(length);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::DuplicateObject(int object_index) {
  auto opcode = TranslationOpcode::DUPLICATED_OBJECT;
  Add(opcode);
  Add(object_index);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreRegister(Register reg) {
  auto opcode = TranslationOpcode::REGISTER;
  Add(opcode);
  Add(reg.code());
}

void TranslationArrayBuilder::StoreInt32Register(Register reg) {
  auto opcode = TranslationOpcode::INT32_REGISTER;
  Add(opcode);
  Add(reg.code());
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreInt64Register(Register reg) {
  auto opcode = TranslationOpcode::INT64_REGISTER;
  Add(opcode);
  Add(reg.code());
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreUint32Register(Register reg) {
  auto opcode = TranslationOpcode::UINT32_REGISTER;
  Add(opcode);
  Add(reg.code());
}

void TranslationArrayBuilder::StoreBoolRegister(Register reg) {
  auto opcode = TranslationOpcode::BOOL_REGISTER;
  Add(opcode);
  Add(reg.code());
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreFloatRegister(FloatRegister reg) {
  auto opcode = TranslationOpcode::FLOAT_REGISTER;
  Add(opcode);
  Add(reg.code());
}

void TranslationArrayBuilder::StoreDoubleRegister(DoubleRegister reg) {
  auto opcode = TranslationOpcode::DOUBLE_REGISTER;
  Add(opcode);
  Add(reg.code());
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreStackSlot(int index) {
  auto opcode = TranslationOpcode::STACK_SLOT;
  Add(opcode);
  Add(index);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreInt32StackSlot(int index) {
  auto opcode = TranslationOpcode::INT32_STACK_SLOT;
  Add(opcode);
  Add(index);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreInt64StackSlot(int index) {
  auto opcode = TranslationOpcode::INT64_STACK_SLOT;
  Add(opcode);
  Add(index);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreUint32StackSlot(int index) {
  auto opcode = TranslationOpcode::UINT32_STACK_SLOT;
  Add(opcode);
  Add(index);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreBoolStackSlot(int index) {
  auto opcode = TranslationOpcode::BOOL_STACK_SLOT;
  Add(opcode);
  Add(index);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreFloatStackSlot(int index) {
  auto opcode = TranslationOpcode::FLOAT_STACK_SLOT;
  Add(opcode);
  Add(index);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreDoubleStackSlot(int index) {
  auto opcode = TranslationOpcode::DOUBLE_STACK_SLOT;
  Add(opcode);
  Add(index);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::StoreLiteral(int literal_id) {
  auto opcode = TranslationOpcode::LITERAL;
  Add(opcode);
  Add(literal_id);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 1);
}

void TranslationArrayBuilder::AddUpdateFeedback(int vector_literal, int slot) {
  auto opcode = TranslationOpcode::UPDATE_FEEDBACK;
  Add(opcode);
  Add(vector_literal);
  Add(slot);
  DCHECK_EQ(TranslationOpcodeOperandCount(opcode), 2);
}

void TranslationArrayBuilder::StoreJSFrameFunction() {
  StoreStackSlot((StandardFrameConstants::kCallerPCOffset -
                  StandardFrameConstants::kFunctionOffset) /
                 kSystemPointerSize);
}

}  // namespace internal
}  // namespace v8
