// Copyright 2006-2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "macro-assembler.h"
#include "mark-compact.h"
#include "platform.h"

namespace v8 {
namespace internal {

// For contiguous spaces, top should be in the space (or at the end) and limit
// should be the end of the space.
#define ASSERT_SEMISPACE_ALLOCATION_INFO(info, space) \
  ASSERT((space).low() <= (info).top                  \
         && (info).top <= (space).high()              \
         && (info).limit == (space).high())

intptr_t Page::watermark_invalidated_mark_ = 1 << Page::WATERMARK_INVALIDATED;

// ----------------------------------------------------------------------------
// HeapObjectIterator

HeapObjectIterator::HeapObjectIterator(PagedSpace* space) {
  Initialize(space->bottom(), space->top(), NULL);
}


HeapObjectIterator::HeapObjectIterator(PagedSpace* space,
                                       HeapObjectCallback size_func) {
  Initialize(space->bottom(), space->top(), size_func);
}


HeapObjectIterator::HeapObjectIterator(Page* page,
                                       HeapObjectCallback size_func) {
  Initialize(page->ObjectAreaStart(), page->AllocationTop(), size_func);
}


void HeapObjectIterator::Initialize(Address cur, Address end,
                                    HeapObjectCallback size_f) {
  cur_addr_ = cur;
  end_addr_ = end;
  end_page_ = Page::FromAllocationTop(end);
  size_func_ = size_f;
  Page* p = Page::FromAllocationTop(cur_addr_);
  cur_limit_ = (p == end_page_) ? end_addr_ : p->AllocationTop();

  if (!p->IsFlagSet(Page::IS_CONTINUOUS)) {
    cur_addr_ = Marking::FirstLiveObject(cur_addr_, cur_limit_);
    if (cur_addr_ > cur_limit_) cur_addr_ = cur_limit_;
  }

#ifdef DEBUG
  Verify();
#endif
}


HeapObject* HeapObjectIterator::FromNextPage() {
  if (cur_addr_ == end_addr_) return NULL;

  Page* cur_page = Page::FromAllocationTop(cur_addr_);
  cur_page = cur_page->next_page();
  ASSERT(cur_page->is_valid());

  cur_addr_ = cur_page->ObjectAreaStart();
  cur_limit_ = (cur_page == end_page_) ? end_addr_ : cur_page->AllocationTop();

  if (!cur_page->IsFlagSet(Page::IS_CONTINUOUS)) {
    cur_addr_ = Marking::FirstLiveObject(cur_addr_, cur_limit_);
    if (cur_addr_ > cur_limit_) cur_addr_ = cur_limit_;
  }

  if (cur_addr_ == end_addr_) return NULL;
  ASSERT(cur_addr_ < cur_limit_);
#ifdef DEBUG
  Verify();
#endif
  return FromCurrentPage();
}


void HeapObjectIterator::AdvanceUsingMarkbits() {
  HeapObject* obj = HeapObject::FromAddress(cur_addr_);
  int obj_size = (size_func_ == NULL) ? obj->Size() : size_func_(obj);
  ASSERT_OBJECT_SIZE(obj_size);
  cur_addr_ = Marking::NextLiveObject(obj,
                                      obj_size,
                                      cur_limit_);
  if (cur_addr_ > cur_limit_) cur_addr_ = cur_limit_;
}


#ifdef DEBUG
void HeapObjectIterator::Verify() {
  Page* p = Page::FromAllocationTop(cur_addr_);
  ASSERT(p == Page::FromAllocationTop(cur_limit_));
  ASSERT(p->Offset(cur_addr_) <= p->Offset(cur_limit_));
}
#endif


// -----------------------------------------------------------------------------
// PageIterator

PageIterator::PageIterator(PagedSpace* space, Mode mode) : space_(space) {
  prev_page_ = NULL;
  switch (mode) {
    case PAGES_IN_USE:
      stop_page_ = space->AllocationTopPage();
      break;
    case ALL_PAGES:
#ifdef DEBUG
      // Verify that the cached last page in the space is actually the
      // last page.
      for (Page* p = space->first_page_; p->is_valid(); p = p->next_page()) {
        if (!p->next_page()->is_valid()) {
          ASSERT(space->last_page_ == p);
        }
      }
#endif
      stop_page_ = space->last_page_;
      break;
  }
}


// -----------------------------------------------------------------------------
// CodeRange

List<CodeRange::FreeBlock> CodeRange::free_list_(0);
List<CodeRange::FreeBlock> CodeRange::allocation_list_(0);
int CodeRange::current_allocation_block_index_ = 0;
VirtualMemory* CodeRange::code_range_ = NULL;


bool CodeRange::Setup(const size_t requested) {
  ASSERT(code_range_ == NULL);

  code_range_ = new VirtualMemory(requested);
  CHECK(code_range_ != NULL);
  if (!code_range_->IsReserved()) {
    delete code_range_;
    code_range_ = NULL;
    return false;
  }

  // We are sure that we have mapped a block of requested addresses.
  ASSERT(code_range_->size() == requested);
  LOG(NewEvent("CodeRange", code_range_->address(), requested));
  Address base = reinterpret_cast<Address>(code_range_->address());
  Address aligned_base =
      RoundUp(reinterpret_cast<Address>(code_range_->address()),
              MemoryChunk::kAlignment);
  int size = code_range_->size() - (aligned_base - base);
  allocation_list_.Add(FreeBlock(aligned_base, size));
  current_allocation_block_index_ = 0;
  return true;
}


int CodeRange::CompareFreeBlockAddress(const FreeBlock* left,
                                       const FreeBlock* right) {
  // The entire point of CodeRange is that the difference between two
  // addresses in the range can be represented as a signed 32-bit int,
  // so the cast is semantically correct.
  return static_cast<int>(left->start - right->start);
}


void CodeRange::GetNextAllocationBlock(size_t requested) {
  for (current_allocation_block_index_++;
       current_allocation_block_index_ < allocation_list_.length();
       current_allocation_block_index_++) {
    if (requested <= allocation_list_[current_allocation_block_index_].size) {
      return;  // Found a large enough allocation block.
    }
  }

  // Sort and merge the free blocks on the free list and the allocation list.
  free_list_.AddAll(allocation_list_);
  allocation_list_.Clear();
  free_list_.Sort(&CompareFreeBlockAddress);
  for (int i = 0; i < free_list_.length();) {
    FreeBlock merged = free_list_[i];
    i++;
    // Add adjacent free blocks to the current merged block.
    while (i < free_list_.length() &&
           free_list_[i].start == merged.start + merged.size) {
      merged.size += free_list_[i].size;
      i++;
    }
    if (merged.size > 0) {
      allocation_list_.Add(merged);
    }
  }
  free_list_.Clear();

  for (current_allocation_block_index_ = 0;
       current_allocation_block_index_ < allocation_list_.length();
       current_allocation_block_index_++) {
    if (requested <= allocation_list_[current_allocation_block_index_].size) {
      return;  // Found a large enough allocation block.
    }
  }

  // Code range is full or too fragmented.
  V8::FatalProcessOutOfMemory("CodeRange::GetNextAllocationBlock");
}



Address CodeRange::AllocateRawMemory(const size_t requested,
                                     size_t* allocated) {
  ASSERT(current_allocation_block_index_ < allocation_list_.length());
  if (requested > allocation_list_[current_allocation_block_index_].size) {
    // Find an allocation block large enough.  This function call may
    // call V8::FatalProcessOutOfMemory if it cannot find a large enough block.
    GetNextAllocationBlock(requested);
  }
  // Commit the requested memory at the start of the current allocation block.
  size_t aligned_requested = RoundUp(requested, MemoryChunk::kAlignment);
  FreeBlock current = allocation_list_[current_allocation_block_index_];
  if (aligned_requested >= (current.size - Page::kPageSize)) {
    // Don't leave a small free block, useless for a large object or chunk.
    *allocated = current.size;
  } else {
    *allocated = aligned_requested;
  }
  ASSERT(*allocated <= current.size);
  ASSERT(IsAddressAligned(current.start, MemoryChunk::kAlignment));
  if (!code_range_->Commit(current.start, *allocated, true)) {
    *allocated = 0;
    return NULL;
  }
  allocation_list_[current_allocation_block_index_].start += *allocated;
  allocation_list_[current_allocation_block_index_].size -= *allocated;
  if (*allocated == current.size) {
    GetNextAllocationBlock(0);  // This block is used up, get the next one.
  }
  return current.start;
}


void CodeRange::FreeRawMemory(Address address, size_t length) {
  ASSERT(IsAddressAligned(address, MemoryChunk::kAlignment));
  free_list_.Add(FreeBlock(address, length));
  code_range_->Uncommit(address, length);
}


void CodeRange::TearDown() {
    delete code_range_;  // Frees all memory in the virtual memory range.
    code_range_ = NULL;
    free_list_.Free();
    allocation_list_.Free();
}


// -----------------------------------------------------------------------------
// MemoryAllocator
//
size_t MemoryAllocator::capacity_ = 0;
size_t MemoryAllocator::capacity_executable_ = 0;
size_t MemoryAllocator::size_ = 0;
size_t MemoryAllocator::size_executable_ = 0;

List<MemoryAllocator::MemoryAllocationCallbackRegistration>
  MemoryAllocator::memory_allocation_callbacks_;

bool MemoryAllocator::Setup(intptr_t capacity, intptr_t capacity_executable) {
  capacity_ = RoundUp(capacity, Page::kPageSize);
  capacity_executable_ = RoundUp(capacity_executable, Page::kPageSize);
  ASSERT_GE(capacity_, capacity_executable_);

  size_ = 0;
  size_executable_ = 0;

  return true;
}


void MemoryAllocator::TearDown() {
  // Check that spaces were teared down before MemoryAllocator.
  ASSERT(size_ == 0);
  ASSERT(size_executable_ == 0);
  capacity_ = 0;
  capacity_executable_ = 0;
}


void MemoryAllocator::FreeMemory(Address base,
                                 size_t size,
                                 Executability executable) {
  if (CodeRange::contains(static_cast<Address>(base))) {
    ASSERT(executable == EXECUTABLE);
    CodeRange::FreeRawMemory(base, size);
  } else {
    ASSERT(executable == NOT_EXECUTABLE || !CodeRange::exists());
    VirtualMemory::ReleaseRegion(base, size);
  }

  Counters::memory_allocated.Decrement(static_cast<int>(size));

  ASSERT(size_ >= size);
  size_ -= size;

  if (executable == EXECUTABLE) {
    ASSERT(size_executable_ >= size);
    size_executable_ -= size;
  }
}


Address MemoryAllocator::ReserveAlignedMemory(const size_t requested,
                                              size_t alignment,
                                              size_t* allocated_size) {
  ASSERT(IsAligned(alignment, OS::AllocateAlignment()));
  if (size_ + requested > capacity_) return NULL;

  size_t allocated = RoundUp(requested + alignment, OS::AllocateAlignment());

  Address base = reinterpret_cast<Address>(
      VirtualMemory::ReserveRegion(allocated));

  Address end = base + allocated;

  if (base == 0) return NULL;

  Address aligned_base = RoundUp(base, alignment);

  ASSERT(aligned_base + requested <= base + allocated);

  // The difference between re-aligned base address and base address is
  // multiple of OS::AllocateAlignment().
  if (aligned_base != base) {
    ASSERT(aligned_base > base);
    // TODO(gc) check result of operation?
    VirtualMemory::ReleaseRegion(reinterpret_cast<void*>(base),
                                 aligned_base - base);
    allocated -= (aligned_base - base);
    base = aligned_base;
  }

  ASSERT(base + allocated == end);

  Address requested_end = base + requested;
  Address aligned_requested_end =
      RoundUp(requested_end, OS::AllocateAlignment());

  if (aligned_requested_end < end) {
    // TODO(gc) check result of operation?
    VirtualMemory::ReleaseRegion(reinterpret_cast<void*>(aligned_requested_end),
                                 end - aligned_requested_end);
    allocated = aligned_requested_end - base;
  }

  size_ += allocated;
  *allocated_size = allocated;
  return base;
}


Address MemoryAllocator::AllocateAlignedMemory(const size_t requested,
                                               size_t alignment,
                                               Executability executable,
                                               size_t* allocated_size) {
  Address base =
      ReserveAlignedMemory(requested, Page::kPageSize, allocated_size);

  if (base == NULL) return NULL;

  if (!VirtualMemory::CommitRegion(base,
                                   *allocated_size,
                                   executable == EXECUTABLE)) {
    VirtualMemory::ReleaseRegion(base, *allocated_size);
    size_ -= *allocated_size;
    return NULL;
  }

  return base;
}


MemoryChunk* MemoryAllocator::AllocateChunk(intptr_t body_size,
                                            Executability executable,
                                            Space* owner) {
  size_t chunk_size = MemoryChunk::kBodyOffset + body_size;
  Address base = NULL;
  if (executable == EXECUTABLE) {
    // Check executable memory limit.
    if (size_executable_ + chunk_size > capacity_executable_) {
      LOG(StringEvent("MemoryAllocator::AllocateRawMemory",
                      "V8 Executable Allocation capacity exceeded"));
      return NULL;
    }

    // Allocate executable memory either from code range or from the
    // OS.
    if (CodeRange::exists()) {
      base = CodeRange::AllocateRawMemory(chunk_size, &chunk_size);
      ASSERT(IsAligned(reinterpret_cast<intptr_t>(base),
                       MemoryChunk::kAlignment));
      size_ += chunk_size;
    } else {
      base = AllocateAlignedMemory(chunk_size,
                                   MemoryChunk::kAlignment,
                                   executable,
                                   &chunk_size);
    }

    if (base == NULL) return NULL;

    // Update executable memory size.
    size_executable_ += chunk_size;
  } else {
    base = AllocateAlignedMemory(chunk_size,
                                 MemoryChunk::kAlignment,
                                 executable,
                                 &chunk_size);

    if (base == NULL) return NULL;
  }

#ifdef DEBUG
  ZapBlock(base, chunk_size);
#endif
  Counters::memory_allocated.Increment(chunk_size);

  LOG(NewEvent("MemoryChunk", base, chunk_size));
  if (owner != NULL) {
    ObjectSpace space = static_cast<ObjectSpace>(1 << owner->identity());
    PerformAllocationCallback(space, kAllocationActionAllocate, chunk_size);
  }

  return MemoryChunk::Initialize(base, chunk_size, executable, owner);
}


Page* MemoryAllocator::AllocatePage(PagedSpace* owner,
                                    Executability executable) {
  MemoryChunk* chunk = AllocateChunk(Page::kObjectAreaSize, executable, owner);

  if (chunk == NULL) return NULL;

  return Page::Initialize(chunk);
}


LargePage* MemoryAllocator::AllocateLargePage(intptr_t object_size,
                                              Executability executable,
                                              Space* owner) {
  MemoryChunk* chunk = AllocateChunk(object_size, executable, owner);
  if (chunk == NULL) return NULL;
  return LargePage::Initialize(chunk);
}


void MemoryAllocator::Free(MemoryChunk* chunk) {
  LOG(DeleteEvent("MemoryChunk", chunk));
  if (chunk->owner() != NULL) {
    ObjectSpace space =
        static_cast<ObjectSpace>(1 << chunk->owner()->identity());
    PerformAllocationCallback(space, kAllocationActionFree, chunk->size());
  }

  FreeMemory(chunk->address(),
             chunk->size(),
             chunk->executable());
}


bool MemoryAllocator::CommitBlock(Address start,
                                  size_t size,
                                  Executability executable) {
  if (!VirtualMemory::CommitRegion(start, size, executable)) return false;
#ifdef DEBUG
  ZapBlock(start, size);
#endif
  Counters::memory_allocated.Increment(static_cast<int>(size));
  return true;
}


bool MemoryAllocator::UncommitBlock(Address start, size_t size) {
  if (!VirtualMemory::UncommitRegion(start, size)) return false;
  Counters::memory_allocated.Decrement(static_cast<int>(size));
  return true;
}


void MemoryAllocator::ZapBlock(Address start, size_t size) {
  for (size_t s = 0; s + kPointerSize <= size; s += kPointerSize) {
    Memory::Address_at(start + s) = kZapValue;
  }
}


void MemoryAllocator::PerformAllocationCallback(ObjectSpace space,
                                                AllocationAction action,
                                                size_t size) {
  for (int i = 0; i < memory_allocation_callbacks_.length(); ++i) {
    MemoryAllocationCallbackRegistration registration =
      memory_allocation_callbacks_[i];
    if ((registration.space & space) == space &&
        (registration.action & action) == action)
      registration.callback(space, action, static_cast<int>(size));
  }
}


bool MemoryAllocator::MemoryAllocationCallbackRegistered(
    MemoryAllocationCallback callback) {
  for (int i = 0; i < memory_allocation_callbacks_.length(); ++i) {
    if (memory_allocation_callbacks_[i].callback == callback) return true;
  }
  return false;
}


void MemoryAllocator::AddMemoryAllocationCallback(
    MemoryAllocationCallback callback,
    ObjectSpace space,
    AllocationAction action) {
  ASSERT(callback != NULL);
  MemoryAllocationCallbackRegistration registration(callback, space, action);
  ASSERT(!MemoryAllocator::MemoryAllocationCallbackRegistered(callback));
  return memory_allocation_callbacks_.Add(registration);
}


void MemoryAllocator::RemoveMemoryAllocationCallback(
     MemoryAllocationCallback callback) {
  ASSERT(callback != NULL);
  for (int i = 0; i < memory_allocation_callbacks_.length(); ++i) {
    if (memory_allocation_callbacks_[i].callback == callback) {
      memory_allocation_callbacks_.Remove(i);
      return;
    }
  }
  UNREACHABLE();
}


#ifdef DEBUG
void MemoryAllocator::ReportStatistics() {
  float pct = static_cast<float>(capacity_ - size_) / capacity_;
  PrintF("  capacity: %" V8_PTR_PREFIX "d"
             ", used: %" V8_PTR_PREFIX "d"
             ", available: %%%d\n\n",
         capacity_, size_, static_cast<int>(pct*100));
}
#endif

// -----------------------------------------------------------------------------
// PagedSpace implementation

PagedSpace::PagedSpace(intptr_t max_capacity,
                       AllocationSpace id,
                       Executability executable)
    : Space(id, executable) {
  max_capacity_ = (RoundDown(max_capacity, Page::kPageSize) / Page::kPageSize)
                  * Page::kObjectAreaSize;
  accounting_stats_.Clear();

  allocation_info_.top = NULL;
  allocation_info_.limit = NULL;
}


bool PagedSpace::Setup() {
  if (HasBeenSetup()) return false;

  // Maximum space capacity can not be less than single page size.
  if (max_capacity_ < Page::kPageSize) return false;

  first_page_ = MemoryAllocator::AllocatePage(this, executable());
  if (!first_page_->is_valid()) return false;

  // We are sure that the first page is valid and that we have at least one
  // page.
  accounting_stats_.ExpandSpace(Page::kObjectAreaSize);
  ASSERT(Capacity() <= max_capacity_);

  last_page_ = first_page_;
  ASSERT(!last_page_->next_page()->is_valid());

  // Use first_page_ for allocation.
  SetAllocationInfo(&allocation_info_, first_page_);

  return true;
}


bool PagedSpace::HasBeenSetup() {
  return (Capacity() > 0);
}


void PagedSpace::TearDown() {
  Page* next = NULL;
  for (Page* p = first_page_; p->is_valid(); p = next) {
    next = p->next_page();
    MemoryAllocator::Free(p);
  }
  first_page_ = NULL;
  last_page_ = NULL;
  accounting_stats_.Clear();
}


#ifdef ENABLE_HEAP_PROTECTION

void PagedSpace::Protect() {
  Page* page = first_page_;
  while (page->is_valid()) {
    MemoryAllocator::ProtectChunkFromPage(page);
    page = MemoryAllocator::FindLastPageInSameChunk(page)->next_page();
  }
}


void PagedSpace::Unprotect() {
  Page* page = first_page_;
  while (page->is_valid()) {
    MemoryAllocator::UnprotectChunkFromPage(page);
    page = MemoryAllocator::FindLastPageInSameChunk(page)->next_page();
  }
}

#endif


MaybeObject* PagedSpace::FindObject(Address addr) {
  // Note: this function can only be called before or after mark-compact GC
  // because it accesses map pointers.
  ASSERT(!MarkCompactCollector::in_use());

  if (!Contains(addr)) return Failure::Exception();

  Page* p = Page::FromAddress(addr);
  ASSERT(IsUsed(p));
  Address cur = p->ObjectAreaStart();
  Address end = p->AllocationTop();
  while (cur < end) {
    HeapObject* obj = HeapObject::FromAddress(cur);
    Address next = cur + obj->Size();
    if ((cur <= addr) && (addr < next)) return obj;
    cur = next;
  }

  UNREACHABLE();
  return Failure::Exception();
}


bool PagedSpace::IsUsed(Page* page) {
  PageIterator it(this, PageIterator::PAGES_IN_USE);
  while (it.has_next()) {
    if (page == it.next()) return true;
  }
  return false;
}


void PagedSpace::SetAllocationInfo(AllocationInfo* alloc_info, Page* p) {
  alloc_info->top = p->ObjectAreaStart();
  alloc_info->limit = p->ObjectAreaEnd();
  ASSERT(alloc_info->VerifyPagedAllocation());
}


bool PagedSpace::Expand() {
  ASSERT(max_capacity_ % Page::kObjectAreaSize == 0);
  ASSERT(Capacity() % Page::kObjectAreaSize == 0);

  if (Capacity() == max_capacity_) return false;

  ASSERT(Capacity() < max_capacity_);
  // Last page must be valid and its next page is invalid.
  ASSERT(last_page_->is_valid() && !last_page_->next_page()->is_valid());

  // We are going to exceed capacity for this space.
  if ((Capacity() + Page::kPageSize) > max_capacity_) return false;

  Page* p = MemoryAllocator::AllocatePage(this, executable());
  if (!p->is_valid()) return false;

  accounting_stats_.ExpandSpace(Page::kObjectAreaSize);
  ASSERT(Capacity() <= max_capacity_);

  last_page_->set_next_page(p);
  last_page_ = p;

  ASSERT(!last_page_->next_page()->is_valid());

  return true;
}


#ifdef DEBUG
int PagedSpace::CountTotalPages() {
  int count = 0;
  for (Page* p = first_page_; p->is_valid(); p = p->next_page()) {
    count++;
  }
  return count;
}
#endif


void PagedSpace::Shrink() {
  Page* top_page = AllocationTopPage();
  ASSERT(top_page->is_valid());

  // TODO(gc) release half of pages?
  if (top_page->next_page()->is_valid()) {
    int pages_freed = 0;
    Page* page = top_page->next_page();
    Page* next_page;
    while (page->is_valid()) {
      next_page = page->next_page();
      MemoryAllocator::Free(page);
      pages_freed++;
      page = next_page;
    }
    top_page->set_next_page(Page::FromAddress(NULL));
    last_page_ = top_page;

    accounting_stats_.ShrinkSpace(pages_freed * Page::kObjectAreaSize);
    ASSERT(Capacity() == CountTotalPages() * Page::kObjectAreaSize);
  }
}


bool PagedSpace::EnsureCapacity(int capacity) {
  while (Capacity() < capacity) {
    // Expand the space until it has the required capacity or expansion fails.
    if (!Expand()) return false;
  }
  return true;
}


#ifdef DEBUG
void PagedSpace::Print() { }
#endif


#ifdef DEBUG
// We do not assume that the PageIterator works, because it depends on the
// invariants we are checking during verification.
void PagedSpace::Verify(ObjectVisitor* visitor) {
  // The allocation pointer should be valid, and it should be in a page in the
  // space.
  ASSERT(allocation_info_.VerifyPagedAllocation());
  Page* top_page = Page::FromAllocationTop(allocation_info_.top);

  // Loop over all the pages.
  bool above_allocation_top = false;
  Page* current_page = first_page_;
  while (current_page->is_valid()) {
    if (above_allocation_top) {
      // We don't care what's above the allocation top.
    } else {
      Address top = current_page->AllocationTop();
      if (current_page == top_page) {
        ASSERT(top == allocation_info_.top);
        // The next page will be above the allocation top.
        above_allocation_top = true;
      }

      HeapObjectIterator it(current_page, NULL);
      Address end_of_previous_object = current_page->ObjectAreaStart();
      for(HeapObject* object = it.next(); object != NULL; object = it.next()) {
        ASSERT(end_of_previous_object <= object->address());

        // The first word should be a map, and we expect all map pointers to
        // be in map space.
        Map* map = object->map();
        ASSERT(map->IsMap());
        ASSERT(Heap::map_space()->Contains(map));

        // Perform space-specific object verification.
        VerifyObject(object);

        if (object->IsCodeCache() && ((uint32_t*)object->address())[2] == 0x2) {
          current_page->PrintMarkbits();
        }

        // The object itself should look OK.
        object->Verify();

        // All the interior pointers should be contained in the heap and
        // have page regions covering intergenerational references should be
        // marked dirty.
        int size = object->Size();
        object->IterateBody(map->instance_type(), size, visitor);

        ASSERT(object->address() + size <= top);
        end_of_previous_object = object->address() + size;
      }
    }

    current_page = current_page->next_page();
  }
}
#endif


// -----------------------------------------------------------------------------
// NewSpace implementation


bool NewSpace::Setup(int maximum_semispace_capacity) {
  // Setup new space based on the preallocated memory block defined by
  // start and size. The provided space is divided into two semi-spaces.
  // To support fast containment testing in the new space, the size of
  // this chunk must be a power of two and it must be aligned to its size.
  int initial_semispace_capacity = Heap::InitialSemiSpaceSize();

  size_t size = 0;
  Address base =
      MemoryAllocator::ReserveAlignedMemory(2 * maximum_semispace_capacity,
                                            2 * maximum_semispace_capacity,
                                            &size);

  if (base == NULL) return false;

  chunk_base_ = base;
  chunk_size_ = static_cast<uintptr_t>(size);
  LOG(NewEvent("InitialChunk", chunk_base_, chunk_size_));

  ASSERT(initial_semispace_capacity <= maximum_semispace_capacity);
  ASSERT(IsPowerOf2(maximum_semispace_capacity));

  // Allocate and setup the histogram arrays if necessary.
#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  allocated_histogram_ = NewArray<HistogramInfo>(LAST_TYPE + 1);
  promoted_histogram_ = NewArray<HistogramInfo>(LAST_TYPE + 1);

#define SET_NAME(name) allocated_histogram_[name].set_name(#name); \
                       promoted_histogram_[name].set_name(#name);
  INSTANCE_TYPE_LIST(SET_NAME)
#undef SET_NAME
#endif

  ASSERT(maximum_semispace_capacity == Heap::ReservedSemiSpaceSize());
  ASSERT(static_cast<intptr_t>(chunk_size_) >=
         2 * Heap::ReservedSemiSpaceSize());
  ASSERT(IsAddressAligned(chunk_base_, 2 * maximum_semispace_capacity, 0));

  if (!to_space_.Setup(chunk_base_,
                       initial_semispace_capacity,
                       maximum_semispace_capacity)) {
    return false;
  }
  if (!from_space_.Setup(chunk_base_ + maximum_semispace_capacity,
                         initial_semispace_capacity,
                         maximum_semispace_capacity)) {
    return false;
  }

  start_ = chunk_base_;
  address_mask_ = ~(2 * maximum_semispace_capacity - 1);
  object_mask_ = address_mask_ | kHeapObjectTagMask;
  object_expected_ = reinterpret_cast<uintptr_t>(start_) | kHeapObjectTag;

  allocation_info_.top = to_space_.low();
  allocation_info_.limit = to_space_.high();

  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);
  return true;
}


void NewSpace::TearDown() {
#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
  if (allocated_histogram_) {
    DeleteArray(allocated_histogram_);
    allocated_histogram_ = NULL;
  }
  if (promoted_histogram_) {
    DeleteArray(promoted_histogram_);
    promoted_histogram_ = NULL;
  }
#endif

  start_ = NULL;
  allocation_info_.top = NULL;
  allocation_info_.limit = NULL;

  to_space_.TearDown();
  from_space_.TearDown();

  LOG(DeleteEvent("InitialChunk", chunk_base_));
  MemoryAllocator::FreeMemory(chunk_base_,
                              static_cast<size_t>(chunk_size_),
                              NOT_EXECUTABLE);
  chunk_base_ = NULL;
  chunk_size_ = 0;
}


#ifdef ENABLE_HEAP_PROTECTION

void NewSpace::Protect() {
  MemoryAllocator::Protect(ToSpaceLow(), Capacity());
  MemoryAllocator::Protect(FromSpaceLow(), Capacity());
}


void NewSpace::Unprotect() {
  MemoryAllocator::Unprotect(ToSpaceLow(), Capacity(),
                             to_space_.executable());
  MemoryAllocator::Unprotect(FromSpaceLow(), Capacity(),
                             from_space_.executable());
}

#endif


void NewSpace::Flip() {
  SemiSpace tmp = from_space_;
  from_space_ = to_space_;
  to_space_ = tmp;
}


void NewSpace::Grow() {
  ASSERT(Capacity() < MaximumCapacity());
  if (to_space_.Grow()) {
    // Only grow from space if we managed to grow to space.
    if (!from_space_.Grow()) {
      // If we managed to grow to space but couldn't grow from space,
      // attempt to shrink to space.
      if (!to_space_.ShrinkTo(from_space_.Capacity())) {
        // We are in an inconsistent state because we could not
        // commit/uncommit memory from new space.
        V8::FatalProcessOutOfMemory("Failed to grow new space.");
      }
    }
  }
  allocation_info_.limit = to_space_.high();
  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);
}


void NewSpace::Shrink() {
  int new_capacity = Max(InitialCapacity(), 2 * SizeAsInt());
  int rounded_new_capacity =
      RoundUp(new_capacity, static_cast<int>(OS::AllocateAlignment()));
  if (rounded_new_capacity < Capacity() &&
      to_space_.ShrinkTo(rounded_new_capacity))  {
    // Only shrink from space if we managed to shrink to space.
    if (!from_space_.ShrinkTo(rounded_new_capacity)) {
      // If we managed to shrink to space but couldn't shrink from
      // space, attempt to grow to space again.
      if (!to_space_.GrowTo(from_space_.Capacity())) {
        // We are in an inconsistent state because we could not
        // commit/uncommit memory from new space.
        V8::FatalProcessOutOfMemory("Failed to shrink new space.");
      }
    }
  }
  allocation_info_.limit = to_space_.high();
  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);
}


