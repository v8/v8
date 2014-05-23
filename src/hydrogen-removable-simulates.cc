// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hydrogen-flow-engine.h"
#include "hydrogen-instructions.h"
#include "hydrogen-removable-simulates.h"

namespace v8 {
namespace internal {

class State : public ZoneObject {
 public:
  explicit State(Zone* zone)
      : zone_(zone), mergelist_(2, zone), first_(true), mode_(NORMAL) { }

  State* Process(HInstruction* instr, Zone* zone) {
    if (FLAG_trace_removable_simulates) {
      PrintF("[State::Process %s #%d %s]\n",
             mode_ == NORMAL ? "normal" : "collect",
             instr->id(), instr->Mnemonic());
    }
    // Forward-merge "trains" of simulates after an instruction with observable
    // side effects to keep live ranges short.
    if (mode_ == COLLECT_CONSECUTIVE_SIMULATES) {
      if (instr->IsSimulate()) {
        HSimulate* current_simulate = HSimulate::cast(instr);
        if (current_simulate->is_candidate_for_removal() &&
            !current_simulate->ast_id().IsNone()) {
          Remember(current_simulate);
          return this;
        }
      }
      FlushSimulates();
      mode_ = NORMAL;
    }
    // Ensure there's a non-foldable HSimulate before an HEnterInlined to avoid
    // folding across HEnterInlined.
    ASSERT(!(instr->IsEnterInlined() &&
             HSimulate::cast(instr->previous())->is_candidate_for_removal()));
    if (instr->IsLeaveInlined() || instr->IsReturn()) {
      // Never fold simulates from inlined environments into simulates in the
      // outer environment. Simply remove all accumulated simulates without
      // merging. This is safe because simulates after instructions with side
      // effects are never added to the merge list. The same reasoning holds for
      // return instructions.
      RemoveSimulates();
      return this;
    }
    if (instr->IsControlInstruction()) {
      // Merge the accumulated simulates at the end of the block.
      FlushSimulates();
      return this;
    }
    // Skip the non-simulates and the first simulate.
    if (!instr->IsSimulate()) return this;
    if (first_) {
      first_ = false;
      return this;
    }
    HSimulate* current_simulate = HSimulate::cast(instr);
    if (!current_simulate->is_candidate_for_removal()) {
      Remember(current_simulate);
      FlushSimulates();
    } else if (current_simulate->ast_id().IsNone()) {
      ASSERT(current_simulate->next()->IsEnterInlined());
      FlushSimulates();
    } else if (current_simulate->previous()->HasObservableSideEffects()) {
      Remember(current_simulate);
      mode_ = COLLECT_CONSECUTIVE_SIMULATES;
    } else {
      Remember(current_simulate);
    }

    return this;
  }

  static State* Merge(State* succ_state,
                      HBasicBlock* succ_block,
                      State* pred_state,
                      HBasicBlock* pred_block,
                      Zone* zone) {
    if (FLAG_trace_removable_simulates) {
      PrintF("[State::Merge predecessor block %d, successor block %d\n",
             pred_block->block_id(), succ_block->block_id());
    }
    return pred_state;
  }

  static State* Finish(State* state, HBasicBlock* block, Zone* zone) {
    if (FLAG_trace_removable_simulates) {
      PrintF("[State::Finish block %d]\n", block->block_id()); }
    // Make sure the merge list is empty at the start of a block.
    ASSERT(state->mergelist_.is_empty());
    // Nasty heuristic: Never remove the first simulate in a block. This
    // just so happens to have a beneficial effect on register allocation.
    state->first_ = true;
    return state;
  }

 private:
  enum Mode { NORMAL, COLLECT_CONSECUTIVE_SIMULATES };

  void Remember(HSimulate* sim) {
    mergelist_.Add(sim, zone_);
  }

  void FlushSimulates() {
    if (!mergelist_.is_empty()) {
      mergelist_.RemoveLast()->MergeWith(&mergelist_);
    }
  }

  void RemoveSimulates() {
    while (!mergelist_.is_empty()) {
      mergelist_.RemoveLast()->DeleteAndReplaceWith(NULL);
    }
  }

  Zone* zone_;
  ZoneList<HSimulate*> mergelist_;
  bool first_;
  Mode mode_;
};


// We don't use effects here.
class Effects : public ZoneObject {
 public:
  explicit Effects(Zone* zone) { }
  bool Disabled() { return true; }
  void Process(HInstruction* instr, Zone* zone) { }
  void Apply(State* state) { }
  void Union(Effects* that, Zone* zone) { }
};


void HMergeRemovableSimulatesPhase::Run() {
  HFlowEngine<State, Effects> engine(graph(), zone());
  State* state = new(zone()) State(zone());
  engine.AnalyzeDominatedBlocks(graph()->blocks()->at(0), state);
}

} }  // namespace v8::internal
