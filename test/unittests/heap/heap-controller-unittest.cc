// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <iostream>
#include <limits>

#include "src/objects-inl.h"
#include "src/objects.h"

#include "src/handles-inl.h"
#include "src/handles.h"

#include "src/heap/heap-controller.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

typedef TestWithIsolate HeapControllerTest;

double Round(double x) {
  // Round to three digits.
  return floor(x * 1000 + 0.5) / 1000;
}

void CheckEqualRounded(double expected, double actual) {
  expected = Round(expected);
  actual = Round(actual);
  EXPECT_DOUBLE_EQ(expected, actual);
}

TEST(HeapController, HeapGrowingFactor) {
  double min_factor = HeapController::kMinHeapGrowingFactor;
  double max_factor = HeapController::kMaxHeapGrowingFactor;
  double target_mu = HeapController::kTargetMutatorUtilization;

  CheckEqualRounded(max_factor,
                    MemoryController::GrowingFactor(min_factor, max_factor,
                                                    target_mu, 34, 1, 4.0));
  CheckEqualRounded(3.553, MemoryController::GrowingFactor(
                               min_factor, max_factor, target_mu, 45, 1, 4.0));
  CheckEqualRounded(2.830, MemoryController::GrowingFactor(
                               min_factor, max_factor, target_mu, 50, 1, 4.0));
  CheckEqualRounded(1.478, MemoryController::GrowingFactor(
                               min_factor, max_factor, target_mu, 100, 1, 4.0));
  CheckEqualRounded(1.193, MemoryController::GrowingFactor(
                               min_factor, max_factor, target_mu, 200, 1, 4.0));
  CheckEqualRounded(1.121, MemoryController::GrowingFactor(
                               min_factor, max_factor, target_mu, 300, 1, 4.0));
  CheckEqualRounded(MemoryController::GrowingFactor(min_factor, max_factor,
                                                    target_mu, 300, 1, 4.0),
                    MemoryController::GrowingFactor(min_factor, max_factor,
                                                    target_mu, 600, 2, 4.0));
  CheckEqualRounded(min_factor,
                    MemoryController::GrowingFactor(min_factor, max_factor,
                                                    target_mu, 400, 1, 4.0));
}

TEST(HeapController, MaxHeapGrowingFactor) {
  CheckEqualRounded(1.3, MemoryController::MaxGrowingFactor(
                             HeapController::kMinOldGenerationSize * MB,
                             HeapController::kMinOldGenerationSize,
                             HeapController::kMaxOldGenerationSize));
  CheckEqualRounded(1.600, MemoryController::MaxGrowingFactor(
                               HeapController::kMaxOldGenerationSize / 2 * MB,
                               HeapController::kMinOldGenerationSize,
                               HeapController::kMaxOldGenerationSize));
  CheckEqualRounded(1.999, MemoryController::MaxGrowingFactor(
                               (HeapController::kMaxOldGenerationSize -
                                Heap::kPointerMultiplier) *
                                   MB,
                               HeapController::kMinOldGenerationSize,
                               HeapController::kMaxOldGenerationSize));
  CheckEqualRounded(
      4.0, MemoryController::MaxGrowingFactor(
               static_cast<size_t>(HeapController::kMaxOldGenerationSize) * MB,
               HeapController::kMinOldGenerationSize,
               HeapController::kMaxOldGenerationSize));
}

TEST_F(HeapControllerTest, OldGenerationAllocationLimit) {
  Heap* heap = i_isolate()->heap();
  size_t old_gen_size = 128 * MB;
  size_t max_old_generation_size = 512 * MB;
  double gc_speed = 100;
  double mutator_speed = 1;
  size_t new_space_capacity = 16 * MB;

  double max_factor = MemoryController::MaxGrowingFactor(
      max_old_generation_size, HeapController::kMinOldGenerationSize,
      HeapController::kMaxOldGenerationSize);
  double factor =
      MemoryController::GrowingFactor(HeapController::kMinHeapGrowingFactor,
                                      HeapController::kMaxHeapGrowingFactor,
                                      HeapController::kTargetMutatorUtilization,
                                      gc_speed, mutator_speed, max_factor);

  EXPECT_EQ(static_cast<size_t>(old_gen_size * factor + new_space_capacity),
            heap->heap_controller()->CalculateOldGenerationAllocationLimit(
                old_gen_size, max_old_generation_size, gc_speed, mutator_speed,
                new_space_capacity, Heap::HeapGrowingMode::kDefault));

  factor = Min(factor, HeapController::kConservativeHeapGrowingFactor);
  EXPECT_EQ(static_cast<size_t>(old_gen_size * factor + new_space_capacity),
            heap->heap_controller()->CalculateOldGenerationAllocationLimit(
                old_gen_size, max_old_generation_size, gc_speed, mutator_speed,
                new_space_capacity, Heap::HeapGrowingMode::kSlow));

  factor = Min(factor, HeapController::kConservativeHeapGrowingFactor);
  EXPECT_EQ(static_cast<size_t>(old_gen_size * factor + new_space_capacity),
            heap->heap_controller()->CalculateOldGenerationAllocationLimit(
                old_gen_size, max_old_generation_size, gc_speed, mutator_speed,
                new_space_capacity, Heap::HeapGrowingMode::kConservative));

  factor = HeapController::kMinHeapGrowingFactor;
  EXPECT_EQ(static_cast<size_t>(old_gen_size * factor + new_space_capacity),
            heap->heap_controller()->CalculateOldGenerationAllocationLimit(
                old_gen_size, max_old_generation_size, gc_speed, mutator_speed,
                new_space_capacity, Heap::HeapGrowingMode::kMinimal));
}

TEST(HeapController, MaxOldGenerationSize) {
  uint64_t configurations[][2] = {
      {0, HeapController::kMinOldGenerationSize},
      {512, HeapController::kMinOldGenerationSize},
      {1 * GB, 256 * Heap::kPointerMultiplier},
      {2 * static_cast<uint64_t>(GB), 512 * Heap::kPointerMultiplier},
      {4 * static_cast<uint64_t>(GB), HeapController::kMaxOldGenerationSize},
      {8 * static_cast<uint64_t>(GB), HeapController::kMaxOldGenerationSize}};

  for (auto configuration : configurations) {
    ASSERT_EQ(configuration[1],
              static_cast<uint64_t>(
                  Heap::ComputeMaxOldGenerationSize(configuration[0])));
  }
}

}  // namespace internal
}  // namespace v8
