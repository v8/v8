// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/source-position-table.h"

#include "src/assembler.h"
#include "src/objects-inl.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace interpreter {

class IsStatementField : public BitField<bool, 0, 1> {};
class SourcePositionField : public BitField<int, 1, 30> {};

void SourcePositionTableBuilder::AddStatementPosition(int bytecode_offset,
                                                      int source_position) {
  AssertMonotonic(bytecode_offset);
  uint32_t encoded = IsStatementField::encode(true) |
                     SourcePositionField::encode(source_position);
  entries_.push_back({bytecode_offset, encoded});
}

void SourcePositionTableBuilder::AddExpressionPosition(int bytecode_offset,
                                                       int source_position) {
  AssertMonotonic(bytecode_offset);
  uint32_t encoded = IsStatementField::encode(false) |
                     SourcePositionField::encode(source_position);
  entries_.push_back({bytecode_offset, encoded});
}

Handle<FixedArray> SourcePositionTableBuilder::ToFixedArray() {
  int length = static_cast<int>(entries_.size());
  Handle<FixedArray> table =
      isolate_->factory()->NewFixedArray(length * 2, TENURED);
  for (int i = 0; i < length; i++) {
    table->set(i * 2, Smi::FromInt(entries_[i].bytecode_offset));
    table->set(i * 2 + 1, Smi::FromInt(entries_[i].source_position_and_type));
  }
  return table;
}

SourcePositionTableIterator::SourcePositionTableIterator(
    BytecodeArray* bytecode_array)
    : table_(bytecode_array->source_position_table()),
      index_(0),
      length_(table_->length()) {
  DCHECK(table_->length() % 2 == 0);
  Advance();
}

void SourcePositionTableIterator::Advance() {
  if (index_ < length_) {
    int new_bytecode_offset = Smi::cast(table_->get(index_))->value();
    // Bytecode offsets are in ascending order.
    DCHECK(bytecode_offset_ < new_bytecode_offset || index_ == 0);
    bytecode_offset_ = new_bytecode_offset;
    uint32_t source_position_and_type =
        static_cast<uint32_t>(Smi::cast(table_->get(index_ + 1))->value());
    is_statement_ = IsStatementField::decode(source_position_and_type);
    source_position_ = SourcePositionField::decode(source_position_and_type);
  }
  index_ += 2;
}

int SourcePositionTableIterator::PositionFromBytecodeOffset(
    BytecodeArray* bytecode_array, int bytecode_offset) {
  int last_position = 0;
  for (SourcePositionTableIterator iterator(bytecode_array);
       !iterator.done() && iterator.bytecode_offset() <= bytecode_offset;
       iterator.Advance()) {
    last_position = iterator.source_position();
  }
  return last_position;
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
