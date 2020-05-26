// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SPACES_H_
#define V8_HEAP_SPACES_H_

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

#include "src/base/iterator.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/heap/basic-memory-chunk.h"
#include "src/heap/heap.h"
#include "src/heap/list.h"
#include "src/heap/memory-chunk.h"
#include "src/objects/free-space.h"
#include "src/objects/heap-object.h"
#include "src/objects/map.h"
#include "src/objects/objects.h"
#include "src/utils/allocation.h"
#include "src/utils/utils.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

namespace heap {
class HeapTester;
class TestCodePageAllocatorScope;
}  // namespace heap

class AllocationObserver;
class FreeList;
class Isolate;
class LargeObjectSpace;
class LargePage;
class LinearAllocationArea;
class LocalArrayBufferTracker;
class Page;
class PagedSpace;
class SemiSpace;

// -----------------------------------------------------------------------------
// Heap structures:
//
// A JS heap consists of a young generation, an old generation, and a large
// object space. The young generation is divided into two semispaces. A
// scavenger implements Cheney's copying algorithm. The old generation is
// separated into a map space and an old object space. The map space contains
// all (and only) map objects, the rest of old objects go into the old space.
// The old generation is collected by a mark-sweep-compact collector.
//
// The semispaces of the young generation are contiguous.  The old and map
// spaces consists of a list of pages. A page has a page header and an object
// area.
//
// There is a separate large object space for objects larger than
// kMaxRegularHeapObjectSize, so that they do not have to move during
// collection. The large object space is paged. Pages in large object space
// may be larger than the page size.
//
// A store-buffer based write barrier is used to keep track of intergenerational
// references.  See heap/store-buffer.h.
//
// During scavenges and mark-sweep collections we sometimes (after a store
// buffer overflow) iterate intergenerational pointers without decoding heap
// object maps so if the page belongs to old space or large object space
// it is essential to guarantee that the page does not contain any
// garbage pointers to new space: every pointer aligned word which satisfies
// the Heap::InNewSpace() predicate must be a pointer to a live heap object in
// new space. Thus objects in old space and large object spaces should have a
// special layout (e.g. no bare integer fields). This requirement does not
// apply to map space which is iterated in a special fashion. However we still
// require pointer fields of dead maps to be cleaned.
//
// To enable lazy cleaning of old space pages we can mark chunks of the page
// as being garbage.  Garbage sections are marked with a special map.  These
// sections are skipped when scanning the page, even if we are otherwise
// scanning without regard for object boundaries.  Garbage sections are chained
// together to form a free list after a GC.  Garbage sections created outside
// of GCs by object trunctation etc. may not be in the free list chain.  Very
// small free spaces are ignored, they need only be cleaned of bogus pointers
// into new space.
//
// Each page may have up to one special garbage section.  The start of this
// section is denoted by the top field in the space.  The end of the section
// is denoted by the limit field in the space.  This special garbage section
// is not marked with a free space map in the data.  The point of this section
// is to enable linear allocation without having to constantly update the byte
// array every time the top field is updated and a new object is created.  The
// special garbage section is not in the chain of garbage sections.
//
// Since the top and limit fields are in the space, not the page, only one page
// has a special garbage section, and if the top and limit are equal then there
// is no special garbage section.

// Some assertion macros used in the debugging mode.

#define DCHECK_OBJECT_SIZE(size) \
  DCHECK((0 < size) && (size <= kMaxRegularHeapObjectSize))

#define DCHECK_CODEOBJECT_SIZE(size, code_space) \
  DCHECK((0 < size) && (size <= code_space->AreaSize()))

using FreeListCategoryType = int32_t;

static const FreeListCategoryType kFirstCategory = 0;
static const FreeListCategoryType kInvalidCategory = -1;

enum FreeMode { kLinkCategory, kDoNotLinkCategory };

enum class SpaceAccountingMode { kSpaceAccounted, kSpaceUnaccounted };

// A free list category maintains a linked list of free memory blocks.
class FreeListCategory {
 public:
  void Initialize(FreeListCategoryType type) {
    type_ = type;
    available_ = 0;
    prev_ = nullptr;
    next_ = nullptr;
  }

  void Reset(FreeList* owner);

  void RepairFreeList(Heap* heap);

  // Relinks the category into the currently owning free list. Requires that the
  // category is currently unlinked.
  void Relink(FreeList* owner);

  void Free(Address address, size_t size_in_bytes, FreeMode mode,
            FreeList* owner);

  // Performs a single try to pick a node of at least |minimum_size| from the
  // category. Stores the actual size in |node_size|. Returns nullptr if no
  // node is found.
  FreeSpace PickNodeFromList(size_t minimum_size, size_t* node_size);

  // Picks a node of at least |minimum_size| from the category. Stores the
  // actual size in |node_size|. Returns nullptr if no node is found.
  FreeSpace SearchForNodeInList(size_t minimum_size, size_t* node_size);

  inline bool is_linked(FreeList* owner) const;
  bool is_empty() { return top().is_null(); }
  uint32_t available() const { return available_; }

  size_t SumFreeList();
  int FreeListLength();

 private:
  // For debug builds we accurately compute free lists lengths up until
  // {kVeryLongFreeList} by manually walking the list.
  static const int kVeryLongFreeList = 500;

  // Updates |available_|, |length_| and free_list_->Available() after an
  // allocation of size |allocation_size|.
  inline void UpdateCountersAfterAllocation(size_t allocation_size);

  FreeSpace top() { return top_; }
  void set_top(FreeSpace top) { top_ = top; }
  FreeListCategory* prev() { return prev_; }
  void set_prev(FreeListCategory* prev) { prev_ = prev; }
  FreeListCategory* next() { return next_; }
  void set_next(FreeListCategory* next) { next_ = next; }

  // |type_|: The type of this free list category.
  FreeListCategoryType type_ = kInvalidCategory;

  // |available_|: Total available bytes in all blocks of this free list
  // category.
  uint32_t available_ = 0;

  // |top_|: Points to the top FreeSpace in the free list category.
  FreeSpace top_;

  FreeListCategory* prev_ = nullptr;
  FreeListCategory* next_ = nullptr;

