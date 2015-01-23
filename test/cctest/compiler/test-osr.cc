// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/diamond.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/operator.h"
#include "src/compiler/osr.h"
#include "test/cctest/cctest.h"

using namespace v8::internal;
using namespace v8::internal::compiler;

// TODO(titzer): move this method to a common testing place.

static int CheckInputs(Node* node, Node* i0 = NULL, Node* i1 = NULL,
                       Node* i2 = NULL, Node* i3 = NULL) {
  int count = 4;
  if (i3 == NULL) count = 3;
  if (i2 == NULL) count = 2;
  if (i1 == NULL) count = 1;
  if (i0 == NULL) count = 0;
  CHECK_EQ(count, node->InputCount());
  if (i0 != NULL) CHECK_EQ(i0, node->InputAt(0));
  if (i1 != NULL) CHECK_EQ(i1, node->InputAt(1));
  if (i2 != NULL) CHECK_EQ(i2, node->InputAt(2));
  if (i3 != NULL) CHECK_EQ(i3, node->InputAt(3));
  return count;
}


static Operator kIntLt(IrOpcode::kInt32LessThan, Operator::kPure,
                       "Int32LessThan", 2, 0, 0, 1, 0, 0);


static const int kMaxOsrValues = 10;

class OsrDeconstructorTester : public HandleAndZoneScope {
 public:
  explicit OsrDeconstructorTester(int num_values)
      : isolate(main_isolate()),
        common(main_zone()),
        graph(main_zone()),
        jsgraph(main_isolate(), &graph, &common, NULL, NULL),
        start(graph.NewNode(common.Start(1))),
        p0(graph.NewNode(common.Parameter(0), start)),
        end(graph.NewNode(common.End(), start)),
        osr_normal_entry(graph.NewNode(common.OsrNormalEntry(), start)),
        osr_loop_entry(graph.NewNode(common.OsrLoopEntry(), start)),
        self(graph.NewNode(common.Int32Constant(0xaabbccdd))) {
    CHECK(num_values <= kMaxOsrValues);
    graph.SetStart(start);
    for (int i = 0; i < num_values; i++) {
      osr_values[i] = graph.NewNode(common.OsrValue(i), osr_loop_entry);
    }
  }

  Isolate* isolate;
  CommonOperatorBuilder common;
  Graph graph;
  JSGraph jsgraph;
  Node* start;
  Node* p0;
  Node* end;
  Node* osr_normal_entry;
  Node* osr_loop_entry;
  Node* self;
  Node* osr_values[kMaxOsrValues];

  Node* NewOsrPhi(Node* loop, Node* incoming, int osr_value, Node* back1 = NULL,
                  Node* back2 = NULL, Node* back3 = NULL) {
    int count = 5;
    if (back3 == NULL) count = 4;
    if (back2 == NULL) count = 3;
    if (back1 == NULL) count = 2;
    CHECK_EQ(loop->InputCount(), count);
    CHECK_EQ(osr_loop_entry, loop->InputAt(1));

    Node* inputs[6];
    inputs[0] = incoming;
    inputs[1] = osr_values[osr_value];
    if (count > 2) inputs[2] = back1;
    if (count > 3) inputs[3] = back2;
    if (count > 4) inputs[4] = back3;
    inputs[count] = loop;
    return graph.NewNode(common.Phi(kMachAnyTagged, count), count + 1, inputs);
  }

  Node* NewOsrLoop(int num_backedges, Node* entry = NULL) {
    CHECK_LT(num_backedges, 4);
    CHECK_GE(num_backedges, 0);
    int count = 2 + num_backedges;
    if (entry == NULL) entry = osr_normal_entry;
    Node* inputs[5] = {entry, osr_loop_entry, self, self, self};

    Node* loop = graph.NewNode(common.Loop(count), count, inputs);
    for (int i = 0; i < num_backedges; i++) {
      loop->ReplaceInput(2 + i, loop);
    }

    return loop;
  }
};


TEST(Deconstruct_osr0) {
  OsrDeconstructorTester T(0);

  Node* loop = T.NewOsrLoop(1);

  T.graph.SetEnd(loop);

  OsrHelper helper(0, 0);
  helper.Deconstruct(&T.jsgraph, &T.common, T.main_zone());

  CheckInputs(loop, T.start, loop);
}


TEST(Deconstruct_osr1) {
  OsrDeconstructorTester T(1);

  Node* loop = T.NewOsrLoop(1);
  Node* osr_phi =
      T.NewOsrPhi(loop, T.jsgraph.OneConstant(), 0, T.jsgraph.ZeroConstant());

  Node* ret = T.graph.NewNode(T.common.Return(), osr_phi, T.start, loop);
  T.graph.SetEnd(ret);

  OsrHelper helper(0, 0);
  helper.Deconstruct(&T.jsgraph, &T.common, T.main_zone());

  CheckInputs(loop, T.start, loop);
  CheckInputs(osr_phi, T.osr_values[0], T.jsgraph.ZeroConstant(), loop);
  CheckInputs(ret, osr_phi, T.start, loop);
}


