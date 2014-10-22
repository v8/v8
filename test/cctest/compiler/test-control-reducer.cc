// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"
#include "test/cctest/cctest.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/control-reducer.h"
#include "src/compiler/graph-inl.h"
#include "src/compiler/js-graph.h"

using namespace v8::internal;
using namespace v8::internal::compiler;

class CTrimTester : HandleAndZoneScope {
 public:
  CTrimTester()
      : isolate(main_isolate()),
        common(main_zone()),
        graph(main_zone()),
        jsgraph(&graph, &common, NULL, NULL),
        start(graph.NewNode(common.Start(1))),
        p0(graph.NewNode(common.Parameter(0), start)),
        one(jsgraph.OneConstant()),
        half(jsgraph.Constant(0.5)) {
    graph.SetEnd(start);
    graph.SetStart(start);
  }

  Isolate* isolate;
  CommonOperatorBuilder common;
  Graph graph;
  JSGraph jsgraph;
  Node* start;
  Node* p0;
  Node* one;
  Node* half;

  void Trim() { ControlReducer::TrimGraph(main_zone(), &jsgraph); }
};


bool IsUsedBy(Node* a, Node* b) {
  for (UseIter i = a->uses().begin(); i != a->uses().end(); ++i) {
    if (b == *i) return true;
  }
  return false;
}


TEST(Trim1_live) {
  CTrimTester T;
  CHECK(IsUsedBy(T.start, T.p0));
  T.graph.SetEnd(T.p0);
  T.Trim();
  CHECK(IsUsedBy(T.start, T.p0));
  CHECK_EQ(T.start, T.p0->InputAt(0));
}


TEST(Trim1_dead) {
  CTrimTester T;
  CHECK(IsUsedBy(T.start, T.p0));
  T.Trim();
  CHECK(!IsUsedBy(T.start, T.p0));
  CHECK_EQ(NULL, T.p0->InputAt(0));
}


TEST(Trim2_live) {
  CTrimTester T;
  Node* phi =
      T.graph.NewNode(T.common.Phi(kMachAnyTagged, 2), T.one, T.half, T.start);
  CHECK(IsUsedBy(T.one, phi));
  CHECK(IsUsedBy(T.half, phi));
  CHECK(IsUsedBy(T.start, phi));
  T.graph.SetEnd(phi);
  T.Trim();
  CHECK(IsUsedBy(T.one, phi));
  CHECK(IsUsedBy(T.half, phi));
  CHECK(IsUsedBy(T.start, phi));
  CHECK_EQ(T.one, phi->InputAt(0));
  CHECK_EQ(T.half, phi->InputAt(1));
  CHECK_EQ(T.start, phi->InputAt(2));
}


TEST(Trim2_dead) {
  CTrimTester T;
  Node* phi =
      T.graph.NewNode(T.common.Phi(kMachAnyTagged, 2), T.one, T.half, T.start);
  CHECK(IsUsedBy(T.one, phi));
  CHECK(IsUsedBy(T.half, phi));
  CHECK(IsUsedBy(T.start, phi));
  T.Trim();
  CHECK(!IsUsedBy(T.one, phi));
  CHECK(!IsUsedBy(T.half, phi));
  CHECK(!IsUsedBy(T.start, phi));
  CHECK_EQ(NULL, phi->InputAt(0));
  CHECK_EQ(NULL, phi->InputAt(1));
  CHECK_EQ(NULL, phi->InputAt(2));
}


TEST(Trim_chain1) {
  CTrimTester T;
  const int kDepth = 15;
  Node* live[kDepth];
  Node* dead[kDepth];
  Node* end = T.start;
  for (int i = 0; i < kDepth; i++) {
    live[i] = end = T.graph.NewNode(T.common.Merge(1), end);
    dead[i] = T.graph.NewNode(T.common.Merge(1), end);
  }
  // end         -> live[last] ->  live[last-1] -> ... -> start
  //     dead[last] ^ dead[last-1] ^ ...                  ^
  T.graph.SetEnd(end);
  T.Trim();
  for (int i = 0; i < kDepth; i++) {
    CHECK(!IsUsedBy(live[i], dead[i]));
    CHECK_EQ(NULL, dead[i]->InputAt(0));
    CHECK_EQ(i == 0 ? T.start : live[i - 1], live[i]->InputAt(0));
  }
}


