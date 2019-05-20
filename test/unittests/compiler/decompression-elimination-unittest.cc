// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-elimination.h"
#include "src/compiler/simplified-operator.h"
#include "test/unittests/compiler/graph-reducer-unittest.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"

using testing::StrictMock;

namespace v8 {
namespace internal {
namespace compiler {

class DecompressionEliminationTest : public GraphTest {
 public:
  DecompressionEliminationTest()
      : GraphTest(),
        machine_(zone(), MachineType::PointerRepresentation(),
                 MachineOperatorBuilder::kNoFlags),
        simplified_(zone()) {}
  ~DecompressionEliminationTest() override = default;

 protected:
  Reduction Reduce(Node* node) {
    StrictMock<MockAdvancedReducerEditor> editor;
    DecompressionElimination decompression_elimination(&editor, graph(),
                                                       machine());
    return decompression_elimination.Reduce(node);
  }
  MachineOperatorBuilder* machine() { return &machine_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

 private:
  MachineOperatorBuilder machine_;
  SimplifiedOperatorBuilder simplified_;
};

// -----------------------------------------------------------------------------
// Direct Decompression & Compression

TEST_F(DecompressionEliminationTest, BasicDecompressionCompression) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::AnyTagged(), kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Node* changeToTagged =
      graph()->NewNode(machine()->ChangeCompressedToTagged(), load);
  Node* changeToCompressed =
      graph()->NewNode(machine()->ChangeTaggedToCompressed(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(access), object, index,
                            changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(DecompressionEliminationTest, BasicDecompressionCompressionSigned) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::TaggedSigned(), kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Node* changeToTagged =
      graph()->NewNode(machine()->ChangeCompressedSignedToTaggedSigned(), load);
  Node* changeToCompressed = graph()->NewNode(
      machine()->ChangeTaggedSignedToCompressedSigned(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(access), object, index,
                            changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(DecompressionEliminationTest, BasicDecompressionCompressionPointer) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const access = {kTaggedBase, kTaggedSize, Type::Any(),
                                MachineType::TaggedPointer(), kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(access), object,
                                index, effect, control);
  Node* changeToTagged = graph()->NewNode(
      machine()->ChangeCompressedPointerToTaggedPointer(), load);
  Node* changeToCompressed = graph()->NewNode(
      machine()->ChangeTaggedPointerToCompressedPointer(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(access), object, index,
                            changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

// -----------------------------------------------------------------------------
// Direct Decompression & Compression - border cases

// For example, if we are lowering a CheckedCompressedToTaggedPointer in the
// effect linearization phase we will change that to
// ChangeCompressedPointerToTaggedPointer. Then, we might end up with a chain of
// Parent <- ChangeCompressedPointerToTaggedPointer <- ChangeTaggedToCompressed
// <- Child.
// Similarly, we have cases with Signed instead of pointer.
// The following border case tests will test that the functionality is robust
// enough to handle that.

TEST_F(DecompressionEliminationTest,
       BasicDecompressionCompressionBorderCaseSigned) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const loadAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                    MachineType::AnyTagged(), kNoWriteBarrier};
  ElementAccess const storeAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                     MachineType::TaggedSigned(),
                                     kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(loadAccess), object,
                                index, effect, control);
  Node* changeToTagged =
      graph()->NewNode(machine()->ChangeCompressedSignedToTaggedSigned(), load);
  Node* changeToCompressed =
      graph()->NewNode(machine()->ChangeTaggedToCompressed(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(storeAccess), object,
                            index, changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

TEST_F(DecompressionEliminationTest,
       BasicDecompressionCompressionBorderCasePointer) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const loadAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                    MachineType::AnyTagged(), kNoWriteBarrier};
  ElementAccess const storeAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                     MachineType::TaggedPointer(),
                                     kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(loadAccess), object,
                                index, effect, control);
  Node* changeToTagged = graph()->NewNode(
      machine()->ChangeCompressedPointerToTaggedPointer(), load);
  Node* changeToCompressed =
      graph()->NewNode(machine()->ChangeTaggedToCompressed(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(storeAccess), object,
                            index, changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

// We also have cases of ChangeCompressedToTagged <-
// ChangeTaggedPointerToCompressedPointer, where the
// ChangeTaggedPointerToCompressedPointer was introduced while lowering a
// NewConsString on effect control linearizer

TEST_F(DecompressionEliminationTest,
       BasicDecompressionCompressionBorderCasePointerDecompression) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);
  ElementAccess const loadAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                    MachineType::TaggedPointer(),
                                    kNoWriteBarrier};
  ElementAccess const storeAccess = {kTaggedBase, kTaggedSize, Type::Any(),
                                     MachineType::AnyTagged(), kNoWriteBarrier};

  // Create the graph
  Node* load = graph()->NewNode(simplified()->LoadElement(loadAccess), object,
                                index, effect, control);
  Node* changeToTagged = graph()->NewNode(
      machine()->ChangeCompressedPointerToTaggedPointer(), load);
  Node* changeToCompressed =
      graph()->NewNode(machine()->ChangeTaggedToCompressed(), changeToTagged);
  effect = graph()->NewNode(simplified()->StoreElement(storeAccess), object,
                            index, changeToCompressed, effect, control);

  // Reduce
  Reduction r = Reduce(changeToCompressed);
  ASSERT_TRUE(r.Changed());
  EXPECT_EQ(load, r.replacement());
}

// -----------------------------------------------------------------------------
// Comparison of two decompressions

TEST_F(DecompressionEliminationTest, TwoDecompressionComparison) {
  // Skip test if pointer compression is not enabled
  if (!COMPRESS_POINTERS_BOOL) {
    return;
  }

  // Define variables
  Node* const control = graph()->start();
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* index = Parameter(Type::UnsignedSmall(), 1);

  const Operator* DecompressionOps[] = {
      machine()->ChangeCompressedToTagged(),
      machine()->ChangeCompressedSignedToTaggedSigned(),
      machine()->ChangeCompressedPointerToTaggedPointer()};

  const ElementAccess ElementAccesses[] = {
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::AnyTagged(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedSigned(),
       kNoWriteBarrier},
      {kTaggedBase, kTaggedSize, Type::Any(), MachineType::TaggedPointer(),
       kNoWriteBarrier}};

  ASSERT_EQ(arraysize(DecompressionOps), arraysize(ElementAccesses));

  // For every decompression (lhs)
  for (size_t j = 0; j < arraysize(DecompressionOps); ++j) {
    // For every decompression (rhs)
    for (size_t k = 0; k < arraysize(DecompressionOps); ++k) {
      // Create the graph
      Node* load1 =
          graph()->NewNode(simplified()->LoadElement(ElementAccesses[j]),
                           object, index, effect, control);
      Node* changeToTagged1 = graph()->NewNode(DecompressionOps[j], load1);
      Node* load2 =
          graph()->NewNode(simplified()->LoadElement(ElementAccesses[k]),
                           object, index, effect, control);
      Node* changeToTagged2 = graph()->NewNode(DecompressionOps[j], load2);
      Node* comparison = graph()->NewNode(machine()->Word64Equal(),
                                          changeToTagged1, changeToTagged2);
      // Reduce
      Reduction r = Reduce(comparison);
      ASSERT_TRUE(r.Changed());
      EXPECT_EQ(r.replacement()->opcode(), IrOpcode::kWord32Equal);
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
