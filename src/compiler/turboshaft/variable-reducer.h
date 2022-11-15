// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_VARIABLE_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_VARIABLE_REDUCER_H_

#include <algorithm>

#include "src/base/logging.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

// When cloning a Block or duplicating an Operation, we end up with some
// Operations of the old graph mapping to multiple Operations in the new graph.
// When using those Operations in subsequent Operations, we need to know which
// of the new-Operation to use, and, in particular, if a Block has 2
// predecessors that have a mapping for the same old-Operation, we need to
// merge them in a Phi node. All of this is handled by the VariableAssembler.
//
// The typical workflow when working with the VariableAssembler would be:
//    - At some point, you need to introduce a Variable (for instance
//      because you cloned a block or an Operation) and call NewFreshVariable to
//      get a fresh Variable.
//    - You can then Set the new-OpIndex associated with this Variable in the
//      current Block with the Set method.
//    - If you later need to set an OpIndex for this Variable in another Block,
//      call Set again.
//    - At any time, you can call Get to get the new-Operation associated to
//      this Variable. Get will return:
//         * if the current block is dominated by a block who did a Set on the
//           Variable, then the Operation that was Set then.
//         * otherwise, the current block must be dominated by a Merge whose
//           predecessors have all Set this Variable. In that case, the
//           VariableAssembler introduced a Phi in this merge, and will return
//           this Phi.
//
// Note that the VariableAssembler does not do "old-OpIndex => Variable"
// book-keeping: the users of the Variable should do that themselves (which
// is what OptimizationPhase does for instance).

using Variable =
    SnapshotTable<OpIndex, base::Optional<RegisterRepresentation>>::Key;

template <class Next>
class VariableReducer : public Next {
  using Scope =
      SnapshotTable<OpIndex, base::Optional<RegisterRepresentation>>::Scope;
  using Snapshot =
      SnapshotTable<OpIndex, base::Optional<RegisterRepresentation>>::Snapshot;

 public:
  using Next::Asm;
  VariableReducer()
      : table_(Asm().phase_zone()),
        block_to_snapshot_mapping_(Asm().input_graph().block_count(),
                                   base::nullopt, Asm().phase_zone()),
        predecessors_(Asm().phase_zone()) {}

  ~VariableReducer() {
    if (current_scope_.has_value()) {
      // Scope's destructor has a DCHECK checking that it's sealed. To avoid
      // triggering this DCHECK, we seal the last Scope when destroying
      // VariableReducer, even though the Snapshot created by Seal will never be
      // used.
      current_scope_->Seal();
    }
  }

  void Bind(Block* new_block, const Block* origin = nullptr) {
    Next::Bind(new_block, origin);

    SealAndSave();

    predecessors_.clear();
    for (const Block* pred = new_block->LastPredecessor(); pred != nullptr;
         pred = pred->NeighboringPredecessor()) {
      DCHECK_LT(pred->index().id(), block_to_snapshot_mapping_.size());
      base::Optional<Snapshot> pred_snapshot =
          block_to_snapshot_mapping_[pred->index().id()];
      DCHECK(pred_snapshot.has_value());
      predecessors_.push_back(pred_snapshot.value());
    }
    std::reverse(predecessors_.begin(), predecessors_.end());

    auto merge_variables = [&](Variable var,
                               base::Vector<OpIndex> predecessors) -> OpIndex {
      for (OpIndex idx : predecessors) {
        if (!idx.valid()) {
          // If any of the predecessors' value is Invalid, then we shouldn't
          // merge {var}.
          return OpIndex::Invalid();
        }
      }
      return MergeOpIndices(predecessors, var.data());
    };

    current_scope_.emplace(table_, base::VectorOf(predecessors_),
                           merge_variables);
    current_block_ = new_block;
  }

  OpIndex Get(Variable var) { return current_scope_->Get(var); }

  void Set(Variable var, OpIndex new_index) {
    current_scope_->Set(var, new_index);
  }

  Variable NewFreshVariable(base::Optional<RegisterRepresentation> rep) {
    return table_.NewKey(rep, OpIndex::Invalid());
  }

 private:
  // SealAndSave seals the current scope, and stores its snapshot in
  // {block_to_snapshot_mapping_}, so that it can be used for later merging.
  void SealAndSave() {
    if (!current_scope_.has_value()) {
      DCHECK_EQ(current_block_, nullptr);
      return;
    }

    DCHECK_NOT_NULL(current_block_);
    Snapshot snapshot = current_scope_->Seal();

    DCHECK(current_block_->index().valid());
    size_t id = current_block_->index().id();
    if (id >= block_to_snapshot_mapping_.size()) {
      // The table initially contains as many entries as blocks in the input
      // graphs. In most cases, the number of blocks between input and ouput
      // graphs shouldn't grow too much, so a growth factor of 1.5 should be
      // reasonable.
      static constexpr double kGrowthFactor = 1.5;
      size_t new_size = std::max<size_t>(
          id, kGrowthFactor * block_to_snapshot_mapping_.size());
      block_to_snapshot_mapping_.resize(new_size);
    }

    block_to_snapshot_mapping_[id] = snapshot;
    current_block_ = nullptr;
    current_scope_.reset();
  }

