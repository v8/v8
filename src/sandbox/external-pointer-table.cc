// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/external-pointer-table.h"

#include <algorithm>

#include "src/execution/isolate.h"
#include "src/logging/counters.h"
#include "src/sandbox/external-pointer-table-inl.h"

#ifdef V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

static_assert(sizeof(ExternalPointerTable) == ExternalPointerTable::kSize);

uint32_t ExternalPointerTable::Sweep(Isolate* isolate) {
  // There must not be any entry allocations while the table is being swept as
  // that would not be safe. Set the freelist head to this special marker value
  // to better catch any violation of this requirement.
  base::Release_Store(&freelist_head_, kTableIsCurrentlySweepingMarker);

  // Keep track of the last block (identified by the index of its first entry)
  // that has live entries. Used to decommit empty blocks at the end.
  DCHECK_GE(capacity(), kEntriesPerBlock);
  const uint32_t last_block = capacity() - kEntriesPerBlock;
  uint32_t last_in_use_block = last_block;

  // Sweep top to bottom and rebuild the freelist from newly dead and
  // previously freed entries while also clearing the marking bit on live
  // entries. This way, the freelist ends up sorted by index, which helps
  // defragment the table. This method must run either on the mutator thread or
  // while the mutator is stopped.
  uint32_t freelist_size = 0;
  uint32_t current_freelist_head = 0;

  // Skip the special null entry. This also guarantees that the first block
  // will never be decommitted.
  DCHECK_GE(capacity(), 1);
  for (uint32_t i = capacity() - 1; i > 0; i--) {
    // No other threads are active during sweep, so there is no need to use
    // atomic operations here.
    Address entry = load(i);
    if (!is_marked(entry)) {
      store(i, make_freelist_entry(current_freelist_head));
      current_freelist_head = i;
      freelist_size++;
    } else {
      store(i, clear_mark_bit(entry));
    }

    if (last_in_use_block == i) {
      // Finished iterating over the last in-use block. Now see if it is empty.
      if (freelist_size == kEntriesPerBlock) {
        // Block is completely empty, so mark it for decommitting.
        last_in_use_block -= kEntriesPerBlock;
        // Freelist is now empty again.
        current_freelist_head = 0;
        freelist_size = 0;
      }
    }
  }

  // Decommit all blocks at the end of the table that are not used anymore.
  if (last_in_use_block != last_block) {
    uint32_t new_capacity = last_in_use_block + kEntriesPerBlock;
    DCHECK_LT(new_capacity, capacity());
    Address new_table_end = buffer_ + new_capacity * sizeof(Address);
    uint32_t bytes_to_decommit = (capacity() - new_capacity) * sizeof(Address);
    set_capacity(new_capacity);

    VirtualAddressSpace* root_space = GetPlatformVirtualAddressSpace();
    CHECK(root_space->DecommitPages(new_table_end, bytes_to_decommit));
  }

  base::Release_Store(&freelist_head_, current_freelist_head);

  uint32_t num_active_entries = capacity() - freelist_size;
  isolate->counters()->external_pointers_count()->AddSample(num_active_entries);
  return num_active_entries;
}

uint32_t ExternalPointerTable::Grow() {
  // Freelist should be empty.
  DCHECK_EQ(0, freelist_head_);
  // Mutex must be held when calling this method.
  mutex_->AssertHeld();

  // Grow the table by one block.
  uint32_t old_capacity = capacity();
  uint32_t new_capacity = old_capacity + kEntriesPerBlock;
  CHECK_LE(new_capacity, kMaxExternalPointers);

  // Failure likely means OOM. TODO(saelo) handle this.
  VirtualAddressSpace* root_space = GetPlatformVirtualAddressSpace();
  DCHECK(IsAligned(kBlockSize, root_space->page_size()));
  CHECK(root_space->SetPagePermissions(buffer_ + old_capacity * sizeof(Address),
                                       kBlockSize,
                                       PagePermissions::kReadWrite));
  set_capacity(new_capacity);

  // Build freelist bottom to top, which might be more cache friendly.
  uint32_t start = std::max<uint32_t>(old_capacity, 1);  // Skip entry zero
  uint32_t last = new_capacity - 1;
  for (uint32_t i = start; i < last; i++) {
    store(i, make_freelist_entry(i + 1));
  }
  store(last, make_freelist_entry(0));

  // This must be a release store to prevent reordering of the preceeding
  // stores to the freelist from being reordered past this store. See
  // Allocate() for more details.
  base::Release_Store(&freelist_head_, start);
  return start;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_COMPRESS_POINTERS
