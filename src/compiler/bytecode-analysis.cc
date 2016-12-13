// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/bytecode-analysis.h"

#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecode-array-random-iterator.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

using namespace interpreter;

BytecodeAnalysis::BytecodeAnalysis(Handle<BytecodeArray> bytecode_array,
                                   Zone* zone, bool do_liveness_analysis)
    : bytecode_array_(bytecode_array),
      do_liveness_analysis_(do_liveness_analysis),
      zone_(zone),
      loop_stack_(zone),
      loop_end_index_queue_(zone),
      end_to_header_(zone),
      header_to_parent_(zone),
      liveness_map_(bytecode_array->length(), zone) {}

namespace {

void UpdateInLiveness(Bytecode bytecode, BytecodeLivenessState& in_liveness,
                      const BytecodeArrayAccessor& accessor) {
  int num_operands = Bytecodes::NumberOfOperands(bytecode);
  const OperandType* operand_types = Bytecodes::GetOperandTypes(bytecode);
  AccumulatorUse accumulator_use = Bytecodes::GetAccumulatorUse(bytecode);

  if (accumulator_use == AccumulatorUse::kWrite) {
    in_liveness.MarkAccumulatorDead();
  }
  for (int i = 0; i < num_operands; ++i) {
    switch (operand_types[i]) {
      case OperandType::kRegOut: {
        interpreter::Register r = accessor.GetRegisterOperand(i);
        if (!r.is_parameter()) {
          in_liveness.MarkRegisterDead(r.index());
        }
        break;
      }
      case OperandType::kRegOutPair: {
        interpreter::Register r = accessor.GetRegisterOperand(i);
        if (!r.is_parameter()) {
          DCHECK(!interpreter::Register(r.index() + 1).is_parameter());
          in_liveness.MarkRegisterDead(r.index());
          in_liveness.MarkRegisterDead(r.index() + 1);
        }
        break;
      }
      case OperandType::kRegOutTriple: {
        interpreter::Register r = accessor.GetRegisterOperand(i);
        if (!r.is_parameter()) {
          DCHECK(!interpreter::Register(r.index() + 1).is_parameter());
          DCHECK(!interpreter::Register(r.index() + 2).is_parameter());
          in_liveness.MarkRegisterDead(r.index());
          in_liveness.MarkRegisterDead(r.index() + 1);
          in_liveness.MarkRegisterDead(r.index() + 2);
        }
        break;
      }
      default:
        DCHECK(!Bytecodes::IsRegisterOutputOperandType(operand_types[i]));
        break;
    }
  }

  if (accumulator_use == AccumulatorUse::kRead) {
    in_liveness.MarkAccumulatorLive();
  }
  for (int i = 0; i < num_operands; ++i) {
    switch (operand_types[i]) {
      case OperandType::kReg: {
        interpreter::Register r = accessor.GetRegisterOperand(i);
        if (!r.is_parameter()) {
          in_liveness.MarkRegisterLive(r.index());
        }
        break;
      }
      case OperandType::kRegPair: {
        interpreter::Register r = accessor.GetRegisterOperand(i);
        if (!r.is_parameter()) {
          DCHECK(!interpreter::Register(r.index() + 1).is_parameter());
          in_liveness.MarkRegisterLive(r.index());
          in_liveness.MarkRegisterLive(r.index() + 1);
        }
        break;
      }
      case OperandType::kRegList: {
        interpreter::Register r = accessor.GetRegisterOperand(i++);
        uint32_t reg_count = accessor.GetRegisterCountOperand(i);
        if (!r.is_parameter()) {
          for (uint32_t j = 0; j < reg_count; ++j) {
            DCHECK(!interpreter::Register(r.index() + j).is_parameter());
            in_liveness.MarkRegisterLive(r.index() + j);
          }
        }
      }
      default:
        DCHECK(!Bytecodes::IsRegisterInputOperandType(operand_types[i]));
        break;
    }
  }
}

void UpdateOutLiveness(Bytecode bytecode, BytecodeLivenessState& out_liveness,
                       BytecodeLivenessState* next_bytecode_in_liveness,
                       const BytecodeArrayAccessor& accessor,
                       const BytecodeLivenessMap& liveness_map) {
  int current_offset = accessor.current_offset();
  const Handle<BytecodeArray>& bytecode_array = accessor.bytecode_array();

  // Update from jump target (if any). Skip loops, we update these manually in
  // the liveness iterations.
  if (Bytecodes::IsForwardJump(bytecode)) {
    int target_offset = accessor.GetJumpTargetOffset();
    out_liveness.Union(*liveness_map.GetInLiveness(target_offset));
  }

  // Update from next bytecode (unless there isn't one or this is an
  // unconditional jump).
  if (next_bytecode_in_liveness != nullptr &&
      !Bytecodes::IsUnconditionalJump(bytecode)) {
    out_liveness.Union(*next_bytecode_in_liveness);
  }

  // Update from exception handler (if any).
  if (!interpreter::Bytecodes::IsWithoutExternalSideEffects(bytecode)) {
    int handler_context;
    // TODO(leszeks): We should look up this range only once per entry.
    HandlerTable* table = HandlerTable::cast(bytecode_array->handler_table());
    int handler_offset =
        table->LookupRange(current_offset, &handler_context, nullptr);

    if (handler_offset != -1) {
      out_liveness.Union(*liveness_map.GetInLiveness(handler_offset));
      out_liveness.MarkRegisterLive(handler_context);
    }
  }
}

}  // namespace

