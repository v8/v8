// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
#define V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_

#include "src/base/atomicops.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/external-pointer.h"
#include "src/utils/allocation.h"

#ifdef V8_SANDBOX_IS_AVAILABLE

namespace v8 {
namespace internal {

void ExternalPointerTable::Init(Isolate* isolate) {
  DCHECK(!is_initialized());

  VirtualAddressSpace* root_space = GetPlatformVirtualAddressSpace();
  DCHECK(IsAligned(kExternalPointerTableReservationSize,
                   root_space->allocation_granularity()));
  buffer_ = root_space->AllocatePages(
      VirtualAddressSpace::kNoHint, kExternalPointerTableReservationSize,
      root_space->allocation_granularity(), PagePermissions::kNoAccess);
  if (!buffer_) {
    V8::FatalProcessOutOfMemory(
        isolate,
        "Failed to reserve memory for ExternalPointerTable backing buffer");
  }

  // Allocate the initial block.
  Grow();

  // Set up the special null entry. This entry must currently contain nullptr
  // so that uninitialized EmbedderDataSlots work correctly. TODO(saelo) maybe
  // make entry non-null once EmbedderDataSlots are properly sandboxified.
  STATIC_ASSERT(kNullExternalPointer == 0);
  store(kNullExternalPointer, kNullAddress);
}

void ExternalPointerTable::TearDown() {
  DCHECK(is_initialized());

  CHECK(GetPlatformVirtualAddressSpace()->FreePages(
      buffer_, kExternalPointerTableReservationSize));

  buffer_ = kNullAddress;
  capacity_ = 0;
  freelist_head_ = 0;
}

Address ExternalPointerTable::Get(uint32_t index,
                                  ExternalPointerTag tag) const {
  DCHECK_LT(index, capacity_);

  Address entry = load_atomic(index);
  DCHECK(!is_free(entry));

  return entry & ~tag;
}

void ExternalPointerTable::Set(uint32_t index, Address value,
                               ExternalPointerTag tag) {
  DCHECK_LT(index, capacity_);
  DCHECK_NE(kNullExternalPointer, index);
  DCHECK_EQ(0, value & kExternalPointerTagMask);
  DCHECK(is_marked(tag));

  store_atomic(index, value | tag);
}

bool ExternalPointerTable::IsValidIndex(uint32_t index) const {
  return index < capacity_ && !is_free(load_atomic(index));
}

uint32_t ExternalPointerTable::Allocate() {
  DCHECK(is_initialized());

  if (!freelist_head_) {
    // Freelist is empty so grow the table.
    Grow();
  }

  // Note: if external pointer table entries are ever allocated from a
  // background thread, this logic must become atomic, for example by doing an
  // atomic load of the currentl freelist head, then writing back the new
  // freelist head in a CAS loop.

  DCHECK(freelist_head_);
  uint32_t index = freelist_head_;
  DCHECK_LT(index, capacity_);
  // The next free element is stored in the lower 32 bits of the entry.
  freelist_head_ = static_cast<uint32_t>(load_atomic(index));
  return index;
}

void ExternalPointerTable::Mark(uint32_t index) {
  DCHECK_LT(index, capacity_);
  STATIC_ASSERT(sizeof(base::Atomic64) == sizeof(Address));

  base::Atomic64 old_val = load_atomic(index);
  DCHECK(!is_free(old_val));
  base::Atomic64 new_val = set_mark_bit(old_val);

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

#endif  // V8_SANDBOX_IS_AVAILABLE

#endif  // V8_SANDBOX_EXTERNAL_POINTER_TABLE_INL_H_