void NewSpace::ResetAllocationInfo() {
  allocation_info_.top = to_space_.low();
  allocation_info_.limit = to_space_.high();
  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);
}


#ifdef DEBUG
// We do not use the SemispaceIterator because verification doesn't assume
// that it works (it depends on the invariants we are checking).
void NewSpace::Verify() {
  // The allocation pointer should be in the space or at the very end.
  ASSERT_SEMISPACE_ALLOCATION_INFO(allocation_info_, to_space_);

  // There should be objects packed in from the low address up to the
  // allocation pointer.
  Address current = to_space_.low();
  while (current < top()) {
    HeapObject* object = HeapObject::FromAddress(current);

    // The first word should be a map, and we expect all map pointers to
    // be in map space.
    Map* map = object->map();
    ASSERT(map->IsMap());
    ASSERT(Heap::map_space()->Contains(map));

    // The object should not be code or a map.
    ASSERT(!object->IsMap());
    ASSERT(!object->IsCode());

    // The object itself should look OK.
    object->Verify();

    // All the interior pointers should be contained in the heap.
    VerifyPointersVisitor visitor;
    int size = object->Size();
    object->IterateBody(map->instance_type(), size, &visitor);

    current += size;
  }

  // The allocation pointer should not be in the middle of an object.
  ASSERT(current == top());
}
#endif