void BytecodeAnalysis::Analyze() {
  loop_stack_.push(-1);

  BytecodeLivenessState* next_bytecode_in_liveness = nullptr;

  BytecodeArrayRandomIterator iterator(bytecode_array(), zone());
  for (iterator.GoToEnd(); iterator.IsValid(); --iterator) {
    Bytecode bytecode = iterator.current_bytecode();
    int current_offset = iterator.current_offset();

    if (bytecode == Bytecode::kJumpLoop) {
      // Every byte up to and including the last byte within the backwards jump
      // instruction is considered part of the loop, set loop end accordingly.
      int loop_end = current_offset + iterator.current_bytecode_size();
      PushLoop(iterator.GetJumpTargetOffset(), loop_end);

      // Save the index so that we can do another pass later.
      if (do_liveness_analysis_) {
        loop_end_index_queue_.push_back(iterator.current_index());
      }
    } else if (current_offset == loop_stack_.top()) {
      loop_stack_.pop();
    }

    if (do_liveness_analysis_) {
      BytecodeLiveness& liveness = liveness_map_.InitializeLiveness(
          current_offset, bytecode_array()->register_count(), zone());

      UpdateOutLiveness(bytecode, *liveness.out, next_bytecode_in_liveness,
                        iterator, liveness_map_);
      liveness.in->CopyFrom(*liveness.out);
      UpdateInLiveness(bytecode, *liveness.in, iterator);

      next_bytecode_in_liveness = liveness.in;
    }
  }

  DCHECK_EQ(loop_stack_.size(), 1u);
  DCHECK_EQ(loop_stack_.top(), -1);

  if (!do_liveness_analysis_) return;

  // At this point, every bytecode has a valid in and out liveness, except for
  // propagating liveness across back edges (i.e. JumpLoop). Subsequent liveness
  // analysis iterations can only add additional liveness bits that are pulled
  // across these back edges.
  //
  // Furthermore, a loop header's in-liveness can only change based on any
  // bytecodes *after* the loop end --  it cannot change as a result of the
  // JumpLoop liveness being updated, as the only liveness bits than can be
  // added to the loop body are those of the loop header.
  //
  // So, if we know that the liveness of bytecodes after a loop header won't
  // change (e.g. because there are no loops in them, or we have already ensured
  // those loops are valid), we can safely update the loop end and pass over the
  // loop body, and then never have to pass over that loop end again, because we
  // have shown that its target, the loop header, can't change from the entries
  // after the loop, and can't change from any loop body pass.
  //
  // This means that in a pass, we can iterate backwards over the bytecode
  // array, process any loops that we encounter, and on subsequent passes we can
  // skip processing those loops (though we still have to process inner loops).
  //
  // Equivalently, we can queue up loop ends from back to front, and pass over
  // the loops in that order, as this preserves both the bottom-to-top and
  // outer-to-inner requirements.

  for (int loop_end_index : loop_end_index_queue_) {
    iterator.GoToIndex(loop_end_index);

    DCHECK_EQ(iterator.current_bytecode(), Bytecode::kJumpLoop);

    int header_offset = iterator.GetJumpTargetOffset();
    int end_offset = iterator.current_offset();

    BytecodeLiveness& header_liveness =
        liveness_map_.GetLiveness(header_offset);
    BytecodeLiveness& end_liveness = liveness_map_.GetLiveness(end_offset);

    if (!end_liveness.out->UnionIsChanged(*header_liveness.in)) {
      // Only update the loop body if the loop end liveness changed.
      continue;
    }
    end_liveness.in->CopyFrom(*end_liveness.out);
    next_bytecode_in_liveness = end_liveness.in;

    // Advance into the loop body.
    --iterator;
    for (; iterator.current_offset() > header_offset; --iterator) {
      Bytecode bytecode = iterator.current_bytecode();

      int current_offset = iterator.current_offset();
      BytecodeLiveness& liveness = liveness_map_.GetLiveness(current_offset);

      UpdateOutLiveness(bytecode, *liveness.out, next_bytecode_in_liveness,
                        iterator, liveness_map_);
      liveness.in->CopyFrom(*liveness.out);
      UpdateInLiveness(bytecode, *liveness.in, iterator);

      next_bytecode_in_liveness = liveness.in;
    }
    // Now we are at the loop header. Since the in-liveness of the header
    // can't change, we need only to update the out-liveness.
    UpdateOutLiveness(iterator.current_bytecode(), *header_liveness.out,
                      next_bytecode_in_liveness, iterator, liveness_map_);
  }

  DCHECK(LivenessIsValid());
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
  auto loop_end_to_header = end_to_header_.upper_bound(offset);
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

const BytecodeLivenessState* BytecodeAnalysis::GetInLivenessFor(
    int offset) const {
  if (!do_liveness_analysis_) return nullptr;

  return liveness_map_.GetInLiveness(offset);
}

const BytecodeLivenessState* BytecodeAnalysis::GetOutLivenessFor(
    int offset) const {
  if (!do_liveness_analysis_) return nullptr;

  return liveness_map_.GetOutLiveness(offset);
}

std::ostream& BytecodeAnalysis::PrintLivenessTo(std::ostream& os) const {
  interpreter::BytecodeArrayIterator iterator(bytecode_array());

  for (; !iterator.done(); iterator.Advance()) {
    int current_offset = iterator.current_offset();

    const BitVector& in_liveness =
        GetInLivenessFor(current_offset)->bit_vector();
    const BitVector& out_liveness =
        GetOutLivenessFor(current_offset)->bit_vector();

    for (int i = 0; i < in_liveness.length(); ++i) {
      os << (in_liveness.Contains(i) ? "L" : ".");
    }
    os << " -> ";

    for (int i = 0; i < out_liveness.length(); ++i) {
      os << (out_liveness.Contains(i) ? "L" : ".");
    }

    os << " | " << current_offset << ": ";
    iterator.PrintTo(os) << std::endl;
  }

  return os;
}

#if DEBUG
bool BytecodeAnalysis::LivenessIsValid() {
  BytecodeArrayRandomIterator iterator(bytecode_array(), zone());

  BytecodeLivenessState previous_liveness(bytecode_array()->register_count(),
                                          zone());

  int invalid_offset = -1;
  int which_invalid = -1;

  BytecodeLivenessState* next_bytecode_in_liveness = nullptr;

  // Ensure that there are no liveness changes if we iterate one more time.
  for (iterator.GoToEnd(); iterator.IsValid(); --iterator) {
    Bytecode bytecode = iterator.current_bytecode();

    int current_offset = iterator.current_offset();

    BytecodeLiveness& liveness = liveness_map_.GetLiveness(current_offset);

    previous_liveness.CopyFrom(*liveness.out);

    UpdateOutLiveness(bytecode, *liveness.out, next_bytecode_in_liveness,
                      iterator, liveness_map_);
    // UpdateOutLiveness skips kJumpLoop, so we update it manually.
    if (bytecode == Bytecode::kJumpLoop) {
      int target_offset = iterator.GetJumpTargetOffset();
      liveness.out->Union(*liveness_map_.GetInLiveness(target_offset));
    }

    if (!liveness.out->Equals(previous_liveness)) {
      // Reset the invalid liveness.
      liveness.out->CopyFrom(previous_liveness);
      invalid_offset = current_offset;
      which_invalid = 1;
      break;
    }

    previous_liveness.CopyFrom(*liveness.in);

    liveness.in->CopyFrom(*liveness.out);
    UpdateInLiveness(bytecode, *liveness.in, iterator);

    if (!liveness.in->Equals(previous_liveness)) {
      // Reset the invalid liveness.
      liveness.in->CopyFrom(previous_liveness);
      invalid_offset = current_offset;
      which_invalid = 0;
      break;
    }

    next_bytecode_in_liveness = liveness.in;
  }

  if (invalid_offset != -1) {
    OFStream of(stderr);
    of << "Invalid liveness:" << std::endl;

    // Dump the bytecode, annotated with the liveness and marking loops.

    int loop_indent = 0;

    BytecodeArrayIterator forward_iterator(bytecode_array());
    for (; !forward_iterator.done(); forward_iterator.Advance()) {
      int current_offset = forward_iterator.current_offset();
      const BitVector& in_liveness =
          GetInLivenessFor(current_offset)->bit_vector();
      const BitVector& out_liveness =
          GetOutLivenessFor(current_offset)->bit_vector();

      for (int i = 0; i < in_liveness.length(); ++i) {
        of << (in_liveness.Contains(i) ? 'L' : '.');
      }

      of << " | ";

      for (int i = 0; i < out_liveness.length(); ++i) {
        of << (out_liveness.Contains(i) ? 'L' : '.');
      }

      of << " : " << current_offset << " : ";

      // Draw loop back edges by indentin everything between loop headers and
      // jump loop instructions.
      if (forward_iterator.current_bytecode() == Bytecode::kJumpLoop) {
        loop_indent--;
      }
      for (int i = 0; i < loop_indent; ++i) {
        of << " | ";
      }
      if (forward_iterator.current_bytecode() == Bytecode::kJumpLoop) {
        of << " `-" << current_offset;
      } else if (IsLoopHeader(current_offset)) {
        of << " .>" << current_offset;
        loop_indent++;
      }
      forward_iterator.PrintTo(of) << std::endl;

      if (current_offset == invalid_offset) {
        // Underline the invalid liveness.
        if (which_invalid == 0) {
          for (int i = 0; i < in_liveness.length(); ++i) {
            of << '^';
          }
        } else {
          for (int i = 0; i < in_liveness.length() + 3; ++i) {
            of << ' ';
          }
          for (int i = 0; i < out_liveness.length(); ++i) {
            of << '^';
          }
        }

        // Make sure to draw the loop indentation marks on this additional line.
        of << " : " << current_offset << " : ";
        for (int i = 0; i < loop_indent; ++i) {
          of << " | ";
        }

        of << std::endl;
      }
    }
  }

  return invalid_offset == -1;
}
#endif

}  // namespace compiler
}  // namespace internal
}  // namespace v8
