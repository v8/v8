// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/worklist.h"

#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

class HeapObject {};

TEST(Worklist, SegmentCreate) {
  Worklist::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  EXPECT_EQ(0u, segment.Size());
  EXPECT_FALSE(segment.IsFull());
}

TEST(Worklist, SegmentPush) {
  Worklist::Segment segment;
  EXPECT_EQ(0u, segment.Size());
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_EQ(1u, segment.Size());
}

TEST(Worklist, SegmentPushPop) {
  Worklist::Segment segment;
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_EQ(1u, segment.Size());
  HeapObject dummy;
  HeapObject* object = &dummy;
  EXPECT_TRUE(segment.Pop(&object));
  EXPECT_EQ(0u, segment.Size());
  EXPECT_EQ(nullptr, object);
}

TEST(Worklist, SegmentIsEmpty) {
  Worklist::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_FALSE(segment.IsEmpty());
}

TEST(Worklist, SegmentIsFull) {
  Worklist::Segment segment;
  EXPECT_FALSE(segment.IsFull());
  for (size_t i = 0; i < Worklist::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
  EXPECT_TRUE(segment.IsFull());
}

TEST(Worklist, SegmentClear) {
  Worklist::Segment segment;
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_FALSE(segment.IsEmpty());
  segment.Clear();
  EXPECT_TRUE(segment.IsEmpty());
  for (size_t i = 0; i < Worklist::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
}

TEST(Worklist, SegmentFullPushFails) {
  Worklist::Segment segment;
  EXPECT_FALSE(segment.IsFull());
  for (size_t i = 0; i < Worklist::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
  EXPECT_TRUE(segment.IsFull());
  EXPECT_FALSE(segment.Push(nullptr));
}

TEST(Worklist, SegmentEmptyPopFails) {
  Worklist::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  HeapObject* object;
  EXPECT_FALSE(segment.Pop(&object));
}

TEST(Worklist, SegmentUpdateNull) {
  Worklist::Segment segment;
  HeapObject* object;
  object = reinterpret_cast<HeapObject*>(&object);
  EXPECT_TRUE(segment.Push(object));
  segment.Update([](HeapObject* object) { return nullptr; });
  EXPECT_TRUE(segment.IsEmpty());
}

TEST(Worklist, SegmentUpdate) {
  Worklist::Segment segment;
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

TEST(Worklist, CreateEmpty) {
  Worklist worklist;
  WorklistView worklist_view(&worklist, 0);
  EXPECT_TRUE(worklist_view.IsLocalEmpty());
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(Worklist, LocalPushPop) {
  Worklist worklist;
  WorklistView worklist_view(&worklist, 0);
  HeapObject dummy;
  HeapObject* retrieved = nullptr;
  EXPECT_TRUE(worklist_view.Push(&dummy));
  EXPECT_FALSE(worklist_view.IsLocalEmpty());
  EXPECT_TRUE(worklist_view.Pop(&retrieved));
  EXPECT_EQ(&dummy, retrieved);
}

TEST(Worklist, LocalIsBasedOnId) {
  Worklist worklist;
  // Use the same id.
  WorklistView worklist_view1(&worklist, 0);
  WorklistView worklist_view2(&worklist, 0);
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

TEST(Worklist, LocalPushStaysPrivate) {
  Worklist worklist;
  WorklistView worklist_view1(&worklist, 0);
  WorklistView worklist_view2(&worklist, 1);
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

TEST(Worklist, GlobalUpdateNull) {
  Worklist worklist;
  WorklistView worklist_view(&worklist, 0);
  HeapObject* object;
  object = reinterpret_cast<HeapObject*>(&object);
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view.Push(object));
  }
  EXPECT_TRUE(worklist_view.Push(object));
  worklist.Update([](HeapObject* object) { return nullptr; });
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(Worklist, GlobalUpdate) {
  Worklist worklist;
  WorklistView worklist_view(&worklist, 0);
  HeapObject* objectA = nullptr;
  objectA = reinterpret_cast<HeapObject*>(&objectA);
  HeapObject* objectB = nullptr;
  objectB = reinterpret_cast<HeapObject*>(&objectB);
  HeapObject* objectC = nullptr;
  objectC = reinterpret_cast<HeapObject*>(&objectC);
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view.Push(objectA));
  }
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view.Push(objectB));
  }
  EXPECT_TRUE(worklist_view.Push(objectA));
  worklist.Update([objectA, objectC](HeapObject* object) {
    return (object == objectA) ? nullptr : objectC;
  });
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
    HeapObject* object;
    EXPECT_TRUE(worklist_view.Pop(&object));
    EXPECT_EQ(object, objectC);
  }
}

TEST(Worklist, Clear) {
  Worklist worklist;
  WorklistView worklist_view(&worklist, 0);
  HeapObject* object;
  object = reinterpret_cast<HeapObject*>(&object);
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view.Push(object));
  }
  EXPECT_TRUE(worklist_view.Push(object));
  worklist.Clear();
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(Worklist, SingleSegmentSteal) {
  Worklist worklist;
  WorklistView worklist_view1(&worklist, 0);
  WorklistView worklist_view2(&worklist, 1);
  HeapObject dummy;
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view1.Push(&dummy));
  }
  HeapObject* retrieved = nullptr;
  // One more push/pop to publish the full segment.
  EXPECT_TRUE(worklist_view1.Push(nullptr));
  EXPECT_TRUE(worklist_view1.Pop(&retrieved));
  EXPECT_EQ(nullptr, retrieved);
  // Stealing.
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view2.Pop(&retrieved));
    EXPECT_EQ(&dummy, retrieved);
    EXPECT_FALSE(worklist_view1.Pop(&retrieved));
  }
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

TEST(Worklist, MultipleSegmentsStolen) {
  Worklist worklist;
  WorklistView worklist_view1(&worklist, 0);
  WorklistView worklist_view2(&worklist, 1);
  WorklistView worklist_view3(&worklist, 2);
  HeapObject dummy1;
  HeapObject dummy2;
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view1.Push(&dummy1));
  }
  for (size_t i = 0; i < Worklist::kSegmentCapacity; i++) {
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
  for (size_t i = 1; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view2.Pop(&retrieved));
    EXPECT_EQ(expect_bag2, retrieved);
    EXPECT_FALSE(worklist_view1.Pop(&retrieved));
  }
  for (size_t i = 1; i < Worklist::kSegmentCapacity; i++) {
    EXPECT_TRUE(worklist_view3.Pop(&retrieved));
    EXPECT_EQ(expect_bag3, retrieved);
    EXPECT_FALSE(worklist_view1.Pop(&retrieved));
  }
  EXPECT_TRUE(worklist.IsGlobalEmpty());
}

}  // namespace internal
}  // namespace v8