  friend class FreeList;
  friend class FreeListManyCached;
  friend class PagedSpace;
  friend class MapSpace;
};

// A free list maintains free blocks of memory. The free list is organized in
// a way to encourage objects allocated around the same time to be near each
// other. The normal way to allocate is intended to be by bumping a 'top'
// pointer until it hits a 'limit' pointer.  When the limit is hit we need to
// find a new space to allocate from. This is done with the free list, which is
// divided up into rough categories to cut down on waste. Having finer
// categories would scatter allocation more.
class FreeList {
 public:
  // Creates a Freelist of the default class (FreeListLegacy for now).
  V8_EXPORT_PRIVATE static FreeList* CreateFreeList();

  virtual ~FreeList() = default;

  // Returns how much memory can be allocated after freeing maximum_freed
  // memory.
  virtual size_t GuaranteedAllocatable(size_t maximum_freed) = 0;

  // Adds a node on the free list. The block of size {size_in_bytes} starting
  // at {start} is placed on the free list. The return value is the number of
  // bytes that were not added to the free list, because the freed memory block
  // was too small. Bookkeeping information will be written to the block, i.e.,
  // its contents will be destroyed. The start address should be word aligned,
  // and the size should be a non-zero multiple of the word size.
  virtual size_t Free(Address start, size_t size_in_bytes, FreeMode mode);

  // Allocates a free space node frome the free list of at least size_in_bytes
  // bytes. Returns the actual node size in node_size which can be bigger than
  // size_in_bytes. This method returns null if the allocation request cannot be
  // handled by the free list.
  virtual V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                                   size_t* node_size,
                                                   AllocationOrigin origin) = 0;

  // Returns a page containing an entry for a given type, or nullptr otherwise.
  V8_EXPORT_PRIVATE virtual Page* GetPageForSize(size_t size_in_bytes) = 0;

  virtual void Reset();

  // Return the number of bytes available on the free list.
  size_t Available() {
    DCHECK(available_ == SumFreeLists());
    return available_;
  }

  // Update number of available  bytes on the Freelists.
  void IncreaseAvailableBytes(size_t bytes) { available_ += bytes; }
  void DecreaseAvailableBytes(size_t bytes) { available_ -= bytes; }

  bool IsEmpty() {
    bool empty = true;
    ForAllFreeListCategories([&empty](FreeListCategory* category) {
      if (!category->is_empty()) empty = false;
    });
    return empty;
  }

  // Used after booting the VM.
  void RepairLists(Heap* heap);

  V8_EXPORT_PRIVATE size_t EvictFreeListItems(Page* page);

  int number_of_categories() { return number_of_categories_; }
  FreeListCategoryType last_category() { return last_category_; }

  size_t wasted_bytes() { return wasted_bytes_; }

  template <typename Callback>
  void ForAllFreeListCategories(FreeListCategoryType type, Callback callback) {
    FreeListCategory* current = categories_[type];
    while (current != nullptr) {
      FreeListCategory* next = current->next();
      callback(current);
      current = next;
    }
  }

  template <typename Callback>
  void ForAllFreeListCategories(Callback callback) {
    for (int i = kFirstCategory; i < number_of_categories(); i++) {
      ForAllFreeListCategories(static_cast<FreeListCategoryType>(i), callback);
    }
  }

  virtual bool AddCategory(FreeListCategory* category);
  virtual V8_EXPORT_PRIVATE void RemoveCategory(FreeListCategory* category);
  void PrintCategories(FreeListCategoryType type);

 protected:
  class FreeListCategoryIterator final {
   public:
    FreeListCategoryIterator(FreeList* free_list, FreeListCategoryType type)
        : current_(free_list->categories_[type]) {}

    bool HasNext() const { return current_ != nullptr; }

    FreeListCategory* Next() {
      DCHECK(HasNext());
      FreeListCategory* tmp = current_;
      current_ = current_->next();
      return tmp;
    }

   private:
    FreeListCategory* current_;
  };

#ifdef DEBUG
  V8_EXPORT_PRIVATE size_t SumFreeLists();
  bool IsVeryLong();
#endif

  // Tries to retrieve a node from the first category in a given |type|.
  // Returns nullptr if the category is empty or the top entry is smaller
  // than minimum_size.
  FreeSpace TryFindNodeIn(FreeListCategoryType type, size_t minimum_size,
                          size_t* node_size);

  // Searches a given |type| for a node of at least |minimum_size|.
  FreeSpace SearchForNodeInList(FreeListCategoryType type, size_t minimum_size,
                                size_t* node_size);

  // Returns the smallest category in which an object of |size_in_bytes| could
  // fit.
  virtual FreeListCategoryType SelectFreeListCategoryType(
      size_t size_in_bytes) = 0;

  FreeListCategory* top(FreeListCategoryType type) const {
    return categories_[type];
  }

  inline Page* GetPageForCategoryType(FreeListCategoryType type);

  int number_of_categories_ = 0;
  FreeListCategoryType last_category_ = 0;
  size_t min_block_size_ = 0;

  std::atomic<size_t> wasted_bytes_{0};
  FreeListCategory** categories_ = nullptr;

  // |available_|: The number of bytes in this freelist.
  size_t available_ = 0;

  friend class FreeListCategory;
  friend class Page;
  friend class MemoryChunk;
  friend class ReadOnlyPage;
  friend class MapSpace;
};

// FreeList used for spaces that don't have freelists
// (only the LargeObject space for now).
class NoFreeList final : public FreeList {
 public:
  size_t GuaranteedAllocatable(size_t maximum_freed) final {
    FATAL("NoFreeList can't be used as a standard FreeList. ");
  }
  size_t Free(Address start, size_t size_in_bytes, FreeMode mode) final {
    FATAL("NoFreeList can't be used as a standard FreeList.");
  }
  V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                           size_t* node_size,
                                           AllocationOrigin origin) final {
    FATAL("NoFreeList can't be used as a standard FreeList.");
  }
  Page* GetPageForSize(size_t size_in_bytes) final {
    FATAL("NoFreeList can't be used as a standard FreeList.");
  }

 private:
  FreeListCategoryType SelectFreeListCategoryType(size_t size_in_bytes) final {
    FATAL("NoFreeList can't be used as a standard FreeList.");
  }
};

