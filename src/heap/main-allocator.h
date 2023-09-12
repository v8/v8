// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MAIN_ALLOCATOR_H_
#define V8_HEAP_MAIN_ALLOCATOR_H_

#include "src/base/optional.h"
#include "src/common/globals.h"
#include "src/heap/allocation-observer.h"
#include "src/heap/linear-allocation-area.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class Heap;
class Space;

class LinearAreaOriginalData {
 public:
  Address get_original_top_acquire() const {
    return original_top_.load(std::memory_order_acquire);
  }
  Address get_original_limit_relaxed() const {
    return original_limit_.load(std::memory_order_relaxed);
  }

  void set_original_top_release(Address top) {
    original_top_.store(top, std::memory_order_release);
  }
  void set_original_limit_relaxed(Address limit) {
    original_limit_.store(limit, std::memory_order_relaxed);
  }

  base::SharedMutex* linear_area_lock() { return &linear_area_lock_; }

 private:
  // The top and the limit at the time of setting the linear allocation area.
  // These values can be accessed by background tasks. Protected by
  // pending_allocation_mutex_.
  std::atomic<Address> original_top_ = 0;
  std::atomic<Address> original_limit_ = 0;

  // Protects original_top_ and original_limit_.
  base::SharedMutex linear_area_lock_;
};

class MainAllocator {
 public:
  MainAllocator(Heap* heap, Space* space, AllocationCounter& allocation_counter,
                LinearAllocationArea& allocation_info,
                LinearAreaOriginalData& linear_area_original_data);

  // Returns the allocation pointer in this space.
  Address start() const { return allocation_info_.start(); }
  Address top() const { return allocation_info_.top(); }
  Address limit() const { return allocation_info_.limit(); }

  // The allocation top address.
  Address* allocation_top_address() const {
    return allocation_info_.top_address();
  }

  // The allocation limit address.
  Address* allocation_limit_address() const {
    return allocation_info_.limit_address();
  }

  base::SharedMutex* linear_area_lock() {
    return linear_area_original_data_.linear_area_lock();
  }

  Address original_top_acquire() const {
    return linear_area_original_data_.get_original_top_acquire();
  }

  Address original_limit_relaxed() const {
    return linear_area_original_data_.get_original_limit_relaxed();
  }

  void MoveOriginalTopForward() {
    base::SharedMutexGuard<base::kExclusive> guard(linear_area_lock());
    DCHECK_GE(top(), linear_area_original_data_.get_original_top_acquire());
    DCHECK_LE(top(), linear_area_original_data_.get_original_limit_relaxed());
    linear_area_original_data_.set_original_top_release(top());
  }

  LinearAllocationArea& allocation_info() { return allocation_info_; }

  const LinearAllocationArea& allocation_info() const {
    return allocation_info_;
  }

  LinearAreaOriginalData& linear_area_original_data() {
    return linear_area_original_data_;
  }

  const LinearAreaOriginalData& linear_area_original_data() const {
    return linear_area_original_data_;
  }

 private:
  Heap* heap_;
  Space* space_;

  AllocationCounter& allocation_counter_;
  LinearAllocationArea& allocation_info_;
  LinearAreaOriginalData& linear_area_original_data_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MAIN_ALLOCATOR_H_
