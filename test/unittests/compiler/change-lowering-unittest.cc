// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-stubs.h"
#include "src/compiler/change-lowering.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "test/unittests/compiler/compiler-test-utils.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::_;
using testing::AllOf;
using testing::BitEq;
using testing::Capture;
using testing::CaptureEq;

namespace v8 {
namespace internal {
namespace compiler {

class ChangeLoweringTest : public TypedGraphTest {
 public:
  ChangeLoweringTest() : simplified_(zone()) {}

  virtual MachineRepresentation WordRepresentation() const = 0;

 protected:
  bool Is32() const {
    return WordRepresentation() == MachineRepresentation::kWord32;
  }
  bool Is64() const {
    return WordRepresentation() == MachineRepresentation::kWord64;
  }

  Reduction Reduce(Node* node) {
    GraphReducer graph_reducer(zone(), graph());
    MachineOperatorBuilder machine(zone(), WordRepresentation());
    JSOperatorBuilder javascript(zone());
    JSGraph jsgraph(isolate(), graph(), common(), &javascript, nullptr,
                    &machine);
    ChangeLowering reducer(&graph_reducer, &jsgraph);
    return reducer.Reduce(node);
  }

  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

  Matcher<Node*> IsAllocateHeapNumber(const Matcher<Node*>& effect_matcher,
                                      const Matcher<Node*>& control_matcher) {
    return IsCall(
        _, IsHeapConstant(AllocateHeapNumberStub(isolate()).GetCode()),
        IsNumberConstant(BitEq(0.0)), effect_matcher, control_matcher);
  }
  Matcher<Node*> IsChangeInt32ToSmi(const Matcher<Node*>& value_matcher) {
    return Is64() ? IsWord64Shl(IsChangeInt32ToInt64(value_matcher),
                                IsSmiShiftBitsConstant())
                  : IsWord32Shl(value_matcher, IsSmiShiftBitsConstant());
  }
  Matcher<Node*> IsChangeSmiToInt32(const Matcher<Node*>& value_matcher) {
    return Is64() ? IsTruncateInt64ToInt32(
                        IsWord64Sar(value_matcher, IsSmiShiftBitsConstant()))
                  : IsWord32Sar(value_matcher, IsSmiShiftBitsConstant());
  }
  Matcher<Node*> IsChangeUint32ToSmi(const Matcher<Node*>& value_matcher) {
    return Is64() ? IsWord64Shl(IsChangeUint32ToUint64(value_matcher),
                                IsSmiShiftBitsConstant())
                  : IsWord32Shl(value_matcher, IsSmiShiftBitsConstant());
  }
  Matcher<Node*> IsLoadHeapNumber(const Matcher<Node*>& value_matcher,
                                  const Matcher<Node*>& control_matcher) {
    return IsLoad(MachineType::Float64(), value_matcher,
                  IsIntPtrConstant(HeapNumber::kValueOffset - kHeapObjectTag),
                  graph()->start(), control_matcher);
  }
  Matcher<Node*> IsIntPtrConstant(int value) {
    return Is32() ? IsInt32Constant(value) : IsInt64Constant(value);
  }
  Matcher<Node*> IsSmiShiftBitsConstant() {
    return IsIntPtrConstant(kSmiShiftSize + kSmiTagSize);
  }
  Matcher<Node*> IsWordEqual(const Matcher<Node*>& lhs_matcher,
                             const Matcher<Node*>& rhs_matcher) {
    return Is32() ? IsWord32Equal(lhs_matcher, rhs_matcher)
                  : IsWord64Equal(lhs_matcher, rhs_matcher);
  }