TEST(Deconstruct_osr_remove_prologue) {
  OsrDeconstructorTester T(1);
  Diamond d(&T.graph, &T.common, T.p0);
  d.Chain(T.osr_normal_entry);

  Node* loop = T.NewOsrLoop(1, d.merge);
  Node* osr_phi =
      T.NewOsrPhi(loop, T.jsgraph.OneConstant(), 0, T.jsgraph.ZeroConstant());

  Node* ret = T.graph.NewNode(T.common.Return(), osr_phi, T.start, loop);
  T.graph.SetEnd(ret);

  OsrHelper helper(0, 0);
  helper.Deconstruct(&T.jsgraph, &T.common, T.main_zone());

  CheckInputs(loop, T.start, loop);
  CheckInputs(osr_phi, T.osr_values[0], T.jsgraph.ZeroConstant(), loop);
  CheckInputs(ret, osr_phi, T.start, loop);

  // The control before the loop should have been removed.
  CHECK(d.branch->IsDead());
  CHECK(d.if_true->IsDead());
  CHECK(d.if_false->IsDead());
  CHECK(d.merge->IsDead());
}


TEST(Deconstruct_osr_with_body1) {
  OsrDeconstructorTester T(1);

  Node* loop = T.NewOsrLoop(1);

  Node* branch = T.graph.NewNode(T.common.Branch(), T.p0, loop);
  Node* if_true = T.graph.NewNode(T.common.IfTrue(), branch);
  Node* if_false = T.graph.NewNode(T.common.IfFalse(), branch);
  loop->ReplaceInput(2, if_true);

  Node* osr_phi =
      T.NewOsrPhi(loop, T.jsgraph.OneConstant(), 0, T.jsgraph.ZeroConstant());

  Node* ret = T.graph.NewNode(T.common.Return(), osr_phi, T.start, if_false);
  T.graph.SetEnd(ret);

  OsrHelper helper(0, 0);
  helper.Deconstruct(&T.jsgraph, &T.common, T.main_zone());

  CheckInputs(loop, T.start, if_true);
  CheckInputs(branch, T.p0, loop);
  CheckInputs(if_true, branch);
  CheckInputs(if_false, branch);
  CheckInputs(osr_phi, T.osr_values[0], T.jsgraph.ZeroConstant(), loop);
  CheckInputs(ret, osr_phi, T.start, if_false);
}


TEST(Deconstruct_osr_with_body2) {
  OsrDeconstructorTester T(1);

  Node* loop = T.NewOsrLoop(1);

  // Two chained branches in the the body of the loop.
  Node* branch1 = T.graph.NewNode(T.common.Branch(), T.p0, loop);
  Node* if_true1 = T.graph.NewNode(T.common.IfTrue(), branch1);
  Node* if_false1 = T.graph.NewNode(T.common.IfFalse(), branch1);

  Node* branch2 = T.graph.NewNode(T.common.Branch(), T.p0, if_true1);
  Node* if_true2 = T.graph.NewNode(T.common.IfTrue(), branch2);
  Node* if_false2 = T.graph.NewNode(T.common.IfFalse(), branch2);
  loop->ReplaceInput(2, if_true2);

  Node* osr_phi =
      T.NewOsrPhi(loop, T.jsgraph.OneConstant(), 0, T.jsgraph.ZeroConstant());

  Node* merge = T.graph.NewNode(T.common.Merge(2), if_false1, if_false2);
  Node* ret = T.graph.NewNode(T.common.Return(), osr_phi, T.start, merge);
  T.graph.SetEnd(ret);

  OsrHelper helper(0, 0);
  helper.Deconstruct(&T.jsgraph, &T.common, T.main_zone());

  CheckInputs(loop, T.start, if_true2);
  CheckInputs(branch1, T.p0, loop);
  CheckInputs(branch2, T.p0, if_true1);
  CheckInputs(if_true1, branch1);
  CheckInputs(if_false1, branch1);
  CheckInputs(if_true2, branch2);
  CheckInputs(if_false2, branch2);

  CheckInputs(osr_phi, T.osr_values[0], T.jsgraph.ZeroConstant(), loop);
  CheckInputs(ret, osr_phi, T.start, merge);
  CheckInputs(merge, if_false1, if_false2);
}


TEST(Deconstruct_osr_with_body3) {
  OsrDeconstructorTester T(1);

  Node* loop = T.NewOsrLoop(2);

  // Two branches that create two different backedges.
  Node* branch1 = T.graph.NewNode(T.common.Branch(), T.p0, loop);
  Node* if_true1 = T.graph.NewNode(T.common.IfTrue(), branch1);
  Node* if_false1 = T.graph.NewNode(T.common.IfFalse(), branch1);

  Node* branch2 = T.graph.NewNode(T.common.Branch(), T.p0, if_true1);
  Node* if_true2 = T.graph.NewNode(T.common.IfTrue(), branch2);
  Node* if_false2 = T.graph.NewNode(T.common.IfFalse(), branch2);
  loop->ReplaceInput(2, if_false1);
  loop->ReplaceInput(3, if_true2);

  Node* osr_phi =
      T.NewOsrPhi(loop, T.jsgraph.OneConstant(), 0, T.jsgraph.ZeroConstant(),
                  T.jsgraph.ZeroConstant());

  Node* ret = T.graph.NewNode(T.common.Return(), osr_phi, T.start, if_false2);
  T.graph.SetEnd(ret);

  OsrHelper helper(0, 0);
  helper.Deconstruct(&T.jsgraph, &T.common, T.main_zone());

  CheckInputs(loop, T.start, if_false1, if_true2);
  CheckInputs(branch1, T.p0, loop);
  CheckInputs(branch2, T.p0, if_true1);
  CheckInputs(if_true1, branch1);
  CheckInputs(if_false1, branch1);
  CheckInputs(if_true2, branch2);
  CheckInputs(if_false2, branch2);

  CheckInputs(osr_phi, T.osr_values[0], T.jsgraph.ZeroConstant(),
              T.jsgraph.ZeroConstant(), loop);
  CheckInputs(ret, osr_phi, T.start, if_false2);
}
