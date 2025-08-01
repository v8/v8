// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_EVACUATION_ALLOCATOR_INL_H_
#define V8_HEAP_EVACUATION_ALLOCATOR_INL_H_

#include "src/heap/evacuation-allocator.h"
// Include the non-inl header before the rest of the headers.

#include "src/common/globals.h"
#include "src/heap/spaces-inl.h"

namespace v8 {
namespace internal {

AllocationResult EvacuationAllocator::Allocate(AllocationSpace space,
                                               SafeHeapObjectSize object_size,
                                               AllocationAlignment alignment) {
  // TODO(425150995): We should have uint versions for allocation to avoid
  // introducing OOBs via sign-extended ints along the way.
  DCHECK_IMPLIES(!shared_space_allocator_, space != SHARED_SPACE);
  object_size = ALIGN_TO_ALLOCATION_ALIGNMENT(object_size);
  switch (space) {
    case NEW_SPACE:
      return new_space_allocator()->AllocateRaw(object_size.value(), alignment,
                                                AllocationOrigin::kGC,
                                                AllocationHint());
    case OLD_SPACE:
      return old_space_allocator()->AllocateRaw(object_size.value(), alignment,
                                                AllocationOrigin::kGC,
                                                AllocationHint());
    case CODE_SPACE:
      return code_space_allocator()->AllocateRaw(object_size.value(), alignment,
                                                 AllocationOrigin::kGC,
                                                 AllocationHint());
    case SHARED_SPACE:
      return shared_space_allocator()->AllocateRaw(
          object_size.value(), alignment, AllocationOrigin::kGC,
          AllocationHint());
    case TRUSTED_SPACE:
      return trusted_space_allocator()->AllocateRaw(
          object_size.value(), alignment, AllocationOrigin::kGC,
          AllocationHint());
    default:
      UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_EVACUATION_ALLOCATOR_INL_H_
