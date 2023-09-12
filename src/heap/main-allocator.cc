// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/main-allocator.h"

namespace v8 {
namespace internal {

MainAllocator::MainAllocator(Heap* heap, Space* space,
                             AllocationCounter& allocation_counter,
                             LinearAllocationArea& allocation_info,
                             LinearAreaOriginalData& linear_area_original_data)
    : heap_(heap),
      space_(space),
      allocation_counter_(allocation_counter),
      allocation_info_(allocation_info),
      linear_area_original_data_(linear_area_original_data) {
  USE(heap_);
  USE(space_);
  USE(allocation_counter_);
}

}  // namespace internal
}  // namespace v8
