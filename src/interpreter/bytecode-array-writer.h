// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_ARRAY_WRITER_H_
#define V8_INTERPRETER_BYTECODE_ARRAY_WRITER_H_

#include "src/interpreter/bytecode-pipeline.h"
#include "src/interpreter/source-position-table.h"

namespace v8 {
namespace internal {
namespace interpreter {

class BytecodeLabel;
class SourcePositionTableBuilder;
class ConstantArrayBuilder;

// Class for emitting bytecode as the final stage of the bytecode
// generation pipeline.
class BytecodeArrayWriter final : public BytecodePipelineStage {
 public:
  BytecodeArrayWriter(Isolate* isolate, Zone* zone,
                      ConstantArrayBuilder* constant_array_builder);
  virtual ~BytecodeArrayWriter();

  // BytecodePipelineStage interface.
  void Write(BytecodeNode* node) override;
  void WriteJump(BytecodeNode* node, BytecodeLabel* label) override;
  void BindLabel(BytecodeLabel* label) override;
  void BindLabel(const BytecodeLabel& target, BytecodeLabel* label) override;
  Handle<BytecodeArray> ToBytecodeArray(
      int fixed_register_count, int parameter_count,
      Handle<FixedArray> handler_table) override;

 private:
  // Constants that act as placeholders for jump operands to be
  // patched. These have operand sizes that match the sizes of
  // reserved constant pool entries.
  const uint32_t k8BitJumpPlaceholder = 0x7f;
  const uint32_t k16BitJumpPlaceholder =
      k8BitJumpPlaceholder | (k8BitJumpPlaceholder << 8);
  const uint32_t k32BitJumpPlaceholder =
      k16BitJumpPlaceholder | (k16BitJumpPlaceholder << 16);

  void PatchJump(size_t jump_target, size_t jump_location);
  void PatchJumpWith8BitOperand(size_t jump_location, int delta);
  void PatchJumpWith16BitOperand(size_t jump_location, int delta);
  void PatchJumpWith32BitOperand(size_t jump_location, int delta);

  void EmitBytecode(const BytecodeNode* const node);
  void EmitJump(BytecodeNode* node, BytecodeLabel* label);
  void UpdateSourcePositionTable(const BytecodeNode* const node);

  Isolate* isolate() { return isolate_; }
  ZoneVector<uint8_t>* bytecodes() { return &bytecodes_; }
  SourcePositionTableBuilder* source_position_table_builder() {
    return &source_position_table_builder_;
  }
  ConstantArrayBuilder* constant_array_builder() {
    return constant_array_builder_;
  }
  int max_register_count() { return max_register_count_; }

  Isolate* isolate_;
  ZoneVector<uint8_t> bytecodes_;
  int max_register_count_;
  int unbound_jumps_;
  SourcePositionTableBuilder source_position_table_builder_;
  ConstantArrayBuilder* constant_array_builder_;

  friend class BytecodeArrayWriterUnittest;
  DISALLOW_COPY_AND_ASSIGN(BytecodeArrayWriter);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_ARRAY_WRITER_H_
