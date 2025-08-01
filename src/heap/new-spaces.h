// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_NEW_SPACES_H_
#define V8_HEAP_NEW_SPACES_H_

#include <atomic>
#include <memory>
#include <numeric>
#include <optional>

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/heap/allocation-observer.h"
#include "src/heap/heap-verifier.h"
#include "src/heap/heap.h"
#include "src/heap/memory-chunk.h"
#include "src/heap/page-metadata.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/spaces.h"
#include "src/objects/heap-object.h"

namespace v8 {
namespace internal {

class Heap;
class MutablePageMetadata;
class SemiSpaceNewSpace;

enum SemiSpaceId { kFromSpace = 0, kToSpace = 1 };

// -----------------------------------------------------------------------------
// SemiSpace in young generation
//
// A SemiSpace is a contiguous chunk of memory holding page-like memory chunks.
// The mark-compact collector  uses the memory of the first page in the from
// space as a marking stack when tracing live objects.
class SemiSpace final : public Space {
 public:
  using iterator = PageIterator;
  using const_iterator = ConstPageIterator;

  static void Swap(SemiSpace* from, SemiSpace* to);

  SemiSpace(Heap* heap, SemiSpaceId semispace);
  V8_EXPORT_PRIVATE ~SemiSpace();

  inline bool Contains(Tagged<HeapObject> o) const;
  inline bool Contains(Tagged<Object> o) const;
  template <typename T>
  inline bool Contains(Tagged<T> o) const;
  inline bool ContainsSlow(Address a) const;

  void Uncommit();
  bool IsCommitted() const { return !memory_chunk_list_.Empty(); }

  // Returns the start address of the first page of the space.
  Address space_start() const {
    const PageMetadata* page = first_page();
    return page ? page->area_start() : kNullAddress;
  }

  PageMetadata* current_page() const { return current_page_; }

  // Returns the start address of the current page of the space.
  Address page_low() const { return current_page_->area_start(); }

  // Returns one past the end address of the current page of the space.
  Address page_high() const { return current_page_->area_end(); }

  bool AdvancePage(size_t target_capacity);

  void RemovePage(PageMetadata* page);
  void MovePageToTheEnd(PageMetadata* page);

  PageMetadata* InitializePage(MutablePageMetadata* chunk) final;

  // Returns the current capacity of the semispace.
  size_t current_capacity() const { return current_capacity_; }
  // Returns the current capacity of the semispace using an atomic load.
  size_t current_capacity_safe() const {
    return base::AsAtomicWord::Relaxed_Load(&current_capacity_);
  }

  SemiSpaceId id() const { return id_; }

  // Approximate amount of physical memory committed for this space.
  size_t CommittedPhysicalMemory() const final;

  // If we don't have these here then SemiSpace will be abstract.  However
  // they should never be called:

  size_t Size() const final { UNREACHABLE(); }

  size_t SizeOfObjects() const final { return Size(); }

  size_t Available() const final { UNREACHABLE(); }

  PageMetadata* first_page() final {
    return PageMetadata::cast(memory_chunk_list_.front());
  }
  PageMetadata* last_page() final {
    return PageMetadata::cast(memory_chunk_list_.back());
  }

  const PageMetadata* first_page() const final {
    return reinterpret_cast<const PageMetadata*>(memory_chunk_list_.front());
  }
  const PageMetadata* last_page() const final {
    return reinterpret_cast<const PageMetadata*>(memory_chunk_list_.back());
  }

  iterator begin() { return iterator(first_page()); }
  iterator end() { return iterator(nullptr); }

  const_iterator begin() const { return const_iterator(first_page()); }
  const_iterator end() const { return const_iterator(nullptr); }

  std::unique_ptr<ObjectIterator> GetObjectIterator(Heap* heap) final;

#ifdef DEBUG
  V8_EXPORT_PRIVATE void Print() final;
  // Validate a range of of addresses in a SemiSpace.
  // The "from" address must be on a page prior to the "to" address,
  // in the linked page order, or it must be earlier on the same page.
  static void AssertValidRange(Address from, Address to);
#else
  // Do nothing.
  inline static void AssertValidRange(Address from, Address to) {}
#endif

#ifdef VERIFY_HEAP
  void Verify(Isolate* isolate, SpaceVerificationVisitor* visitor) const final {
    UNREACHABLE();
  }
  void VerifyPageMetadata() const;
#endif

