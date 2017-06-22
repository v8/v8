// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/workstealing-bag.h"

#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

class HeapObject {};

TEST(WorkStealingBag, SegmentCreate) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  EXPECT_EQ(0u, segment.Size());
  EXPECT_FALSE(segment.IsFull());
}

TEST(WorkStealingBag, SegmentPush) {
  WorkStealingBag::Segment segment;
  EXPECT_EQ(0u, segment.Size());
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_EQ(1u, segment.Size());
}

TEST(WorkStealingBag, SegmentPushPop) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_EQ(1u, segment.Size());
  HeapObject dummy;
  HeapObject* object = &dummy;
  EXPECT_TRUE(segment.Pop(&object));
  EXPECT_EQ(0u, segment.Size());
  EXPECT_EQ(nullptr, object);
}

TEST(WorkStealingBag, SegmentIsEmpty) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_FALSE(segment.IsEmpty());
}

TEST(WorkStealingBag, SegmentIsFull) {
  WorkStealingBag::Segment segment;
  EXPECT_FALSE(segment.IsFull());
  for (size_t i = 0; i < WorkStealingBag::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
  EXPECT_TRUE(segment.IsFull());
}

TEST(WorkStealingBag, SegmentClear) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.Push(nullptr));
  EXPECT_FALSE(segment.IsEmpty());
  segment.Clear();
  EXPECT_TRUE(segment.IsEmpty());
  for (size_t i = 0; i < WorkStealingBag::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
}

TEST(WorkStealingBag, SegmentFullPushFails) {
  WorkStealingBag::Segment segment;
  EXPECT_FALSE(segment.IsFull());
  for (size_t i = 0; i < WorkStealingBag::Segment::kCapacity; i++) {
    EXPECT_TRUE(segment.Push(nullptr));
  }
  EXPECT_TRUE(segment.IsFull());
  EXPECT_FALSE(segment.Push(nullptr));
}

TEST(WorkStealingBag, SegmentEmptyPopFails) {
  WorkStealingBag::Segment segment;
  EXPECT_TRUE(segment.IsEmpty());
  HeapObject* object;
  EXPECT_FALSE(segment.Pop(&object));
}

TEST(WorkStealingBag, CreateEmpty) {
  WorkStealingBag marking_bag;
  LocalWorkStealingBag local_marking_bag(&marking_bag, 0);
  EXPECT_TRUE(local_marking_bag.IsLocalEmpty());
  EXPECT_TRUE(marking_bag.IsGlobalEmpty());
}

TEST(WorkStealingBag, LocalPushPop) {
  WorkStealingBag marking_bag;
  LocalWorkStealingBag local_marking_bag(&marking_bag, 0);
  HeapObject dummy;
  HeapObject* retrieved = nullptr;
  EXPECT_TRUE(local_marking_bag.Push(&dummy));
  EXPECT_FALSE(local_marking_bag.IsLocalEmpty());
  EXPECT_TRUE(local_marking_bag.Pop(&retrieved));
  EXPECT_EQ(&dummy, retrieved);
}

TEST(WorkStealingBag, LocalIsBasedOnId) {
  WorkStealingBag marking_bag;
  // Use the same id.
  LocalWorkStealingBag local_marking_bag1(&marking_bag, 0);
  LocalWorkStealingBag local_marking_bag2(&marking_bag, 0);
  HeapObject dummy;
  HeapObject* retrieved = nullptr;
  EXPECT_TRUE(local_marking_bag1.Push(&dummy));
  EXPECT_FALSE(local_marking_bag1.IsLocalEmpty());
  EXPECT_FALSE(local_marking_bag2.IsLocalEmpty());
  EXPECT_TRUE(local_marking_bag2.Pop(&retrieved));
  EXPECT_EQ(&dummy, retrieved);
  EXPECT_TRUE(local_marking_bag1.IsLocalEmpty());
  EXPECT_TRUE(local_marking_bag2.IsLocalEmpty());
}

