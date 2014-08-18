// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bits.h"
#include "src/compiler/machine-operator-reducer.h"
#include "test/compiler-unittests/graph-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

class MachineOperatorReducerTest : public GraphTest {
 public:
  explicit MachineOperatorReducerTest(int num_parameters = 2)
      : GraphTest(num_parameters), machine_(zone()) {}
  virtual ~MachineOperatorReducerTest() {}

 protected:
  Node* Parameter(int32_t index) {
    return graph()->NewNode(common()->Parameter(index), graph()->start());
  }
  Node* Int32Constant(int32_t value) {
    return graph()->NewNode(common()->Int32Constant(value));
  }

  Reduction Reduce(Node* node) {
    MachineOperatorReducer reducer(graph());
    return reducer.Reduce(node);
  }

  MachineOperatorBuilder* machine() { return &machine_; }

 private:
  MachineOperatorBuilder machine_;
};


namespace {

static const uint32_t kConstants[] = {
    0x00000000, 0x00000001, 0xffffffff, 0x1b09788b, 0x04c5fce8, 0xcc0de5bf,
    0x273a798e, 0x187937a3, 0xece3af83, 0x5495a16b, 0x0b668ecc, 0x11223344,
    0x0000009e, 0x00000043, 0x0000af73, 0x0000116b, 0x00658ecc, 0x002b3b4c,
    0x88776655, 0x70000000, 0x07200000, 0x7fffffff, 0x56123761, 0x7fffff00,
    0x761c4761, 0x80000000, 0x88888888, 0xa0000000, 0xdddddddd, 0xe0000000,
    0xeeeeeeee, 0xfffffffd, 0xf0000000, 0x007fffff, 0x003fffff, 0x001fffff,
    0x000fffff, 0x0007ffff, 0x0003ffff, 0x0001ffff, 0x0000ffff, 0x00007fff,
    0x00003fff, 0x00001fff, 0x00000fff, 0x000007ff, 0x000003ff, 0x000001ff};

}  // namespace


TEST_F(MachineOperatorReducerTest, ReduceToWord32RorWithParameters) {
  Node* value = Parameter(0);
  Node* shift = Parameter(1);
  Node* shl = graph()->NewNode(machine()->Word32Shl(), value, shift);
  Node* shr = graph()->NewNode(
      machine()->Word32Shr(), value,
      graph()->NewNode(machine()->Int32Sub(), Int32Constant(32), shift));

  // (x << y) | (x >> (32 - y)) => x ror y
  Node* node1 = graph()->NewNode(machine()->Word32Or(), shl, shr);
  Reduction reduction1 = Reduce(node1);
  EXPECT_TRUE(reduction1.Changed());
  EXPECT_EQ(reduction1.replacement(), node1);
  EXPECT_THAT(reduction1.replacement(), IsWord32Ror(value, shift));

  // (x >> (32 - y)) | (x << y) => x ror y
  Node* node2 = graph()->NewNode(machine()->Word32Or(), shr, shl);
  Reduction reduction2 = Reduce(node2);
  EXPECT_TRUE(reduction2.Changed());
  EXPECT_EQ(reduction2.replacement(), node2);
  EXPECT_THAT(reduction2.replacement(), IsWord32Ror(value, shift));
}


TEST_F(MachineOperatorReducerTest, ReduceToWord32RorWithConstant) {
  Node* value = Parameter(0);
  TRACED_FORRANGE(int32_t, k, 0, 31) {
    Node* shl =
        graph()->NewNode(machine()->Word32Shl(), value, Int32Constant(k));
    Node* shr =
        graph()->NewNode(machine()->Word32Shr(), value, Int32Constant(32 - k));

    // (x << K) | (x >> ((32 - K) - y)) => x ror K
    Node* node1 = graph()->NewNode(machine()->Word32Or(), shl, shr);
    Reduction reduction1 = Reduce(node1);
    EXPECT_TRUE(reduction1.Changed());
    EXPECT_EQ(reduction1.replacement(), node1);
    EXPECT_THAT(reduction1.replacement(),
                IsWord32Ror(value, IsInt32Constant(k)));

    // (x >> (32 - K)) | (x << K) => x ror K
    Node* node2 = graph()->NewNode(machine()->Word32Or(), shr, shl);
    Reduction reduction2 = Reduce(node2);
    EXPECT_TRUE(reduction2.Changed());
    EXPECT_EQ(reduction2.replacement(), node2);
    EXPECT_THAT(reduction2.replacement(),
                IsWord32Ror(value, IsInt32Constant(k)));
  }
}


TEST_F(MachineOperatorReducerTest, Word32RorWithZeroShift) {
  Node* value = Parameter(0);
  Node* node =
      graph()->NewNode(machine()->Word32Ror(), value, Int32Constant(0));
  Reduction reduction = Reduce(node);
  EXPECT_TRUE(reduction.Changed());
  EXPECT_EQ(reduction.replacement(), value);
}


TEST_F(MachineOperatorReducerTest, Word32RorWithConstants) {
  TRACED_FOREACH(int32_t, x, kConstants) {
    TRACED_FORRANGE(int32_t, y, 0, 31) {
      Node* node = graph()->NewNode(machine()->Word32Ror(), Int32Constant(x),
                                    Int32Constant(y));
      Reduction reduction = Reduce(node);
      EXPECT_TRUE(reduction.Changed());
      EXPECT_THAT(reduction.replacement(),
                  IsInt32Constant(base::bits::RotateRight32(x, y)));
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
