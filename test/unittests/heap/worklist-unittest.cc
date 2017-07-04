// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/worklist.h"

#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

using TestWorklist = Worklist<64>;

class HeapObject {};

TEST(TestWorklist, SegmentCreate) {
  TestWorklist::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  EXPECT_EQ(0u, segment.Size());
  EXPECT_FALSE(segment.IsFull());
}

TEST(TestWorklist, SegmentPush) {
  TestWorklist::Segment segment;
  EXPECT_EQ(0u, segment.Size());
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_EQ(1u, segment.Size());
}

TEST(TestWorklist, SegmentPushPop) {
  TestWorklist::Segment segment;
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_EQ(1u, segment.Size());
  HeapObject dummy;
  HeapObject* object = &dummy;
  EXPECT_TRUE(segment.Pop(&object));
  EXPECT_EQ(0u, segment.Size());
  EXPECT_EQ(nullptr, object);
}

TEST(TestWorklist, SegmentIsEmpty) {
  TestWorklist::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_FALSE(segment.IsEmpty());
}

TEST(TestWorklist, SegmentIsFull) {
  TestWorklist::Segment segment;
  EXPECT_FALSE(segment.IsFull());
  for (size_t i = 0; i < TestWorklist::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
  EXPECT_TRUE(segment.IsFull());
}

TEST(TestWorklist, SegmentClear) {
  TestWorklist::Segment segment;
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_FALSE(segment.IsEmpty());
  segment.Clear();
  EXPECT_TRUE(segment.IsEmpty());
  for (size_t i = 0; i < TestWorklist::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
}

TEST(TestWorklist, SegmentFullPushFails) {
  TestWorklist::Segment segment;
  EXPECT_FALSE(segment.IsFull());
  for (size_t i = 0; i < TestWorklist::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
  EXPECT_TRUE(segment.IsFull());
  EXPECT_FALSE(segment.Push(nullptr));
}

TEST(TestWorklist, SegmentEmptyPopFails) {
  TestWorklist::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  HeapObject* object;
  EXPECT_FALSE(segment.Pop(&object));
}

TEST(TestWorklist, SegmentUpdateNull) {
  TestWorklist::Segment segment;
  HeapObject* object;
  object = reinterpret_cast<HeapObject*>(&object);
  EXPECT_TRUE(segment.Push(object));
  segment.Update([](HeapObject* object) { return nullptr; });
  EXPECT_TRUE(segment.IsEmpty());
}

TEST(TestWorklist, SegmentUpdate) {
  TestWorklist::Segment segment;
  HeapObject* objectA;
  objectA = reinterpret_cast<HeapObject*>(&objectA);
  HeapObject* objectB;
  objectB = reinterpret_cast<HeapObject*>(&objectB);
  EXPECT_TRUE(segment.Push(objectA));
  segment.Update([objectB](HeapObject* object) { return objectB; });
  HeapObject* object;
  EXPECT_TRUE(segment.Pop(&object));
  EXPECT_EQ(object, objectB);
}

TEST(TestWorklist, CreateEmpty) {
  TestWorklist worklist;
  TestWorklist::View worklist_view(&worklist, 0);
  EXPECT_TRUE(worklist_view.IsLocalEmpty());
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(TestWorklist, LocalPushPop) {
  TestWorklist worklist;
  TestWorklist::View worklist_view(&worklist, 0);
  HeapObject dummy;
  HeapObject* retrieved = nullptr;
  EXPECT_TRUE(worklist_view.Push(&dummy));
  EXPECT_FALSE(worklist_view.IsLocalEmpty());
  EXPECT_TRUE(worklist_view.Pop(&retrieved));
  EXPECT_EQ(&dummy, retrieved);
}

TEST(TestWorklist, LocalIsBasedOnId) {
  TestWorklist worklist;
  // Use the same id.
  TestWorklist::View worklist_view1(&worklist, 0);
  TestWorklist::View worklist_view2(&worklist, 0);
  HeapObject dummy;
  HeapObject* retrieved = nullptr;
  EXPECT_TRUE(worklist_view1.Push(&dummy));
  EXPECT_FALSE(worklist_view1.IsLocalEmpty());
  EXPECT_FALSE(worklist_view2.IsLocalEmpty());
  EXPECT_TRUE(worklist_view2.Pop(&retrieved));
  EXPECT_EQ(&dummy, retrieved);
  EXPECT_TRUE(worklist_view1.IsLocalEmpty());
  EXPECT_TRUE(worklist_view2.IsLocalEmpty());
}

TEST(TestWorklist, LocalPushStaysPrivate) {
  TestWorklist worklist;
  TestWorklist::View worklist_view1(&worklist, 0);
  TestWorklist::View worklist_view2(&worklist, 1);
  HeapObject dummy;
  HeapObject* retrieved = nullptr;
  EXPECT_TRUE(worklist.IsGlobalEmpty());
  EXPECT_TRUE(worklist_view1.Push(&dummy));
  EXPECT_FALSE(worklist.IsGlobalEmpty());
  EXPECT_FALSE(worklist_view2.Pop(&retrieved));
  EXPECT_EQ(nullptr, retrieved);
  EXPECT_TRUE(worklist_view1.Pop(&retrieved));
  EXPECT_EQ(&dummy, retrieved);
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(TestWorklist, GlobalUpdateNull) {
  TestWorklist worklist;
  TestWorklist::View worklist_view(&worklist, 0);
  HeapObject* object;
  object = reinterpret_cast<HeapObject*>(&object);
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view.Push(object));
  }
  EXPECT_TRUE(worklist_view.Push(object));
  worklist.Update([](HeapObject* object) { return nullptr; });
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(TestWorklist, GlobalUpdate) {
  TestWorklist worklist;
  TestWorklist::View worklist_view(&worklist, 0);
  HeapObject* objectA = nullptr;
  objectA = reinterpret_cast<HeapObject*>(&objectA);
  HeapObject* objectB = nullptr;
  objectB = reinterpret_cast<HeapObject*>(&objectB);
  HeapObject* objectC = nullptr;
  objectC = reinterpret_cast<HeapObject*>(&objectC);
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view.Push(objectA));
  }
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view.Push(objectB));
  }
  EXPECT_TRUE(worklist_view.Push(objectA));
  worklist.Update([objectA, objectC](HeapObject* object) {
    return (object == objectA) ? nullptr : objectC;
  });
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    HeapObject* object;
    EXPECT_TRUE(worklist_view.Pop(&object));
    EXPECT_EQ(object, objectC);
  }
}

