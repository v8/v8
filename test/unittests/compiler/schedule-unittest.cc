// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node.h"
#include "src/compiler/schedule.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::ElementsAre;

namespace v8 {
namespace internal {
namespace compiler {

typedef TestWithZone ScheduleTest;


namespace {

const Operator kBranchOperator(IrOpcode::kBranch, Operator::kNoProperties,
                               "Branch", 0, 0, 0, 0, 0, 0);
const Operator kDummyOperator(IrOpcode::kParameter, Operator::kNoProperties,
                              "Dummy", 0, 0, 0, 0, 0, 0);

}  // namespace


TEST_F(ScheduleTest, Constructor) {
  Schedule schedule(zone());
  EXPECT_NE(nullptr, schedule.start());
  EXPECT_EQ(schedule.start(),
            schedule.GetBlockById(BasicBlock::Id::FromInt(0)));
  EXPECT_NE(nullptr, schedule.end());
  EXPECT_EQ(schedule.end(), schedule.GetBlockById(BasicBlock::Id::FromInt(1)));
  EXPECT_NE(schedule.start(), schedule.end());
}


TEST_F(ScheduleTest, AddNode) {
  Schedule schedule(zone());
  BasicBlock* start = schedule.start();

  Node* node0 = Node::New(zone(), 0, &kDummyOperator, 0, nullptr, false);
  EXPECT_EQ(nullptr, schedule.block(node0));
  schedule.AddNode(start, node0);
  EXPECT_EQ(start, schedule.block(node0));
  EXPECT_THAT(*start, ElementsAre(node0));

  Node* node1 = Node::New(zone(), 1, &kDummyOperator, 0, nullptr, false);
  EXPECT_EQ(nullptr, schedule.block(node1));
  schedule.AddNode(start, node1);
  EXPECT_EQ(start, schedule.block(node1));
  EXPECT_THAT(*start, ElementsAre(node0, node1));

  EXPECT_TRUE(schedule.SameBasicBlock(node0, node1));
}


TEST_F(ScheduleTest, AddGoto) {
  Schedule schedule(zone());
  BasicBlock* start = schedule.start();
  BasicBlock* end = schedule.end();

  BasicBlock* block = schedule.NewBasicBlock();
  schedule.AddGoto(start, block);

  EXPECT_EQ(0u, start->PredecessorCount());
  EXPECT_EQ(1u, start->SuccessorCount());
  EXPECT_EQ(block, start->SuccessorAt(0));
  EXPECT_THAT(start->successors(), ElementsAre(block));

  EXPECT_EQ(1u, block->PredecessorCount());
  EXPECT_EQ(0u, block->SuccessorCount());
  EXPECT_EQ(start, block->PredecessorAt(0));
  EXPECT_THAT(block->predecessors(), ElementsAre(start));

  EXPECT_EQ(0u, end->PredecessorCount());
  EXPECT_EQ(0u, end->SuccessorCount());
}


TEST_F(ScheduleTest, AddBranch) {
  Schedule schedule(zone());
  BasicBlock* start = schedule.start();

  Node* branch = Node::New(zone(), 0, &kBranchOperator, 0, nullptr, false);
  BasicBlock* tblock = schedule.NewBasicBlock();
  BasicBlock* fblock = schedule.NewBasicBlock();
  schedule.AddBranch(start, branch, tblock, fblock);

  EXPECT_EQ(start, schedule.block(branch));

  EXPECT_EQ(0u, start->PredecessorCount());
  EXPECT_EQ(2u, start->SuccessorCount());
  EXPECT_EQ(tblock, start->SuccessorAt(0));
  EXPECT_EQ(fblock, start->SuccessorAt(1));
  EXPECT_THAT(start->successors(), ElementsAre(tblock, fblock));

  EXPECT_EQ(1u, tblock->PredecessorCount());
  EXPECT_EQ(0u, tblock->SuccessorCount());
  EXPECT_EQ(start, tblock->PredecessorAt(0));
  EXPECT_THAT(tblock->predecessors(), ElementsAre(start));

  EXPECT_EQ(1u, fblock->PredecessorCount());
  EXPECT_EQ(0u, fblock->SuccessorCount());
  EXPECT_EQ(start, fblock->PredecessorAt(0));
  EXPECT_THAT(fblock->predecessors(), ElementsAre(start));
}


TEST_F(ScheduleTest, AddReturn) {
  Schedule schedule(zone());
  BasicBlock* start = schedule.start();
  BasicBlock* end = schedule.end();

  Node* node = Node::New(zone(), 0, &kDummyOperator, 0, nullptr, false);
  schedule.AddReturn(start, node);

  EXPECT_EQ(0u, start->PredecessorCount());
  EXPECT_EQ(1u, start->SuccessorCount());
  EXPECT_EQ(end, start->SuccessorAt(0));
  EXPECT_THAT(start->successors(), ElementsAre(end));
}


TEST_F(ScheduleTest, InsertBranch) {
  Schedule schedule(zone());
  BasicBlock* start = schedule.start();
  BasicBlock* end = schedule.end();

  Node* node = Node::New(zone(), 0, &kDummyOperator, 0, nullptr, false);
  Node* branch = Node::New(zone(), 0, &kBranchOperator, 0, nullptr, false);
  BasicBlock* tblock = schedule.NewBasicBlock();
  BasicBlock* fblock = schedule.NewBasicBlock();
  BasicBlock* mblock = schedule.NewBasicBlock();

  schedule.AddReturn(start, node);
  schedule.AddGoto(tblock, mblock);
  schedule.AddGoto(fblock, mblock);
  schedule.InsertBranch(start, mblock, branch, tblock, fblock);

  EXPECT_EQ(0u, start->PredecessorCount());
  EXPECT_EQ(2u, start->SuccessorCount());
  EXPECT_EQ(tblock, start->SuccessorAt(0));
  EXPECT_EQ(fblock, start->SuccessorAt(1));
  EXPECT_THAT(start->successors(), ElementsAre(tblock, fblock));

  EXPECT_EQ(2u, mblock->PredecessorCount());
  EXPECT_EQ(1u, mblock->SuccessorCount());
  EXPECT_EQ(end, mblock->SuccessorAt(0));
  EXPECT_THAT(mblock->predecessors(), ElementsAre(tblock, fblock));
  EXPECT_THAT(mblock->successors(), ElementsAre(end));

  EXPECT_EQ(1u, end->PredecessorCount());
  EXPECT_EQ(0u, end->SuccessorCount());
  EXPECT_EQ(mblock, end->PredecessorAt(0));
  EXPECT_THAT(end->predecessors(), ElementsAre(mblock));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