// ----------------------------------------------------------------------------
// Space is the abstract superclass for all allocation spaces.
class V8_EXPORT_PRIVATE Space : public Malloced {
 public:
  Space(Heap* heap, AllocationSpace id, FreeList* free_list)
      : allocation_observers_paused_(false),
        heap_(heap),
        id_(id),
        committed_(0),
        max_committed_(0),
        free_list_(std::unique_ptr<FreeList>(free_list)) {
    external_backing_store_bytes_ =
        new std::atomic<size_t>[ExternalBackingStoreType::kNumTypes];
    external_backing_store_bytes_[ExternalBackingStoreType::kArrayBuffer] = 0;
    external_backing_store_bytes_[ExternalBackingStoreType::kExternalString] =
        0;
  }

  static inline void MoveExternalBackingStoreBytes(
      ExternalBackingStoreType type, Space* from, Space* to, size_t amount);

  virtual ~Space() {
    delete[] external_backing_store_bytes_;
    external_backing_store_bytes_ = nullptr;
  }

  Heap* heap() const {
    DCHECK_NOT_NULL(heap_);
    return heap_;
  }

  bool IsDetached() const { return heap_ == nullptr; }

  AllocationSpace identity() { return id_; }

  const char* name() { return Heap::GetSpaceName(id_); }

  virtual void AddAllocationObserver(AllocationObserver* observer);

  virtual void RemoveAllocationObserver(AllocationObserver* observer);

  virtual void PauseAllocationObservers();

  virtual void ResumeAllocationObservers();

  virtual void StartNextInlineAllocationStep() {}

  void AllocationStep(int bytes_since_last, Address soon_object, int size);

  // An AllocationStep equivalent to be called after merging a contiguous
  // chunk of an off-thread space into this space. The chunk is treated as a
  // single allocation-folding group.
  void AllocationStepAfterMerge(Address first_object_in_chunk, int size);

  // Return the total amount committed memory for this space, i.e., allocatable
  // memory and page headers.
  virtual size_t CommittedMemory() { return committed_; }

  virtual size_t MaximumCommittedMemory() { return max_committed_; }

  // Returns allocated size.
  virtual size_t Size() = 0;

  // Returns size of objects. Can differ from the allocated size
  // (e.g. see OldLargeObjectSpace).
  virtual size_t SizeOfObjects() { return Size(); }

  // Approximate amount of physical memory committed for this space.
  virtual size_t CommittedPhysicalMemory() = 0;

  // Return the available bytes without growing.
  virtual size_t Available() = 0;

  virtual int RoundSizeDownToObjectAlignment(int size) {
    if (id_ == CODE_SPACE) {
      return RoundDown(size, kCodeAlignment);
    } else {
      return RoundDown(size, kTaggedSize);
    }
  }

  virtual std::unique_ptr<ObjectIterator> GetObjectIterator(Heap* heap) = 0;

  void AccountCommitted(size_t bytes) {
    DCHECK_GE(committed_ + bytes, committed_);
    committed_ += bytes;
    if (committed_ > max_committed_) {
      max_committed_ = committed_;
    }
  }

  void AccountUncommitted(size_t bytes) {
    DCHECK_GE(committed_, committed_ - bytes);
    committed_ -= bytes;
  }

  inline void IncrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                 size_t amount);

  inline void DecrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                                 size_t amount);

  // Returns amount of off-heap memory in-use by objects in this Space.
  virtual size_t ExternalBackingStoreBytes(
      ExternalBackingStoreType type) const {
    return external_backing_store_bytes_[type];
  }

  void* GetRandomMmapAddr();

  MemoryChunk* first_page() { return memory_chunk_list_.front(); }
  MemoryChunk* last_page() { return memory_chunk_list_.back(); }

  const MemoryChunk* first_page() const { return memory_chunk_list_.front(); }
  const MemoryChunk* last_page() const { return memory_chunk_list_.back(); }

  heap::List<MemoryChunk>& memory_chunk_list() { return memory_chunk_list_; }

  FreeList* free_list() { return free_list_.get(); }

#ifdef DEBUG
  virtual void Print() = 0;
#endif

 protected:
  intptr_t GetNextInlineAllocationStepSize();
  bool AllocationObserversActive() {
    return !allocation_observers_paused_ && !allocation_observers_.empty();
  }

  void DetachFromHeap() { heap_ = nullptr; }

  std::vector<AllocationObserver*> allocation_observers_;

  // The List manages the pages that belong to the given space.
  heap::List<MemoryChunk> memory_chunk_list_;

  // Tracks off-heap memory used by this space.
  std::atomic<size_t>* external_backing_store_bytes_;

  bool allocation_observers_paused_;
  Heap* heap_;
  AllocationSpace id_;

  // Keeps track of committed memory in a space.
  std::atomic<size_t> committed_;
  size_t max_committed_;

  std::unique_ptr<FreeList> free_list_;

  DISALLOW_COPY_AND_ASSIGN(Space);
};

STATIC_ASSERT(sizeof(std::atomic<intptr_t>) == kSystemPointerSize);

// -----------------------------------------------------------------------------
// A page is a memory chunk of a size 256K. Large object pages may be larger.
//
// The only way to get a page pointer is by calling factory methods:
//   Page* p = Page::FromAddress(addr); or
//   Page* p = Page::FromAllocationAreaAddress(address);
class Page : public MemoryChunk {
 public:
  static const intptr_t kCopyAllFlags = ~0;

  // Page flags copied from from-space to to-space when flipping semispaces.
  static const intptr_t kCopyOnFlipFlagsMask =
      static_cast<intptr_t>(MemoryChunk::POINTERS_TO_HERE_ARE_INTERESTING) |
      static_cast<intptr_t>(MemoryChunk::POINTERS_FROM_HERE_ARE_INTERESTING) |
      static_cast<intptr_t>(MemoryChunk::INCREMENTAL_MARKING);

  // Returns the page containing a given address. The address ranges
  // from [page_addr .. page_addr + kPageSize[. This only works if the object
  // is in fact in a page.
  static Page* FromAddress(Address addr) {
    return reinterpret_cast<Page*>(addr & ~kPageAlignmentMask);
  }
  static Page* FromHeapObject(HeapObject o) {
    return reinterpret_cast<Page*>(o.ptr() & ~kAlignmentMask);
  }

