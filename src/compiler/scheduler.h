// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SCHEDULER_H_
#define V8_COMPILER_SCHEDULER_H_

#include <vector>

#include "src/v8.h"

#include "src/compiler/opcodes.h"
#include "src/compiler/schedule.h"
#include "src/zone-allocator.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class Scheduler {
 public:
  explicit Scheduler(Zone* zone);
  Scheduler(Zone* zone, Graph* graph, Schedule* schedule);

  Schedule* NewSchedule(Graph* graph);

  BasicBlockVector* ComputeSpecialRPO();

 private:
  Zone* zone_;
  Graph* graph_;
  Schedule* schedule_;
  NodeVector branches_;
  NodeVector calls_;
  NodeVector deopts_;
  NodeVector returns_;
  NodeVector loops_and_merges_;
  BasicBlockVector node_block_placement_;
  IntVector unscheduled_uses_;
  NodeVectorVector scheduled_nodes_;
  NodeVector schedule_root_nodes_;
  IntVector schedule_early_rpo_index_;

  int GetRPONumber(BasicBlock* block) {
    DCHECK(block->rpo_number_ >= 0 &&
           block->rpo_number_ < static_cast<int>(schedule_->rpo_order_.size()));
    DCHECK(schedule_->rpo_order_[block->rpo_number_] == block);
    return block->rpo_number_;
  }

  void PrepareAuxiliaryNodeData();
  void PrepareAuxiliaryBlockData();

  friend class CreateBlockVisitor;
  void CreateBlocks();

  void WireBlocks();

  void AddPredecessorsForLoopsAndMerges();
  void AddSuccessorsForBranches();
  void AddSuccessorsForReturns();
  void AddSuccessorsForCalls();
  void AddSuccessorsForDeopts();

  void GenerateImmediateDominatorTree();
  BasicBlock* GetCommonDominator(BasicBlock* b1, BasicBlock* b2);

  friend class ScheduleEarlyNodeVisitor;
  void ScheduleEarly();

  friend class PrepareUsesVisitor;
  void PrepareUses();

  friend class ScheduleLateNodeVisitor;
  void ScheduleLate();
};
}
}
}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_SCHEDULER_H_
