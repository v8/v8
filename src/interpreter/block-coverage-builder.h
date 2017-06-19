// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BLOCK_COVERAGE_BUILDER_H_
#define V8_INTERPRETER_BLOCK_COVERAGE_BUILDER_H_

#include "src/ast/ast.h"
#include "src/interpreter/bytecode-array-builder.h"

#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace interpreter {

// Used to generate IncBlockCounter bytecodes and the {source range, slot}
// mapping for block coverage.
class BlockCoverageBuilder final : public ZoneObject {
 public:
  BlockCoverageBuilder(Zone* zone, BytecodeArrayBuilder* builder)
      : slots_(0, zone), builder_(builder) {}

  static const int kNoCoverageArraySlot = -1;

  int AllocateBlockCoverageSlot(SourceRange range) {
    if (range.IsEmpty()) return kNoCoverageArraySlot;
    const int slot = static_cast<int>(slots_.size());
    slots_.emplace_back(range);
    return slot;
  }

  void IncrementBlockCounter(int coverage_array_slot) {
    if (coverage_array_slot == kNoCoverageArraySlot) return;
    builder_->IncBlockCounter(coverage_array_slot);
  }

  const ZoneVector<SourceRange>& slots() const { return slots_; }

 private:
  // Contains source range information for allocated block coverage counter
  // slots. Slot i covers range slots_[i].
  ZoneVector<SourceRange> slots_;
  BytecodeArrayBuilder* builder_;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BLOCK_COVERAGE_BUILDER_H_