  void AddRangeToActiveSystemPages(Address start, Address end);

  void MoveQuarantinedPage(PageMetadata* metadata);

 private:
  bool AllocateFreshPage();

  void RewindPages(int num_pages);

  // Iterates all pages and properly initializes page flags for this space.
  void FixPagesFlags();

  void IncrementCommittedPhysicalMemory(size_t increment_value);
  void DecrementCommittedPhysicalMemory(size_t decrement_value);

  void ShrinkCapacityTo(size_t capacity);
  void Reset();

  // The currently committed space capacity.
  size_t current_capacity_ = 0;
  size_t committed_physical_memory_ = 0;
  SemiSpaceId id_;
  PageMetadata* current_page_ = nullptr;

  // The number of quarantined pages in this space. Those pages are at the front
  // of memory_chunk_list_.
  size_t quarantined_pages_count_ = 0;

  friend class SemiSpaceNewSpace;
  friend class SemiSpaceObjectIterator;
};

// A SemiSpaceObjectIterator is an ObjectIterator that iterates over the active
// semispace of the heap's new space.
class SemiSpaceObjectIterator : public ObjectIterator {
 public:
  // Create an iterator over the objects in the given to-space.
  inline explicit SemiSpaceObjectIterator(const SemiSpaceNewSpace* space);

  inline Tagged<HeapObject> Next() final;

 private:
  // The current iteration point.
  Address current_;
};

class NewSpace : NON_EXPORTED_BASE(public SpaceWithLinearArea) {
 public:
  using iterator = PageIterator;
  using const_iterator = ConstPageIterator;

  explicit NewSpace(Heap* heap);

  base::Mutex* mutex() { return &mutex_; }

  inline bool Contains(Tagged<Object> o) const;
  inline bool Contains(Tagged<HeapObject> o) const;
  virtual bool ContainsSlow(Address a) const = 0;

  size_t ExternalBackingStoreOverallBytes() const {
    size_t result = 0;
    ForAll<ExternalBackingStoreType>(
        [this, &result](ExternalBackingStoreType type, int index) {
          result += ExternalBackingStoreBytes(type);
        });
    return result;
  }

  // Promotes a young generation page to the old generation.
  //
  // Does not clear `will_be_promoted()` to allow for different collector
  // handling.
  void PromotePageToOldSpace(PageMetadata* page, FreeMode free_mode);

  virtual size_t Capacity() const = 0;
  virtual size_t TotalCapacity() const = 0;
  virtual size_t MinimumCapacity() const = 0;
  virtual size_t MaximumCapacity() const = 0;
  virtual size_t AllocatedSinceLastGC() const = 0;

  // Grow the capacity of the space.
  virtual void Grow(size_t new_capacity) = 0;

  virtual void MakeIterable() = 0;

  virtual iterator begin() = 0;
  virtual iterator end() = 0;

  virtual const_iterator begin() const = 0;
  virtual const_iterator end() const = 0;

  virtual Address first_allocatable_address() const = 0;

  virtual void GarbageCollectionPrologue() {}
  virtual void GarbageCollectionEpilogue() = 0;

  virtual bool IsPromotionCandidate(const MutablePageMetadata* page) const = 0;

  virtual void GrowToMaximumCapacityForTesting() = 0;

 protected:
  static const int kAllocationBufferParkingThreshold = 4 * KB;

  base::Mutex mutex_;

  virtual void RemovePage(PageMetadata* page) = 0;
};

// -----------------------------------------------------------------------------
// The young generation space.
//
// The new space consists of a contiguous pair of semispaces.  It simply
// forwards most functions to the appropriate semispace.

class V8_EXPORT_PRIVATE SemiSpaceNewSpace final : public NewSpace {
  using ParkedAllocationBuffer = std::pair<int, Address>;
  using ParkedAllocationBuffersVector = std::vector<ParkedAllocationBuffer>;

 public:
  static SemiSpaceNewSpace* From(NewSpace* space) {
    DCHECK(!v8_flags.minor_ms);
    return static_cast<SemiSpaceNewSpace*>(space);
  }

