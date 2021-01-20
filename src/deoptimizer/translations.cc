// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/deoptimizer/translations.h"

#include "src/execution/frames.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/oddball.h"

namespace v8 {
namespace internal {
namespace {

// Encodes the return type of a Wasm function as the integer value of
// wasm::ValueType::Kind, or -1 if the function returns void.

int EncodeWasmReturnType(base::Optional<wasm::ValueType::Kind> return_type) {
  return return_type ? static_cast<int>(return_type.value()) : -1;
}

}  // namespace

void TranslationBuffer::Add(int32_t value) {
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

Handle<ByteArray> TranslationBuffer::CreateByteArray(Factory* factory) {
  Handle<ByteArray> result =
      factory->NewByteArray(CurrentIndex(), AllocationType::kOld);
  contents_.CopyTo(result->GetDataStartAddress());
  return result;
}

void Translation::BeginBuiltinContinuationFrame(BytecodeOffset bytecode_offset,
                                                int literal_id,
                                                unsigned height) {
  buffer_->Add(BUILTIN_CONTINUATION_FRAME);
  buffer_->Add(bytecode_offset.ToInt());
  buffer_->Add(literal_id);
  buffer_->Add(height);
}

void Translation::BeginJSToWasmBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height,
    base::Optional<wasm::ValueType::Kind> return_type) {
  buffer_->Add(JS_TO_WASM_BUILTIN_CONTINUATION_FRAME);
  buffer_->Add(bytecode_offset.ToInt());
  buffer_->Add(literal_id);
  buffer_->Add(height);
  buffer_->Add(EncodeWasmReturnType(return_type));
}

void Translation::BeginJavaScriptBuiltinContinuationFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  buffer_->Add(JAVA_SCRIPT_BUILTIN_CONTINUATION_FRAME);
  buffer_->Add(bytecode_offset.ToInt());
  buffer_->Add(literal_id);
  buffer_->Add(height);
}

void Translation::BeginJavaScriptBuiltinContinuationWithCatchFrame(
    BytecodeOffset bytecode_offset, int literal_id, unsigned height) {
  buffer_->Add(JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME);
  buffer_->Add(bytecode_offset.ToInt());
  buffer_->Add(literal_id);
  buffer_->Add(height);
}

void Translation::BeginConstructStubFrame(BytecodeOffset bytecode_offset,
                                          int literal_id, unsigned height) {
  buffer_->Add(CONSTRUCT_STUB_FRAME);
  buffer_->Add(bytecode_offset.ToInt());
  buffer_->Add(literal_id);
  buffer_->Add(height);
}

void Translation::BeginArgumentsAdaptorFrame(int literal_id, unsigned height) {
  buffer_->Add(ARGUMENTS_ADAPTOR_FRAME);
  buffer_->Add(literal_id);
  buffer_->Add(height);
}

void Translation::BeginInterpretedFrame(BytecodeOffset bytecode_offset,
                                        int literal_id, unsigned height,
                                        int return_value_offset,
                                        int return_value_count) {
  buffer_->Add(INTERPRETED_FRAME);
  buffer_->Add(bytecode_offset.ToInt());
  buffer_->Add(literal_id);
  buffer_->Add(height);
  buffer_->Add(return_value_offset);
  buffer_->Add(return_value_count);
}

void Translation::ArgumentsElements(CreateArgumentsType type) {
  buffer_->Add(ARGUMENTS_ELEMENTS);
  buffer_->Add(static_cast<uint8_t>(type));
}

void Translation::ArgumentsLength() { buffer_->Add(ARGUMENTS_LENGTH); }

void Translation::BeginCapturedObject(int length) {
  buffer_->Add(CAPTURED_OBJECT);
  buffer_->Add(length);
}

void Translation::DuplicateObject(int object_index) {
  buffer_->Add(DUPLICATED_OBJECT);
  buffer_->Add(object_index);
}

void Translation::StoreRegister(Register reg) {
  buffer_->Add(REGISTER);
  buffer_->Add(reg.code());
}

void Translation::StoreInt32Register(Register reg) {
  buffer_->Add(INT32_REGISTER);
  buffer_->Add(reg.code());
}

void Translation::StoreInt64Register(Register reg) {
  buffer_->Add(INT64_REGISTER);
  buffer_->Add(reg.code());
}

void Translation::StoreUint32Register(Register reg) {
  buffer_->Add(UINT32_REGISTER);
  buffer_->Add(reg.code());
}

void Translation::StoreBoolRegister(Register reg) {
  buffer_->Add(BOOL_REGISTER);
  buffer_->Add(reg.code());
}

void Translation::StoreFloatRegister(FloatRegister reg) {
  buffer_->Add(FLOAT_REGISTER);
  buffer_->Add(reg.code());
}

void Translation::StoreDoubleRegister(DoubleRegister reg) {
  buffer_->Add(DOUBLE_REGISTER);
  buffer_->Add(reg.code());
}

void Translation::StoreStackSlot(int index) {
  buffer_->Add(STACK_SLOT);
  buffer_->Add(index);
}

void Translation::StoreInt32StackSlot(int index) {
  buffer_->Add(INT32_STACK_SLOT);
  buffer_->Add(index);
}

void Translation::StoreInt64StackSlot(int index) {
  buffer_->Add(INT64_STACK_SLOT);
  buffer_->Add(index);
}

void Translation::StoreUint32StackSlot(int index) {
  buffer_->Add(UINT32_STACK_SLOT);
  buffer_->Add(index);
}

void Translation::StoreBoolStackSlot(int index) {
  buffer_->Add(BOOL_STACK_SLOT);
  buffer_->Add(index);
}

void Translation::StoreFloatStackSlot(int index) {
  buffer_->Add(FLOAT_STACK_SLOT);
  buffer_->Add(index);
}

void Translation::StoreDoubleStackSlot(int index) {
  buffer_->Add(DOUBLE_STACK_SLOT);
  buffer_->Add(index);
}

void Translation::StoreLiteral(int literal_id) {
  buffer_->Add(LITERAL);
  buffer_->Add(literal_id);
}

void Translation::AddUpdateFeedback(int vector_literal, int slot) {
  buffer_->Add(UPDATE_FEEDBACK);
  buffer_->Add(vector_literal);
  buffer_->Add(slot);
}

void Translation::StoreJSFrameFunction() {
  StoreStackSlot((StandardFrameConstants::kCallerPCOffset -
                  StandardFrameConstants::kFunctionOffset) /
                 kSystemPointerSize);
}

int Translation::NumberOfOperandsFor(Opcode opcode) {
  switch (opcode) {
    case ARGUMENTS_LENGTH:
      return 0;
    case DUPLICATED_OBJECT:
    case ARGUMENTS_ELEMENTS:
    case CAPTURED_OBJECT:
    case REGISTER:
    case INT32_REGISTER:
    case INT64_REGISTER:
    case UINT32_REGISTER:
    case BOOL_REGISTER:
    case FLOAT_REGISTER:
    case DOUBLE_REGISTER:
    case STACK_SLOT:
    case INT32_STACK_SLOT:
    case INT64_STACK_SLOT:
    case UINT32_STACK_SLOT:
    case BOOL_STACK_SLOT:
    case FLOAT_STACK_SLOT:
    case DOUBLE_STACK_SLOT:
    case LITERAL:
      return 1;
    case ARGUMENTS_ADAPTOR_FRAME:
    case UPDATE_FEEDBACK:
      return 2;
    case BEGIN:
    case CONSTRUCT_STUB_FRAME:
    case BUILTIN_CONTINUATION_FRAME:
    case JS_TO_WASM_BUILTIN_CONTINUATION_FRAME:
    case JAVA_SCRIPT_BUILTIN_CONTINUATION_FRAME:
    case JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME:
      return 3;
    case INTERPRETED_FRAME:
      return 5;
  }
  FATAL("Unexpected translation type");
  return -1;
}

#if defined(OBJECT_PRINT) || defined(ENABLE_DISASSEMBLER)

const char* Translation::StringFor(Opcode opcode) {
#define TRANSLATION_OPCODE_CASE(item) \
  case item:                          \
    return #item;
  switch (opcode) { TRANSLATION_OPCODE_LIST(TRANSLATION_OPCODE_CASE) }
#undef TRANSLATION_OPCODE_CASE
  UNREACHABLE();
}

#endif

}  // namespace internal
}  // namespace v8