TEST(Trim_chain2) {
  CTrimTester T;
  const int kDepth = 15;
  Node* live[kDepth];
  Node* dead[kDepth];
  Node* l = T.start;
  Node* d = T.start;
  for (int i = 0; i < kDepth; i++) {
    live[i] = l = T.graph.NewNode(T.common.Merge(1), l);
    dead[i] = d = T.graph.NewNode(T.common.Merge(1), d);
  }
  // end -> live[last] -> live[last-1] -> ... -> start
  //        dead[last] -> dead[last-1] -> ... -> start
  T.graph.SetEnd(l);
  T.Trim();
  CHECK(!IsUsedBy(T.start, dead[0]));
  for (int i = 0; i < kDepth; i++) {
    CHECK_EQ(i == 0 ? NULL : dead[i - 1], dead[i]->InputAt(0));
    CHECK_EQ(i == 0 ? T.start : live[i - 1], live[i]->InputAt(0));
  }
}


TEST(Trim_cycle1) {
  CTrimTester T;
  Node* loop = T.graph.NewNode(T.common.Loop(1), T.start, T.start);
  loop->ReplaceInput(1, loop);
  Node* end = T.graph.NewNode(T.common.End(), loop);
  T.graph.SetEnd(end);

  CHECK(IsUsedBy(T.start, loop));
  CHECK(IsUsedBy(loop, end));
  CHECK(IsUsedBy(loop, loop));

  T.Trim();

  // nothing should have happened to the loop itself.
  CHECK(IsUsedBy(T.start, loop));
  CHECK(IsUsedBy(loop, end));
  CHECK(IsUsedBy(loop, loop));
  CHECK_EQ(T.start, loop->InputAt(0));
  CHECK_EQ(loop, loop->InputAt(1));
  CHECK_EQ(loop, end->InputAt(0));
}


TEST(Trim_cycle2) {
  CTrimTester T;
  Node* loop = T.graph.NewNode(T.common.Loop(2), T.start, T.start);
  loop->ReplaceInput(1, loop);
  Node* end = T.graph.NewNode(T.common.End(), loop);
  Node* phi =
      T.graph.NewNode(T.common.Phi(kMachAnyTagged, 2), T.one, T.half, loop);
  T.graph.SetEnd(end);

  CHECK(IsUsedBy(T.start, loop));
  CHECK(IsUsedBy(loop, end));
  CHECK(IsUsedBy(loop, loop));
  CHECK(IsUsedBy(loop, phi));
  CHECK(IsUsedBy(T.one, phi));
  CHECK(IsUsedBy(T.half, phi));

  T.Trim();

  // nothing should have happened to the loop itself.
  CHECK(IsUsedBy(T.start, loop));
  CHECK(IsUsedBy(loop, end));
  CHECK(IsUsedBy(loop, loop));
  CHECK_EQ(T.start, loop->InputAt(0));
  CHECK_EQ(loop, loop->InputAt(1));
  CHECK_EQ(loop, end->InputAt(0));

  // phi should have been trimmed away.
  CHECK(!IsUsedBy(loop, phi));
  CHECK(!IsUsedBy(T.one, phi));
  CHECK(!IsUsedBy(T.half, phi));
  CHECK_EQ(NULL, phi->InputAt(0));
  CHECK_EQ(NULL, phi->InputAt(1));
  CHECK_EQ(NULL, phi->InputAt(2));
}


void CheckTrimConstant(CTrimTester* T, Node* k) {
  Node* phi = T->graph.NewNode(T->common.Phi(kMachInt32, 1), k, T->start);
  CHECK(IsUsedBy(k, phi));
  T->Trim();
  CHECK(!IsUsedBy(k, phi));
  CHECK_EQ(NULL, phi->InputAt(0));
  CHECK_EQ(NULL, phi->InputAt(1));
}


TEST(Trim_constants) {
  CTrimTester T;
  int32_t int32_constants[] = {
      0, -1,  -2,  2,  2,  3,  3,  4,  4,  5,  5,  4,  5,  6, 6, 7, 8, 7, 8, 9,
      0, -11, -12, 12, 12, 13, 13, 14, 14, 15, 15, 14, 15, 6, 6, 7, 8, 7, 8, 9};

  for (size_t i = 0; i < arraysize(int32_constants); i++) {
    CheckTrimConstant(&T, T.jsgraph.Int32Constant(int32_constants[i]));
    CheckTrimConstant(&T, T.jsgraph.Float64Constant(int32_constants[i]));
    CheckTrimConstant(&T, T.jsgraph.Constant(int32_constants[i]));
  }

  Node* other_constants[] = {
      T.jsgraph.UndefinedConstant(), T.jsgraph.TheHoleConstant(),
      T.jsgraph.TrueConstant(),      T.jsgraph.FalseConstant(),
      T.jsgraph.NullConstant(),      T.jsgraph.ZeroConstant(),
      T.jsgraph.OneConstant(),       T.jsgraph.NaNConstant(),
      T.jsgraph.Constant(21),        T.jsgraph.Constant(22.2)};

  for (size_t i = 0; i < arraysize(other_constants); i++) {
    CheckTrimConstant(&T, other_constants[i]);
  }
}
