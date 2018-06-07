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

class Heap;

class HeapController {
 public:
  explicit HeapController(Heap* heap) : heap_(heap) {}

  // Computes the allocation limit to trigger the next full garbage collection.
  size_t ComputeOldGenerationAllocationLimit(size_t old_gen_size,
                                             size_t max_old_generation_size,
                                             double gc_speed,
                                             double mutator_speed);

  // Decrease the allocation limit if the new limit based on the given
  // parameters is lower than the current limit.
  size_t DampenOldGenerationAllocationLimit(size_t old_gen_size,
                                            size_t max_old_generation_size,
                                            double gc_speed,
                                            double mutator_speed);

  size_t MinimumAllocationLimitGrowingStep();

  // The old space size has to be a multiple of Page::kPageSize.
  // Sizes are in MB.
  static const size_t kMinOldGenerationSize = 128 * Heap::kPointerMultiplier;
  static const size_t kMaxOldGenerationSize = 1024 * Heap::kPointerMultiplier;

 private:
  FRIEND_TEST(HeapController, HeapGrowingFactor);
  FRIEND_TEST(HeapController, MaxHeapGrowingFactor);
  FRIEND_TEST(HeapController, OldGenerationSize);

  V8_EXPORT_PRIVATE static const double kMinHeapGrowingFactor;
  V8_EXPORT_PRIVATE static const double kMaxHeapGrowingFactor;
  V8_EXPORT_PRIVATE static double MaxHeapGrowingFactor(
      size_t max_old_generation_size);
  V8_EXPORT_PRIVATE static double HeapGrowingFactor(double gc_speed,
                                                    double mutator_speed,
                                                    double max_factor);

  // Calculates the allocation limit based on a given growing factor and a
  // given old generation size.
  size_t CalculateOldGenerationAllocationLimit(double factor,
                                               size_t old_gen_size,
                                               size_t max_old_generation_size);

  Heap* heap_;

  static const double kMaxHeapGrowingFactorMemoryConstrained;
  static const double kMaxHeapGrowingFactorIdle;
  static const double kConservativeHeapGrowingFactor;
  static const double kTargetMutatorUtilization;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_CONTROLLER_H_