TEST(TestWorklist, FlushToGlobalPushSegment) {
  TestWorklist worklist;
  TestWorklist::View worklist_view0(&worklist, 0);
  TestWorklist::View worklist_view1(&worklist, 1);
  HeapObject* object = nullptr;
  HeapObject* objectA = nullptr;
  objectA = reinterpret_cast<HeapObject*>(&objectA);
  EXPECT_TRUE(worklist_view0.Push(objectA));
  worklist.FlushToGlobal(0);
  EXPECT_TRUE(worklist_view1.Pop(&object));
}

TEST(TestWorklist, FlushToGlobalPopSegment) {
  TestWorklist worklist;
  TestWorklist::View worklist_view0(&worklist, 0);
  TestWorklist::View worklist_view1(&worklist, 1);
  HeapObject* object = nullptr;
  HeapObject* objectA = nullptr;
  objectA = reinterpret_cast<HeapObject*>(&objectA);
  EXPECT_TRUE(worklist_view0.Push(objectA));
  EXPECT_TRUE(worklist_view0.Push(objectA));
  EXPECT_TRUE(worklist_view0.Pop(&object));
  worklist.FlushToGlobal(0);
  EXPECT_TRUE(worklist_view1.Pop(&object));
}

TEST(TestWorklist, Clear) {
  TestWorklist worklist;
  TestWorklist::View worklist_view(&worklist, 0);
  HeapObject* object;
  object = reinterpret_cast<HeapObject*>(&object);
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view.Push(object));
  }
  EXPECT_TRUE(worklist_view.Push(object));
  worklist.Clear();
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(TestWorklist, SingleSegmentSteal) {
  TestWorklist worklist;
  TestWorklist::View worklist_view1(&worklist, 0);
  TestWorklist::View worklist_view2(&worklist, 1);
  HeapObject dummy;
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view1.Push(&dummy));
  }
  HeapObject* retrieved = nullptr;
  // One more push/pop to publish the full segment.
  EXPECT_TRUE(worklist_view1.Push(nullptr));
  EXPECT_TRUE(worklist_view1.Pop(&retrieved));
  EXPECT_EQ(nullptr, retrieved);
  // Stealing.
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view2.Pop(&retrieved));
    EXPECT_EQ(&dummy, retrieved);
    EXPECT_FALSE(worklist_view1.Pop(&retrieved));
  }
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(TestWorklist, MultipleSegmentsStolen) {
  TestWorklist worklist;
  TestWorklist::View worklist_view1(&worklist, 0);
  TestWorklist::View worklist_view2(&worklist, 1);
  TestWorklist::View worklist_view3(&worklist, 2);
  HeapObject dummy1;
  HeapObject dummy2;
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view1.Push(&dummy1));
  }
  for (size_t i = 0; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view1.Push(&dummy2));
  }
  HeapObject* retrieved = nullptr;
  HeapObject dummy3;
  // One more push/pop to publish the full segment.
  EXPECT_TRUE(worklist_view1.Push(&dummy3));
  EXPECT_TRUE(worklist_view1.Pop(&retrieved));
  EXPECT_EQ(&dummy3, retrieved);
  // Stealing.
  EXPECT_TRUE(worklist_view2.Pop(&retrieved));
  HeapObject* const expect_bag2 = retrieved;
  EXPECT_TRUE(worklist_view3.Pop(&retrieved));
  HeapObject* const expect_bag3 = retrieved;
  EXPECT_NE(expect_bag2, expect_bag3);
  EXPECT_TRUE(expect_bag2 == &dummy1 || expect_bag2 == &dummy2);
  EXPECT_TRUE(expect_bag3 == &dummy1 || expect_bag3 == &dummy2);
  for (size_t i = 1; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view2.Pop(&retrieved));
    EXPECT_EQ(expect_bag2, retrieved);
    EXPECT_FALSE(worklist_view1.Pop(&retrieved));
  }
  for (size_t i = 1; i < TestWorklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view3.Pop(&retrieved));
    EXPECT_EQ(expect_bag3, retrieved);
    EXPECT_FALSE(worklist_view1.Pop(&retrieved));
  }
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

}  // namespace internal
}  // namespace v8
