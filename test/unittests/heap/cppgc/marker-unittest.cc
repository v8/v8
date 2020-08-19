// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/marker.h"

#include "include/cppgc/allocation.h"
#include "include/cppgc/internal/pointer-policies.h"
#include "include/cppgc/member.h"
#include "include/cppgc/persistent.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/marking-visitor.h"
#include "src/heap/cppgc/stats-collector.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

class MarkerTest : public testing::TestWithHeap {
 public:
  using MarkingConfig = Marker::MarkingConfig;

  void DoMarking(MarkingConfig::StackState stack_state) {
    const MarkingConfig config = {MarkingConfig::CollectionType::kMajor,
                                  stack_state};
    auto* heap = Heap::From(GetHeap());
    Marker marker(*heap, GetPlatformHandle().get(), config);
    marker.StartMarking();
    marker.FinishMarking(stack_state);
    marker.ProcessWeakness();
    // Pretend do finish sweeping as StatsCollector verifies that Notify*
    // methods are called in the right order.
    heap->stats_collector()->NotifySweepingCompleted();
  }
};

class GCed : public GarbageCollected<GCed> {
 public:
  void SetChild(GCed* child) { child_ = child; }
  void SetWeakChild(GCed* child) { weak_child_ = child; }
  GCed* child() const { return child_.Get(); }
  GCed* weak_child() const { return weak_child_.Get(); }
  void Trace(cppgc::Visitor* visitor) const {
    visitor->Trace(child_);
    visitor->Trace(weak_child_);
  }

 private:
  Member<GCed> child_;
  WeakMember<GCed> weak_child_;
};

template <typename T>
V8_NOINLINE T access(volatile const T& t) {
  return t;
}

}  // namespace

TEST_F(MarkerTest, PersistentIsMarked) {
  Persistent<GCed> object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  HeapObjectHeader& header = HeapObjectHeader::FromPayload(object);
  EXPECT_FALSE(header.IsMarked());
  DoMarking(MarkingConfig::StackState::kNoHeapPointers);
  EXPECT_TRUE(header.IsMarked());
}

TEST_F(MarkerTest, ReachableMemberIsMarked) {
  Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
  parent->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  HeapObjectHeader& header = HeapObjectHeader::FromPayload(parent->child());
  EXPECT_FALSE(header.IsMarked());
  DoMarking(MarkingConfig::StackState::kNoHeapPointers);
  EXPECT_TRUE(header.IsMarked());
}

TEST_F(MarkerTest, UnreachableMemberIsNotMarked) {
  Member<GCed> object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  HeapObjectHeader& header = HeapObjectHeader::FromPayload(object);
  EXPECT_FALSE(header.IsMarked());
  DoMarking(MarkingConfig::StackState::kNoHeapPointers);
  EXPECT_FALSE(header.IsMarked());
}

TEST_F(MarkerTest, ObjectReachableFromStackIsMarked) {
  GCed* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  EXPECT_FALSE(HeapObjectHeader::FromPayload(object).IsMarked());
  DoMarking(MarkingConfig::StackState::kMayContainHeapPointers);
  EXPECT_TRUE(HeapObjectHeader::FromPayload(object).IsMarked());
  access(object);
}

TEST_F(MarkerTest, ObjectReachableOnlyFromStackIsNotMarkedIfStackIsEmpty) {
  GCed* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
  HeapObjectHeader& header = HeapObjectHeader::FromPayload(object);
  EXPECT_FALSE(header.IsMarked());
  DoMarking(MarkingConfig::StackState::kNoHeapPointers);
  EXPECT_FALSE(header.IsMarked());
  access(object);
}

TEST_F(MarkerTest, WeakReferenceToUnreachableObjectIsCleared) {
  {
    WeakPersistent<GCed> weak_object =
        MakeGarbageCollected<GCed>(GetAllocationHandle());
    EXPECT_TRUE(weak_object);
    DoMarking(MarkingConfig::StackState::kNoHeapPointers);
    EXPECT_FALSE(weak_object);
  }
  {
    Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
    parent->SetWeakChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
    EXPECT_TRUE(parent->weak_child());
    DoMarking(MarkingConfig::StackState::kNoHeapPointers);
    EXPECT_FALSE(parent->weak_child());
  }
}