  // Returns the page containing the address provided. The address can
  // potentially point righter after the page. To be also safe for tagged values
  // we subtract a hole word. The valid address ranges from
  // [page_addr + area_start_ .. page_addr + kPageSize + kTaggedSize].
  static Page* FromAllocationAreaAddress(Address address) {
    return Page::FromAddress(address - kTaggedSize);
  }

  // Checks if address1 and address2 are on the same new space page.
  static bool OnSamePage(Address address1, Address address2) {
    return Page::FromAddress(address1) == Page::FromAddress(address2);
  }

  // Checks whether an address is page aligned.
  static bool IsAlignedToPageSize(Address addr) {
    return (addr & kPageAlignmentMask) == 0;
  }

  static Page* ConvertNewToOld(Page* old_page);

  inline void MarkNeverAllocateForTesting();
  inline void MarkEvacuationCandidate();
  inline void ClearEvacuationCandidate();

  Page* next_page() { return static_cast<Page*>(list_node_.next()); }
  Page* prev_page() { return static_cast<Page*>(list_node_.prev()); }

  const Page* next_page() const {
    return static_cast<const Page*>(list_node_.next());
  }
  const Page* prev_page() const {
    return static_cast<const Page*>(list_node_.prev());
  }

  template <typename Callback>
  inline void ForAllFreeListCategories(Callback callback) {
    for (int i = kFirstCategory;
         i < owner()->free_list()->number_of_categories(); i++) {
      callback(categories_[i]);
    }
  }

  // Returns the offset of a given address to this page.
  inline size_t Offset(Address a) { return static_cast<size_t>(a - address()); }

  // Returns the address for a given offset to the this page.
  Address OffsetToAddress(size_t offset) {
    Address address_in_page = address() + offset;
    DCHECK_GE(address_in_page, area_start());
    DCHECK_LT(address_in_page, area_end());
    return address_in_page;
  }

  void AllocateLocalTracker();
  inline LocalArrayBufferTracker* local_tracker() { return local_tracker_; }
  bool contains_array_buffers();

  size_t AvailableInFreeList();

  size_t AvailableInFreeListFromAllocatedBytes() {
    DCHECK_GE(area_size(), wasted_memory() + allocated_bytes());
    return area_size() - wasted_memory() - allocated_bytes();
  }

  FreeListCategory* free_list_category(FreeListCategoryType type) {
    return categories_[type];
  }

  size_t wasted_memory() { return wasted_memory_; }
  void add_wasted_memory(size_t waste) { wasted_memory_ += waste; }
  size_t allocated_bytes() { return allocated_bytes_; }
  void IncreaseAllocatedBytes(size_t bytes) {
    DCHECK_LE(bytes, area_size());
    allocated_bytes_ += bytes;
  }
  void DecreaseAllocatedBytes(size_t bytes) {
    DCHECK_LE(bytes, area_size());
    DCHECK_GE(allocated_bytes(), bytes);
    allocated_bytes_ -= bytes;
  }

  void ResetAllocationStatistics();

  size_t ShrinkToHighWaterMark();

  V8_EXPORT_PRIVATE void CreateBlackArea(Address start, Address end);
  V8_EXPORT_PRIVATE void CreateBlackAreaBackground(Address start, Address end);
  void DestroyBlackArea(Address start, Address end);
  void DestroyBlackAreaBackground(Address start, Address end);

  void InitializeFreeListCategories();
  void AllocateFreeListCategories();
  void ReleaseFreeListCategories();

  void MoveOldToNewRememberedSetForSweeping();
  void MergeOldToNewRememberedSets();

 private:
  friend class MemoryAllocator;
};

// Validate our estimates on the header size.
STATIC_ASSERT(sizeof(BasicMemoryChunk) <= BasicMemoryChunk::kHeaderSize);
STATIC_ASSERT(sizeof(MemoryChunk) <= MemoryChunk::kHeaderSize);
STATIC_ASSERT(sizeof(Page) <= MemoryChunk::kHeaderSize);

// -----------------------------------------------------------------------------
// Interface for heap object iterator to be implemented by all object space
// object iterators.

class V8_EXPORT_PRIVATE ObjectIterator : public Malloced {
 public:
  virtual ~ObjectIterator() = default;
  virtual HeapObject Next() = 0;
};

template <class PAGE_TYPE>
class PageIteratorImpl
    : public base::iterator<std::forward_iterator_tag, PAGE_TYPE> {
 public:
  explicit PageIteratorImpl(PAGE_TYPE* p) : p_(p) {}
  PageIteratorImpl(const PageIteratorImpl<PAGE_TYPE>& other) : p_(other.p_) {}
  PAGE_TYPE* operator*() { return p_; }
  bool operator==(const PageIteratorImpl<PAGE_TYPE>& rhs) {
    return rhs.p_ == p_;
  }
  bool operator!=(const PageIteratorImpl<PAGE_TYPE>& rhs) {
    return rhs.p_ != p_;
  }
  inline PageIteratorImpl<PAGE_TYPE>& operator++();
  inline PageIteratorImpl<PAGE_TYPE> operator++(int);

 private:
  PAGE_TYPE* p_;
};

using PageIterator = PageIteratorImpl<Page>;
using ConstPageIterator = PageIteratorImpl<const Page>;
using LargePageIterator = PageIteratorImpl<LargePage>;

class PageRange {
 public:
  using iterator = PageIterator;
  PageRange(Page* begin, Page* end) : begin_(begin), end_(end) {}
  explicit PageRange(Page* page) : PageRange(page, page->next_page()) {}
  inline PageRange(Address start, Address limit);

  iterator begin() { return iterator(begin_); }
  iterator end() { return iterator(end_); }

 private:
  Page* begin_;
  Page* end_;
};

// -----------------------------------------------------------------------------
// A space has a circular list of pages. The next page can be accessed via
// Page::next_page() call.

// An abstraction of allocation and relocation pointers in a page-structured
// space.
class LinearAllocationArea {
 public:
  LinearAllocationArea() : top_(kNullAddress), limit_(kNullAddress) {}
  LinearAllocationArea(Address top, Address limit) : top_(top), limit_(limit) {}