  SemiSpaceNewSpace(Heap* heap, size_t initial_semispace_capacity,
                    size_t min_semispace_capacity_,
                    size_t max_semispace_capacity);

  ~SemiSpaceNewSpace() final = default;

  bool ContainsSlow(Address a) const final;

  // Grow the capacity of the semispaces.  Assumes that they are not at
  // their maximum capacity.
  void Grow(size_t new_capacity) final;

  // Shrink the capacity of the semispaces.
  void Shrink(size_t new_capacity);

  // Return the allocated bytes in the active semispace.
  size_t Size() const final;

  size_t SizeOfObjects() const final { return Size(); }

  // Return the allocatable capacity of a semispace.
  size_t Capacity() const final {
    size_t actual_capacity =
        std::max(to_space_.current_capacity(), target_capacity_);
    return (actual_capacity / PageMetadata::kPageSize) *
           MemoryChunkLayout::AllocatableMemoryInDataPage();
  }

  // Return the capacity of pages currently used for allocations. This is
  // a capped overapproximation of the size of objects.
  size_t CurrentCapacitySafe() const {
    return (to_space_.current_capacity_safe() / PageMetadata::kPageSize) *
           MemoryChunkLayout::AllocatableMemoryInDataPage();
  }

  // Return the current size of a semispace, allocatable and non-allocatable
  // memory.
  size_t TotalCapacity() const final { return target_capacity_; }

  // Committed memory for NewSpace is the committed memory of both semi-spaces
  // combined.
  size_t CommittedMemory() const final {
    return from_space_.CommittedMemory() + to_space_.CommittedMemory();
  }

  size_t MaximumCommittedMemory() const final {
    return from_space_.MaximumCommittedMemory() +
           to_space_.MaximumCommittedMemory();
  }

  // Approximate amount of physical memory committed for this space.
  size_t CommittedPhysicalMemory() const final;

  // Return the available bytes without growing.
  size_t Available() const final;

  bool ReachedTargetCapacity() const {
    return to_space_.current_capacity_ >= target_capacity_;
  }

  size_t ExternalBackingStoreBytes(ExternalBackingStoreType type) const final {
    if (type == ExternalBackingStoreType::kArrayBuffer)
      return heap()->YoungArrayBufferBytes();
    DCHECK_EQ(0, from_space_.ExternalBackingStoreBytes(type));
    return to_space_.ExternalBackingStoreBytes(type);
  }

  size_t AllocatedSinceLastGC() const final;

  void GrowToMaximumCapacityForTesting() final;

  // Return the maximum capacity of a semispace.
  size_t MaximumCapacity() const final { return maximum_capacity_; }

  // Returns the minimum capacity of a semispace.
  size_t MinimumCapacity() const final { return minimum_capacity_; }

  // Return the address of the first allocatable address in the active
  // semispace. This may be the address where the first object resides.
  Address first_allocatable_address() const final {
    return to_space_.space_start();
  }

  // Get the age mark of the inactive semispace.
  Address age_mark() const { return age_mark_; }

  // Sets the age mark to the current top pointer. It also sets proper page
  // flags for all pages before the age mark.
  void SetAgeMarkAndBelowAgeMarkPageFlags();

  // Try to switch the active semispace to a new, empty, page.
  // Returns false if this isn't possible or reasonable (i.e., there
  // are no pages, or the current page is already empty), or true
  // if successful.
  bool AddFreshPage();

  bool AddParkedAllocationBuffer(int size_in_bytes,
                                 AllocationAlignment alignment);

  void ResetParkedAllocationBuffers();

#ifdef VERIFY_HEAP
  // Verify the active semispace.
  void Verify(Isolate* isolate, SpaceVerificationVisitor* visitor) const final;

  // VerifyObjects verifies all objects in the active semi space.
  void VerifyObjects(Isolate* isolate, SpaceVerificationVisitor* visitor) const;
#endif

#ifdef DEBUG
  // Print the active semispace.
  void Print() override { to_space_.Print(); }
#endif

  void MakeIterable() override;

  void MakeAllPagesInFromSpaceIterable();
  void MakeUnusedPagesInToSpaceIterable();