TEST_F(MarkerTest, WeakReferenceToReachableObjectIsNotCleared) {
  // Reachable from Persistent
  {
    Persistent<GCed> object = MakeGarbageCollected<GCed>(GetAllocationHandle());
    WeakPersistent<GCed> weak_object(object);
    EXPECT_TRUE(weak_object);
    DoMarking(MarkingConfig::StackState::kNoHeapPointers);
    EXPECT_TRUE(weak_object);
  }
  {
    Persistent<GCed> object = MakeGarbageCollected<GCed>(GetAllocationHandle());
    Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
    parent->SetWeakChild(object);
    EXPECT_TRUE(parent->weak_child());
    DoMarking(MarkingConfig::StackState::kNoHeapPointers);
    EXPECT_TRUE(parent->weak_child());
  }
  // Reachable from Member
  {
    Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
    WeakPersistent<GCed> weak_object(
        MakeGarbageCollected<GCed>(GetAllocationHandle()));
    parent->SetChild(weak_object);
    EXPECT_TRUE(weak_object);
    DoMarking(MarkingConfig::StackState::kNoHeapPointers);
    EXPECT_TRUE(weak_object);
  }
  {
    Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
    parent->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
    parent->SetWeakChild(parent->child());
    EXPECT_TRUE(parent->weak_child());
    DoMarking(MarkingConfig::StackState::kNoHeapPointers);
    EXPECT_TRUE(parent->weak_child());
  }
  // Reachable from stack
  {
    GCed* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
    WeakPersistent<GCed> weak_object(object);
    EXPECT_TRUE(weak_object);
    DoMarking(MarkingConfig::StackState::kMayContainHeapPointers);
    EXPECT_TRUE(weak_object);
    access(object);
  }
  {
    GCed* object = MakeGarbageCollected<GCed>(GetAllocationHandle());
    Persistent<GCed> parent = MakeGarbageCollected<GCed>(GetAllocationHandle());
    parent->SetWeakChild(object);
    EXPECT_TRUE(parent->weak_child());
    DoMarking(MarkingConfig::StackState::kMayContainHeapPointers);
    EXPECT_TRUE(parent->weak_child());
    access(object);
  }
}

TEST_F(MarkerTest, DeepHierarchyIsMarked) {
  static constexpr int kHierarchyDepth = 10;
  Persistent<GCed> root = MakeGarbageCollected<GCed>(GetAllocationHandle());
  GCed* parent = root;
  for (int i = 0; i < kHierarchyDepth; ++i) {
    parent->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
    parent->SetWeakChild(parent->child());
    parent = parent->child();
  }
  DoMarking(MarkingConfig::StackState::kNoHeapPointers);
  EXPECT_TRUE(HeapObjectHeader::FromPayload(root).IsMarked());
  parent = root;
  for (int i = 0; i < kHierarchyDepth; ++i) {
    EXPECT_TRUE(HeapObjectHeader::FromPayload(parent->child()).IsMarked());
    EXPECT_TRUE(parent->weak_child());
    parent = parent->child();
  }
}

TEST_F(MarkerTest, NestedObjectsOnStackAreMarked) {
  GCed* root = MakeGarbageCollected<GCed>(GetAllocationHandle());
  root->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  root->child()->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  DoMarking(MarkingConfig::StackState::kMayContainHeapPointers);
  EXPECT_TRUE(HeapObjectHeader::FromPayload(root).IsMarked());
  EXPECT_TRUE(HeapObjectHeader::FromPayload(root->child()).IsMarked());
  EXPECT_TRUE(HeapObjectHeader::FromPayload(root->child()->child()).IsMarked());
}