bool SemiSpace::Commit() {
  ASSERT(!is_committed());
  if (!MemoryAllocator::CommitBlock(start_, capacity_, executable())) {
    return false;
  }
  committed_ = true;
  return true;
}


bool SemiSpace::Uncommit() {
  ASSERT(is_committed());
  if (!MemoryAllocator::UncommitBlock(start_, capacity_)) {
    return false;
  }
  committed_ = false;
  return true;
}


// -----------------------------------------------------------------------------
// SemiSpace implementation

bool SemiSpace::Setup(Address start,
                      int initial_capacity,
                      int maximum_capacity) {
  // Creates a space in the young generation. The constructor does not
  // allocate memory from the OS.  A SemiSpace is given a contiguous chunk of
  // memory of size 'capacity' when set up, and does not grow or shrink
  // otherwise.  In the mark-compact collector, the memory region of the from
  // space is used as the marking stack. It requires contiguous memory
  // addresses.
  initial_capacity_ = initial_capacity;
  capacity_ = initial_capacity;
  maximum_capacity_ = maximum_capacity;
  committed_ = false;

  start_ = start;
  address_mask_ = ~(maximum_capacity - 1);
  object_mask_ = address_mask_ | kHeapObjectTagMask;
  object_expected_ = reinterpret_cast<uintptr_t>(start) | kHeapObjectTag;
  age_mark_ = start_;

  return Commit();
}


