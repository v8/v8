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

void ExternalPointerTable::Init(Isolate* isolate) {
  DCHECK(!is_initialized());

  VirtualAddressSpace* root_space = GetPlatformVirtualAddressSpace();
  DCHECK(IsAligned(kExternalPointerTableReservationSize,
                   root_space->allocation_granularity()));

  size_t reservation_size = kExternalPointerTableReservationSize;
#if defined(LEAK_SANITIZER)
  // When LSan is active, we use a "shadow table" which contains the raw
  // pointers stored in this external pointer table so that LSan can scan them.
  // This is necessary to avoid false leak reports. The shadow table is located
  // right after the real table in memory. See also lsan_record_ptr().
  reservation_size *= 2;
#endif  // LEAK_SANITIZER

  buffer_ = root_space->AllocatePages(
      VirtualAddressSpace::kNoHint, reservation_size,
      root_space->allocation_granularity(), PagePermissions::kNoAccess);
  if (!buffer_) {
    V8::FatalProcessOutOfMemory(
        isolate,
        "Failed to reserve memory for ExternalPointerTable backing buffer");
  }

  mutex_ = new base::Mutex;
  if (!mutex_) {
    V8::FatalProcessOutOfMemory(
        isolate, "Failed to allocate mutex for ExternalPointerTable");
  }

#if defined(LEAK_SANITIZER)
  // Make the shadow table accessible.
  if (!root_space->SetPagePermissions(
          buffer_ + kExternalPointerTableReservationSize,
          kExternalPointerTableReservationSize, PagePermissions::kReadWrite)) {
    V8::FatalProcessOutOfMemory(isolate,
                                "Failed to allocate memory for the "
                                "ExternalPointerTable LSan shadow table");
  }
#endif  // LEAK_SANITIZER

  // Allocate the initial block. Mutex must be held for that.
  base::MutexGuard guard(mutex_);
  Grow();

  // Set up the special null entry. This entry must contain nullptr so that
  // empty EmbedderDataSlots represent nullptr.
  static_assert(kNullExternalPointerHandle == 0);
  store(kNullExternalPointerHandle, kNullAddress);
}

void ExternalPointerTable::TearDown() {
  DCHECK(is_initialized());

  size_t reservation_size = kExternalPointerTableReservationSize;
#if defined(LEAK_SANITIZER)
  reservation_size *= 2;
#endif  // LEAK_SANITIZER

  GetPlatformVirtualAddressSpace()->FreePages(buffer_, reservation_size);
  delete mutex_;

  buffer_ = kNullAddress;
  capacity_ = 0;
  freelist_head_ = 0;
  mutex_ = nullptr;
}

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

ExternalPointerHandle ExternalPointerTable::AllocateInternal(
    bool is_evacuation_entry) {
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
      // Evacuation entries must be allocated below the start of the evacuation
      // area so there's no point in growing the table.
      if (is_evacuation_entry) return kNullExternalPointerHandle;

      // Freelist is empty. Need to take the lock, then attempt to grow the
      // table if no other thread has done it in the meantime.
      base::MutexGuard guard(mutex_);

      // Reload freelist head in case another thread already grew the table.
      freelist_head = base::Relaxed_Load(&freelist_head_);

      if (!freelist_head) {
        // Freelist is (still) empty so grow the table.
        freelist_head = Grow();
      }
    }

    DCHECK(freelist_head);
    DCHECK_NE(freelist_head, kTableIsCurrentlySweepingMarker);
    DCHECK_LT(freelist_head, capacity());
    index = freelist_head;

    if (is_evacuation_entry && index >= start_of_evacuation_area_)
      return kNullExternalPointerHandle;

    Address entry = load_atomic(index);
    uint32_t new_freelist_head = extract_next_entry_from_freelist_entry(entry);

    uint32_t old_val = base::Relaxed_CompareAndSwap(
        &freelist_head_, freelist_head, new_freelist_head);
    success = old_val == freelist_head;
  }

  return index_to_handle(index);
}

ExternalPointerHandle ExternalPointerTable::AllocateEntry() {
  constexpr bool is_evacuation_entry = false;
  return AllocateInternal(is_evacuation_entry);
}

ExternalPointerHandle ExternalPointerTable::AllocateEvacuationEntry() {
  constexpr bool is_evacuation_entry = true;
  return AllocateInternal(is_evacuation_entry);
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

  // Check if the entry should be evacuated.
  if (IsCompacting() && index >= start_of_evacuation_area_) {
    ExternalPointerHandle new_handle = AllocateEvacuationEntry();
    if (new_handle) {
      DCHECK_LT(handle_to_index(new_handle), start_of_evacuation_area_);
      uint32_t index = handle_to_index(new_handle);
      // No need for an atomic store as the entry will only be accessed during
      // sweeping.
      store(index, make_evacuation_entry(handle_location));
    } else {
      // In this case, the application has allocated a sufficiently large
      // number of entries from the freelist so that new entries would now be
      // allocated inside the area that is being compacted. While it would be
      // possible to shrink that area and continue compacting, we probably do
      // not want to put more pressure on the freelist and so instead simply
      // abort compaction here. Entries that have already been visited will
      // still be compacted during Sweep, but there is no guarantee that any
      // blocks at the end of the table will now be completely free.
      start_of_evacuation_area_ = kTableCompactionAbortedMarker;
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

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS

#endif  // V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