  PageMetadata* first_page() final { return to_space_.first_page(); }
  PageMetadata* last_page() final { return to_space_.last_page(); }

  const PageMetadata* first_page() const final {
    return to_space_.first_page();
  }
  const PageMetadata* last_page() const final { return to_space_.last_page(); }

  iterator begin() final { return to_space_.begin(); }
  iterator end() final { return to_space_.end(); }

  const_iterator begin() const final { return to_space_.begin(); }
  const_iterator end() const final { return to_space_.end(); }

  std::unique_ptr<ObjectIterator> GetObjectIterator(Heap* heap) final;

  SemiSpace& from_space() { return from_space_; }
  const SemiSpace& from_space() const { return from_space_; }
  SemiSpace& to_space() { return to_space_; }
  const SemiSpace& to_space() const { return to_space_; }

  // Used for conservative stack scanning to determine if a page with pinned
  // objects should remain in new space or move to old space.
  bool ShouldPageBePromoted(const MemoryChunk* chunk) const;

  V8_INLINE bool ShouldBePromoted(Address object) const;

  // Invoked at the beginning of a GC to swap from- and to-space.
  void SwapSemiSpaces();

  void GarbageCollectionPrologue() final;
  void GarbageCollectionEpilogue() final;

  void ZapUnusedMemory();

  bool IsPromotionCandidate(const MutablePageMetadata* page) const final;

  AllocatorPolicy* CreateAllocatorPolicy(MainAllocator* allocator) final;

  int GetSpaceRemainingOnCurrentPageForTesting();
  void FillCurrentPageForTesting();

  void MoveQuarantinedPage(PageMetadata* metadata);
  size_t QuarantinedSize() const { return quarantined_size_; }
  size_t QuarantinedPageCount() const {
    return to_space_.quarantined_pages_count_;
  }
  void SetQuarantinedSize(size_t quarantined_size) {
    quarantined_size_ = quarantined_size;
  }

  V8_INLINE bool IsAddressBelowAgeMark(Address address) const;

 private:
  bool IsFromSpaceCommitted() const { return from_space_.IsCommitted(); }

  SemiSpace* active_space() { return &to_space_; }

  std::optional<std::pair<Address, Address>> Allocate(
      int size_in_bytes, AllocationAlignment alignment);

  std::optional<std::pair<Address, Address>> AllocateOnNewPageBeyondCapacity(
      int size_in_bytes, AllocationAlignment alignment);

  // Removes a page from the space. Assumes the page is in the `from_space` semi
  // space.
  void RemovePage(PageMetadata* page) final;

  // Frees the given memory region. Will be resuable for allocation if this was
  // the last allocation.
  void Free(Address start, Address end);

  void ResetAllocationTopToCurrentPageStart() {
    allocation_top_ =
        to_space_.current_page() ? to_space_.page_low() : kNullAddress;
  }

  void SetAllocationTop(Address top) { allocation_top_ = top; }

  V8_INLINE void IncrementAllocationTop(Address new_top);

  V8_INLINE void DecrementAllocationTop(Address new_top);

  Address allocation_top() const { return allocation_top_; }

  // The semispaces.
  SemiSpace to_space_;
  SemiSpace from_space_;

  // Bump pointer for allocation. to_space_.page_low() <= allocation_top_ <=
  // to_space.page_high() always holds.
  Address allocation_top_ = kNullAddress;

  ParkedAllocationBuffersVector parked_allocation_buffers_;

  // Current overall size of objects that were quarantined in the last GC.
  size_t quarantined_size_ = 0;

  // Size right after the last GC. Used for computing `AllocatedSinceLastGC()`.
  size_t size_after_last_gc_ = 0;

  // The minimum semi space capacity. A semi space cannot shrink below this
  // size.
  const size_t minimum_capacity_ = 0;

  // The maximum capacity of a semi space. A space cannot grow beyond that size.
  const size_t maximum_capacity_ = 0;

  // Used to govern object promotion during mark-compact collection.
  Address age_mark_ = kNullAddress;

  // The target capacity of a semi space.
  size_t target_capacity_ = 0;

