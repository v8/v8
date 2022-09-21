// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_HEAP_CONFIG_H_
#define V8_HEAP_CPPGC_HEAP_CONFIG_H_

#include "include/cppgc/heap.h"

namespace cppgc::internal {

struct SweepingConfig {
  using SweepingType = cppgc::Heap::SweepingType;
  enum class CompactableSpaceHandling { kSweep, kIgnore };
  enum class FreeMemoryHandling { kDoNotDiscard, kDiscardWherePossible };

  SweepingType sweeping_type = SweepingType::kIncrementalAndConcurrent;
  CompactableSpaceHandling compactable_space_handling =
      CompactableSpaceHandling::kSweep;
  FreeMemoryHandling free_memory_handling = FreeMemoryHandling::kDoNotDiscard;
};

}  // namespace cppgc::internal

#endif  // V8_HEAP_CPPGC_HEAP_CONFIG_H_
