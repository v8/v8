// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/bytecode-analysis.h"

#include "src/interpreter/bytecode-array-reverse-iterator.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

BytecodeAnalysis::BytecodeAnalysis(Handle<BytecodeArray> bytecode_array,
                                   Zone* zone)
    : bytecode_array_(bytecode_array),
      zone_(zone),
      loop_stack_(zone),
      end_to_header_(zone),
      header_to_parent_(zone) {}

void BytecodeAnalysis::Analyze() {
  loop_stack_.push(-1);

  interpreter::BytecodeArrayReverseIterator iterator(bytecode_array(), zone());
  while (!iterator.done()) {
    interpreter::Bytecode bytecode = iterator.current_bytecode();
    if (bytecode == interpreter::Bytecode::kJumpLoop) {
      PushLoop(iterator.GetJumpTargetOffset(), iterator.current_offset());
    } else if (iterator.current_offset() == loop_stack_.top()) {
      loop_stack_.pop();
    }
    iterator.Advance();
  }

  DCHECK_EQ(loop_stack_.size(), 1u);
  DCHECK_EQ(loop_stack_.top(), -1);
}

void BytecodeAnalysis::PushLoop(int loop_header, int loop_end) {
  DCHECK(loop_header < loop_end);
  DCHECK(loop_stack_.top() < loop_header);
  DCHECK(end_to_header_.find(loop_end) == end_to_header_.end());
  DCHECK(header_to_parent_.find(loop_header) == header_to_parent_.end());

  end_to_header_.insert(ZoneMap<int, int>::value_type(loop_end, loop_header));
  header_to_parent_.insert(
      ZoneMap<int, int>::value_type(loop_header, loop_stack_.top()));
  loop_stack_.push(loop_header);
}

bool BytecodeAnalysis::IsLoopHeader(int offset) const {
  return header_to_parent_.find(offset) != header_to_parent_.end();
}

int BytecodeAnalysis::GetLoopOffsetFor(int offset) const {
  auto loop_end_to_header = end_to_header_.lower_bound(offset);
  // If there is no next end => offset is not in a loop.
  if (loop_end_to_header == end_to_header_.end()) {
    return -1;
  }
  // If the header preceeds the offset, this is the loop
  //
  //   .> header  <--loop_end_to_header
  //   |
  //   |  <--offset
  //   |
  //   `- end
  if (loop_end_to_header->second <= offset) {
    return loop_end_to_header->second;
  }
  // Otherwise there is a (potentially nested) loop after this offset.
  //
  //    <--offset
  //
  //   .> header
  //   |
  //   | .> header  <--loop_end_to_header
  //   | |
  //   | `- end
  //   |
  //   `- end
  // We just return the parent of the next loop header (might be -1).
  DCHECK(header_to_parent_.upper_bound(offset) != header_to_parent_.end());

  return header_to_parent_.upper_bound(offset)->second;
}

int BytecodeAnalysis::GetParentLoopFor(int header_offset) const {
  DCHECK(IsLoopHeader(header_offset));

  return header_to_parent_.find(header_offset)->second;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
