// Copyright 2011 the V8 project authors. All rights reserved.
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

#include "store-buffer.h"
#include "store-buffer-inl.h"
#include "v8-counters.h"

namespace v8 {
namespace internal {

StoreBuffer::StoreBuffer(Heap* heap)
    : heap_(heap),
      start_(NULL),
      limit_(NULL),
      old_start_(NULL),
      old_limit_(NULL),
      old_top_(NULL),
      old_buffer_is_sorted_(false),
      old_buffer_is_filtered_(false),
      during_gc_(false),
      store_buffer_rebuilding_enabled_(false),
      callback_(NULL),
      may_move_store_buffer_entries_(true),
      virtual_memory_(NULL),
      hash_map_1_(NULL),
      hash_map_2_(NULL) {
}


void StoreBuffer::Setup() {
  virtual_memory_ = new VirtualMemory(kStoreBufferSize * 3);
  uintptr_t start_as_int =
      reinterpret_cast<uintptr_t>(virtual_memory_->address());
  start_ =
      reinterpret_cast<Address*>(RoundUp(start_as_int, kStoreBufferSize * 2));
  limit_ = start_ + (kStoreBufferSize / sizeof(*start_));

  old_top_ = old_start_ = new Address[kOldStoreBufferLength];
  old_limit_ = old_start_ + kOldStoreBufferLength;

  ASSERT(reinterpret_cast<Address>(start_) >= virtual_memory_->address());
  ASSERT(reinterpret_cast<Address>(limit_) >= virtual_memory_->address());
  Address* vm_limit = reinterpret_cast<Address*>(
      reinterpret_cast<char*>(virtual_memory_->address()) +
          virtual_memory_->size());
  ASSERT(start_ <= vm_limit);
  ASSERT(limit_ <= vm_limit);
  USE(vm_limit);
  ASSERT((reinterpret_cast<uintptr_t>(limit_) & kStoreBufferOverflowBit) != 0);
  ASSERT((reinterpret_cast<uintptr_t>(limit_ - 1) & kStoreBufferOverflowBit) ==
         0);

  virtual_memory_->Commit(reinterpret_cast<Address>(start_),
                          kStoreBufferSize,
                          false);  // Not executable.
  heap_->public_set_store_buffer_top(start_);

  hash_map_1_ = new uintptr_t[kHashMapLength];
  hash_map_2_ = new uintptr_t[kHashMapLength];

  heap_->AddGCPrologueCallback(&GCPrologue, kGCTypeAll);
  heap_->AddGCEpilogueCallback(&GCEpilogue, kGCTypeAll);

  ZapHashTables();
}


void StoreBuffer::TearDown() {
  delete virtual_memory_;
  delete[] hash_map_1_;
  delete[] hash_map_2_;
  delete[] old_start_;
  old_start_ = old_top_ = old_limit_ = NULL;
  start_ = limit_ = NULL;
  heap_->public_set_store_buffer_top(start_);
}


void StoreBuffer::StoreBufferOverflow(Isolate* isolate) {
  isolate->heap()->store_buffer()->Compact();
}


#if V8_TARGET_ARCH_X64
static int CompareAddresses(const void* void_a, const void* void_b) {
  intptr_t a =
      reinterpret_cast<intptr_t>(*reinterpret_cast<const Address*>(void_a));
  intptr_t b =
      reinterpret_cast<intptr_t>(*reinterpret_cast<const Address*>(void_b));
  // Unfortunately if int is smaller than intptr_t there is no branch-free
  // way to return a number with the same sign as the difference between the
  // pointers.
  if (a == b) return 0;
  if (a < b) return -1;
  ASSERT(a > b);
  return 1;
}
#else
static int CompareAddresses(const void* void_a, const void* void_b) {
  intptr_t a =
      reinterpret_cast<intptr_t>(*reinterpret_cast<const Address*>(void_a));
  intptr_t b =
      reinterpret_cast<intptr_t>(*reinterpret_cast<const Address*>(void_b));
  ASSERT(sizeof(1) == sizeof(a));
  // Shift down to avoid wraparound.
  return (a >> kPointerSizeLog2) - (b >> kPointerSizeLog2);
}
#endif


void StoreBuffer::Uniq() {
  ASSERT(HashTablesAreZapped());
  // Remove adjacent duplicates and cells that do not point at new space.
  Address previous = NULL;
  Address* write = old_start_;
  ASSERT(may_move_store_buffer_entries_);
  for (Address* read = old_start_; read < old_top_; read++) {
    Address current = *read;
    if (current != previous) {
      if (heap_->InNewSpace(*reinterpret_cast<Object**>(current))) {
        *write++ = current;
      }
    }
    previous = current;
  }
  old_top_ = write;
}


void StoreBuffer::HandleFullness() {
  if (old_buffer_is_filtered_) return;
  ASSERT(may_move_store_buffer_entries_);
  Compact();

  old_buffer_is_filtered_ = true;
  bool page_has_scan_on_scavenge_flag = false;

  PointerChunkIterator it;
  MemoryChunk* chunk;
  while ((chunk = it.next()) != NULL) {
    if (chunk->scan_on_scavenge()) page_has_scan_on_scavenge_flag = true;
  }

  if (page_has_scan_on_scavenge_flag) {
    FilterScanOnScavengeEntries();
  }

  // If filtering out the entries from scan_on_scavenge pages got us down to
  // less than half full, then we are satisfied with that.
  if (old_limit_ - old_top_ > old_top_ - old_start_) return;

  // Sample 1 entry in 97 and filter out the pages where we estimate that more
  // than 1 in 8 pointers are to new space.
  static const int kSampleFinenesses = 5;
  static const struct Samples {
    int prime_sample_step;
    int threshold;
  } samples[kSampleFinenesses] =  {
    { 97, ((Page::kPageSize / kPointerSize) / 97) / 8 },
    { 23, ((Page::kPageSize / kPointerSize) / 23) / 16 },
    { 7, ((Page::kPageSize / kPointerSize) / 7) / 32 },
    { 3, ((Page::kPageSize / kPointerSize) / 3) / 256 },
    { 1, 0}
  };
  for (int i = kSampleFinenesses - 1; i >= 0; i--) {
    ExemptPopularPages(samples[i].prime_sample_step, samples[i].threshold);
    // As a last resort we mark all pages as being exempt from the store buffer.
    ASSERT(i != 0 || old_top_ == old_start_);
    if (old_limit_ - old_top_ > old_top_ - old_start_) return;
  }
  UNREACHABLE();
}


// Sample the store buffer to see if some pages are taking up a lot of space
// in the store buffer.
void StoreBuffer::ExemptPopularPages(int prime_sample_step, int threshold) {
  PointerChunkIterator it;
  MemoryChunk* chunk;
  while ((chunk = it.next()) != NULL) {
    chunk->set_store_buffer_counter(0);
  }
  bool created_new_scan_on_scavenge_pages = false;
  MemoryChunk* previous_chunk = NULL;
  for (Address* p = old_start_; p < old_top_; p += prime_sample_step) {
    Address addr = *p;
    MemoryChunk* containing_chunk = NULL;
    if (previous_chunk != NULL && previous_chunk->Contains(addr)) {
      containing_chunk = previous_chunk;
    } else {
      containing_chunk = MemoryChunk::FromAnyPointerAddress(addr);
    }
    int old_counter = containing_chunk->store_buffer_counter();
    if (old_counter == threshold) {
      containing_chunk->set_scan_on_scavenge(true);
      created_new_scan_on_scavenge_pages = true;
    }
    containing_chunk->set_store_buffer_counter(old_counter + 1);
    previous_chunk = containing_chunk;
  }
  if (created_new_scan_on_scavenge_pages) {
    FilterScanOnScavengeEntries();
  }
  old_buffer_is_filtered_ = true;
}


void StoreBuffer::FilterScanOnScavengeEntries() {
  Address* new_top = old_start_;
  MemoryChunk* previous_chunk = NULL;
  for (Address* p = old_start_; p < old_top_; p++) {
    Address addr = *p;
    MemoryChunk* containing_chunk = NULL;
    if (previous_chunk != NULL && previous_chunk->Contains(addr)) {
      containing_chunk = previous_chunk;
    } else {
      containing_chunk = MemoryChunk::FromAnyPointerAddress(addr);
      previous_chunk = containing_chunk;
    }
    if (!containing_chunk->scan_on_scavenge()) {
      *new_top++ = addr;
    }
  }
  old_top_ = new_top;
}


void StoreBuffer::SortUniq() {
  Compact();
  if (old_buffer_is_sorted_) return;
  ZapHashTables();
  qsort(reinterpret_cast<void*>(old_start_),
        old_top_ - old_start_,
        sizeof(*old_top_),
        &CompareAddresses);
  Uniq();

  old_buffer_is_sorted_ = true;
}


bool StoreBuffer::PrepareForIteration() {
  Compact();
  PointerChunkIterator it;
  MemoryChunk* chunk;
  bool page_has_scan_on_scavenge_flag = false;
  while ((chunk = it.next()) != NULL) {
    if (chunk->scan_on_scavenge()) page_has_scan_on_scavenge_flag = true;
  }

  if (page_has_scan_on_scavenge_flag) {
    FilterScanOnScavengeEntries();
  }
  ZapHashTables();
  return page_has_scan_on_scavenge_flag;
}


#ifdef DEBUG
void StoreBuffer::Clean() {
  ZapHashTables();
  Uniq();  // Also removes things that no longer point to new space.
  CheckForFullBuffer();
}


static bool Zapped(char* start, int size) {
  for (int i = 0; i < size; i++) {
    if (start[i] != 0) return false;
  }
  return true;
}


bool StoreBuffer::HashTablesAreZapped() {
  return Zapped(reinterpret_cast<char*>(hash_map_1_),
                sizeof(uintptr_t) * kHashMapLength) &&
      Zapped(reinterpret_cast<char*>(hash_map_2_),
             sizeof(uintptr_t) * kHashMapLength);
}


static Address* in_store_buffer_1_element_cache = NULL;


bool StoreBuffer::CellIsInStoreBuffer(Address cell_address) {
  if (!FLAG_enable_slow_asserts) return true;
  if (in_store_buffer_1_element_cache != NULL &&
      *in_store_buffer_1_element_cache == cell_address) {
    return true;
  }
  Address* top = reinterpret_cast<Address*>(heap_->store_buffer_top());
  for (Address* current = top - 1; current >= start_; current--) {
    if (*current == cell_address) {
      in_store_buffer_1_element_cache = current;
      return true;
    }
  }
  for (Address* current = old_top_ - 1; current >= old_start_; current--) {
    if (*current == cell_address) {
      in_store_buffer_1_element_cache = current;
      return true;
    }
  }
  return false;
}
#endif


void StoreBuffer::ZapHashTables() {
  memset(reinterpret_cast<void*>(hash_map_1_),
         0,
         sizeof(uintptr_t) * kHashMapLength);
  memset(reinterpret_cast<void*>(hash_map_2_),
         0,
         sizeof(uintptr_t) * kHashMapLength);
}


void StoreBuffer::GCPrologue(GCType type, GCCallbackFlags flags) {
  // TODO(gc) ISOLATES MERGE
  HEAP->store_buffer()->ZapHashTables();
  HEAP->store_buffer()->during_gc_ = true;
}


void StoreBuffer::Verify() {
}


void StoreBuffer::GCEpilogue(GCType type, GCCallbackFlags flags) {
  // TODO(gc) ISOLATES MERGE
  HEAP->store_buffer()->during_gc_ = false;
  HEAP->store_buffer()->Verify();
}


void StoreBuffer::IteratePointersToNewSpace(ObjectSlotCallback callback) {
  // We do not sort or remove duplicated entries from the store buffer because
  // we expect that callback will rebuild the store buffer thus removing
  // all duplicates and pointers to old space.
  bool some_pages_to_scan = PrepareForIteration();

  Address* limit = old_top_;
  old_top_ = old_start_;
  {
    DontMoveStoreBufferEntriesScope scope(this);
    if (FLAG_trace_gc) {
      PrintF("Store buffer: %d entries\n", limit - old_start_);
    }
    for (Address* current = old_start_; current < limit; current++) {
#ifdef DEBUG
      Address* saved_top = old_top_;
#endif
      Object** cell = reinterpret_cast<Object**>(*current);
      Object* object = *cell;
      // May be invalid if object is not in new space.
      HeapObject* heap_object = reinterpret_cast<HeapObject*>(object);
      if (heap_->InFromSpace(object)) {
        callback(reinterpret_cast<HeapObject**>(cell), heap_object);
      }
      ASSERT(old_top_ == saved_top + 1 || old_top_ == saved_top);
    }
  }
  // We are done scanning all the pointers that were in the store buffer, but
  // there may be some pages marked scan_on_scavenge that have pointers to new
  // space that are not in the store buffer.  We must scan them now.  As we
  // scan, the surviving pointers to new space will be added to the store
  // buffer.  If there are still a lot of pointers to new space then we will
  // keep the scan_on_scavenge flag on the page and discard the pointers that
  // were added to the store buffer.  If there are not many pointers to new
  // space left on the page we will keep the pointers in the store buffer and
  // remove the flag from the page.
  if (some_pages_to_scan) {
    if (callback_ != NULL) {
      (*callback_)(heap_, NULL, kStoreBufferStartScanningPagesEvent);
    }
    PointerChunkIterator it;
    MemoryChunk* chunk;
    while ((chunk = it.next()) != NULL) {
      if (chunk->scan_on_scavenge()) {
        if (callback_ != NULL) {
          (*callback_)(heap_, chunk, kStoreBufferScanningPageEvent);
        }
        if (chunk->owner() == heap_->lo_space()) {
          LargePage* large_page = reinterpret_cast<LargePage*>(chunk);
          HeapObject* array = large_page->GetObject();
          ASSERT(array->IsFixedArray());
          Address start = array->address();
          Address object_end = start + array->Size();
          heap_->IteratePointersToNewSpace(heap_, start, object_end, callback);
        } else {
          Page* page = reinterpret_cast<Page*>(chunk);
          heap_->IteratePointersOnPage(
              reinterpret_cast<PagedSpace*>(page->owner()),
              &Heap::IteratePointersToNewSpace,
              callback,
              page);
        }
      }
    }
    (*callback_)(heap_, NULL, kStoreBufferScanningPageEvent);
  }
}


void StoreBuffer::Compact() {
  Address* top = reinterpret_cast<Address*>(heap_->store_buffer_top());

  if (top == start_) return;

  // There's no check of the limit in the loop below so we check here for
  // the worst case (compaction doesn't eliminate any pointers).
  ASSERT(top <= limit_);
  heap_->public_set_store_buffer_top(start_);
  if (top - start_ > old_limit_ - old_top_) {
    HandleFullness();
  }
  ASSERT(may_move_store_buffer_entries_);
  // Goes through the addresses in the store buffer attempting to remove
  // duplicates.  In the interest of speed this is a lossy operation.  Some
  // duplicates will remain.  We have two hash tables with different hash
  // functions to reduce the number of unnecessary clashes.
  for (Address* current = start_; current < top; current++) {
    ASSERT(!heap_->cell_space()->Contains(*current));
    ASSERT(!heap_->code_space()->Contains(*current));
    ASSERT(!heap_->old_data_space()->Contains(*current));
    uintptr_t int_addr = reinterpret_cast<uintptr_t>(*current);
    // Shift out the last bits including any tags.
    int_addr >>= kPointerSizeLog2;
    int hash1 =
        ((int_addr ^ (int_addr >> kHashMapLengthLog2)) & (kHashMapLength - 1));
    if (hash_map_1_[hash1] == int_addr) continue;
    int hash2 =
        ((int_addr - (int_addr >> kHashMapLengthLog2)) & (kHashMapLength - 1));
    hash2 ^= hash2 >> (kHashMapLengthLog2 * 2);
    if (hash_map_2_[hash2] == int_addr) continue;
    if (hash_map_1_[hash1] == 0) {
      hash_map_1_[hash1] = int_addr;
    } else if (hash_map_2_[hash2] == 0) {
      hash_map_2_[hash2] = int_addr;
    } else {
      // Rather than slowing down we just throw away some entries.  This will
      // cause some duplicates to remain undetected.
      hash_map_1_[hash1] = int_addr;
      hash_map_2_[hash2] = 0;
    }
    old_buffer_is_sorted_ = false;
    old_buffer_is_filtered_ = false;
    *old_top_++ = reinterpret_cast<Address>(int_addr << kPointerSizeLog2);
    ASSERT(old_top_ <= old_limit_);
  }
  heap_->isolate()->counters()->store_buffer_compactions()->Increment();
  CheckForFullBuffer();
}


void StoreBuffer::CheckForFullBuffer() {
  if (old_limit_ - old_top_ < kStoreBufferSize * 2) {
    HandleFullness();
  }
}

} }  // namespace v8::internal
