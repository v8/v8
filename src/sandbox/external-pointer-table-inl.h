// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_

#include "src/base/atomicops.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/external-pointer.h"
#include "src/utils/allocation.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

Address ExternalPointerTable::Get(ExternalPointerHandle handle,
                                  ExternalPointerTag tag) const {
  uint32_t index = handle_to_index(handle);
  Address entry = load_atomic(index);
  DCHECK(!is_free(entry));

  return entry & ~tag;
}

void ExternalPointerTable::Set(ExternalPointerHandle handle, Address value,
                               ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalPointerHandle, handle);
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(is_marked(tag));

  uint32_t index = handle_to_index(handle);
  store_atomic(index, value | tag);
}

Address ExternalPointerTable::Exchange(ExternalPointerHandle handle,
                                       Address value, ExternalPointerTag tag) {
  DCHECK_NE(kNullExternalPointerHandle, handle);
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(is_marked(tag));

  uint32_t index = handle_to_index(handle);
  Address entry = exchange_atomic(index, value | tag);
  DCHECK(!is_free(entry));
  return entry & ~tag;
}

ExternalPointerHandle ExternalPointerTable::AllocateAndInitializeEntry(
    Isolate* isolate, Address initial_value, ExternalPointerTag tag) {
  DCHECK(is_initialized());

  uint32_t index;
  bool success = false;
  while (!success) {
    // This is essentially DCLP (see
    // https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/)
    // and so requires an acquire load as well as a release store in Grow() to
    // prevent reordering of memory accesses, which could for example cause one
    // thread to read a freelist entry before it has been properly initialized.
    uint32_t freelist_head = base::Acquire_Load(&freelist_head_);
    if (!freelist_head) {
      // Freelist is empty. Need to take the lock, then attempt to grow the
      // table if no other thread has done it in the meantime.
      base::MutexGuard guard(mutex_);

      // Reload freelist head in case another thread already grew the table.
      freelist_head = base::Relaxed_Load(&freelist_head_);

      if (!freelist_head) {
        // Freelist is (still) empty so grow the table.
        freelist_head = Grow(isolate);
      }
    }

    DCHECK(freelist_head);
    DCHECK_NE(freelist_head, kTableIsCurrentlySweepingMarker);
    DCHECK_LT(freelist_head, capacity());
    index = freelist_head;

    Address entry = load_atomic(index);
    uint32_t new_freelist_head = extract_next_entry_from_freelist_entry(entry);

    uint32_t old_val = base::Relaxed_CompareAndSwap(
        &freelist_head_, freelist_head, new_freelist_head);
    success = old_val == freelist_head;
  }

  store_atomic(index, initial_value | tag);

  return index_to_handle(index);
}

ExternalPointerHandle ExternalPointerTable::AllocateEvacuationEntry(
    uint32_t start_of_evacuation_area) {
  DCHECK(is_initialized());

  uint32_t index;
  bool success = false;
  while (!success) {
    uint32_t freelist_head = base::Acquire_Load(&freelist_head_);
    if (!freelist_head) {
      // Evacuation entries must be allocated below the start of the evacuation
      // area so there's no point in growing the table.
      return kNullExternalPointerHandle;
    }

    DCHECK(freelist_head);
    DCHECK_LT(freelist_head, capacity());
    index = freelist_head;

    if (index >= start_of_evacuation_area) return kNullExternalPointerHandle;

    Address entry = load_atomic(index);
    uint32_t new_freelist_head = extract_next_entry_from_freelist_entry(entry);
    uint32_t old_val = base::Relaxed_CompareAndSwap(
        &freelist_head_, freelist_head, new_freelist_head);
    success = old_val == freelist_head;
  }

  return index_to_handle(index);
}

uint32_t ExternalPointerTable::FreelistSize() {
  Address entry = 0;
  while (!is_free(entry)) {
    uint32_t freelist_head = base::Relaxed_Load(&freelist_head_);
    if (!freelist_head) {
      return 0;
    }
    entry = load_atomic(freelist_head);
  }
  uint32_t freelist_size = extract_freelist_size_from_freelist_entry(entry);
  DCHECK_LE(freelist_size, capacity());
  return freelist_size;
}

void ExternalPointerTable::Mark(ExternalPointerHandle handle,
                                Address handle_location) {
  static_assert(sizeof(base::Atomic64) == sizeof(Address));
  DCHECK_EQ(handle, *reinterpret_cast<ExternalPointerHandle*>(handle_location));

  uint32_t index = handle_to_index(handle);

  // Check if the entry should be evacuated for table compaction.
  // The current value of the start of the evacuation area is cached in a local
  // variable here as it otherwise may be changed by another marking thread
  // while this method runs, causing non-optimal behaviour (for example, the
  // allocation of an evacuation entry _after_ the entry that is evacuated).
  uint32_t current_start_of_evacuation_area = start_of_evacuation_area();
  if (index >= current_start_of_evacuation_area) {
    DCHECK(IsCompacting());
    ExternalPointerHandle new_handle =
        AllocateEvacuationEntry(current_start_of_evacuation_area);
    if (new_handle) {
      DCHECK_LT(handle_to_index(new_handle), current_start_of_evacuation_area);
      uint32_t index = handle_to_index(new_handle);
      // No need for an atomic store as the entry will only be accessed during
      // sweeping.
      store(index, make_evacuation_entry(handle_location));
#ifdef DEBUG
      // Mark the handle as visited in debug builds to detect double
      // initialization of external pointer fields.
      auto handle_ptr = reinterpret_cast<base::Atomic32*>(handle_location);
      base::Relaxed_Store(handle_ptr, handle | kVisitedHandleMarker);
#endif  // DEBUG
    } else {
      // In this case, the application has allocated a sufficiently large
      // number of entries from the freelist so that new entries would now be
      // allocated inside the area that is being compacted. While it would be
      // possible to shrink that area and continue compacting, we probably do
      // not want to put more pressure on the freelist and so instead simply
      // abort compaction here. Entries that have already been visited will
      // still be compacted during Sweep, but there is no guarantee that any
      // blocks at the end of the table will now be completely free.
      uint32_t compaction_aborted_marker =
          current_start_of_evacuation_area | kCompactionAbortedMarker;
      set_start_of_evacuation_area(compaction_aborted_marker);
    }
  }
  // Even if the entry is marked for evacuation, it still needs to be marked as
  // alive as it may be visited during sweeping before being evacuation.

  base::Atomic64 old_val = load_atomic(index);
  base::Atomic64 new_val = set_mark_bit(old_val);
  DCHECK(!is_free(old_val));

  // We don't need to perform the CAS in a loop: if the new value is not equal
  // to the old value, then the mutator must've just written a new value into
  // the entry. This in turn must've set the marking bit already (see
  // ExternalPointerTable::Set), so we don't need to do it again.
  base::Atomic64* ptr = reinterpret_cast<base::Atomic64*>(entry_address(index));
  base::Atomic64 val = base::Relaxed_CompareAndSwap(ptr, old_val, new_val);
  DCHECK((val == old_val) || is_marked(val));
  USE(val);
}

bool ExternalPointerTable::IsCompacting() {
  return start_of_evacuation_area() != kNotCompactingMarker;
}

bool ExternalPointerTable::CompactingWasAbortedDuringMarking() {
  return (start_of_evacuation_area() & kCompactionAbortedMarker) ==
         kCompactionAbortedMarker;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
