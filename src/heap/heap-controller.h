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

class MemoryController {
 protected:
  V8_EXPORT_PRIVATE static double GrowingFactor(
      double min_growing_factor, double max_growing_factor,
      double target_mutator_utilization, double gc_speed, double mutator_speed,
      double max_factor);
  V8_EXPORT_PRIVATE static double MaxGrowingFactor(size_t curr_max_size,
                                                   size_t min_size,
                                                   size_t max_size);

  FRIEND_TEST(HeapController, HeapGrowingFactor);
  FRIEND_TEST(HeapController, MaxHeapGrowingFactor);
  FRIEND_TEST(HeapControllerTest, OldGenerationAllocationLimit);
};

class HeapController : public MemoryController {
 public:
  explicit HeapController(Heap* heap) : heap_(heap) {}

  // Computes the allocation limit to trigger the next full garbage collection.
  V8_EXPORT_PRIVATE size_t CalculateOldGenerationAllocationLimit(
      size_t old_gen_size, size_t max_old_generation_size, double gc_speed,
      double mutator_speed, size_t new_space_capacity,
      Heap::HeapGrowingMode growing_mode);

  size_t MinimumAllocationLimitGrowingStep(Heap::HeapGrowingMode growing_mode);

  // The old space size has to be a multiple of Page::kPageSize.
  // Sizes are in MB.
  static const size_t kMinOldGenerationSize = 128 * Heap::kPointerMultiplier;
  static const size_t kMaxOldGenerationSize = 1024 * Heap::kPointerMultiplier;

 private:
  FRIEND_TEST(HeapController, HeapGrowingFactor);
  FRIEND_TEST(HeapController, MaxHeapGrowingFactor);
  FRIEND_TEST(HeapControllerTest, OldGenerationAllocationLimit);

  V8_EXPORT_PRIVATE static const double kMinHeapGrowingFactor;
  V8_EXPORT_PRIVATE static const double kMaxHeapGrowingFactor;
  V8_EXPORT_PRIVATE static const double kConservativeHeapGrowingFactor;
  V8_EXPORT_PRIVATE static const double kTargetMutatorUtilization;

  Heap* heap_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_CONTROLLER_H_