  void Reset(Address top, Address limit) {
    set_top(top);
    set_limit(limit);
  }

  V8_INLINE void set_top(Address top) {
    SLOW_DCHECK(top == kNullAddress || (top & kHeapObjectTagMask) == 0);
    top_ = top;
  }

  V8_INLINE Address top() const {
    SLOW_DCHECK(top_ == kNullAddress || (top_ & kHeapObjectTagMask) == 0);
    return top_;
  }

  Address* top_address() { return &top_; }

  V8_INLINE void set_limit(Address limit) { limit_ = limit; }

  V8_INLINE Address limit() const { return limit_; }

  Address* limit_address() { return &limit_; }

#ifdef DEBUG
  bool VerifyPagedAllocation() {
    return (Page::FromAllocationAreaAddress(top_) ==
            Page::FromAllocationAreaAddress(limit_)) &&
           (top_ <= limit_);
  }
#endif

 private:
  // Current allocation top.
  Address top_;
  // Current allocation limit.
  Address limit_;
};

// An abstraction of the accounting statistics of a page-structured space.
//
// The stats are only set by functions that ensure they stay balanced. These
// functions increase or decrease one of the non-capacity stats in conjunction
// with capacity, or else they always balance increases and decreases to the
// non-capacity stats.
class AllocationStats {
 public:
  AllocationStats() { Clear(); }

  AllocationStats& operator=(const AllocationStats& stats) V8_NOEXCEPT {
    capacity_ = stats.capacity_.load();
    max_capacity_ = stats.max_capacity_;
    size_.store(stats.size_);
#ifdef DEBUG
    allocated_on_page_ = stats.allocated_on_page_;
#endif
    return *this;
  }

  // Zero out all the allocation statistics (i.e., no capacity).
  void Clear() {
    capacity_ = 0;
    max_capacity_ = 0;
    ClearSize();
  }

  void ClearSize() {
    size_ = 0;
#ifdef DEBUG
    allocated_on_page_.clear();
#endif
  }

  // Accessors for the allocation statistics.
  size_t Capacity() { return capacity_; }
  size_t MaxCapacity() { return max_capacity_; }
  size_t Size() { return size_; }
#ifdef DEBUG
  size_t AllocatedOnPage(Page* page) { return allocated_on_page_[page]; }
#endif

  void IncreaseAllocatedBytes(size_t bytes, Page* page) {
#ifdef DEBUG
    size_t size = size_;
    DCHECK_GE(size + bytes, size);
#endif
    size_.fetch_add(bytes);
#ifdef DEBUG
    allocated_on_page_[page] += bytes;
#endif
  }

  void DecreaseAllocatedBytes(size_t bytes, Page* page) {
    DCHECK_GE(size_, bytes);
    size_.fetch_sub(bytes);
#ifdef DEBUG
    DCHECK_GE(allocated_on_page_[page], bytes);
    allocated_on_page_[page] -= bytes;
#endif
  }

  void DecreaseCapacity(size_t bytes) {
    DCHECK_GE(capacity_, bytes);
    DCHECK_GE(capacity_ - bytes, size_);
    capacity_ -= bytes;
  }

  void IncreaseCapacity(size_t bytes) {
    DCHECK_GE(capacity_ + bytes, capacity_);
    capacity_ += bytes;
    if (capacity_ > max_capacity_) {
      max_capacity_ = capacity_;
    }
  }

 private:
  // |capacity_|: The number of object-area bytes (i.e., not including page
  // bookkeeping structures) currently in the space.
  // During evacuation capacity of the main spaces is accessed from multiple
  // threads to check the old generation hard limit.
  std::atomic<size_t> capacity_;

  // |max_capacity_|: The maximum capacity ever observed.
  size_t max_capacity_;

  // |size_|: The number of allocated bytes.
  std::atomic<size_t> size_;

#ifdef DEBUG
  std::unordered_map<Page*, size_t, Page::Hasher> allocated_on_page_;
#endif
};

// The free list is organized in categories as follows:
// kMinBlockSize-10 words (tiniest): The tiniest blocks are only used for
//   allocation, when categories >= small do not have entries anymore.
// 11-31 words (tiny): The tiny blocks are only used for allocation, when
//   categories >= small do not have entries anymore.
// 32-255 words (small): Used for allocating free space between 1-31 words in
//   size.
// 256-2047 words (medium): Used for allocating free space between 32-255 words
//   in size.
// 1048-16383 words (large): Used for allocating free space between 256-2047
//   words in size.
// At least 16384 words (huge): This list is for objects of 2048 words or
//   larger. Empty pages are also added to this list.
class V8_EXPORT_PRIVATE FreeListLegacy : public FreeList {
 public:
  size_t GuaranteedAllocatable(size_t maximum_freed) override {
    if (maximum_freed <= kTiniestListMax) {
      // Since we are not iterating over all list entries, we cannot guarantee
      // that we can find the maximum freed block in that free list.
      return 0;
    } else if (maximum_freed <= kTinyListMax) {
      return kTinyAllocationMax;
    } else if (maximum_freed <= kSmallListMax) {
      return kSmallAllocationMax;
    } else if (maximum_freed <= kMediumListMax) {
      return kMediumAllocationMax;
    } else if (maximum_freed <= kLargeListMax) {
      return kLargeAllocationMax;
    }
    return maximum_freed;
  }

  inline Page* GetPageForSize(size_t size_in_bytes) override;

  FreeListLegacy();
  ~FreeListLegacy() override;

