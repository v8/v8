// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_CONTROLLER_H_
#define V8_HEAP_HEAP_CONTROLLER_H_

#include <cstddef>
#include "src/allocation.h"
#include "src/heap/heap.h"
#include "testing/gtest/include/gtest/gtest_prod.h"  // nogncheck

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE MemoryController {
 public:
  // Computes the growing step when the limit increases.
  static size_t MinimumAllocationLimitGrowingStep(
      Heap::HeapGrowingMode growing_mode);

  virtual ~MemoryController() = default;

 protected:
  MemoryController(Heap* heap, double min_growing_factor,
                   double max_growing_factor,
                   double conservative_growing_factor,
                   double target_mutator_utlization)
      : heap_(heap),
        min_growing_factor_(min_growing_factor),
        max_growing_factor_(max_growing_factor),
        conservative_growing_factor_(conservative_growing_factor),
        target_mutator_utlization_(target_mutator_utlization) {}

  // Computes the allocation limit to trigger the next garbage collection.
  size_t CalculateAllocationLimitBase(size_t curr_size, size_t max_size,
                                      double factor, size_t additional_bytes,
                                      Heap::HeapGrowingMode growing_mode);

  double GrowingFactor(double gc_speed, double mutator_speed,
                       double max_factor);

  virtual const char* ControllerName() = 0;

  Heap* const heap_;
  const double min_growing_factor_;
  const double max_growing_factor_;
  const double conservative_growing_factor_;
  const double target_mutator_utlization_;
};

class V8_EXPORT_PRIVATE HeapController : public MemoryController {
 public:
  // Sizes are in MB.
  static constexpr size_t kMinSize = 128 * Heap::kPointerMultiplier;
  static constexpr size_t kMaxSize = 1024 * Heap::kPointerMultiplier;

  explicit HeapController(Heap* heap)
      : MemoryController(heap, 1.1, 4.0, 1.3, 0.97) {}

  size_t CalculateAllocationLimit(size_t curr_size, size_t max_size,
                                  double gc_speed, double mutator_speed,
                                  size_t new_space_capacity,
                                  Heap::HeapGrowingMode growing_mode);

 protected:
  double MaxGrowingFactor(size_t curr_max_size);

  const char* ControllerName() override { return "HeapController"; }

  FRIEND_TEST(HeapControllerTest, HeapGrowingFactor);
  FRIEND_TEST(HeapControllerTest, MaxHeapGrowingFactor);
  FRIEND_TEST(HeapControllerTest, MaxOldGenerationSize);
  FRIEND_TEST(HeapControllerTest, OldGenerationAllocationLimit);
};

class V8_EXPORT_PRIVATE GlobalMemoryController : public MemoryController {
 public:
  // Sizes are in MB.
  static constexpr size_t kMinSize = 128 * Heap::kPointerMultiplier;
  static constexpr size_t kMaxSize = 1024 * Heap::kPointerMultiplier;

  explicit GlobalMemoryController(Heap* heap)
      : MemoryController(heap, 1.1, 4.0, 1.3, 0.97) {}

  size_t CalculateAllocationLimit(size_t curr_size, size_t max_size,
                                  double gc_speed, double mutator_speed,
                                  size_t new_space_capacity,
                                  Heap::HeapGrowingMode growing_mode);

 protected:
  double MaxGrowingFactor(size_t curr_max_size);

  const char* ControllerName() override { return "GlobalMemoryController"; }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_CONTROLLER_H_
