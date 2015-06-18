// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/control-reducer.h"
#include "src/compiler/diamond.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::_;
using testing::AllOf;
using testing::Capture;
using testing::CaptureEq;

namespace v8 {
namespace internal {
namespace compiler {

class ControlReducerTest : public GraphTest {
 public:
  ControlReducerTest()
      : GraphTest(1),
        machine_(zone()),
        javascript_(zone()),
        jsgraph_(isolate(), graph(), common(), &javascript_, &machine_) {}

 protected:
  MachineOperatorBuilder machine_;
  JSOperatorBuilder javascript_;
  JSGraph jsgraph_;

  void ReduceGraph(int max_phis_for_select = 0) {
    if (FLAG_trace_turbo_graph) {
      OFStream os(stdout);
      os << "-- Graph before control reduction" << std::endl;
      os << AsRPO(*graph());
    }
    ControlReducer::ReduceGraph(zone(), jsgraph(), max_phis_for_select);
    if (FLAG_trace_turbo_graph) {
      OFStream os(stdout);
      os << "-- Graph after control reduction" << std::endl;
      os << AsRPO(*graph());
    }
  }

  JSGraph* jsgraph() { return &jsgraph_; }
};


TEST_F(ControlReducerTest, SelectPhi) {
  Node* p0 = Parameter(0);
  const MachineType kType = kMachInt32;
  Diamond d(graph(), common(), p0);
  Node* phi =
      d.Phi(kType, jsgraph()->Int32Constant(1), jsgraph()->Int32Constant(2));

  Node* ret =
      graph()->NewNode(common()->Return(), phi, graph()->start(), d.merge);
  graph()->end()->ReplaceInput(0, ret);

  ReduceGraph(1);

  // Phi should be replaced with a select.
  EXPECT_THAT(graph()->end(),
              IsEnd(IsReturn(
                  IsSelect(kType, p0, IsInt32Constant(1), IsInt32Constant(2)),
                  graph()->start(), graph()->start())));
}


TEST_F(ControlReducerTest, SelectPhis_fail) {
  Node* p0 = Parameter(0);
  const MachineType kType = kMachInt32;
  Diamond d(graph(), common(), p0);
  Node* phi =
      d.Phi(kType, jsgraph()->Int32Constant(1), jsgraph()->Int32Constant(2));
  Node* phi2 =
      d.Phi(kType, jsgraph()->Int32Constant(11), jsgraph()->Int32Constant(22));
  USE(phi2);
  Node* ret =
      graph()->NewNode(common()->Return(), phi, graph()->start(), d.merge);
  graph()->end()->ReplaceInput(0, ret);

  ReduceGraph(1);

  // Diamond should not be replaced with a select (too many phis).
  EXPECT_THAT(ret, IsReturn(phi, graph()->start(), d.merge));
  EXPECT_THAT(graph()->end(), IsEnd(ret));
}


TEST_F(ControlReducerTest, SelectTwoPhis) {
  Node* p0 = Parameter(0);
  const MachineType kType = kMachInt32;
  Diamond d(graph(), common(), p0);
  Node* phi1 =
      d.Phi(kType, jsgraph()->Int32Constant(1), jsgraph()->Int32Constant(2));
  Node* phi2 =
      d.Phi(kType, jsgraph()->Int32Constant(2), jsgraph()->Int32Constant(3));
  MachineOperatorBuilder machine(zone());
  Node* add = graph()->NewNode(machine.Int32Add(), phi1, phi2);
  Node* ret =
      graph()->NewNode(common()->Return(), add, graph()->start(), d.merge);
  graph()->end()->ReplaceInput(0, ret);

  ReduceGraph(2);

  // Phis should be replaced with two selects.
  EXPECT_THAT(
      ret,
      IsReturn(IsInt32Add(
                   IsSelect(kType, p0, IsInt32Constant(1), IsInt32Constant(2)),
                   IsSelect(kType, p0, IsInt32Constant(2), IsInt32Constant(3))),
               graph()->start(), graph()->start()));
  EXPECT_THAT(graph()->end(), IsEnd(ret));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
