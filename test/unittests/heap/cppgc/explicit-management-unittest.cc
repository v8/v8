// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/explicit-management.h"

#include "include/cppgc/garbage-collected.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-space.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/sweeper.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

class ExplicitManagementTest : public testing::TestSupportingAllocationOnly {
 public:
  size_t AllocatedObjectSize() const {
    auto* heap = Heap::From(GetHeap());
    heap->stats_collector()->NotifySafePointForTesting();
    return heap->stats_collector()->allocated_object_size();
  }

  void ResetLinearAllocationBuffers() const {
    return Heap::From(GetHeap())
        ->object_allocator()
        .ResetLinearAllocationBuffers();
  }
};

namespace {

class DynamicallySized final : public GarbageCollected<DynamicallySized> {
 public:
  void Trace(Visitor*) const {}
};

}  // namespace

TEST_F(ExplicitManagementTest, FreeRegularObjectToLAB) {
  auto* o =
      MakeGarbageCollected<DynamicallySized>(GetHeap()->GetAllocationHandle());
  const auto* space = NormalPageSpace::From(BasePage::FromPayload(o)->space());
  const auto& lab = space->linear_allocation_buffer();
  auto& header = HeapObjectHeader::FromPayload(o);
  const size_t size = header.GetSize();
  Address needle = reinterpret_cast<Address>(&header);
  // Test checks freeing to LAB.
  ASSERT_EQ(lab.start(), header.PayloadEnd());
  const size_t lab_size_before_free = lab.size();
  const size_t allocated_size_before = AllocatedObjectSize();
  subtle::FreeUnreferencedObject(o);
  EXPECT_EQ(lab.start(), reinterpret_cast<Address>(needle));
  EXPECT_EQ(lab_size_before_free + size, lab.size());
  // LAB is included in allocated object size, so no change is expected.
  EXPECT_EQ(allocated_size_before, AllocatedObjectSize());
  EXPECT_FALSE(space->free_list().ContainsForTesting({needle, size}));
}

TEST_F(ExplicitManagementTest, FreeRegularObjectToFreeList) {
  auto* o =
      MakeGarbageCollected<DynamicallySized>(GetHeap()->GetAllocationHandle());
  const auto* space = NormalPageSpace::From(BasePage::FromPayload(o)->space());
  const auto& lab = space->linear_allocation_buffer();
  auto& header = HeapObjectHeader::FromPayload(o);
  const size_t size = header.GetSize();
  Address needle = reinterpret_cast<Address>(&header);
  // Test checks freeing to free list.
  ResetLinearAllocationBuffers();
  ASSERT_EQ(lab.start(), nullptr);
  const size_t allocated_size_before = AllocatedObjectSize();
  subtle::FreeUnreferencedObject(o);
  EXPECT_EQ(lab.start(), nullptr);
  EXPECT_EQ(allocated_size_before - size, AllocatedObjectSize());
  EXPECT_TRUE(space->free_list().ContainsForTesting({needle, size}));
}

TEST_F(ExplicitManagementTest, FreeLargeObject) {
  auto* o = MakeGarbageCollected<DynamicallySized>(
      GetHeap()->GetAllocationHandle(),
      AdditionalBytes(kLargeObjectSizeThreshold));
  const auto* page = BasePage::FromPayload(o);
  auto* heap = page->heap();
  ASSERT_TRUE(page->is_large());
  ConstAddress needle = reinterpret_cast<ConstAddress>(o);
  const size_t size = LargePage::From(page)->PayloadSize();
  EXPECT_TRUE(heap->page_backend()->Lookup(needle));
  const size_t allocated_size_before = AllocatedObjectSize();
  subtle::FreeUnreferencedObject(o);
  EXPECT_FALSE(heap->page_backend()->Lookup(needle));
  EXPECT_EQ(allocated_size_before - size, AllocatedObjectSize());
}

TEST_F(ExplicitManagementTest, FreeBailsOutDuringGC) {
  const size_t snapshot_before = AllocatedObjectSize();
  auto* o =
      MakeGarbageCollected<DynamicallySized>(GetHeap()->GetAllocationHandle());
  auto* heap = BasePage::FromPayload(o)->heap();
  heap->SetInAtomicPauseForTesting(true);
  const size_t allocated_size_before = AllocatedObjectSize();
  subtle::FreeUnreferencedObject(o);
  EXPECT_EQ(allocated_size_before, AllocatedObjectSize());
  heap->SetInAtomicPauseForTesting(false);
  ResetLinearAllocationBuffers();
  subtle::FreeUnreferencedObject(o);
  EXPECT_EQ(snapshot_before, AllocatedObjectSize());
}

TEST_F(ExplicitManagementTest, FreeNull) {
  DynamicallySized* o = nullptr;
  // Noop.
  subtle::FreeUnreferencedObject(o);
}

}  // namespace internal
}  // namespace cppgc
