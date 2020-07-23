// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ALLOCATION_OBSERVER_H_
#define V8_HEAP_ALLOCATION_OBSERVER_H_

#include <vector>

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class AllocationObserver;

class AllocationCounter {
 public:
  AllocationCounter() : paused_(false) {}

  auto begin() { return allocation_observers_.begin(); }
  auto end() { return allocation_observers_.end(); }

  void AddAllocationObserver(AllocationObserver* observer);
  void RemoveAllocationObserver(AllocationObserver* observer);

  bool HasAllocationObservers() { return !allocation_observers_.empty(); }
  size_t NumberAllocationObservers() { return allocation_observers_.size(); }

  bool IsActive() { return !IsPaused() && HasAllocationObservers(); }

  void Pause() {
    DCHECK(!paused_);
    paused_ = true;
  }

  void Resume() {
    DCHECK(paused_);
    paused_ = false;
  }

 private:
  bool IsPaused() { return paused_; }

  std::vector<AllocationObserver*> allocation_observers_;
  bool paused_;
};

// -----------------------------------------------------------------------------
// Allows observation of allocations.
class AllocationObserver {
 public:
  explicit AllocationObserver(intptr_t step_size)
      : step_size_(step_size), bytes_to_next_step_(step_size) {
    DCHECK_LE(kTaggedSize, step_size);
  }
  virtual ~AllocationObserver() = default;

  // Called each time the observed space does an allocation step. This may be
  // more frequently than the step_size we are monitoring (e.g. when there are
  // multiple observers, or when page or space boundary is encountered.)
  void AllocationStep(int bytes_allocated, Address soon_object, size_t size);

 protected:
  intptr_t step_size() const { return step_size_; }
  intptr_t bytes_to_next_step() const { return bytes_to_next_step_; }

  // Pure virtual method provided by the subclasses that gets called when at
  // least step_size bytes have been allocated. soon_object is the address just
  // allocated (but not yet initialized.) size is the size of the object as
  // requested (i.e. w/o the alignment fillers). Some complexities to be aware
  // of:
  // 1) soon_object will be nullptr in cases where we end up observing an
  //    allocation that happens to be a filler space (e.g. page boundaries.)
  // 2) size is the requested size at the time of allocation. Right-trimming
  //    may change the object size dynamically.
  // 3) soon_object may actually be the first object in an allocation-folding
  //    group. In such a case size is the size of the group rather than the
  //    first object.
  virtual void Step(int bytes_allocated, Address soon_object, size_t size) = 0;

  // Subclasses can override this method to make step size dynamic.
  virtual intptr_t GetNextStepSize() { return step_size_; }

  intptr_t step_size_;
  intptr_t bytes_to_next_step_;

 private:
  friend class Space;
  DISALLOW_COPY_AND_ASSIGN(AllocationObserver);
};

class V8_EXPORT_PRIVATE PauseAllocationObserversScope {
 public:
  explicit PauseAllocationObserversScope(Heap* heap);
  ~PauseAllocationObserversScope();

 private:
  Heap* heap_;
  DISALLOW_COPY_AND_ASSIGN(PauseAllocationObserversScope);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATION_OBSERVER_H_