TEST(WorkStealingBag, LocalPushStaysPrivate) {
  WorkStealingBag marking_bag;
  LocalWorkStealingBag local_marking_bag1(&marking_bag, 0);
  LocalWorkStealingBag local_marking_bag2(&marking_bag, 1);
  HeapObject dummy;
  HeapObject* retrieved = nullptr;
  EXPECT_TRUE(marking_bag.IsGlobalEmpty());
  EXPECT_TRUE(local_marking_bag1.Push(&dummy));
  EXPECT_FALSE(marking_bag.IsGlobalEmpty());
  EXPECT_FALSE(local_marking_bag2.Pop(&retrieved));
  EXPECT_EQ(nullptr, retrieved);
  EXPECT_TRUE(local_marking_bag1.Pop(&retrieved));
  EXPECT_EQ(&dummy, retrieved);
  EXPECT_TRUE(marking_bag.IsGlobalEmpty());
}

TEST(WorkStealingBag, SingleSegmentSteal) {
  WorkStealingBag marking_bag;
  LocalWorkStealingBag local_marking_bag1(&marking_bag, 0);
  LocalWorkStealingBag local_marking_bag2(&marking_bag, 1);
  HeapObject dummy;
  for (size_t i = 0; i < WorkStealingBag::kSegmentCapacity; i++) {
    EXPECT_TRUE(local_marking_bag1.Push(&dummy));
  }
  HeapObject* retrieved = nullptr;
  // One more push/pop to publish the full segment.
  EXPECT_TRUE(local_marking_bag1.Push(nullptr));
  EXPECT_TRUE(local_marking_bag1.Pop(&retrieved));
  EXPECT_EQ(nullptr, retrieved);
  // Stealing.
  for (size_t i = 0; i < WorkStealingBag::kSegmentCapacity; i++) {
    EXPECT_TRUE(local_marking_bag2.Pop(&retrieved));
    EXPECT_EQ(&dummy, retrieved);
    EXPECT_FALSE(local_marking_bag1.Pop(&retrieved));
  }
  EXPECT_TRUE(marking_bag.IsGlobalEmpty());
}

TEST(WorkStealingBag, MultipleSegmentsStolen) {
  WorkStealingBag marking_bag;
  LocalWorkStealingBag local_marking_bag1(&marking_bag, 0);
  LocalWorkStealingBag local_marking_bag2(&marking_bag, 1);
  LocalWorkStealingBag local_marking_bag3(&marking_bag, 2);
  HeapObject dummy1;
  HeapObject dummy2;
  for (size_t i = 0; i < WorkStealingBag::kSegmentCapacity; i++) {
    EXPECT_TRUE(local_marking_bag1.Push(&dummy1));
  }
  for (size_t i = 0; i < WorkStealingBag::kSegmentCapacity; i++) {
    EXPECT_TRUE(local_marking_bag1.Push(&dummy2));
  }
  HeapObject* retrieved = nullptr;
  HeapObject dummy3;
  // One more push/pop to publish the full segment.
  EXPECT_TRUE(local_marking_bag1.Push(&dummy3));
  EXPECT_TRUE(local_marking_bag1.Pop(&retrieved));
  EXPECT_EQ(&dummy3, retrieved);
  // Stealing.
  EXPECT_TRUE(local_marking_bag2.Pop(&retrieved));
  HeapObject* const expect_bag2 = retrieved;
  EXPECT_TRUE(local_marking_bag3.Pop(&retrieved));
  HeapObject* const expect_bag3 = retrieved;
  EXPECT_NE(expect_bag2, expect_bag3);
  EXPECT_TRUE(expect_bag2 == &dummy1 || expect_bag2 == &dummy2);
  EXPECT_TRUE(expect_bag3 == &dummy1 || expect_bag3 == &dummy2);
  for (size_t i = 1; i < WorkStealingBag::kSegmentCapacity; i++) {
    EXPECT_TRUE(local_marking_bag2.Pop(&retrieved));
    EXPECT_EQ(expect_bag2, retrieved);
    EXPECT_FALSE(local_marking_bag1.Pop(&retrieved));
  }
  for (size_t i = 1; i < WorkStealingBag::kSegmentCapacity; i++) {
    EXPECT_TRUE(local_marking_bag3.Pop(&retrieved));
    EXPECT_EQ(expect_bag3, retrieved);
    EXPECT_FALSE(local_marking_bag1.Pop(&retrieved));
  }
  EXPECT_TRUE(marking_bag.IsGlobalEmpty());
}

}  // namespace internal
}  // namespace v8