  OpIndex MergeOpIndices(base::Vector<OpIndex> inputs,
                         base::Optional<RegisterRepresentation> maybe_rep) {
    if (maybe_rep.has_value()) {
      // Every Operation that has a RegisterRepresentation can be merged with a
      // simple Phi.
      return Asm().Phi(base::VectorOf(inputs), maybe_rep.value());
    } else {
      switch (Asm().output_graph().Get(inputs[0]).opcode) {
        case Opcode::kStackPointerGreaterThan:
          return Asm().Phi(base::VectorOf(inputs),
                           RegisterRepresentation::Word32());
        case Opcode::kFrameConstant:
          return Asm().Phi(base::VectorOf(inputs),
                           RegisterRepresentation::PointerSized());

        case Opcode::kFrameState:
          // Merging inputs of the n kFrameState one by one.
          return MergeFrameState(inputs);

        case Opcode::kOverflowCheckedBinop:
        case Opcode::kFloat64InsertWord32:
        case Opcode::kStore:
        case Opcode::kRetain:
        case Opcode::kStackSlot:
        case Opcode::kCheckLazyDeopt:
        case Opcode::kDeoptimize:
        case Opcode::kDeoptimizeIf:
        case Opcode::kTrapIf:
        case Opcode::kParameter:
        case Opcode::kOsrValue:
        case Opcode::kCall:
        case Opcode::kTailCall:
        case Opcode::kUnreachable:
        case Opcode::kReturn:
        case Opcode::kGoto:
        case Opcode::kBranch:
        case Opcode::kCatchException:
        case Opcode::kSwitch:
        case Opcode::kTuple:
        case Opcode::kProjection:
        case Opcode::kSelect:
          return OpIndex::Invalid();

        default:
          // In all other cases, {maybe_rep} should have a value and we
          // shouldn't end up here.
          UNREACHABLE();
      }
    }
  }

  OpIndex MergeFrameState(base::Vector<OpIndex> frame_states_indices) {
    base::SmallVector<const FrameStateOp*, 32> frame_states;
    for (OpIndex idx : frame_states_indices) {
      frame_states.push_back(
          &Asm().output_graph().Get(idx).template Cast<FrameStateOp>());
    }
    const FrameStateOp* first_frame = frame_states[0];

#if DEBUG
    // Making sure that all frame states have the same number of inputs, the
    // same "inlined" field, and the same data.
    for (auto frame_state : frame_states) {
      DCHECK_EQ(first_frame->input_count, frame_state->input_count);
      DCHECK_EQ(first_frame->inlined, frame_state->inlined);
      DCHECK_EQ(first_frame->data, frame_state->data);
    }
#endif

    base::SmallVector<OpIndex, 32> new_inputs;

    // Merging the parent frame states.
    if (first_frame->inlined) {
      ZoneVector<OpIndex> indices_to_merge(Asm().phase_zone());
      bool all_parent_frame_states_are_the_same = true;
      for (auto frame_state : frame_states) {
        indices_to_merge.push_back(frame_state->parent_frame_state());
        all_parent_frame_states_are_the_same =
            all_parent_frame_states_are_the_same &&
            first_frame->parent_frame_state() ==
                frame_state->parent_frame_state();
      }
      if (all_parent_frame_states_are_the_same) {
        new_inputs.push_back(first_frame->parent_frame_state());
      } else {
        OpIndex merged_parent_frame_state =
            MergeFrameState(base::VectorOf(indices_to_merge));
        new_inputs.push_back(merged_parent_frame_state);
      }
    }

    // Merging the state values.
    for (int i = 0; i < first_frame->state_values_count(); i++) {
      ZoneVector<OpIndex> indices_to_merge(Asm().phase_zone());
      bool all_inputs_are_the_same = true;
      for (auto frame_state : frame_states) {
        indices_to_merge.push_back(frame_state->state_value(i));
        all_inputs_are_the_same =
            all_inputs_are_the_same &&
            first_frame->input(i) == frame_state->state_value(i);
      }
      if (all_inputs_are_the_same) {
        // This input does not need to be merged, since its identical for all of
        // the frame states.
        new_inputs.push_back(first_frame->state_value(i));
      } else {
        RegisterRepresentation rep = first_frame->state_value_rep(i);
        OpIndex new_input =
            MergeOpIndices(base::VectorOf(indices_to_merge), rep);
        new_inputs.push_back(new_input);
      }
    }

    return Asm().FrameState(base::VectorOf(new_inputs), first_frame->inlined,
                            first_frame->data);
  }

  SnapshotTable<OpIndex, base::Optional<RegisterRepresentation>> table_;
  base::Optional<Scope> current_scope_ = base::nullopt;
  const Block* current_block_ = nullptr;
  ZoneVector<base::Optional<Snapshot>> block_to_snapshot_mapping_;

  // {predecessors_} is used during merging, but we use an instance variable for
  // it, in order to save memory and not reallocate it for each merge.
  ZoneVector<Snapshot> predecessors_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_VARIABLE_REDUCER_H_