  friend class SemiSpaceObjectIterator;
  friend class SemiSpaceNewSpaceAllocatorPolicy;
};

// -----------------------------------------------------------------------------
// PagedNewSpace

class V8_EXPORT_PRIVATE PagedSpaceForNewSpace final : public PagedSpaceBase {
 public:
  // Creates an old space object. The constructor does not allocate pages
  // from OS.
  explicit PagedSpaceForNewSpace(Heap* heap, size_t initial_capacity,
                                 size_t min_capacity, size_t max_capacity);

  void TearDown() { PagedSpaceBase::TearDown(); }

  // Grow the capacity of the space.
  void Grow(size_t new_capacity);
  void GrowToMaximumCapacityForTesting();

  // Shrink the capacity of the space.
  bool StartShrinking(size_t new_target_capacity);
  void FinishShrinking();

  size_t AllocatedSinceLastGC() const;

  // Return the minimum capacity of the space.
  size_t MinimumCapacity() const { return min_capacity_; }

  // Return the maximum capacity of the space.
  size_t MaximumCapacity() const { return max_capacity_; }

  size_t TotalCapacity() const { return target_capacity_; }

  // Return the address of the first allocatable address in the active
  // semispace. This may be the address where the first object resides.
  Address first_allocatable_address() const {
    return first_page()->area_start();
  }

  // Reset the allocation pointer.
  void GarbageCollectionEpilogue() {
    size_at_last_gc_ = Size();
    last_lab_page_ = nullptr;
  }

  PageMetadata* InitializePage(MutablePageMetadata* chunk) final;

  size_t AddPage(PageMetadata* page) final;
  void RemovePage(PageMetadata* page) final;
  void RemovePageFromSpace(PageMetadata* page) final;

  size_t ExternalBackingStoreBytes(ExternalBackingStoreType type) const final {
    if (type == ExternalBackingStoreType::kArrayBuffer)
      return heap()->YoungArrayBufferBytes();
    return external_backing_store_bytes_[static_cast<int>(type)];
  }

#ifdef VERIFY_HEAP
  void Verify(Isolate* isolate, SpaceVerificationVisitor* visitor) const final;
#endif  // VERIFY_HEAP

  void MakeIterable() { free_list()->RepairLists(heap()); }

  bool ShouldReleaseEmptyPage() const;

  // Allocates pages as long as current capacity is below the target capacity.
  void AllocatePageUpToCapacityForTesting();

  bool IsPromotionCandidate(const MutablePageMetadata* page) const;

  // Return the available bytes without growing.
  size_t Available() const final;

  size_t UsableCapacity() const {
    DCHECK_LE(free_list_->wasted_bytes(), current_capacity_);
    return current_capacity_ - free_list_->wasted_bytes();
  }

  AllocatorPolicy* CreateAllocatorPolicy(MainAllocator* allocator) final {
    UNREACHABLE();
  }

 private:
  bool AllocatePage();

  const size_t min_capacity_;
  const size_t max_capacity_;
  size_t target_capacity_ = 0;
  size_t current_capacity_ = 0;

  PageMetadata* last_lab_page_ = nullptr;

  friend class PagedNewSpaceAllocatorPolicy;
};

// TODO(v8:12612): PagedNewSpace is a bridge between the NewSpace interface and
// the PagedSpaceForNewSpace implementation. Once we settle on a single new
// space implementation, we can merge these 3 classes into 1.
class V8_EXPORT_PRIVATE PagedNewSpace final : public NewSpace {
 public:
  static PagedNewSpace* From(NewSpace* space) {
    DCHECK(v8_flags.minor_ms);
    return static_cast<PagedNewSpace*>(space);
  }

  PagedNewSpace(Heap* heap, size_t initial_capacity, size_t min_capacity,
                size_t max_capacity);

  ~PagedNewSpace() final;

  bool ContainsSlow(Address a) const final {
    return paged_space_.ContainsSlow(a);
  }

  // Grow the capacity of the space.
  void Grow(size_t new_capacity) final { paged_space_.Grow(new_capacity); }

  // Shrink the capacity of the space.
  bool StartShrinking(size_t new_target_capacity) {
    return paged_space_.StartShrinking(new_target_capacity);
  }
  void FinishShrinking() { paged_space_.FinishShrinking(); }

