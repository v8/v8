// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/allocation-observer.h"

#include "src/heap/heap.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

void AllocationCounter::AddAllocationObserver(AllocationObserver* observer) {
  intptr_t step_size = observer->GetNextStepSize();
  size_t observer_next_counter = current_counter_ + step_size;

#if DEBUG
  auto it =
      std::find_if(observers_.begin(), observers_.end(),
                   [observer](const ObserverAccounting& observer_accounting) {
                     return observer_accounting.observer_ == observer;
                   });
  DCHECK_EQ(observers_.end(), it);
#endif

  observers_.push_back(
      ObserverAccounting(observer, current_counter_, observer_next_counter));

  if (observers_.size() == 1) {
    DCHECK_EQ(current_counter_, next_counter_);
    next_counter_ = observer_next_counter;
  } else {
    size_t missing_bytes = next_counter_ - current_counter_;
    next_counter_ =
        current_counter_ + Min(static_cast<intptr_t>(missing_bytes), step_size);
  }
}

void AllocationCounter::RemoveAllocationObserver(AllocationObserver* observer) {
  auto it =
      std::find_if(observers_.begin(), observers_.end(),
                   [observer](const ObserverAccounting& observer_accounting) {
                     return observer_accounting.observer_ == observer;
                   });
  DCHECK_NE(observers_.end(), it);
  observers_.erase(it);

  if (observers_.size() == 0) {
    current_counter_ = next_counter_ = 0;
  } else {
    size_t step_size = 0;

    for (ObserverAccounting& observer : observers_) {
      size_t left_in_step = observer.next_counter_ - current_counter_;
      DCHECK_GT(left_in_step, 0);
      step_size = step_size ? Min(step_size, left_in_step) : left_in_step;
    }

    next_counter_ = current_counter_ + step_size;
  }
}

void AllocationCounter::AdvanceAllocationObservers(size_t allocated) {
  if (!IsActive()) {
    return;
  }

  DCHECK_LT(allocated, next_counter_ - current_counter_);
  current_counter_ += allocated;
}

void AllocationCounter::InvokeAllocationObservers(Address soon_object,
                                                  size_t object_size,
                                                  size_t aligned_object_size) {
  if (!IsActive()) {
    return;
  }

  DCHECK_GE(aligned_object_size, next_counter_ - current_counter_);
  DCHECK(soon_object);
  size_t step_size = 0;
  bool step_run = false;
  for (ObserverAccounting& observer_accounting : observers_) {
    if (observer_accounting.next_counter_ - current_counter_ <=
        aligned_object_size) {
      observer_accounting.observer_->Step(
          static_cast<int>(current_counter_ -
                           observer_accounting.prev_counter_),
          soon_object, object_size);
      size_t observer_step_size =
          observer_accounting.observer_->GetNextStepSize();

      observer_accounting.prev_counter_ = current_counter_;
      observer_accounting.next_counter_ =
          current_counter_ + aligned_object_size + observer_step_size;
      step_run = true;
    }

    size_t left_in_step = observer_accounting.next_counter_ - current_counter_;
    step_size = step_size ? Min(step_size, left_in_step) : left_in_step;
  }

  CHECK(step_run);
  next_counter_ = current_counter_ + step_size;
}

PauseAllocationObserversScope::PauseAllocationObserversScope(Heap* heap)
    : heap_(heap) {
  DCHECK_EQ(heap->gc_state(), Heap::NOT_IN_GC);

  for (SpaceIterator it(heap_); it.HasNext();) {
    it.Next()->PauseAllocationObservers();
  }
}

PauseAllocationObserversScope::~PauseAllocationObserversScope() {
  for (SpaceIterator it(heap_); it.HasNext();) {
    it.Next()->ResumeAllocationObservers();
  }
}

}  // namespace internal
}  // namespace v8