void SemiSpace::TearDown() {
  start_ = NULL;
  capacity_ = 0;
}


bool SemiSpace::Grow() {
  // Double the semispace size but only up to maximum capacity.
  int maximum_extra = maximum_capacity_ - capacity_;
  int extra = Min(RoundUp(capacity_, static_cast<int>(OS::AllocateAlignment())),
                  maximum_extra);
  if (!MemoryAllocator::CommitBlock(high(), extra, executable())) {
    return false;
  }
  capacity_ += extra;
  return true;
}


bool SemiSpace::GrowTo(int new_capacity) {
  ASSERT(new_capacity <= maximum_capacity_);
  ASSERT(new_capacity > capacity_);
  size_t delta = new_capacity - capacity_;
  ASSERT(IsAligned(delta, OS::AllocateAlignment()));
  if (!MemoryAllocator::CommitBlock(high(), delta, executable())) {
    return false;
  }
  capacity_ = new_capacity;
  return true;
}


bool SemiSpace::ShrinkTo(int new_capacity) {
  ASSERT(new_capacity >= initial_capacity_);
  ASSERT(new_capacity < capacity_);
  size_t delta = capacity_ - new_capacity;
  ASSERT(IsAligned(delta, OS::AllocateAlignment()));
  if (!MemoryAllocator::UncommitBlock(high() - delta, delta)) {
    return false;
  }
  capacity_ = new_capacity;
  return true;
}


#ifdef DEBUG
void SemiSpace::Print() { }


void SemiSpace::Verify() { }
#endif


// -----------------------------------------------------------------------------
// SemiSpaceIterator implementation.
SemiSpaceIterator::SemiSpaceIterator(NewSpace* space) {
  Initialize(space, space->bottom(), space->top(), NULL);
}


SemiSpaceIterator::SemiSpaceIterator(NewSpace* space,
                                     HeapObjectCallback size_func) {
  Initialize(space, space->bottom(), space->top(), size_func);
}


SemiSpaceIterator::SemiSpaceIterator(NewSpace* space, Address start) {
  Initialize(space, start, space->top(), NULL);
}


void SemiSpaceIterator::Initialize(NewSpace* space, Address start,
                                   Address end,
                                   HeapObjectCallback size_func) {
  ASSERT(space->ToSpaceContains(start));
  ASSERT(space->ToSpaceLow() <= end
         && end <= space->ToSpaceHigh());
  space_ = &space->to_space_;
  current_ = start;
  limit_ = end;
  size_func_ = size_func;
}


#ifdef DEBUG
// A static array of histogram info for each type.
static HistogramInfo heap_histograms[LAST_TYPE+1];
static JSObject::SpillInformation js_spill_information;

// heap_histograms is shared, always clear it before using it.
static void ClearHistograms() {
  // We reset the name each time, though it hasn't changed.
#define DEF_TYPE_NAME(name) heap_histograms[name].set_name(#name);
  INSTANCE_TYPE_LIST(DEF_TYPE_NAME)
#undef DEF_TYPE_NAME

#define CLEAR_HISTOGRAM(name) heap_histograms[name].clear();
  INSTANCE_TYPE_LIST(CLEAR_HISTOGRAM)
#undef CLEAR_HISTOGRAM

  js_spill_information.Clear();
}


static int code_kind_statistics[Code::NUMBER_OF_KINDS];


static void ClearCodeKindStatistics() {
  for (int i = 0; i < Code::NUMBER_OF_KINDS; i++) {
    code_kind_statistics[i] = 0;
  }
}


static void ReportCodeKindStatistics() {
  const char* table[Code::NUMBER_OF_KINDS] = { NULL };

#define CASE(name)                            \
  case Code::name: table[Code::name] = #name; \
  break

  for (int i = 0; i < Code::NUMBER_OF_KINDS; i++) {
    switch (static_cast<Code::Kind>(i)) {
      CASE(FUNCTION);
      CASE(OPTIMIZED_FUNCTION);
      CASE(STUB);
      CASE(BUILTIN);
      CASE(LOAD_IC);
      CASE(KEYED_LOAD_IC);
      CASE(STORE_IC);
      CASE(KEYED_STORE_IC);
      CASE(CALL_IC);
      CASE(KEYED_CALL_IC);
      CASE(BINARY_OP_IC);
      CASE(TYPE_RECORDING_BINARY_OP_IC);
      CASE(COMPARE_IC);
    }
  }

#undef CASE

  PrintF("\n   Code kind histograms: \n");
  for (int i = 0; i < Code::NUMBER_OF_KINDS; i++) {
    if (code_kind_statistics[i] > 0) {
      PrintF("     %-20s: %10d bytes\n", table[i], code_kind_statistics[i]);
    }
  }
  PrintF("\n");
}


static int CollectHistogramInfo(HeapObject* obj) {
  InstanceType type = obj->map()->instance_type();
  ASSERT(0 <= type && type <= LAST_TYPE);
  ASSERT(heap_histograms[type].name() != NULL);
  heap_histograms[type].increment_number(1);
  heap_histograms[type].increment_bytes(obj->Size());

  if (FLAG_collect_heap_spill_statistics && obj->IsJSObject()) {
    JSObject::cast(obj)->IncrementSpillStatistics(&js_spill_information);
  }

  return obj->Size();
}


static void ReportHistogram(bool print_spill) {
  PrintF("\n  Object Histogram:\n");
  for (int i = 0; i <= LAST_TYPE; i++) {
    if (heap_histograms[i].number() > 0) {
      PrintF("    %-34s%10d (%10d bytes)\n",
             heap_histograms[i].name(),
             heap_histograms[i].number(),
             heap_histograms[i].bytes());
    }
  }
  PrintF("\n");

  // Summarize string types.
  int string_number = 0;
  int string_bytes = 0;
#define INCREMENT(type, size, name, camel_name)      \
    string_number += heap_histograms[type].number(); \
    string_bytes += heap_histograms[type].bytes();
  STRING_TYPE_LIST(INCREMENT)
#undef INCREMENT
  if (string_number > 0) {
    PrintF("    %-34s%10d (%10d bytes)\n\n", "STRING_TYPE", string_number,
           string_bytes);
  }

  if (FLAG_collect_heap_spill_statistics && print_spill) {
    js_spill_information.Print();
  }
}
#endif  // DEBUG


// Support for statistics gathering for --heap-stats and --log-gc.
#if defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)
void NewSpace::ClearHistograms() {
  for (int i = 0; i <= LAST_TYPE; i++) {
    allocated_histogram_[i].clear();
    promoted_histogram_[i].clear();
  }
}

