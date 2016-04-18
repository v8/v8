// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/effect-control-linearizer.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/schedule.h"
#include "src/compiler/simplified-operator.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace v8 {
namespace internal {
namespace compiler {

class EffectControlLinearizerTest : public TypedGraphTest {
 public:
  EffectControlLinearizerTest()
      : TypedGraphTest(3),
        machine_(zone()),
        javascript_(zone()),
        simplified_(zone()),
        jsgraph_(isolate(), graph(), common(), &javascript_, &simplified_,
                 &machine_) {}

  JSGraph* jsgraph() { return &jsgraph_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

 private:
  MachineOperatorBuilder machine_;
  JSOperatorBuilder javascript_;
  SimplifiedOperatorBuilder simplified_;
  JSGraph jsgraph_;
};

namespace {

BasicBlock* AddBlockToSchedule(Schedule* schedule) {
  BasicBlock* block = schedule->NewBasicBlock();
  block->set_rpo_number(static_cast<int32_t>(schedule->rpo_order()->size()));
  schedule->rpo_order()->push_back(block);
  return block;
}

}  // namespace

TEST_F(EffectControlLinearizerTest, SimpleLoad) {
  Schedule schedule(zone());

  // Create the graph.
  Node* heap_number = NumberConstant(0.5);
  Node* load = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForHeapNumberValue()), heap_number,
      graph()->start(), graph()->start());
  Node* ret = graph()->NewNode(common()->Return(), load, graph()->start(),
                               graph()->start());

  // Build the basic block structure.
  BasicBlock* start = schedule.start();
  schedule.rpo_order()->push_back(start);
  start->set_rpo_number(0);

  // Populate the basic blocks with nodes.
  schedule.AddNode(start, graph()->start());
  schedule.AddNode(start, heap_number);
  schedule.AddNode(start, load);
  schedule.AddReturn(start, ret);

  // Run the state effect introducer.
  EffectControlLinearizer introducer(jsgraph(), &schedule, zone());
  introducer.Run();

  EXPECT_THAT(load,
              IsLoadField(AccessBuilder::ForHeapNumberValue(), heap_number,
                          graph()->start(), graph()->start()));
  // The return should have reconnected effect edge to the load.
  EXPECT_THAT(ret, IsReturn(load, load, graph()->start()));
}

TEST_F(EffectControlLinearizerTest, DiamondLoad) {
  Schedule schedule(zone());

  // Create the graph.
  Node* branch =
      graph()->NewNode(common()->Branch(), Int32Constant(0), graph()->start());

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* heap_number = NumberConstant(0.5);
  Node* vtrue = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForHeapNumberValue()), heap_number,
      graph()->start(), if_true);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* vfalse = Float64Constant(2);

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi = graph()->NewNode(
      common()->Phi(MachineRepresentation::kFloat64, 2), vtrue, vfalse, merge);

  Node* ret =
      graph()->NewNode(common()->Return(), phi, graph()->start(), merge);

  // Build the basic block structure.
  BasicBlock* start = schedule.start();
  schedule.rpo_order()->push_back(start);
  start->set_rpo_number(0);

  BasicBlock* tblock = AddBlockToSchedule(&schedule);
  BasicBlock* fblock = AddBlockToSchedule(&schedule);
  BasicBlock* mblock = AddBlockToSchedule(&schedule);

  // Populate the basic blocks with nodes.
  schedule.AddNode(start, graph()->start());
  schedule.AddBranch(start, branch, tblock, fblock);

  schedule.AddNode(tblock, if_true);
  schedule.AddNode(tblock, heap_number);
  schedule.AddNode(tblock, vtrue);
  schedule.AddGoto(tblock, mblock);

  schedule.AddNode(fblock, if_false);
  schedule.AddNode(fblock, vfalse);
  schedule.AddGoto(fblock, mblock);

  schedule.AddNode(mblock, merge);
  schedule.AddNode(mblock, phi);
  schedule.AddReturn(mblock, ret);

  // Run the state effect introducer.
  EffectControlLinearizer introducer(jsgraph(), &schedule, zone());
  introducer.Run();

  // The effect input to the return should be an effect phi with the
  // newly introduced effectful change operators.
  ASSERT_THAT(
      ret, IsReturn(phi, IsEffectPhi(vtrue, graph()->start(), merge), merge));
}

TEST_F(EffectControlLinearizerTest, LoopLoad) {
  Schedule schedule(zone());

  // Create the graph.
  Node* loop = graph()->NewNode(common()->Loop(1), graph()->start());
  Node* effect_phi =
      graph()->NewNode(common()->EffectPhi(1), graph()->start(), loop);

  Node* cond = Int32Constant(0);
  Node* branch = graph()->NewNode(common()->Branch(), cond, loop);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);

  loop->AppendInput(zone(), if_false);
  NodeProperties::ChangeOp(loop, common()->Loop(2));

  effect_phi->InsertInput(zone(), 1, effect_phi);
  NodeProperties::ChangeOp(effect_phi, common()->EffectPhi(2));

  Node* heap_number = NumberConstant(0.5);
  Node* load = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForHeapNumberValue()), heap_number,
      graph()->start(), loop);

  Node* ret = graph()->NewNode(common()->Return(), load, effect_phi, if_true);

  // Build the basic block structure.
  BasicBlock* start = schedule.start();
  schedule.rpo_order()->push_back(start);
  start->set_rpo_number(0);

  BasicBlock* lblock = AddBlockToSchedule(&schedule);
  BasicBlock* fblock = AddBlockToSchedule(&schedule);
  BasicBlock* rblock = AddBlockToSchedule(&schedule);

  // Populate the basic blocks with nodes.
  schedule.AddNode(start, graph()->start());
  schedule.AddGoto(start, lblock);

  schedule.AddNode(lblock, loop);
  schedule.AddNode(lblock, effect_phi);
  schedule.AddNode(lblock, heap_number);
  schedule.AddNode(lblock, load);
  schedule.AddNode(lblock, cond);
  schedule.AddBranch(lblock, branch, rblock, fblock);

  schedule.AddNode(fblock, if_false);
  schedule.AddGoto(fblock, lblock);

  schedule.AddNode(rblock, if_true);
  schedule.AddReturn(rblock, ret);

  // Run the state effect introducer.
  EffectControlLinearizer introducer(jsgraph(), &schedule, zone());
  introducer.Run();

  ASSERT_THAT(ret, IsReturn(load, load, if_true));
  EXPECT_THAT(load, IsLoadField(AccessBuilder::ForHeapNumberValue(),
                                heap_number, effect_phi, loop));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
