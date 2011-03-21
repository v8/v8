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

#include "v8-counters.h"
#include "store-buffer.h"
#include "store-buffer-inl.h"

namespace v8 {
namespace internal {

Address* StoreBuffer::start_ = NULL;
Address* StoreBuffer::limit_ = NULL;
Address* StoreBuffer::old_start_ = NULL;
Address* StoreBuffer::old_limit_ = NULL;
Address* StoreBuffer::old_top_ = NULL;
uintptr_t* StoreBuffer::hash_map_1_ = NULL;
uintptr_t* StoreBuffer::hash_map_2_ = NULL;
VirtualMemory* StoreBuffer::virtual_memory_ = NULL;
StoreBuffer::StoreBufferMode StoreBuffer::store_buffer_mode_ =
    kStoreBufferFunctional;
bool StoreBuffer::old_buffer_is_sorted_ = false;
bool StoreBuffer::during_gc_ = false;
bool StoreBuffer::store_buffer_rebuilding_enabled_ = false;
bool StoreBuffer::may_move_store_buffer_entries_ = true;

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
  Heap::public_set_store_buffer_top(start_);

  hash_map_1_ = new uintptr_t[kHashMapLength];
  hash_map_2_ = new uintptr_t[kHashMapLength];

  Heap::AddGCPrologueCallback(&GCPrologue, kGCTypeAll);
  Heap::AddGCEpilogueCallback(&GCEpilogue, kGCTypeAll);

  ZapHashTables();
}


void StoreBuffer::TearDown() {
  delete virtual_memory_;
  delete[] hash_map_1_;
  delete[] hash_map_2_;
  delete[] old_start_;
  old_start_ = old_top_ = old_limit_ = NULL;
  start_ = limit_ = NULL;
  Heap::public_set_store_buffer_top(start_);
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
      if (Heap::InNewSpace(*reinterpret_cast<Object**>(current))) {
        *write++ = current;
      }
    }
    previous = current;
  }
  old_top_ = write;
}


void StoreBuffer::SortUniq() {
  Compact();
  if (old_buffer_is_sorted_) return;
  if (store_buffer_mode() == kStoreBufferDisabled) {
    old_top_ = old_start_;
    return;
  }
  ZapHashTables();
  qsort(reinterpret_cast<void*>(old_start_),
        old_top_ - old_start_,
        sizeof(*old_top_),
        &CompareAddresses);
  Uniq();

  old_buffer_is_sorted_ = true;
}


void StoreBuffer::PrepareForIteration() {
  Compact();
  if (store_buffer_mode() == kStoreBufferDisabled) {
    old_top_ = old_start_;
    return;
  }
  ZapHashTables();
}


#ifdef DEBUG
void StoreBuffer::Clean() {
  if (store_buffer_mode() == kStoreBufferDisabled) {
    old_top_ = old_start_;  // Just clear the cache.
    return;
  }
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
  if (store_buffer_mode() != kStoreBufferFunctional) return true;
  if (in_store_buffer_1_element_cache != NULL &&
      *in_store_buffer_1_element_cache == cell_address) {
    return true;
  }
  Address* top = reinterpret_cast<Address*>(Heap::store_buffer_top());
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
  ZapHashTables();
  during_gc_ = true;
}


void StoreBuffer::Verify() {
#ifdef DEBUG
  if (FLAG_verify_heap &&
      StoreBuffer::store_buffer_mode() == kStoreBufferFunctional) {
    Heap::OldPointerSpaceCheckStoreBuffer(Heap::WATERMARK_SHOULD_BE_VALID);
    Heap::MapSpaceCheckStoreBuffer(Heap::WATERMARK_SHOULD_BE_VALID);
    Heap::LargeObjectSpaceCheckStoreBuffer();
  }
#endif
}


void StoreBuffer::GCEpilogue(GCType type, GCCallbackFlags flags) {
  during_gc_ = false;
  if (store_buffer_mode() == kStoreBufferBeingRebuilt) {
    set_store_buffer_mode(kStoreBufferFunctional);
  }
  Verify();
}