// Because the copying collector does not touch garbage objects, we iterate
// the new space before a collection to get a histogram of allocated objects.
// This only happens (1) when compiled with DEBUG and the --heap-stats flag is
// set, or when compiled with ENABLE_LOGGING_AND_PROFILING and the --log-gc
// flag is set.
void NewSpace::CollectStatistics() {
  ClearHistograms();
  SemiSpaceIterator it(this);
  for (HeapObject* obj = it.next(); obj != NULL; obj = it.next())
    RecordAllocation(obj);
}


#ifdef ENABLE_LOGGING_AND_PROFILING
static void DoReportStatistics(HistogramInfo* info, const char* description) {
  LOG(HeapSampleBeginEvent("NewSpace", description));
  // Lump all the string types together.
  int string_number = 0;
  int string_bytes = 0;
#define INCREMENT(type, size, name, camel_name)       \
    string_number += info[type].number();             \
    string_bytes += info[type].bytes();
  STRING_TYPE_LIST(INCREMENT)
#undef INCREMENT
  if (string_number > 0) {
    LOG(HeapSampleItemEvent("STRING_TYPE", string_number, string_bytes));
  }

  // Then do the other types.
  for (int i = FIRST_NONSTRING_TYPE; i <= LAST_TYPE; ++i) {
    if (info[i].number() > 0) {
      LOG(HeapSampleItemEvent(info[i].name(), info[i].number(),
                              info[i].bytes()));
    }
  }
  LOG(HeapSampleEndEvent("NewSpace", description));
}
#endif  // ENABLE_LOGGING_AND_PROFILING


void NewSpace::ReportStatistics() {
#ifdef DEBUG
  if (FLAG_heap_stats) {
    float pct = static_cast<float>(Available()) / Capacity();
    PrintF("  capacity: %" V8_PTR_PREFIX "d"
               ", available: %" V8_PTR_PREFIX "d, %%%d\n",
           Capacity(), Available(), static_cast<int>(pct*100));
    PrintF("\n  Object Histogram:\n");
    for (int i = 0; i <= LAST_TYPE; i++) {
      if (allocated_histogram_[i].number() > 0) {
        PrintF("    %-34s%10d (%10d bytes)\n",
               allocated_histogram_[i].name(),
               allocated_histogram_[i].number(),
               allocated_histogram_[i].bytes());
      }
    }
    PrintF("\n");
  }
#endif  // DEBUG

#ifdef ENABLE_LOGGING_AND_PROFILING
  if (FLAG_log_gc) {
    DoReportStatistics(allocated_histogram_, "allocated");
    DoReportStatistics(promoted_histogram_, "promoted");
  }
#endif  // ENABLE_LOGGING_AND_PROFILING
}


void NewSpace::RecordAllocation(HeapObject* obj) {
  InstanceType type = obj->map()->instance_type();
  ASSERT(0 <= type && type <= LAST_TYPE);
  allocated_histogram_[type].increment_number(1);
  allocated_histogram_[type].increment_bytes(obj->Size());
}


void NewSpace::RecordPromotion(HeapObject* obj) {
  InstanceType type = obj->map()->instance_type();
  ASSERT(0 <= type && type <= LAST_TYPE);
  promoted_histogram_[type].increment_number(1);
  promoted_histogram_[type].increment_bytes(obj->Size());
}
#endif  // defined(DEBUG) || defined(ENABLE_LOGGING_AND_PROFILING)


// -----------------------------------------------------------------------------
// Free lists for old object spaces implementation

void FreeListNode::set_size(int size_in_bytes) {
  ASSERT(size_in_bytes > 0);
  ASSERT(IsAligned(size_in_bytes, kPointerSize));

  // We write a map and possibly size information to the block.  If the block
  // is big enough to be a ByteArray with at least one extra word (the next
  // pointer), we set its map to be the byte array map and its size to an
  // appropriate array length for the desired size from HeapObject::Size().
  // If the block is too small (eg, one or two words), to hold both a size
  // field and a next pointer, we give it a filler map that gives it the
  // correct size.
  if (size_in_bytes > ByteArray::kHeaderSize) {
    set_map(Heap::raw_unchecked_byte_array_map());
    // Can't use ByteArray::cast because it fails during deserialization.
    ByteArray* this_as_byte_array = reinterpret_cast<ByteArray*>(this);
    this_as_byte_array->set_length(ByteArray::LengthFor(size_in_bytes));
  } else if (size_in_bytes == kPointerSize) {
    set_map(Heap::raw_unchecked_one_pointer_filler_map());
  } else if (size_in_bytes == 2 * kPointerSize) {
    set_map(Heap::raw_unchecked_two_pointer_filler_map());
  } else {
    UNREACHABLE();
  }
  // We would like to ASSERT(Size() == size_in_bytes) but this would fail during
  // deserialization because the byte array map is not done yet.
}


Address FreeListNode::next() {
  ASSERT(IsFreeListNode(this));
  if (map() == Heap::raw_unchecked_byte_array_map()) {
    ASSERT(Size() >= kNextOffset + kPointerSize);
    return Memory::Address_at(address() + kNextOffset);
  } else {
    return Memory::Address_at(address() + kPointerSize);
  }
}


void FreeListNode::set_next(Address next) {
  ASSERT(IsFreeListNode(this));
  if (map() == Heap::raw_unchecked_byte_array_map()) {
    ASSERT(Size() >= kNextOffset + kPointerSize);
    Memory::Address_at(address() + kNextOffset) = next;
  } else {
    Memory::Address_at(address() + kPointerSize) = next;
  }
}


OldSpaceFreeList::OldSpaceFreeList(AllocationSpace owner) : owner_(owner) {
  Reset();
}


void OldSpaceFreeList::Reset() {
  available_ = 0;
  for (int i = 0; i < kFreeListsLength; i++) {
    free_[i].head_node_ = NULL;
  }
  needs_rebuild_ = false;
  finger_ = kHead;
  free_[kHead].next_size_ = kEnd;
}


void OldSpaceFreeList::RebuildSizeList() {
  ASSERT(needs_rebuild_);
  int cur = kHead;
  for (int i = cur + 1; i < kFreeListsLength; i++) {
    if (free_[i].head_node_ != NULL) {
      free_[cur].next_size_ = i;
      cur = i;
    }
  }
  free_[cur].next_size_ = kEnd;
  needs_rebuild_ = false;
}


int OldSpaceFreeList::Free(Address start, int size_in_bytes) {
#ifdef DEBUG
  MemoryAllocator::ZapBlock(start, size_in_bytes);
#endif
  FreeListNode* node = FreeListNode::FromAddress(start);
  node->set_size(size_in_bytes);

  // We don't use the freelists in compacting mode.  This makes it more like a
  // GC that only has mark-sweep-compact and doesn't have a mark-sweep
  // collector.
  if (FLAG_always_compact) {
    return size_in_bytes;
  }

  // Early return to drop too-small blocks on the floor (one or two word
  // blocks cannot hold a map pointer, a size field, and a pointer to the
  // next block in the free list).
  if (size_in_bytes < kMinBlockSize) {
    return size_in_bytes;
  }

  // Insert other blocks at the head of an exact free list.
  int index = size_in_bytes >> kPointerSizeLog2;
  node->set_next(free_[index].head_node_);
  free_[index].head_node_ = node->address();
  available_ += size_in_bytes;
  needs_rebuild_ = true;
  return 0;
}


MaybeObject* OldSpaceFreeList::Allocate(int size_in_bytes, int* wasted_bytes) {
  ASSERT(0 < size_in_bytes);
  ASSERT(size_in_bytes <= kMaxBlockSize);
  ASSERT(IsAligned(size_in_bytes, kPointerSize));

  if (needs_rebuild_) RebuildSizeList();
  int index = size_in_bytes >> kPointerSizeLog2;
  // Check for a perfect fit.
  if (free_[index].head_node_ != NULL) {
    FreeListNode* node = FreeListNode::FromAddress(free_[index].head_node_);
    // If this was the last block of its size, remove the size.
    if ((free_[index].head_node_ = node->next()) == NULL) RemoveSize(index);
    available_ -= size_in_bytes;
    *wasted_bytes = 0;
    ASSERT(!FLAG_always_compact);  // We only use the freelists with mark-sweep.
    return node;
  }
  // Search the size list for the best fit.
  int prev = finger_ < index ? finger_ : kHead;
  int cur = FindSize(index, &prev);
  ASSERT(index < cur);
  if (cur == kEnd) {
    // No large enough size in list.
    *wasted_bytes = 0;
    return Failure::RetryAfterGC(owner_);
  }
  ASSERT(!FLAG_always_compact);  // We only use the freelists with mark-sweep.
  int rem = cur - index;
  int rem_bytes = rem << kPointerSizeLog2;
  FreeListNode* cur_node = FreeListNode::FromAddress(free_[cur].head_node_);
  ASSERT(cur_node->Size() == (cur << kPointerSizeLog2));
  FreeListNode* rem_node = FreeListNode::FromAddress(free_[cur].head_node_ +
                                                     size_in_bytes);
  // Distinguish the cases prev < rem < cur and rem <= prev < cur
  // to avoid many redundant tests and calls to Insert/RemoveSize.
  if (prev < rem) {
    // Simple case: insert rem between prev and cur.
    finger_ = prev;
    free_[prev].next_size_ = rem;
    // If this was the last block of size cur, remove the size.
    if ((free_[cur].head_node_ = cur_node->next()) == NULL) {
      free_[rem].next_size_ = free_[cur].next_size_;
    } else {
      free_[rem].next_size_ = cur;
    }
    // Add the remainder block.
    rem_node->set_size(rem_bytes);
    rem_node->set_next(free_[rem].head_node_);
    free_[rem].head_node_ = rem_node->address();
  } else {
    // If this was the last block of size cur, remove the size.
    if ((free_[cur].head_node_ = cur_node->next()) == NULL) {
      finger_ = prev;
      free_[prev].next_size_ = free_[cur].next_size_;
    }
    if (rem_bytes < kMinBlockSize) {
      // Too-small remainder is wasted.
      rem_node->set_size(rem_bytes);
      available_ -= size_in_bytes + rem_bytes;
      *wasted_bytes = rem_bytes;
      return cur_node;
    }
    // Add the remainder block and, if needed, insert its size.
    rem_node->set_size(rem_bytes);
    rem_node->set_next(free_[rem].head_node_);
    free_[rem].head_node_ = rem_node->address();
    if (rem_node->next() == NULL) InsertSize(rem);
  }
  available_ -= size_in_bytes;
  *wasted_bytes = 0;
  return cur_node;
}