  // Return the allocated bytes in the active space.
  size_t Size() const final { return paged_space_.Size(); }

  size_t SizeOfObjects() const final { return paged_space_.SizeOfObjects(); }

  // Return the allocatable capacity of the space.
  size_t Capacity() const final { return paged_space_.Capacity(); }

  // Return the current size of the space, allocatable and non-allocatable
  // memory.
  size_t TotalCapacity() const final { return paged_space_.TotalCapacity(); }

  // Committed memory for PagedNewSpace.
  size_t CommittedMemory() const final {
    return paged_space_.CommittedMemory();
  }

  size_t MaximumCommittedMemory() const final {
    return paged_space_.MaximumCommittedMemory();
  }

  // Approximate amount of physical memory committed for this space.
  size_t CommittedPhysicalMemory() const final {
    return paged_space_.CommittedPhysicalMemory();
  }

  // Return the available bytes without growing.
  size_t Available() const final { return paged_space_.Available(); }

  size_t ExternalBackingStoreBytes(ExternalBackingStoreType type) const final {
    return paged_space_.ExternalBackingStoreBytes(type);
  }

  size_t AllocatedSinceLastGC() const final {
    return paged_space_.AllocatedSinceLastGC();
  }

  // Return the maximum capacity of the space.
  size_t MinimumCapacity() const final {
    return paged_space_.MinimumCapacity();
  }

  // Return the maximum capacity of the space.
  size_t MaximumCapacity() const final {
    return paged_space_.MaximumCapacity();
  }

  // Return the address of the first allocatable address in the active
  // semispace. This may be the address where the first object resides.
  Address first_allocatable_address() const final {
    return paged_space_.first_allocatable_address();
  }

#ifdef VERIFY_HEAP
  // Verify the active semispace.
  void Verify(Isolate* isolate, SpaceVerificationVisitor* visitor) const final {
    paged_space_.Verify(isolate, visitor);
  }
#endif

#ifdef DEBUG
  // Print the active semispace.
  void Print() final { paged_space_.Print(); }
#endif

  PageMetadata* first_page() final { return paged_space_.first_page(); }
  PageMetadata* last_page() final { return paged_space_.last_page(); }

  const PageMetadata* first_page() const final {
    return paged_space_.first_page();
  }
  const PageMetadata* last_page() const final {
    return paged_space_.last_page();
  }

  iterator begin() final { return paged_space_.begin(); }
  iterator end() final { return paged_space_.end(); }

  const_iterator begin() const final { return paged_space_.begin(); }
  const_iterator end() const final { return paged_space_.end(); }

  std::unique_ptr<ObjectIterator> GetObjectIterator(Heap* heap) final {
    return paged_space_.GetObjectIterator(heap);
  }

  void GarbageCollectionEpilogue() final {
    paged_space_.GarbageCollectionEpilogue();
  }

  bool IsPromotionCandidate(const MutablePageMetadata* page) const final {
    return paged_space_.IsPromotionCandidate(page);
  }

  void GrowToMaximumCapacityForTesting() final {
    paged_space_.GrowToMaximumCapacityForTesting();
  }

  PagedSpaceForNewSpace* paged_space() { return &paged_space_; }
  const PagedSpaceForNewSpace* paged_space() const { return &paged_space_; }

  void MakeIterable() override { paged_space_.MakeIterable(); }

  // All operations on `memory_chunk_list_` should go through `paged_space_`.
  heap::List<MutablePageMetadata>& memory_chunk_list() final { UNREACHABLE(); }

  bool ShouldReleaseEmptyPage() {
    return paged_space_.ShouldReleaseEmptyPage();
  }

  AllocatorPolicy* CreateAllocatorPolicy(MainAllocator* allocator) final;

 private:
  void RemovePage(PageMetadata* page) final { paged_space_.RemovePage(page); }

  PagedSpaceForNewSpace paged_space_;
};

// For contiguous spaces, top should be in the space (or at the end) and limit
// should be the end of the space.
#define DCHECK_SEMISPACE_ALLOCATION_TOP(top, space) \
  SLOW_DCHECK((space).page_low() <= (top) && (top) <= (space).page_high())

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_NEW_SPACES_H_
