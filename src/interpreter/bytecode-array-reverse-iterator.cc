// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/bytecode-array-reverse-iterator.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {

BytecodeArrayReverseIterator::BytecodeArrayReverseIterator(
    Handle<BytecodeArray> bytecode_array, Zone* zone)
    : BytecodeArrayAccessor(bytecode_array, 0), offsets_(zone) {
  // Run forwards through the bytecode array to determine the offset of each
  // bytecode.
  while (current_offset() < bytecode_array->length()) {
    offsets_.push_back(current_offset());
    SetOffset(current_offset() + current_bytecode_size());
  }
  Reset();
}

void BytecodeArrayReverseIterator::Advance() {
  it_offsets_++;
  UpdateOffsetFromIterator();
}

void BytecodeArrayReverseIterator::Reset() {
  it_offsets_ = offsets_.rbegin();
  UpdateOffsetFromIterator();
}

bool BytecodeArrayReverseIterator::done() const {
  return it_offsets_ == offsets_.rend();
}

void BytecodeArrayReverseIterator::UpdateOffsetFromIterator() {
  if (it_offsets_ != offsets_.rend()) {
    SetOffset(*it_offsets_);
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