void OldSpaceFreeList::MarkNodes() {
  for (int i = 0; i < kFreeListsLength; i++) {
    Address cur_addr = free_[i].head_node_;
    while (cur_addr != NULL) {
      FreeListNode* cur_node = FreeListNode::FromAddress(cur_addr);
      cur_addr = cur_node->next();
      IntrusiveMarking::SetMark(cur_node);
    }
  }
}


#ifdef DEBUG
bool OldSpaceFreeList::Contains(FreeListNode* node) {
  for (int i = 0; i < kFreeListsLength; i++) {
    Address cur_addr = free_[i].head_node_;
    while (cur_addr != NULL) {
      FreeListNode* cur_node = FreeListNode::FromAddress(cur_addr);
      if (cur_node == node) return true;
      cur_addr = cur_node->next();
    }
  }
  return false;
}
#endif


FixedSizeFreeList::FixedSizeFreeList(AllocationSpace owner, int object_size)
    : owner_(owner), object_size_(object_size) {
  Reset();
}


void FixedSizeFreeList::Reset() {
  available_ = 0;
  head_ = tail_ = NULL;
}


void FixedSizeFreeList::Free(Address start) {
#ifdef DEBUG
  MemoryAllocator::ZapBlock(start, object_size_);
#endif
  // We only use the freelists with mark-sweep.
  ASSERT(!MarkCompactCollector::IsCompacting());
  FreeListNode* node = FreeListNode::FromAddress(start);
  node->set_size(object_size_);
  node->set_next(NULL);
  if (head_ == NULL) {
    tail_ = head_ = node->address();
  } else {
    FreeListNode::FromAddress(tail_)->set_next(node->address());
    tail_ = node->address();
  }
  available_ += object_size_;
}


MaybeObject* FixedSizeFreeList::Allocate() {
  if (head_ == NULL) {
    return Failure::RetryAfterGC(owner_);
  }

  ASSERT(!FLAG_always_compact);  // We only use the freelists with mark-sweep.
  FreeListNode* node = FreeListNode::FromAddress(head_);
  head_ = node->next();
  available_ -= object_size_;
  return node;
}


void FixedSizeFreeList::MarkNodes() {
  Address cur_addr = head_;
  while (cur_addr != NULL && cur_addr != tail_) {
    FreeListNode* cur_node = FreeListNode::FromAddress(cur_addr);
    cur_addr = cur_node->next();
    IntrusiveMarking::SetMark(cur_node);
  }
}


// -----------------------------------------------------------------------------
// OldSpace implementation

void OldSpace::PrepareForMarkCompact(bool will_compact) {
  ASSERT(!will_compact);
  // Call prepare of the super class.
  PagedSpace::PrepareForMarkCompact(will_compact);

  // During a non-compacting collection, everything below the linear
  // allocation pointer is considered allocated (everything above is
  // available) and we will rediscover available and wasted bytes during
  // the collection.
  accounting_stats_.AllocateBytes(free_list_.available());
  accounting_stats_.FillWastedBytes(Waste());

  // Clear the free list before a full GC---it will be rebuilt afterward.
  free_list_.Reset();
}


bool NewSpace::ReserveSpace(int bytes) {
  // We can't reliably unpack a partial snapshot that needs more new space
  // space than the minimum NewSpace size.
  ASSERT(bytes <= InitialCapacity());
  Address limit = allocation_info_.limit;
  Address top = allocation_info_.top;
  return limit - top >= bytes;
}


void PagedSpace::FreePages(Page* prev, Page* last) {
  if (last == AllocationTopPage()) {
    // Pages are already at the end of used pages.
    // Just mark them as continuos.
    Page* p = prev == NULL ? first_page_ : prev->next_page();
    Page* end_page = last->next_page();
    do {
      p->SetFlag(Page::IS_CONTINUOUS);
      p = p->next_page();
    } while (p != end_page);
    return;
  }

  Page* first = NULL;

  // Remove pages from the list.
  if (prev == NULL) {
    first = first_page_;
    first_page_ = last->next_page();
  } else {
    first = prev->next_page();
    prev->set_next_page(last->next_page());
  }

  // Attach it after the last page.
  last_page_->set_next_page(first);
  last_page_ = last;
  last->set_next_page(NULL);

  // Clean them up.
  do {
    first->InvalidateWatermark(true);
    first->SetAllocationWatermark(first->ObjectAreaStart());
    first->SetCachedAllocationWatermark(first->ObjectAreaStart());
    first->SetRegionMarks(Page::kAllRegionsCleanMarks);
    first->SetFlag(Page::IS_CONTINUOUS);
    first->markbits()->Clear();
    first = first->next_page();
  } while (first->is_valid());
}


void PagedSpace::PrepareForMarkCompact(bool will_compact) {
  ASSERT(!will_compact);
}


bool PagedSpace::ReserveSpace(int bytes) {
  Address limit = allocation_info_.limit;
  Address top = allocation_info_.top;
  if (limit - top >= bytes) return true;

  // There wasn't enough space in the current page.  Lets put the rest
  // of the page on the free list and start a fresh page.
  PutRestOfCurrentPageOnFreeList(TopPageOf(allocation_info_));

  Page* reserved_page = TopPageOf(allocation_info_);
  int bytes_left_to_reserve = bytes;
  while (bytes_left_to_reserve > 0) {
    if (!reserved_page->next_page()->is_valid()) {
      if (Heap::OldGenerationAllocationLimitReached()) return false;
      Expand();
    }
    bytes_left_to_reserve -= Page::kPageSize;
    reserved_page = reserved_page->next_page();
    if (!reserved_page->is_valid()) return false;
  }
  ASSERT(TopPageOf(allocation_info_)->next_page()->is_valid());
  TopPageOf(allocation_info_)->next_page()->InvalidateWatermark(true);
  SetAllocationInfo(&allocation_info_,
                    TopPageOf(allocation_info_)->next_page());
  return true;
}


// You have to call this last, since the implementation from PagedSpace
// doesn't know that memory was 'promised' to large object space.
bool LargeObjectSpace::ReserveSpace(int bytes) {
  return Heap::OldGenerationSpaceAvailable() >= bytes;
}


// Slow case for normal allocation.  Try in order: (1) allocate in the next
// page in the space, (2) allocate off the space's free list, (3) expand the
// space, (4) fail.
HeapObject* OldSpace::SlowAllocateRaw(int size_in_bytes) {
  // Linear allocation in this space has failed.  If there is another page
  // in the space, move to that page and allocate there.  This allocation
  // should succeed (size_in_bytes should not be greater than a page's
  // object area size).
  Page* current_page = TopPageOf(allocation_info_);
  if (current_page->next_page()->is_valid()) {
    return AllocateInNextPage(current_page, size_in_bytes);
  }

  // There is no next page in this space.  Try free list allocation unless that
  // is currently forbidden.
  if (!Heap::linear_allocation()) {
    int wasted_bytes;
    Object* result;
    MaybeObject* maybe = free_list_.Allocate(size_in_bytes, &wasted_bytes);
    accounting_stats_.WasteBytes(wasted_bytes);
    if (maybe->ToObject(&result)) {
      accounting_stats_.AllocateBytes(size_in_bytes);

      HeapObject* obj = HeapObject::cast(result);
      Page* p = Page::FromAddress(obj->address());

      if (obj->address() >= p->AllocationWatermark()) {
        // There should be no hole between the allocation watermark
        // and allocated object address.
        // Memory above the allocation watermark was not swept and
        // might contain garbage pointers to new space.
        ASSERT(obj->address() == p->AllocationWatermark());
        p->SetAllocationWatermark(obj->address() + size_in_bytes);
      }

      if (!p->IsFlagSet(Page::IS_CONTINUOUS)) {
        // This page is not continuous so we have to mark objects
        // that should be visited by HeapObjectIterator.
        ASSERT(!Marking::IsMarked(obj));
        Marking::SetMark(obj);
      }

      return obj;
    }
  }

  // Free list allocation failed and there is no next page.  Fail if we have
  // hit the old generation size limit that should cause a garbage
  // collection.
  if (!Heap::always_allocate() && Heap::OldGenerationAllocationLimitReached()) {
    return NULL;
  }

  // Try to expand the space and allocate in the new next page.
  ASSERT(!current_page->next_page()->is_valid());
  if (Expand()) {
    return AllocateInNextPage(current_page, size_in_bytes);
  }

  // Finally, fail.
  return NULL;
}


void OldSpace::PutRestOfCurrentPageOnFreeList(Page* current_page) {
  current_page->SetAllocationWatermark(allocation_info_.top);
  int free_size =
      static_cast<int>(current_page->ObjectAreaEnd() - allocation_info_.top);
  if (free_size > 0) {
    int wasted_bytes = free_list_.Free(allocation_info_.top, free_size);
    accounting_stats_.WasteBytes(wasted_bytes);
  }
}


void FixedSpace::PutRestOfCurrentPageOnFreeList(Page* current_page) {
  current_page->SetAllocationWatermark(allocation_info_.top);
  int free_size =
      static_cast<int>(current_page->ObjectAreaEnd() - allocation_info_.top);
  // In the fixed space free list all the free list items have the right size.
  // We use up the rest of the page while preserving this invariant.
  while (free_size >= object_size_in_bytes_) {
    free_list_.Free(allocation_info_.top);
    allocation_info_.top += object_size_in_bytes_;
    free_size -= object_size_in_bytes_;
    accounting_stats_.WasteBytes(object_size_in_bytes_);
  }
}


// Add the block at the top of the page to the space's free list, set the
// allocation info to the next page (assumed to be one), and allocate
// linearly there.
HeapObject* OldSpace::AllocateInNextPage(Page* current_page,
                                         int size_in_bytes) {
  ASSERT(current_page->next_page()->is_valid());
  Page* next_page = current_page->next_page();
  next_page->ClearGCFields();
  PutRestOfCurrentPageOnFreeList(current_page);
  SetAllocationInfo(&allocation_info_, next_page);
  return AllocateLinearly(&allocation_info_, size_in_bytes);
}


