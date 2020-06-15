// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_
#define INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_

#include "cppgc/internal/api-constants.h"
#include "cppgc/internal/process-heap.h"
#include "v8config.h"  // NOLINT(build/include_directory)

#if defined(CPPGC_CAGED_HEAP)
#include "cppgc/internal/caged-heap-local-data.h"
#endif

namespace cppgc {
namespace internal {

class V8_EXPORT WriteBarrier final {
 public:
  static V8_INLINE void MarkingBarrier(const void* slot, const void* value) {
#if defined(CPPGC_CAGED_HEAP)
    const uintptr_t start =
        reinterpret_cast<uintptr_t>(value) &
        ~(api_constants::kCagedHeapReservationAlignment - 1);
    const uintptr_t slot_offset = reinterpret_cast<uintptr_t>(slot) - start;
    if (slot_offset > api_constants::kCagedHeapReservationSize) {
      // Check if slot is on stack or value is sentinel or nullptr.
      return;
    }

    CagedHeapLocalData* local_data =
        reinterpret_cast<CagedHeapLocalData*>(start);
    if (V8_LIKELY(!local_data->is_marking_in_progress)) return;

    MarkingBarrierSlow(value);
#else
    if (V8_LIKELY(!ProcessHeap::IsAnyIncrementalOrConcurrentMarking())) return;

    MarkingBarrierSlowWithSentinelCheck(value);
#endif  // CPPGC_CAGED_HEAP
  }

 private:
  WriteBarrier() = delete;

  static void MarkingBarrierSlow(const void* value);
  static void MarkingBarrierSlowWithSentinelCheck(const void* value);
};

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_
