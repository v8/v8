// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ALLOCATION_RESULT_H_
#define V8_HEAP_ALLOCATION_RESULT_H_

#include "src/common/globals.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

// The result of an allocation attempt. Either represents a successful
// allocation that can be turned into an object or a failed attempt.
class AllocationResult final {
 public:
  static AllocationResult Failure(AllocationSpace space) {
    return AllocationResult(space);
  }

  static AllocationResult FromObject(HeapObject heap_object) {
    return AllocationResult(heap_object);
  }

  // Empty constructor creates a failed result that will turn into a full
  // garbage collection.
  AllocationResult() : AllocationResult(AllocationSpace::OLD_SPACE) {}

  bool IsFailure() const { return object_.IsSmi(); }

  template <typename T>
  bool To(T* obj) const {
    if (IsFailure()) return false;
    *obj = T::cast(object_);
    return true;
  }

  HeapObject ToObjectChecked() const {
    CHECK(!IsFailure());
    return HeapObject::cast(object_);
  }

  HeapObject ToObject() const {
    DCHECK(!IsFailure());
    return HeapObject::cast(object_);
  }

  Address ToAddress() const {
    DCHECK(!IsFailure());
    return HeapObject::cast(object_).address();
  }

  // Returns the space that should be passed to a garbage collection call.
  AllocationSpace ToGarbageCollectionSpace() const {
    DCHECK(IsFailure());
    return static_cast<AllocationSpace>(Smi::ToInt(object_));
  }

 private:
  explicit AllocationResult(AllocationSpace space)
      : object_(Smi::FromInt(static_cast<int>(space))) {}

  explicit AllocationResult(HeapObject heap_object) : object_(heap_object) {}

  Object object_;
};

STATIC_ASSERT(sizeof(AllocationResult) == kSystemPointerSize);

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATION_RESULT_H_