  V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                           size_t* node_size,
                                           AllocationOrigin origin) override;

 private:
  enum { kTiniest, kTiny, kSmall, kMedium, kLarge, kHuge };

  static const size_t kMinBlockSize = 3 * kTaggedSize;

  // This is a conservative upper bound. The actual maximum block size takes
  // padding and alignment of data and code pages into account.
  static const size_t kMaxBlockSize = Page::kPageSize;

  static const size_t kTiniestListMax = 0xa * kTaggedSize;
  static const size_t kTinyListMax = 0x1f * kTaggedSize;
  static const size_t kSmallListMax = 0xff * kTaggedSize;
  static const size_t kMediumListMax = 0x7ff * kTaggedSize;
  static const size_t kLargeListMax = 0x1fff * kTaggedSize;
  static const size_t kTinyAllocationMax = kTiniestListMax;
  static const size_t kSmallAllocationMax = kTinyListMax;
  static const size_t kMediumAllocationMax = kSmallListMax;
  static const size_t kLargeAllocationMax = kMediumListMax;

  FreeListCategoryType SelectFreeListCategoryType(
      size_t size_in_bytes) override {
    if (size_in_bytes <= kTiniestListMax) {
      return kTiniest;
    } else if (size_in_bytes <= kTinyListMax) {
      return kTiny;
    } else if (size_in_bytes <= kSmallListMax) {
      return kSmall;
    } else if (size_in_bytes <= kMediumListMax) {
      return kMedium;
    } else if (size_in_bytes <= kLargeListMax) {
      return kLarge;
    }
    return kHuge;
  }

  // Returns the category to be used to allocate |size_in_bytes| in the fast
  // path. The tiny categories are not used for fast allocation.
  FreeListCategoryType SelectFastAllocationFreeListCategoryType(
      size_t size_in_bytes) {
    if (size_in_bytes <= kSmallAllocationMax) {
      return kSmall;
    } else if (size_in_bytes <= kMediumAllocationMax) {
      return kMedium;
    } else if (size_in_bytes <= kLargeAllocationMax) {
      return kLarge;
    }
    return kHuge;
  }

  friend class FreeListCategory;
  friend class heap::HeapTester;
};

// Inspired by FreeListLegacy.
// Only has 3 categories: Medium, Large and Huge.
// Any block that would have belong to tiniest, tiny or small in FreeListLegacy
// is considered wasted.
// Allocation is done only in Huge, Medium and Large (in that order),
// using a first-fit strategy (only the first block of each freelist is ever
// considered though). Performances is supposed to be better than
// FreeListLegacy, but memory usage should be higher (because fragmentation will
// probably be higher).
class V8_EXPORT_PRIVATE FreeListFastAlloc : public FreeList {
 public:
  size_t GuaranteedAllocatable(size_t maximum_freed) override {
    if (maximum_freed <= kMediumListMax) {
      // Since we are not iterating over all list entries, we cannot guarantee
      // that we can find the maximum freed block in that free list.
      return 0;
    } else if (maximum_freed <= kLargeListMax) {
      return kLargeAllocationMax;
    }
    return kHugeAllocationMax;
  }

  inline Page* GetPageForSize(size_t size_in_bytes) override;

  FreeListFastAlloc();
  ~FreeListFastAlloc() override;

  V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                           size_t* node_size,
                                           AllocationOrigin origin) override;

 private:
  enum { kMedium, kLarge, kHuge };

  static const size_t kMinBlockSize = 0xff * kTaggedSize;

  // This is a conservative upper bound. The actual maximum block size takes
  // padding and alignment of data and code pages into account.
  static const size_t kMaxBlockSize = Page::kPageSize;

  static const size_t kMediumListMax = 0x7ff * kTaggedSize;
  static const size_t kLargeListMax = 0x1fff * kTaggedSize;
  static const size_t kMediumAllocationMax = kMinBlockSize;
  static const size_t kLargeAllocationMax = kMediumListMax;
  static const size_t kHugeAllocationMax = kLargeListMax;

  // Returns the category used to hold an object of size |size_in_bytes|.
  FreeListCategoryType SelectFreeListCategoryType(
      size_t size_in_bytes) override {
    if (size_in_bytes <= kMediumListMax) {
      return kMedium;
    } else if (size_in_bytes <= kLargeListMax) {
      return kLarge;
    }
    return kHuge;
  }
};

// Use 24 Freelists: on per 16 bytes between 24 and 256, and then a few ones for
// larger sizes. See the variable |categories_min| for the size of each
// Freelist.  Allocation is done using a best-fit strategy (considering only the
// first element of each category though).
// Performances are expected to be worst than FreeListLegacy, but memory
// consumption should be lower (since fragmentation should be lower).
class V8_EXPORT_PRIVATE FreeListMany : public FreeList {
 public:
  size_t GuaranteedAllocatable(size_t maximum_freed) override;

  Page* GetPageForSize(size_t size_in_bytes) override;

  FreeListMany();
  ~FreeListMany() override;

  V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                           size_t* node_size,
                                           AllocationOrigin origin) override;

 protected:
  static const size_t kMinBlockSize = 3 * kTaggedSize;

  // This is a conservative upper bound. The actual maximum block size takes
  // padding and alignment of data and code pages into account.
  static const size_t kMaxBlockSize = Page::kPageSize;
  // Largest size for which categories are still precise, and for which we can
  // therefore compute the category in constant time.
  static const size_t kPreciseCategoryMaxSize = 256;

  // Categories boundaries generated with:
  // perl -E '
  //      @cat = (24, map {$_*16} 2..16, 48, 64);
  //      while ($cat[-1] <= 32768) {
  //        push @cat, $cat[-1]*2
  //      }
  //      say join ", ", @cat;
  //      say "\n", scalar @cat'
  static const int kNumberOfCategories = 24;
  static constexpr unsigned int categories_min[kNumberOfCategories] = {
      24,  32,  48,  64,  80,  96,   112,  128,  144,  160,   176,   192,
      208, 224, 240, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};

  // Return the smallest category that could hold |size_in_bytes| bytes.
  FreeListCategoryType SelectFreeListCategoryType(
      size_t size_in_bytes) override {
    if (size_in_bytes <= kPreciseCategoryMaxSize) {
      if (size_in_bytes < categories_min[1]) return 0;
      return static_cast<FreeListCategoryType>(size_in_bytes >> 4) - 1;
    }
    for (int cat = (kPreciseCategoryMaxSize >> 4) - 1; cat < last_category_;
         cat++) {
      if (size_in_bytes < categories_min[cat + 1]) {
        return cat;
      }
    }
    return last_category_;
  }

  FRIEND_TEST(SpacesTest, FreeListManySelectFreeListCategoryType);
  FRIEND_TEST(SpacesTest, FreeListManyGuaranteedAllocatable);
};

