// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEOPTIMIZER_TRANSLATIONS_H_
#define V8_DEOPTIMIZER_TRANSLATIONS_H_

#include <stack>
#include <vector>

#include "src/builtins/builtins.h"
#include "src/codegen/register-arch.h"
#include "src/deoptimizer/translated-state.h"
#include "src/objects/fixed-array.h"
#include "src/wasm/value-type.h"
#include "src/zone/zone-chunk-list.h"

namespace v8 {
namespace internal {

class Factory;

class TranslationBuffer {
 public:
  explicit TranslationBuffer(Zone* zone) : contents_(zone) {}

  int CurrentIndex() const { return static_cast<int>(contents_.size()); }
  void Add(int32_t value);

  Handle<ByteArray> CreateByteArray(Factory* factory);

 private:
  ZoneChunkList<uint8_t> contents_;
};

#define TRANSLATION_OPCODE_LIST(V)                     \
  V(BEGIN)                                             \
  V(INTERPRETED_FRAME)                                 \
  V(BUILTIN_CONTINUATION_FRAME)                        \
  V(JS_TO_WASM_BUILTIN_CONTINUATION_FRAME)             \
  V(JAVA_SCRIPT_BUILTIN_CONTINUATION_FRAME)            \
  V(JAVA_SCRIPT_BUILTIN_CONTINUATION_WITH_CATCH_FRAME) \
  V(CONSTRUCT_STUB_FRAME)                              \
  V(ARGUMENTS_ADAPTOR_FRAME)                           \
  V(DUPLICATED_OBJECT)                                 \
  V(ARGUMENTS_ELEMENTS)                                \
  V(ARGUMENTS_LENGTH)                                  \
  V(CAPTURED_OBJECT)                                   \
  V(REGISTER)                                          \
  V(INT32_REGISTER)                                    \
  V(INT64_REGISTER)                                    \
  V(UINT32_REGISTER)                                   \
  V(BOOL_REGISTER)                                     \
  V(FLOAT_REGISTER)                                    \
  V(DOUBLE_REGISTER)                                   \
  V(STACK_SLOT)                                        \
  V(INT32_STACK_SLOT)                                  \
  V(INT64_STACK_SLOT)                                  \
  V(UINT32_STACK_SLOT)                                 \
  V(BOOL_STACK_SLOT)                                   \
  V(FLOAT_STACK_SLOT)                                  \
  V(DOUBLE_STACK_SLOT)                                 \
  V(LITERAL)                                           \
  V(UPDATE_FEEDBACK)

class Translation {
 public:
#define DECLARE_TRANSLATION_OPCODE_ENUM(item) item,
  enum Opcode {
    TRANSLATION_OPCODE_LIST(DECLARE_TRANSLATION_OPCODE_ENUM) LAST = LITERAL
  };
#undef DECLARE_TRANSLATION_OPCODE_ENUM

  Translation(TranslationBuffer* buffer, int frame_count, int jsframe_count,
              int update_feedback_count, Zone* zone)
      : buffer_(buffer), index_(buffer->CurrentIndex()), zone_(zone) {
    buffer_->Add(BEGIN);
    buffer_->Add(frame_count);
    buffer_->Add(jsframe_count);
    buffer_->Add(update_feedback_count);
  }

  int index() const { return index_; }

  // Commands.
  void BeginInterpretedFrame(BytecodeOffset bytecode_offset, int literal_id,
                             unsigned height, int return_value_offset,
                             int return_value_count);
  void BeginArgumentsAdaptorFrame(int literal_id, unsigned height);
  void BeginConstructStubFrame(BytecodeOffset bailout_id, int literal_id,
                               unsigned height);
  void BeginBuiltinContinuationFrame(BytecodeOffset bailout_id, int literal_id,
                                     unsigned height);
  void BeginJSToWasmBuiltinContinuationFrame(
      BytecodeOffset bailout_id, int literal_id, unsigned height,
      base::Optional<wasm::ValueType::Kind> return_type);
  void BeginJavaScriptBuiltinContinuationFrame(BytecodeOffset bailout_id,
                                               int literal_id, unsigned height);
  void BeginJavaScriptBuiltinContinuationWithCatchFrame(
      BytecodeOffset bailout_id, int literal_id, unsigned height);
  void ArgumentsElements(CreateArgumentsType type);
  void ArgumentsLength();
  void BeginCapturedObject(int length);
  void AddUpdateFeedback(int vector_literal, int slot);
  void DuplicateObject(int object_index);
  void StoreRegister(Register reg);
  void StoreInt32Register(Register reg);
  void StoreInt64Register(Register reg);
  void StoreUint32Register(Register reg);
  void StoreBoolRegister(Register reg);
  void StoreFloatRegister(FloatRegister reg);
  void StoreDoubleRegister(DoubleRegister reg);
  void StoreStackSlot(int index);
  void StoreInt32StackSlot(int index);
  void StoreInt64StackSlot(int index);
  void StoreUint32StackSlot(int index);
  void StoreBoolStackSlot(int index);
  void StoreFloatStackSlot(int index);
  void StoreDoubleStackSlot(int index);
  void StoreLiteral(int literal_id);
  void StoreJSFrameFunction();

  Zone* zone() const { return zone_; }

  static int NumberOfOperandsFor(Opcode opcode);

#if defined(OBJECT_PRINT) || defined(ENABLE_DISASSEMBLER)
  static const char* StringFor(Opcode opcode);
#endif

 private:
  TranslationBuffer* buffer_;
  int index_;
  Zone* zone_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_DEOPTIMIZER_TRANSLATIONS_H_