namespace {
class GCedWithCallback : public GarbageCollected<GCedWithCallback> {
 public:
  template <typename Callback>
  explicit GCedWithCallback(Callback callback) {
    callback(this);
  }

  void Trace(Visitor*) const {}
};
}  // namespace

TEST_F(MarkerTest, InConstructionObjectIsEventuallyMarkedEmptyStack) {
  static const Marker::MarkingConfig config = {
      MarkingConfig::CollectionType::kMajor,
      MarkingConfig::StackState::kMayContainHeapPointers};
  Marker marker(*Heap::From(GetHeap()), GetPlatformHandle().get(), config);
  marker.StartMarking();
  GCedWithCallback* object = MakeGarbageCollected<GCedWithCallback>(
      GetAllocationHandle(), [&marker](GCedWithCallback* obj) {
        Member<GCedWithCallback> member(obj);
        marker.VisitorForTesting().Trace(member);
      });
  EXPECT_TRUE(HeapObjectHeader::FromPayload(object).IsMarked());
  marker.FinishMarking(MarkingConfig::StackState::kMayContainHeapPointers);
  EXPECT_TRUE(HeapObjectHeader::FromPayload(object).IsMarked());
}

TEST_F(MarkerTest, InConstructionObjectIsEventuallyMarkedNonEmptyStack) {
  static const Marker::MarkingConfig config = {
      MarkingConfig::CollectionType::kMajor,
      MarkingConfig::StackState::kMayContainHeapPointers};
  Marker marker(*Heap::From(GetHeap()), GetPlatformHandle().get(), config);
  marker.StartMarking();
  MakeGarbageCollected<GCedWithCallback>(
      GetAllocationHandle(), [&marker](GCedWithCallback* obj) {
        Member<GCedWithCallback> member(obj);
        marker.VisitorForTesting().Trace(member);
        EXPECT_TRUE(HeapObjectHeader::FromPayload(obj).IsMarked());
        marker.FinishMarking(
            MarkingConfig::StackState::kMayContainHeapPointers);
        EXPECT_TRUE(HeapObjectHeader::FromPayload(obj).IsMarked());
      });
}

TEST_F(MarkerTest, SentinelNotClearedOnWeakPersistentHandling) {
  static const Marker::MarkingConfig config = {
      MarkingConfig::CollectionType::kMajor,
      MarkingConfig::StackState::kNoHeapPointers};
  Marker marker(*Heap::From(GetHeap()), GetPlatformHandle().get(), config);
  Persistent<GCed> root = MakeGarbageCollected<GCed>(GetAllocationHandle());
  auto* tmp = MakeGarbageCollected<GCed>(GetAllocationHandle());
  root->SetWeakChild(tmp);
  marker.StartMarking();
  marker.FinishMarking(MarkingConfig::StackState::kNoHeapPointers);
  root->SetWeakChild(kSentinelPointer);
  marker.ProcessWeakness();
  EXPECT_EQ(kSentinelPointer, root->weak_child());
}

// Incremental Marking

class IncrementalMarkingTest : public testing::TestWithHeap {
 public:
  using MarkingConfig = Marker::MarkingConfig;

  static constexpr MarkingConfig IncrementalPreciseMarkingConfig = {
      MarkingConfig::CollectionType::kMajor,
      MarkingConfig::StackState::kNoHeapPointers,
      MarkingConfig::MarkingType::kIncremental};
  static constexpr MarkingConfig IncrementalConservativeMarkingConfig = {
      MarkingConfig::CollectionType::kMajor,
      MarkingConfig::StackState::kMayContainHeapPointers,
      MarkingConfig::MarkingType::kIncremental};

  void FinishSteps(Marker& marker, MarkingConfig::StackState stack_state) {
    SingleStep(marker, stack_state, v8::base::TimeDelta::Max());
  }

  void FinishMarking(Marker& marker) {
    marker.FinishMarking(MarkingConfig::StackState::kMayContainHeapPointers);
    marker.ProcessWeakness();
    // Pretend do finish sweeping as StatsCollector verifies that Notify*
    // methods are called in the right order.
    Heap::From(GetHeap())->stats_collector()->NotifySweepingCompleted();
  }

