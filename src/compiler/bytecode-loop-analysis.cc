// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/bytecode-loop-analysis.h"

#include "src/compiler/bytecode-branch-analysis.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

BytecodeLoopAnalysis::BytecodeLoopAnalysis(
    Handle<BytecodeArray> bytecode_array,
    const BytecodeBranchAnalysis* branch_analysis, Zone* zone)
    : bytecode_array_(bytecode_array),
      branch_analysis_(branch_analysis),
      zone_(zone),
      backedge_to_header_(zone),
      loop_header_to_parent_(zone) {}

void BytecodeLoopAnalysis::Analyze() {
  current_loop_offset_ = -1;
  interpreter::BytecodeArrayIterator iterator(bytecode_array());
  while (!iterator.done()) {
    interpreter::Bytecode bytecode = iterator.current_bytecode();
    int current_offset = iterator.current_offset();
    if (branch_analysis_->backward_branches_target(current_offset)) {
      AddLoopEntry(current_offset);
    } else if (interpreter::Bytecodes::IsJump(bytecode)) {
      AddBranch(current_offset, iterator.GetJumpTargetOffset());
    }
    iterator.Advance();
  }
}

void BytecodeLoopAnalysis::AddLoopEntry(int entry_offset) {
  loop_header_to_parent_[entry_offset] = current_loop_offset_;
  current_loop_offset_ = entry_offset;
}

void BytecodeLoopAnalysis::AddBranch(int origin_offset, int target_offset) {
  // If this is a backedge, record it and update the current loop to the parent.
  if (target_offset < origin_offset) {
    backedge_to_header_[origin_offset] = target_offset;
    // Check that we are finishing the current loop. This assumes that
    // there is one backedge for each loop.
    DCHECK_EQ(target_offset, current_loop_offset_);
    current_loop_offset_ = loop_header_to_parent_[target_offset];
  }
}

int BytecodeLoopAnalysis::GetLoopOffsetFor(int offset) const {
  auto next_backedge = backedge_to_header_.lower_bound(offset);
  // If there is no next backedge => offset is not in a loop.
  if (next_backedge == backedge_to_header_.end()) {
    return -1;
  }
  // If the header preceeds the offset, it is the backedge of the containing
  // loop.
  if (next_backedge->second <= offset) {
    return next_backedge->second;
  }
  // Otherwise there is a nested loop after this offset. We just return the
  // parent of the next nested loop.
  return loop_header_to_parent_.upper_bound(offset)->second;
}

int BytecodeLoopAnalysis::GetParentLoopFor(int header_offset) const {
  auto parent = loop_header_to_parent_.find(header_offset);
  DCHECK(parent != loop_header_to_parent_.end());
  return parent->second;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