 private:
  SimplifiedOperatorBuilder simplified_;
};


// -----------------------------------------------------------------------------
// Common.


class ChangeLoweringCommonTest
    : public ChangeLoweringTest,
      public ::testing::WithParamInterface<MachineRepresentation> {
 public:
  ~ChangeLoweringCommonTest() override {}

  MachineRepresentation WordRepresentation() const final { return GetParam(); }
};

TARGET_TEST_P(ChangeLoweringCommonTest, StoreFieldSmi) {
  FieldAccess access = {
      kTaggedBase, FixedArrayBase::kHeaderSize, Handle<Name>::null(),
      Type::Any(), MachineType::AnyTagged(),    kNoWriteBarrier};
  Node* p0 = Parameter(Type::TaggedPointer());
  Node* p1 = Parameter(Type::TaggedSigned());
  Node* store = graph()->NewNode(simplified()->StoreField(access), p0, p1,
                                 graph()->start(), graph()->start());
  Reduction r = Reduce(store);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(),
              IsStore(StoreRepresentation(MachineRepresentation::kTagged,
                                          kNoWriteBarrier),
                      p0, IsIntPtrConstant(access.offset - access.tag()), p1,
                      graph()->start(), graph()->start()));
}


TARGET_TEST_P(ChangeLoweringCommonTest, StoreFieldTagged) {
  FieldAccess access = {
      kTaggedBase, FixedArrayBase::kHeaderSize, Handle<Name>::null(),
      Type::Any(), MachineType::AnyTagged(),    kFullWriteBarrier};
  Node* p0 = Parameter(Type::TaggedPointer());
  Node* p1 = Parameter(Type::Tagged());
  Node* store = graph()->NewNode(simplified()->StoreField(access), p0, p1,
                                 graph()->start(), graph()->start());
  Reduction r = Reduce(store);

  ASSERT_TRUE(r.Changed());
  EXPECT_THAT(r.replacement(),
              IsStore(StoreRepresentation(MachineRepresentation::kTagged,
                                          kFullWriteBarrier),
                      p0, IsIntPtrConstant(access.offset - access.tag()), p1,
                      graph()->start(), graph()->start()));
}


TARGET_TEST_P(ChangeLoweringCommonTest, LoadField) {
  FieldAccess access = {
      kTaggedBase, FixedArrayBase::kHeaderSize, Handle<Name>::null(),
      Type::Any(), MachineType::AnyTagged(),    kNoWriteBarrier};
  Node* p0 = Parameter(Type::TaggedPointer());
  Node* load = graph()->NewNode(simplified()->LoadField(access), p0,
                                graph()->start(), graph()->start());
  Reduction r = Reduce(load);

  ASSERT_TRUE(r.Changed());
  Matcher<Node*> index_match = IsIntPtrConstant(access.offset - access.tag());
  EXPECT_THAT(r.replacement(),
              IsLoad(MachineType::AnyTagged(), p0,
                     IsIntPtrConstant(access.offset - access.tag()),
                     graph()->start(), graph()->start()));
}


TARGET_TEST_P(ChangeLoweringCommonTest, StoreElementTagged) {
  ElementAccess access = {kTaggedBase, FixedArrayBase::kHeaderSize, Type::Any(),
                          MachineType::AnyTagged(), kFullWriteBarrier};
  Node* p0 = Parameter(Type::TaggedPointer());
  Node* p1 = Parameter(Type::Signed32());
  Node* p2 = Parameter(Type::Tagged());
  Node* store = graph()->NewNode(simplified()->StoreElement(access), p0, p1, p2,
                                 graph()->start(), graph()->start());
  Reduction r = Reduce(store);

  const int element_size_shift =
      ElementSizeLog2Of(access.machine_type.representation());
  ASSERT_TRUE(r.Changed());
  Matcher<Node*> index_match =
      IsInt32Add(IsWord32Shl(p1, IsInt32Constant(element_size_shift)),
                 IsInt32Constant(access.header_size - access.tag()));
  if (!Is32()) {
    index_match = IsChangeUint32ToUint64(index_match);
  }

  EXPECT_THAT(r.replacement(),
              IsStore(StoreRepresentation(MachineRepresentation::kTagged,
                                          kFullWriteBarrier),
                      p0, index_match, p2, graph()->start(), graph()->start()));
}


TARGET_TEST_P(ChangeLoweringCommonTest, StoreElementUint8) {
  ElementAccess access = {kTaggedBase, FixedArrayBase::kHeaderSize,
                          Type::Signed32(), MachineType::Uint8(),
                          kNoWriteBarrier};
  Node* p0 = Parameter(Type::TaggedPointer());
  Node* p1 = Parameter(Type::Signed32());
  Node* p2 = Parameter(Type::Signed32());
  Node* store = graph()->NewNode(simplified()->StoreElement(access), p0, p1, p2,
                                 graph()->start(), graph()->start());
  Reduction r = Reduce(store);

  ASSERT_TRUE(r.Changed());
  Matcher<Node*> index_match =
      IsInt32Add(p1, IsInt32Constant(access.header_size - access.tag()));
  if (!Is32()) {
    index_match = IsChangeUint32ToUint64(index_match);
  }

  EXPECT_THAT(r.replacement(),
              IsStore(StoreRepresentation(MachineRepresentation::kWord8,
                                          kNoWriteBarrier),
                      p0, index_match, p2, graph()->start(), graph()->start()));
}


TARGET_TEST_P(ChangeLoweringCommonTest, LoadElementTagged) {
  ElementAccess access = {kTaggedBase, FixedArrayBase::kHeaderSize, Type::Any(),
                          MachineType::AnyTagged(), kNoWriteBarrier};
  Node* p0 = Parameter(Type::TaggedPointer());
  Node* p1 = Parameter(Type::Signed32());
  Node* load = graph()->NewNode(simplified()->LoadElement(access), p0, p1,
                                graph()->start(), graph()->start());
  Reduction r = Reduce(load);

  const int element_size_shift =
      ElementSizeLog2Of(access.machine_type.representation());
  ASSERT_TRUE(r.Changed());
  Matcher<Node*> index_match =
      IsInt32Add(IsWord32Shl(p1, IsInt32Constant(element_size_shift)),
                 IsInt32Constant(access.header_size - access.tag()));
  if (!Is32()) {
    index_match = IsChangeUint32ToUint64(index_match);
  }

  EXPECT_THAT(r.replacement(), IsLoad(MachineType::AnyTagged(), p0, index_match,
                                      graph()->start(), graph()->start()));
}


TARGET_TEST_P(ChangeLoweringCommonTest, LoadElementInt8) {
  ElementAccess access = {kTaggedBase, FixedArrayBase::kHeaderSize,
                          Type::Signed32(), MachineType::Int8(),
                          kNoWriteBarrier};
  Node* p0 = Parameter(Type::TaggedPointer());
  Node* p1 = Parameter(Type::Signed32());
  Node* load = graph()->NewNode(simplified()->LoadElement(access), p0, p1,
                                graph()->start(), graph()->start());
  Reduction r = Reduce(load);

  ASSERT_TRUE(r.Changed());
  Matcher<Node*> index_match =
      IsInt32Add(p1, IsInt32Constant(access.header_size - access.tag()));
  if (!Is32()) {
    index_match = IsChangeUint32ToUint64(index_match);
  }

  EXPECT_THAT(r.replacement(), IsLoad(MachineType::Int8(), p0, index_match,
                                      graph()->start(), graph()->start()));
}


TARGET_TEST_P(ChangeLoweringCommonTest, Allocate) {
  Node* p0 = Parameter(Type::Signed32());
  Node* alloc = graph()->NewNode(simplified()->Allocate(TENURED), p0,
                                 graph()->start(), graph()->start());
  Reduction r = Reduce(alloc);

  // Only check that we lowered, but do not specify the exact form since
  // this is subject to change.
  ASSERT_TRUE(r.Changed());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
