// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/machine-type.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/revectorizer.h"
#include "src/compiler/wasm-compiler.h"
#include "src/wasm/wasm-module.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::AllOf;
using testing::Capture;
using testing::CaptureEq;

namespace v8 {
namespace internal {
namespace compiler {

class RevecTest : public TestWithIsolateAndZone {
 public:
  RevecTest()
      : TestWithIsolateAndZone(kCompressGraphZone),
        graph_(zone()),
        common_(zone()),
        machine_(zone(), MachineRepresentation::kWord64,
                 MachineOperatorBuilder::Flag::kAllOptionalOps),
        mcgraph_(&graph_, &common_, &machine_) {}

  Graph* graph() { return &graph_; }
  CommonOperatorBuilder* common() { return &common_; }
  MachineOperatorBuilder* machine() { return &machine_; }
  MachineGraph* mcgraph() { return &mcgraph_; }

 private:
  Graph graph_;
  CommonOperatorBuilder common_;
  MachineOperatorBuilder machine_;
  MachineGraph mcgraph_;
};

// Create a graph which add two 256 bit vectors(a, b), store the result in c:
// simd128 *a,*b,*c;
// *c = *a + *b;
// *(c+1) = *(a+1) + *(b+1);
// In Revectorization, two simd 128 nodes can be combined into one 256 node:
// simd256 *d, *e, *f;
// *f = *d + *e;
TEST_F(RevecTest, F32x8Add) {
  Node* start = graph()->NewNode(common()->Start(5));
  graph()->SetStart(start);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* sixteen = graph()->NewNode(common()->Int64Constant(16));
  // offset of memory start field in WASM instance object.
  Node* offset = graph()->NewNode(common()->Int64Constant(23));

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* p2 = graph()->NewNode(common()->Parameter(2), start);
  Node* p3 = graph()->NewNode(common()->Parameter(3), start);

  StoreRepresentation store_rep(MachineRepresentation::kSimd128,
                                WriteBarrierKind::kNoWriteBarrier);
  LoadRepresentation load_rep(MachineType::Simd128());
  Node* load0 = graph()->NewNode(machine()->Load(MachineType::Int64()), p0,
                                 offset, start, start);
  Node* mem_buffer1 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_buffer2 = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* mem_store = graph()->NewNode(machine()->Int64Add(), load0, sixteen);
  Node* load1 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p1,
                                 load0, start);
  Node* load2 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer1, p1, load1, start);
  Node* load3 = graph()->NewNode(machine()->ProtectedLoad(load_rep), load0, p2,
                                 load2, start);
  Node* load4 = graph()->NewNode(machine()->ProtectedLoad(load_rep),
                                 mem_buffer2, p2, load3, start);
  Node* add1 = graph()->NewNode(machine()->F32x4Add(), load1, load3);
  Node* add2 = graph()->NewNode(machine()->F32x4Add(), load2, load4);
  Node* store1 = graph()->NewNode(machine()->Store(store_rep), load0, p3, add1,
                                  load4, start);
  Node* store2 = graph()->NewNode(machine()->Store(store_rep), mem_store, p3,
                                  add2, store1, start);
  Node* ret = graph()->NewNode(common()->Return(0), zero, store2, start);
  Node* end = graph()->NewNode(common()->End(1), ret);
  graph()->SetEnd(end);

  graph()->RecordSimdStore(store1);
  graph()->RecordSimdStore(store2);
  graph()->SetSimd(true);

  // Test whether the graph can be revectorized
  Revectorizer revec(zone(), graph(), mcgraph());
  EXPECT_TRUE(revec.TryRevectorize(nullptr));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