 private:
  bool SingleStep(Marker& marker, MarkingConfig::StackState stack_state,
                  v8::base::TimeDelta deadline) {
    return marker.IncrementalMarkingStepForTesting(stack_state, deadline);
  }
};
constexpr IncrementalMarkingTest::MarkingConfig
    IncrementalMarkingTest::IncrementalPreciseMarkingConfig;
constexpr IncrementalMarkingTest::MarkingConfig
    IncrementalMarkingTest::IncrementalConservativeMarkingConfig;

TEST_F(IncrementalMarkingTest, RootIsMarkedAfterStartMarking) {
  Persistent<GCed> root = MakeGarbageCollected<GCed>(GetAllocationHandle());
  EXPECT_FALSE(HeapObjectHeader::FromPayload(root).IsMarked());
  Marker marker(*Heap::From(GetHeap()), GetPlatformHandle().get(),
                IncrementalPreciseMarkingConfig);
  marker.StartMarking();
  EXPECT_TRUE(HeapObjectHeader::FromPayload(root).IsMarked());
  FinishMarking(marker);
}

TEST_F(IncrementalMarkingTest, MemberIsMarkedAfterMarkingSteps) {
  Persistent<GCed> root = MakeGarbageCollected<GCed>(GetAllocationHandle());
  root->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  HeapObjectHeader& header = HeapObjectHeader::FromPayload(root->child());
  EXPECT_FALSE(header.IsMarked());
  Marker marker(*Heap::From(GetHeap()), GetPlatformHandle().get(),
                IncrementalPreciseMarkingConfig);
  marker.StartMarking();
  FinishSteps(marker, MarkingConfig::StackState::kNoHeapPointers);
  EXPECT_TRUE(header.IsMarked());
  FinishMarking(marker);
}

TEST_F(IncrementalMarkingTest,
       MemberWithWriteBarrierIsMarkedAfterMarkingSteps) {
  Persistent<GCed> root = MakeGarbageCollected<GCed>(GetAllocationHandle());
  Marker marker(*Heap::From(GetHeap()), GetPlatformHandle().get(),
                IncrementalPreciseMarkingConfig);
  marker.StartMarking();
  root->SetChild(MakeGarbageCollected<GCed>(GetAllocationHandle()));
  HeapObjectHeader& header = HeapObjectHeader::FromPayload(root->child());
  EXPECT_FALSE(header.IsMarked());
  FinishSteps(marker, MarkingConfig::StackState::kNoHeapPointers);
  EXPECT_TRUE(header.IsMarked());
  FinishMarking(marker);
}

namespace {
class Holder : public GarbageCollected<Holder> {
 public:
  void Trace(Visitor* visitor) const { visitor->Trace(member_); }

  Member<GCedWithCallback> member_;
};
}  // namespace

TEST_F(IncrementalMarkingTest, IncrementalStepDuringAllocation) {
  Persistent<Holder> holder =
      MakeGarbageCollected<Holder>(GetAllocationHandle());
  Marker marker(*Heap::From(GetHeap()), GetPlatformHandle().get(),
                IncrementalPreciseMarkingConfig);
  marker.StartMarking();
  const HeapObjectHeader* header;
  MakeGarbageCollected<GCedWithCallback>(
      GetAllocationHandle(),
      [this, &holder, &header, &marker](GCedWithCallback* obj) {
        header = &HeapObjectHeader::FromPayload(obj);
        holder->member_ = obj;
        EXPECT_FALSE(header->IsMarked());
        FinishSteps(marker, MarkingConfig::StackState::kMayContainHeapPointers);
        EXPECT_TRUE(header->IsMarked());
      });
  FinishSteps(marker, MarkingConfig::StackState::kNoHeapPointers);
  EXPECT_TRUE(header->IsMarked());
  FinishMarking(marker);
}

}  // namespace internal
}  // namespace cppgc