// Same as FreeListMany but uses a cache to know which categories are empty.
// The cache (|next_nonempty_category|) is maintained in a way such that for
// each category c, next_nonempty_category[c] contains the first non-empty
// category greater or equal to c, that may hold an object of size c.
// Allocation is done using the same strategy as FreeListMany (ie, best fit).
class V8_EXPORT_PRIVATE FreeListManyCached : public FreeListMany {
 public:
  FreeListManyCached();

  V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                           size_t* node_size,
                                           AllocationOrigin origin) override;

  size_t Free(Address start, size_t size_in_bytes, FreeMode mode) override;

  void Reset() override;

  bool AddCategory(FreeListCategory* category) override;
  void RemoveCategory(FreeListCategory* category) override;

 protected:
  // Updates the cache after adding something in the category |cat|.
  void UpdateCacheAfterAddition(FreeListCategoryType cat) {
    for (int i = cat; i >= kFirstCategory && next_nonempty_category[i] > cat;
         i--) {
      next_nonempty_category[i] = cat;
    }
  }

  // Updates the cache after emptying category |cat|.
  void UpdateCacheAfterRemoval(FreeListCategoryType cat) {
    for (int i = cat; i >= kFirstCategory && next_nonempty_category[i] == cat;
         i--) {
      next_nonempty_category[i] = next_nonempty_category[cat + 1];
    }
  }

#ifdef DEBUG
  void CheckCacheIntegrity() {
    for (int i = 0; i <= last_category_; i++) {
      DCHECK(next_nonempty_category[i] == last_category_ + 1 ||
             categories_[next_nonempty_category[i]] != nullptr);
      for (int j = i; j < next_nonempty_category[i]; j++) {
        DCHECK(categories_[j] == nullptr);
      }
    }
  }
#endif

  // The cache is overallocated by one so that the last element is always
  // defined, and when updating the cache, we can always use cache[i+1] as long
  // as i is < kNumberOfCategories.
  int next_nonempty_category[kNumberOfCategories + 1];

 private:
  void ResetCache() {
    for (int i = 0; i < kNumberOfCategories; i++) {
      next_nonempty_category[i] = kNumberOfCategories;
    }
    // Setting the after-last element as well, as explained in the cache's
    // declaration.
    next_nonempty_category[kNumberOfCategories] = kNumberOfCategories;
  }
};

// Same as FreeListManyCached but uses a fast path.
// The fast path overallocates by at least 1.85k bytes. The idea of this 1.85k
// is: we want the fast path to always overallocate, even for larger
// categories. Therefore, we have two choices: either overallocate by
// "size_in_bytes * something" or overallocate by "size_in_bytes +
// something". We choose the later, as the former will tend to overallocate too
// much for larger objects. The 1.85k (= 2048 - 128) has been chosen such that
// for tiny objects (size <= 128 bytes), the first category considered is the
// 36th (which holds objects of 2k to 3k), while for larger objects, the first
// category considered will be one that guarantees a 1.85k+ bytes
// overallocation. Using 2k rather than 1.85k would have resulted in either a
// more complex logic for SelectFastAllocationFreeListCategoryType, or the 36th
// category (2k to 3k) not being used; both of which are undesirable.
// A secondary fast path is used for tiny objects (size <= 128), in order to
// consider categories from 256 to 2048 bytes for them.
// Note that this class uses a precise GetPageForSize (inherited from
// FreeListMany), which makes its fast path less fast in the Scavenger. This is
// done on purpose, since this class's only purpose is to be used by
// FreeListManyCachedOrigin, which is precise for the scavenger.
class V8_EXPORT_PRIVATE FreeListManyCachedFastPath : public FreeListManyCached {
 public:
  V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                           size_t* node_size,
                                           AllocationOrigin origin) override;

 protected:
  // Objects in the 18th category are at least 2048 bytes
  static const FreeListCategoryType kFastPathFirstCategory = 18;
  static const size_t kFastPathStart = 2048;
  static const size_t kTinyObjectMaxSize = 128;
  static const size_t kFastPathOffset = kFastPathStart - kTinyObjectMaxSize;
  // Objects in the 15th category are at least 256 bytes
  static const FreeListCategoryType kFastPathFallBackTiny = 15;

  STATIC_ASSERT(categories_min[kFastPathFirstCategory] == kFastPathStart);
  STATIC_ASSERT(categories_min[kFastPathFallBackTiny] ==
                kTinyObjectMaxSize * 2);

  FreeListCategoryType SelectFastAllocationFreeListCategoryType(
      size_t size_in_bytes) {
    DCHECK(size_in_bytes < kMaxBlockSize);

    if (size_in_bytes >= categories_min[last_category_]) return last_category_;

    size_in_bytes += kFastPathOffset;
    for (int cat = kFastPathFirstCategory; cat < last_category_; cat++) {
      if (size_in_bytes <= categories_min[cat]) {
        return cat;
      }
    }
    return last_category_;
  }

  FRIEND_TEST(
      SpacesTest,
      FreeListManyCachedFastPathSelectFastAllocationFreeListCategoryType);
};

// Uses FreeListManyCached if in the GC; FreeListManyCachedFastPath otherwise.
// The reasonning behind this FreeList is the following: the GC runs in
// parallel, and therefore, more expensive allocations there are less
// noticeable. On the other hand, the generated code and runtime need to be very
// fast. Therefore, the strategy for the former is one that is not very
// efficient, but reduces fragmentation (FreeListManyCached), while the strategy
// for the later is one that is very efficient, but introduces some
// fragmentation (FreeListManyCachedFastPath).
class V8_EXPORT_PRIVATE FreeListManyCachedOrigin
    : public FreeListManyCachedFastPath {
 public:
  V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                           size_t* node_size,
                                           AllocationOrigin origin) override;
};

// FreeList for maps: since maps are all the same size, uses a single freelist.
class V8_EXPORT_PRIVATE FreeListMap : public FreeList {
 public:
  size_t GuaranteedAllocatable(size_t maximum_freed) override;

  Page* GetPageForSize(size_t size_in_bytes) override;

  FreeListMap();
  ~FreeListMap() override;

  V8_WARN_UNUSED_RESULT FreeSpace Allocate(size_t size_in_bytes,
                                           size_t* node_size,
                                           AllocationOrigin origin) override;