void OldSpace::DeallocateBlock(Address start,
                                 int size_in_bytes,
                                 bool add_to_freelist) {
  Free(start, size_in_bytes, add_to_freelist);
}


#ifdef DEBUG
struct CommentStatistic {
  const char* comment;
  int size;
  int count;
  void Clear() {
    comment = NULL;
    size = 0;
    count = 0;
  }
};


// must be small, since an iteration is used for lookup
const int kMaxComments = 64;
static CommentStatistic comments_statistics[kMaxComments+1];


void PagedSpace::ReportCodeStatistics() {
  ReportCodeKindStatistics();
  PrintF("Code comment statistics (\"   [ comment-txt   :    size/   "
         "count  (average)\"):\n");
  for (int i = 0; i <= kMaxComments; i++) {
    const CommentStatistic& cs = comments_statistics[i];
    if (cs.size > 0) {
      PrintF("   %-30s: %10d/%6d     (%d)\n", cs.comment, cs.size, cs.count,
             cs.size/cs.count);
    }
  }
  PrintF("\n");
}


void PagedSpace::ResetCodeStatistics() {
  ClearCodeKindStatistics();
  for (int i = 0; i < kMaxComments; i++) comments_statistics[i].Clear();
  comments_statistics[kMaxComments].comment = "Unknown";
  comments_statistics[kMaxComments].size = 0;
  comments_statistics[kMaxComments].count = 0;
}


// Adds comment to 'comment_statistics' table. Performance OK sa long as
// 'kMaxComments' is small
static void EnterComment(const char* comment, int delta) {
  // Do not count empty comments
  if (delta <= 0) return;
  CommentStatistic* cs = &comments_statistics[kMaxComments];
  // Search for a free or matching entry in 'comments_statistics': 'cs'
  // points to result.
  for (int i = 0; i < kMaxComments; i++) {
    if (comments_statistics[i].comment == NULL) {
      cs = &comments_statistics[i];
      cs->comment = comment;
      break;
    } else if (strcmp(comments_statistics[i].comment, comment) == 0) {
      cs = &comments_statistics[i];
      break;
    }
  }
  // Update entry for 'comment'
  cs->size += delta;
  cs->count += 1;
}


// Call for each nested comment start (start marked with '[ xxx', end marked
// with ']'.  RelocIterator 'it' must point to a comment reloc info.
static void CollectCommentStatistics(RelocIterator* it) {
  ASSERT(!it->done());
  ASSERT(it->rinfo()->rmode() == RelocInfo::COMMENT);
  const char* tmp = reinterpret_cast<const char*>(it->rinfo()->data());
  if (tmp[0] != '[') {
    // Not a nested comment; skip
    return;
  }

  // Search for end of nested comment or a new nested comment
  const char* const comment_txt =
      reinterpret_cast<const char*>(it->rinfo()->data());
  const byte* prev_pc = it->rinfo()->pc();
  int flat_delta = 0;
  it->next();
  while (true) {
    // All nested comments must be terminated properly, and therefore exit
    // from loop.
    ASSERT(!it->done());
    if (it->rinfo()->rmode() == RelocInfo::COMMENT) {
      const char* const txt =
          reinterpret_cast<const char*>(it->rinfo()->data());
      flat_delta += static_cast<int>(it->rinfo()->pc() - prev_pc);
      if (txt[0] == ']') break;  // End of nested  comment
      // A new comment
      CollectCommentStatistics(it);
      // Skip code that was covered with previous comment
      prev_pc = it->rinfo()->pc();
    }
    it->next();
  }
  EnterComment(comment_txt, flat_delta);
}


// Collects code size statistics:
// - by code kind
// - by code comment
void PagedSpace::CollectCodeStatistics() {
  HeapObjectIterator obj_it(this);
  for (HeapObject* obj = obj_it.next(); obj != NULL; obj = obj_it.next()) {
    if (obj->IsCode()) {
      Code* code = Code::cast(obj);
      code_kind_statistics[code->kind()] += code->Size();
      RelocIterator it(code);
      int delta = 0;
      const byte* prev_pc = code->instruction_start();
      while (!it.done()) {
        if (it.rinfo()->rmode() == RelocInfo::COMMENT) {
          delta += static_cast<int>(it.rinfo()->pc() - prev_pc);
          CollectCommentStatistics(&it);
          prev_pc = it.rinfo()->pc();
        }
        it.next();
      }

      ASSERT(code->instruction_start() <= prev_pc &&
             prev_pc <= code->instruction_end());
      delta += static_cast<int>(code->instruction_end() - prev_pc);
      EnterComment("NoComment", delta);
    }
  }
}


void OldSpace::ReportStatistics() {
  int pct = static_cast<int>(Available() * 100 / Capacity());
  PrintF("  capacity: %" V8_PTR_PREFIX "d"
             ", waste: %" V8_PTR_PREFIX "d"
             ", available: %" V8_PTR_PREFIX "d, %%%d\n",
         Capacity(), Waste(), Available(), pct);

  ClearHistograms();
  HeapObjectIterator obj_it(this);
  for (HeapObject* obj = obj_it.next(); obj != NULL; obj = obj_it.next())
    CollectHistogramInfo(obj);
  ReportHistogram(true);
}
#endif

// -----------------------------------------------------------------------------
// FixedSpace implementation

void FixedSpace::PrepareForMarkCompact(bool will_compact) {
  // Call prepare of the super class.
  PagedSpace::PrepareForMarkCompact(will_compact);

  ASSERT(!will_compact);

  // During a non-compacting collection, everything below the linear
  // allocation pointer except wasted top-of-page blocks is considered
  // allocated and we will rediscover available bytes during the
  // collection.
  accounting_stats_.AllocateBytes(free_list_.available());

  // Clear the free list before a full GC---it will be rebuilt afterward.
  free_list_.Reset();
}


// Slow case for normal allocation. Try in order: (1) allocate in the next
// page in the space, (2) allocate off the space's free list, (3) expand the
// space, (4) fail.
HeapObject* FixedSpace::SlowAllocateRaw(int size_in_bytes) {
  ASSERT_EQ(object_size_in_bytes_, size_in_bytes);
  // Linear allocation in this space has failed.  If there is another page
  // in the space, move to that page and allocate there.  This allocation
  // should succeed.
  Page* current_page = TopPageOf(allocation_info_);
  if (current_page->next_page()->is_valid()) {
    return AllocateInNextPage(current_page, size_in_bytes);
  }

  // There is no next page in this space.  Try free list allocation unless
  // that is currently forbidden.  The fixed space free list implicitly assumes
  // that all free blocks are of the fixed size.
  if (!Heap::linear_allocation()) {
    Object* result;
    MaybeObject* maybe = free_list_.Allocate();
    if (maybe->ToObject(&result)) {
      accounting_stats_.AllocateBytes(size_in_bytes);
      HeapObject* obj = HeapObject::cast(result);
      Page* p = Page::FromAddress(obj->address());

      if (obj->address() >= p->AllocationWatermark()) {
        // There should be no hole between the allocation watermark
        // and allocated object address.
        // Memory above the allocation watermark was not swept and
        // might contain garbage pointers to new space.
        ASSERT(obj->address() == p->AllocationWatermark());
        p->SetAllocationWatermark(obj->address() + size_in_bytes);
      }

      return obj;
    }
  }

  // Free list allocation failed and there is no next page.  Fail if we have
  // hit the old generation size limit that should cause a garbage
  // collection.
  if (!Heap::always_allocate() && Heap::OldGenerationAllocationLimitReached()) {
    return NULL;
  }

  // Try to expand the space and allocate in the new next page.
  ASSERT(!current_page->next_page()->is_valid());
  if (Expand()) {
    return AllocateInNextPage(current_page, size_in_bytes);
  }

  // Finally, fail.
  return NULL;
}


// Move to the next page (there is assumed to be one) and allocate there.
// The top of page block is always wasted, because it is too small to hold a
// map.
HeapObject* FixedSpace::AllocateInNextPage(Page* current_page,
                                           int size_in_bytes) {
  ASSERT(current_page->next_page()->is_valid());
  ASSERT(allocation_info_.top == PageAllocationLimit(current_page));
  ASSERT_EQ(object_size_in_bytes_, size_in_bytes);
  Page* next_page = current_page->next_page();
  next_page->ClearGCFields();
  current_page->SetAllocationWatermark(allocation_info_.top);
  accounting_stats_.WasteBytes(page_extra_);
  SetAllocationInfo(&allocation_info_, next_page);
  return AllocateLinearly(&allocation_info_, size_in_bytes);
}


void FixedSpace::DeallocateBlock(Address start,
                                 int size_in_bytes,
                                 bool add_to_freelist) {
  // Free-list elements in fixed space are assumed to have a fixed size.
  // We break the free block into chunks and add them to the free list
  // individually.
  int size = object_size_in_bytes();
  ASSERT(size_in_bytes % size == 0);
  Address end = start + size_in_bytes;
  for (Address a = start; a < end; a += size) {
    Free(a, add_to_freelist);
  }
}


#ifdef DEBUG
void FixedSpace::ReportStatistics() {
  int pct = static_cast<int>(Available() * 100 / Capacity());
  PrintF("  capacity: %" V8_PTR_PREFIX "d"
             ", waste: %" V8_PTR_PREFIX "d"
             ", available: %" V8_PTR_PREFIX "d, %%%d\n",
         Capacity(), Waste(), Available(), pct);

  ClearHistograms();
  HeapObjectIterator obj_it(this);
  for (HeapObject* obj = obj_it.next(); obj != NULL; obj = obj_it.next())
    CollectHistogramInfo(obj);
  ReportHistogram(false);
}
#endif


// -----------------------------------------------------------------------------
// MapSpace implementation

void MapSpace::PrepareForMarkCompact(bool will_compact) {
  // Call prepare of the super class.
  FixedSpace::PrepareForMarkCompact(will_compact);
}