void StoreBuffer::IteratePointersToNewSpace(ObjectSlotCallback callback) {
  if (store_buffer_mode() == kStoreBufferFunctional) {
    // We do not sort or remove duplicated entries from the store buffer because
    // we expect that callback will rebuild the store buffer thus removing
    // all duplicates and pointers to old space.
    PrepareForIteration();
  }
  if (store_buffer_mode() != kStoreBufferFunctional) {
    old_top_ = old_start_;
    ZapHashTables();
    Heap::public_set_store_buffer_top(start_);
    set_store_buffer_mode(kStoreBufferBeingRebuilt);
    Heap::IteratePointers(Heap::old_pointer_space(),
                          &Heap::IteratePointersToNewSpace,
                          callback,
                          Heap::WATERMARK_SHOULD_BE_VALID);

    Heap::IteratePointers(Heap::map_space(),
                          &Heap::IteratePointersFromMapsToNewSpace,
                          callback,
                          Heap::WATERMARK_SHOULD_BE_VALID);

    Heap::lo_space()->IteratePointersToNewSpace(callback);
  } else {
    Address* limit = old_top_;
    old_top_ = old_start_;
    {
      DontMoveStoreBufferEntriesScope scope;
      for (Address* current = old_start_; current < limit; current++) {
#ifdef DEBUG
        Address* saved_top = old_top_;
#endif
        Object** cell = reinterpret_cast<Object**>(*current);
        Object* object = *cell;
        // May be invalid if object is not in new space.
        HeapObject* heap_object = reinterpret_cast<HeapObject*>(object);
        if (Heap::InFromSpace(object)) {
          callback(reinterpret_cast<HeapObject**>(cell), heap_object);
        }
        ASSERT(old_top_ == saved_top + 1 || old_top_ == saved_top);
      }
    }
  }
}


void StoreBuffer::Compact() {
  Address* top = reinterpret_cast<Address*>(Heap::store_buffer_top());

  if (top == start_) return;

  // There's no check of the limit in the loop below so we check here for
  // the worst case (compaction doesn't eliminate any pointers).
  ASSERT(top <= limit_);
  Heap::public_set_store_buffer_top(start_);
  if (top - start_ > old_limit_ - old_top_) {
    CheckForFullBuffer();
  }
  if (store_buffer_mode() == kStoreBufferDisabled) return;
  ASSERT(may_move_store_buffer_entries_);
  // Goes through the addresses in the store buffer attempting to remove
  // duplicates.  In the interest of speed this is a lossy operation.  Some
  // duplicates will remain.  We have two hash tables with different hash
  // functions to reduce the number of unnecessary clashes.
  for (Address* current = start_; current < top; current++) {
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
    *old_top_++ = reinterpret_cast<Address>(int_addr << kPointerSizeLog2);
    ASSERT(old_top_ <= old_limit_);
  }
  Counters::store_buffer_compactions.Increment();
  CheckForFullBuffer();
}


void StoreBuffer::CheckForFullBuffer() {
  if (old_limit_ - old_top_ < kStoreBufferSize * 2) {
    // After compression we don't have enough space that we can be sure that
    // the next two compressions will have enough space in the buffer.  We
    // start by trying a more agressive compression.  If this frees up at least
    // half the space then we can keep going, otherwise it is time to brake.
    if (!during_gc_) {
      SortUniq();
    }
    if (old_limit_ - old_top_ > old_top_ - old_start_) {
      return;
    }
    // TODO(gc): Set an interrupt to do a GC on the next back edge.
    // TODO(gc): Allocate the rest of new space to force a GC on the next
    // allocation.
    // TODO(gc): Make the disabling of the store buffer dependendent on
    // those two measures failing:
    // After compression not enough space was freed up in the store buffer.  We
    // might as well stop sorting and trying to eliminate duplicates.
    Counters::store_buffer_overflows.Increment();
    set_store_buffer_mode(kStoreBufferDisabled);
  }
}

} }  // namespace v8::internal