 private:
  static const size_t kMinBlockSize = Map::kSize;
  static const size_t kMaxBlockSize = Page::kPageSize;
  static const FreeListCategoryType kOnlyCategory = 0;

  FreeListCategoryType SelectFreeListCategoryType(
      size_t size_in_bytes) override {
    return kOnlyCategory;
  }
};

// LocalAllocationBuffer represents a linear allocation area that is created
// from a given {AllocationResult} and can be used to allocate memory without
// synchronization.
//
// The buffer is properly closed upon destruction and reassignment.
// Example:
//   {
//     AllocationResult result = ...;
//     LocalAllocationBuffer a(heap, result, size);
//     LocalAllocationBuffer b = a;
//     CHECK(!a.IsValid());
//     CHECK(b.IsValid());
//     // {a} is invalid now and cannot be used for further allocations.
//   }
//   // Since {b} went out of scope, the LAB is closed, resulting in creating a
//   // filler object for the remaining area.
class LocalAllocationBuffer {
 public:
  // Indicates that a buffer cannot be used for allocations anymore. Can result
  // from either reassigning a buffer, or trying to construct it from an
  // invalid {AllocationResult}.
  static LocalAllocationBuffer InvalidBuffer() {
    return LocalAllocationBuffer(
        nullptr, LinearAllocationArea(kNullAddress, kNullAddress));
  }

  // Creates a new LAB from a given {AllocationResult}. Results in
  // InvalidBuffer if the result indicates a retry.
  static inline LocalAllocationBuffer FromResult(Heap* heap,
                                                 AllocationResult result,
                                                 intptr_t size);

  ~LocalAllocationBuffer() { CloseAndMakeIterable(); }

  LocalAllocationBuffer(const LocalAllocationBuffer& other) = delete;
  V8_EXPORT_PRIVATE LocalAllocationBuffer(LocalAllocationBuffer&& other)
      V8_NOEXCEPT;

  LocalAllocationBuffer& operator=(const LocalAllocationBuffer& other) = delete;
  V8_EXPORT_PRIVATE LocalAllocationBuffer& operator=(
      LocalAllocationBuffer&& other) V8_NOEXCEPT;

  V8_WARN_UNUSED_RESULT inline AllocationResult AllocateRawAligned(
      int size_in_bytes, AllocationAlignment alignment);

  inline bool IsValid() { return allocation_info_.top() != kNullAddress; }

  // Try to merge LABs, which is only possible when they are adjacent in memory.
  // Returns true if the merge was successful, false otherwise.
  inline bool TryMerge(LocalAllocationBuffer* other);

  inline bool TryFreeLast(HeapObject object, int object_size);

  // Close a LAB, effectively invalidating it. Returns the unused area.
  V8_EXPORT_PRIVATE LinearAllocationArea CloseAndMakeIterable();
  void MakeIterable();

  Address top() const { return allocation_info_.top(); }
  Address limit() const { return allocation_info_.limit(); }

 private:
  V8_EXPORT_PRIVATE LocalAllocationBuffer(
      Heap* heap, LinearAllocationArea allocation_info) V8_NOEXCEPT;

  Heap* heap_;
  LinearAllocationArea allocation_info_;
};

class SpaceWithLinearArea : public Space {
 public:
  SpaceWithLinearArea(Heap* heap, AllocationSpace id, FreeList* free_list)
      : Space(heap, id, free_list), top_on_previous_step_(0) {
    allocation_info_.Reset(kNullAddress, kNullAddress);
  }

  virtual bool SupportsInlineAllocation() = 0;

  // Returns the allocation pointer in this space.
  Address top() { return allocation_info_.top(); }
  Address limit() { return allocation_info_.limit(); }

  // The allocation top address.
  Address* allocation_top_address() { return allocation_info_.top_address(); }

  // The allocation limit address.
  Address* allocation_limit_address() {
    return allocation_info_.limit_address();
  }

  V8_EXPORT_PRIVATE void AddAllocationObserver(
      AllocationObserver* observer) override;
  V8_EXPORT_PRIVATE void RemoveAllocationObserver(
      AllocationObserver* observer) override;
  V8_EXPORT_PRIVATE void ResumeAllocationObservers() override;
  V8_EXPORT_PRIVATE void PauseAllocationObservers() override;

  // When allocation observers are active we may use a lower limit to allow the
  // observers to 'interrupt' earlier than the natural limit. Given a linear
  // area bounded by [start, end), this function computes the limit to use to
  // allow proper observation based on existing observers. min_size specifies
  // the minimum size that the limited area should have.
  Address ComputeLimit(Address start, Address end, size_t min_size);
  V8_EXPORT_PRIVATE virtual void UpdateInlineAllocationLimit(
      size_t min_size) = 0;

  V8_EXPORT_PRIVATE void UpdateAllocationOrigins(AllocationOrigin origin);

  void PrintAllocationsOrigins();

 protected:
  // If we are doing inline allocation in steps, this method performs the 'step'
  // operation. top is the memory address of the bump pointer at the last
  // inline allocation (i.e. it determines the numbers of bytes actually
  // allocated since the last step.) top_for_next_step is the address of the
  // bump pointer where the next byte is going to be allocated from. top and
  // top_for_next_step may be different when we cross a page boundary or reset
  // the space.
  // TODO(ofrobots): clarify the precise difference between this and
  // Space::AllocationStep.
  void InlineAllocationStep(Address top, Address top_for_next_step,
                            Address soon_object, size_t size);
  V8_EXPORT_PRIVATE void StartNextInlineAllocationStep() override;

  // TODO(ofrobots): make these private after refactoring is complete.
  LinearAllocationArea allocation_info_;
  Address top_on_previous_step_;

  size_t allocations_origins_[static_cast<int>(
      AllocationOrigin::kNumberOfAllocationOrigins)] = {0};
};

class V8_EXPORT_PRIVATE PauseAllocationObserversScope {
 public:
  explicit PauseAllocationObserversScope(Heap* heap);
  ~PauseAllocationObserversScope();

 private:
  Heap* heap_;
  DISALLOW_COPY_AND_ASSIGN(PauseAllocationObserversScope);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SPACES_H_