#ifdef DEBUG
void MapSpace::VerifyObject(HeapObject* object) {
  // The object should be a map or a free-list node.
  ASSERT(object->IsMap() || object->IsByteArray());
}
#endif


// -----------------------------------------------------------------------------
// GlobalPropertyCellSpace implementation

#ifdef DEBUG
void CellSpace::VerifyObject(HeapObject* object) {
  // The object should be a global object property cell or a free-list node.
  ASSERT(object->IsJSGlobalPropertyCell() ||
         object->map() == Heap::two_pointer_filler_map());
}
#endif


// -----------------------------------------------------------------------------
// LargeObjectIterator

LargeObjectIterator::LargeObjectIterator(LargeObjectSpace* space) {
  current_ = space->first_page_;
  size_func_ = NULL;
}


LargeObjectIterator::LargeObjectIterator(LargeObjectSpace* space,
                                         HeapObjectCallback size_func) {
  current_ = space->first_page_;
  size_func_ = size_func;
}


HeapObject* LargeObjectIterator::next() {
  if (current_ == NULL) return NULL;

  HeapObject* object = current_->GetObject();
  current_ = current_->next_page();
  return object;
}


// -----------------------------------------------------------------------------
// LargeObjectSpace

LargeObjectSpace::LargeObjectSpace(AllocationSpace id)
    : Space(id, NOT_EXECUTABLE),  // Managed on a per-allocation basis
      first_page_(NULL),
      size_(0),
      page_count_(0),
      objects_size_(0) {}


bool LargeObjectSpace::Setup() {
  first_page_ = NULL;
  size_ = 0;
  page_count_ = 0;
  objects_size_ = 0;
  return true;
}


void LargeObjectSpace::TearDown() {
  while (first_page_ != NULL) {
    LargePage* page = first_page_;
    first_page_ = first_page_->next_page();

    MemoryAllocator::Free(page);
  }

  size_ = 0;
  page_count_ = 0;
  objects_size_ = 0;
}


#ifdef ENABLE_HEAP_PROTECTION

void LargeObjectSpace::Protect() {
  LargeObjectChunk* chunk = first_chunk_;
  while (chunk != NULL) {
    MemoryAllocator::Protect(chunk->address(), chunk->size());
    chunk = chunk->next();
  }
}


void LargeObjectSpace::Unprotect() {
  LargeObjectChunk* chunk = first_chunk_;
  while (chunk != NULL) {
    bool is_code = chunk->GetObject()->IsCode();
    MemoryAllocator::Unprotect(chunk->address(), chunk->size(),
                               is_code ? EXECUTABLE : NOT_EXECUTABLE);
    chunk = chunk->next();
  }
}

#endif

MaybeObject* LargeObjectSpace::AllocateRawInternal(int object_size,
                                                   Executability executable) {
  // Check if we want to force a GC before growing the old space further.
  // If so, fail the allocation.
  if (!Heap::always_allocate() && Heap::OldGenerationAllocationLimitReached()) {
    return Failure::RetryAfterGC(identity());
  }

  LargePage* page = MemoryAllocator::AllocateLargePage(object_size,
                                                       executable,
                                                       this);
  if (page == NULL) return Failure::RetryAfterGC(identity());
  ASSERT(page->body_size() >= object_size);

  size_ += static_cast<int>(page->size());
  objects_size_ += object_size;
  page_count_++;
  page->set_next_page(first_page_);
  first_page_ = page;

  return page->GetObject();
}


MaybeObject* LargeObjectSpace::AllocateRawCode(int size_in_bytes) {
  ASSERT(0 < size_in_bytes);
  return AllocateRawInternal(size_in_bytes, EXECUTABLE);
}


MaybeObject* LargeObjectSpace::AllocateRawFixedArray(int size_in_bytes) {
  ASSERT(0 < size_in_bytes);
  return AllocateRawInternal(size_in_bytes, NOT_EXECUTABLE);
}


MaybeObject* LargeObjectSpace::AllocateRaw(int size_in_bytes) {
  ASSERT(0 < size_in_bytes);
  return AllocateRawInternal(size_in_bytes, NOT_EXECUTABLE);
}


// GC support
MaybeObject* LargeObjectSpace::FindObject(Address a) {
  for (LargePage* page = first_page_;
       page != NULL;
       page = page->next_page()) {
    Address page_address = page->address();
    if (page_address <= a && a < page_address + page->size()) {
      return page->GetObject();
    }
  }
  return Failure::Exception();
}


LargePage* LargeObjectSpace::FindPageContainingPc(Address pc) {
  // TODO(853): Change this implementation to only find executable
  // chunks and use some kind of hash-based approach to speed it up.
  for (LargePage* chunk = first_page_;
       chunk != NULL;
       chunk = chunk->next_page()) {
    Address chunk_address = chunk->address();
    if (chunk_address <= pc && pc < chunk_address + chunk->size()) {
      return chunk;
    }
  }
  return NULL;
}


void LargeObjectSpace::IterateDirtyRegions(ObjectSlotCallback copy_object) {
  LargeObjectIterator it(this);
  for (HeapObject* object = it.next(); object != NULL; object = it.next()) {
    // We only have code, sequential strings, or fixed arrays in large
    // object space, and only fixed arrays can possibly contain pointers to
    // the young generation.
    if (object->IsFixedArray()) {
      Page* page = Page::FromAddress(object->address());
      uint32_t marks = page->GetRegionMarks();

      // TODO(gc): we can no longer assume that LargePage is bigger than normal
      // page.
      ASSERT(marks == Page::kAllRegionsDirtyMarks);
      USE(marks);

      Address start = object->address();
      Address object_end = start + object->Size();
      Heap::IteratePointersInDirtyRegion(start, object_end, copy_object);
    }
  }
}


void LargeObjectSpace::FreeUnmarkedObjects() {
  LargePage* previous = NULL;
  LargePage* current = first_page_;
  while (current != NULL) {
    HeapObject* object = current->GetObject();
    if (Marking::IsMarked(object)) {
      Marking::ClearMark(object);
      MarkCompactCollector::tracer()->decrement_marked_count();
      previous = current;
      current = current->next_page();
    } else {
      LargePage* page = current;
      // Cut the chunk out from the chunk list.
      current = current->next_page();
      if (previous == NULL) {
        first_page_ = current;
      } else {
        previous->set_next_page(current);
      }

      // Free the chunk.
      MarkCompactCollector::ReportDeleteIfNeeded(object);
      size_ -= static_cast<int>(page->size());
      objects_size_ -= object->Size();
      page_count_--;

      MemoryAllocator::Free(page);
    }
  }
}


bool LargeObjectSpace::Contains(HeapObject* object) {
  Address address = object->address();
  if (Heap::new_space()->Contains(address)) {
    return false;
  }
  MemoryChunk* chunk = MemoryChunk::FromAddress(address);

  bool owned = chunk->owner() == this;

  SLOW_ASSERT(!owned
              || !FindObject(address)->IsFailure());

  return owned;
}


#ifdef DEBUG
// We do not assume that the large object iterator works, because it depends
// on the invariants we are checking during verification.
void LargeObjectSpace::Verify() {
  for (LargePage* chunk = first_page_;
       chunk != NULL;
       chunk = chunk->next_page()) {
    // Each chunk contains an object that starts at the large object page's
    // object area start.
    HeapObject* object = chunk->GetObject();
    Page* page = Page::FromAddress(object->address());
    ASSERT(object->address() == page->ObjectAreaStart());

    // The first word should be a map, and we expect all map pointers to be
    // in map space.
    Map* map = object->map();
    ASSERT(map->IsMap());
    ASSERT(Heap::map_space()->Contains(map));

    // We have only code, sequential strings, external strings
    // (sequential strings that have been morphed into external
    // strings), fixed arrays, and byte arrays in large object space.
    ASSERT(object->IsCode() || object->IsSeqString() ||
           object->IsExternalString() || object->IsFixedArray() ||
           object->IsByteArray());

    // The object itself should look OK.
    object->Verify();

    // Byte arrays and strings don't have interior pointers.
    if (object->IsCode()) {
      VerifyPointersVisitor code_visitor;
      object->IterateBody(map->instance_type(),
                          object->Size(),
                          &code_visitor);
    } else if (object->IsFixedArray()) {
      // We loop over fixed arrays ourselves, rather then using the visitor,
      // because the visitor doesn't support the start/offset iteration
      // needed for IsRegionDirty.
      FixedArray* array = FixedArray::cast(object);
      for (int j = 0; j < array->length(); j++) {
        Object* element = array->get(j);
        if (element->IsHeapObject()) {
          HeapObject* element_object = HeapObject::cast(element);
          ASSERT(Heap::Contains(element_object));
          ASSERT(element_object->map()->IsMap());
          if (Heap::InNewSpace(element_object)) {
            Address array_addr = object->address();
            Address element_addr = array_addr + FixedArray::kHeaderSize +
                j * kPointerSize;

            ASSERT(Page::FromAddress(array_addr)->IsRegionDirty(element_addr));
          }
        }
      }
    }
  }
}


void LargeObjectSpace::Print() {
  LargeObjectIterator it(this);
  for (HeapObject* obj = it.next(); obj != NULL; obj = it.next()) {
    obj->Print();
  }
}


void LargeObjectSpace::ReportStatistics() {
  PrintF("  size: %" V8_PTR_PREFIX "d\n", size_);
  int num_objects = 0;
  ClearHistograms();
  LargeObjectIterator it(this);
  for (HeapObject* obj = it.next(); obj != NULL; obj = it.next()) {
    num_objects++;
    CollectHistogramInfo(obj);
  }

  PrintF("  number of objects %d, "
         "size of objects %" V8_PTR_PREFIX "d\n", num_objects, objects_size_);
  if (num_objects > 0) ReportHistogram(false);
}


void LargeObjectSpace::CollectCodeStatistics() {
  LargeObjectIterator obj_it(this);
  for (HeapObject* obj = obj_it.next(); obj != NULL; obj = obj_it.next()) {
    if (obj->IsCode()) {
      Code* code = Code::cast(obj);
      code_kind_statistics[code->kind()] += code->Size();
    }
  }
}
#endif  // DEBUG

} }  // namespace v8::internal
