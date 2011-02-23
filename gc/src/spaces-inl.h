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

#ifndef V8_SPACES_INL_H_
#define V8_SPACES_INL_H_

#include "memory.h"
#include "spaces.h"

namespace v8 {
namespace internal {


// -----------------------------------------------------------------------------
// PageIterator

bool PageIterator::has_next() {
  return prev_page_ != stop_page_;
}


Page* PageIterator::next() {
  ASSERT(has_next());
  prev_page_ = (prev_page_ == NULL)
               ? space_->first_page_
               : prev_page_->next_page();
  return prev_page_;
}


// -----------------------------------------------------------------------------
// Page


Address Page::AllocationTop() {
  return static_cast<PagedSpace*>(owner())->PageAllocationTop(this);
}


Address Page::AllocationWatermark() {
  if (this == static_cast<PagedSpace*>(owner())->AllocationTopPage()) {
    return static_cast<PagedSpace*>(owner())->top();
  }
  return address() + AllocationWatermarkOffset();
}


uint32_t Page::AllocationWatermarkOffset() {
  return static_cast<uint32_t>((flags_ & kAllocationWatermarkOffsetMask) >>
                               kAllocationWatermarkOffsetShift);
}


void Page::SetAllocationWatermark(Address allocation_watermark) {
  if ((Heap::gc_state() == Heap::SCAVENGE) && IsWatermarkValid()) {
    // When iterating intergenerational references during scavenge
    // we might decide to promote an encountered young object.
    // We will allocate a space for such an object and put it
    // into the promotion queue to process it later.
    // If space for object was allocated somewhere beyond allocation
    // watermark this might cause garbage pointers to appear under allocation
    // watermark. To avoid visiting them during pointer-to-newspace iteration
    // which might be still in progress we store a valid allocation watermark
    // value and mark this page as having an invalid watermark.
    SetCachedAllocationWatermark(AllocationWatermark());
    InvalidateWatermark(true);
  }

  flags_ = (flags_ & kFlagsMask) |
           Offset(allocation_watermark) << kAllocationWatermarkOffsetShift;
  ASSERT(AllocationWatermarkOffset()
         == static_cast<uint32_t>(Offset(allocation_watermark)));
}


void Page::SetCachedAllocationWatermark(Address allocation_watermark) {
  allocation_watermark_ = allocation_watermark;
}


Address Page::CachedAllocationWatermark() {
  return allocation_watermark_;
}


void Page::FlipMeaningOfInvalidatedWatermarkFlag() {
  watermark_invalidated_mark_ ^= 1 << WATERMARK_INVALIDATED;
}


bool Page::IsWatermarkValid() {
  return (flags_ & (1 << WATERMARK_INVALIDATED)) != watermark_invalidated_mark_;
}


void Page::InvalidateWatermark(bool value) {
  if (value) {
    flags_ = (flags_ & ~(1 << WATERMARK_INVALIDATED)) |
             watermark_invalidated_mark_;
  } else {
    flags_ = (flags_ & ~(1 << WATERMARK_INVALIDATED)) |
             (watermark_invalidated_mark_ ^ (1 << WATERMARK_INVALIDATED));
  }

  ASSERT(IsWatermarkValid() == !value);
}


void Page::ClearGCFields() {
  InvalidateWatermark(true);
  SetAllocationWatermark(ObjectAreaStart());
  if (Heap::gc_state() == Heap::SCAVENGE) {
    SetCachedAllocationWatermark(ObjectAreaStart());
  }
}


// -----------------------------------------------------------------------------
// MemoryAllocator

#ifdef ENABLE_HEAP_PROTECTION

void MemoryAllocator::Protect(Address start, size_t size) {
  OS::Protect(start, size);
}


void MemoryAllocator::Unprotect(Address start,
                                size_t size,
                                Executability executable) {
  OS::Unprotect(start, size, executable);
}


void MemoryAllocator::ProtectChunkFromPage(Page* page) {
  int id = GetChunkId(page);
  OS::Protect(chunks_[id].address(), chunks_[id].size());
}


void MemoryAllocator::UnprotectChunkFromPage(Page* page) {
  int id = GetChunkId(page);
  OS::Unprotect(chunks_[id].address(), chunks_[id].size(),
                chunks_[id].owner()->executable() == EXECUTABLE);
}

#endif


// --------------------------------------------------------------------------
// PagedSpace

bool PagedSpace::Contains(Address addr) {
  Page* p = Page::FromAddress(addr);
  if (!p->is_valid()) return false;
  return p->owner() == this;
}


// Try linear allocation in the page of alloc_info's allocation top.  Does
// not contain slow case logic (eg, move to the next page or try free list
// allocation) so it can be used by all the allocation functions and for all
// the paged spaces.
HeapObject* PagedSpace::AllocateLinearly(AllocationInfo* alloc_info,
                                         int size_in_bytes) {
  Address current_top = alloc_info->top;
  Address new_top = current_top + size_in_bytes;
  if (new_top > alloc_info->limit) return NULL;

  alloc_info->top = new_top;
  ASSERT(alloc_info->VerifyPagedAllocation());
  accounting_stats_.AllocateBytes(size_in_bytes);
  return HeapObject::FromAddress(current_top);
}


// Raw allocation.
MaybeObject* PagedSpace::AllocateRaw(int size_in_bytes) {
  ASSERT(HasBeenSetup());
  ASSERT_OBJECT_SIZE(size_in_bytes);

  HeapObject* object = AllocateLinearly(&allocation_info_, size_in_bytes);
  if (object != NULL) {
    IncrementalMarking::Step(size_in_bytes);
    return object;
  }

  object = SlowAllocateRaw(size_in_bytes);
  if (object != NULL) {
    IncrementalMarking::Step(size_in_bytes);
    return object;
  }

  return Failure::RetryAfterGC(identity());
}


// -----------------------------------------------------------------------------
// NewSpace

MaybeObject* NewSpace::AllocateRawInternal(int size_in_bytes,
                                           AllocationInfo* alloc_info) {
  Address new_top = alloc_info->top + size_in_bytes;
  if (new_top > alloc_info->limit) return Failure::RetryAfterGC();

  Object* obj = HeapObject::FromAddress(alloc_info->top);
  alloc_info->top = new_top;
#ifdef DEBUG
  SemiSpace* space =
      (alloc_info == &allocation_info_) ? &to_space_ : &from_space_;
  ASSERT(space->low() <= alloc_info->top
         && alloc_info->top <= space->high()
         && alloc_info->limit == space->high());
#endif

  IncrementalMarking::Step(size_in_bytes);

  return obj;
}


template <typename StringType>
void NewSpace::ShrinkStringAtAllocationBoundary(String* string, int length) {
  ASSERT(length <= string->length());
  ASSERT(string->IsSeqString());
  ASSERT(string->address() + StringType::SizeFor(string->length()) ==
         allocation_info_.top);
  allocation_info_.top =
      string->address() + StringType::SizeFor(length);
  string->set_length(length);
}


bool FreeListNode::IsFreeListNode(HeapObject* object) {
  return object->map() == Heap::raw_unchecked_byte_array_map()
      || object->map() == Heap::raw_unchecked_one_pointer_filler_map()
      || object->map() == Heap::raw_unchecked_two_pointer_filler_map();
}

} }  // namespace v8::internal

#endif  // V8_SPACES_INL_H_
