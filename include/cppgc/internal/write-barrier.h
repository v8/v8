// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_
#define INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_

#include "cppgc/internal/api-constants.h"
#include "cppgc/internal/process-heap.h"
#include "cppgc/trace-trait.h"
#include "v8config.h"  // NOLINT(build/include_directory)

#if defined(CPPGC_CAGED_HEAP)
#include "cppgc/internal/caged-heap-local-data.h"
#endif

namespace cppgc {

class HeapHandle;

namespace internal {

class V8_EXPORT WriteBarrier final {
 public:
  static V8_INLINE void DijkstraMarkingBarrier(const void* slot,
                                               const void* value) {
#if defined(CPPGC_CAGED_HEAP)
    CagedHeapResult result;
    if (!TryGetCagedHeap(slot, value, result)) {
      return;
    }
    if (V8_UNLIKELY(result.caged_heap().is_marking_in_progress)) {
      DijkstraMarkingBarrierSlow(value);
      return;
    }
#if defined(CPPGC_YOUNG_GENERATION)
    GenerationalBarrier(result.caged_heap(), slot, result.slot_offset,
                        reinterpret_cast<uintptr_t>(value) - result.start);
#endif
#else
    if (V8_LIKELY(!ProcessHeap::IsAnyIncrementalOrConcurrentMarking())) return;

    DijkstraMarkingBarrierSlowWithSentinelCheck(value);
#endif  // CPPGC_CAGED_HEAP
  }

  template <typename LazyHeapCallback>
  static V8_INLINE void DijkstraMarkingBarrierRange(
      LazyHeapCallback heap_callback, const void* first_element,
      size_t element_size, size_t number_of_elements,
      TraceCallback trace_callback) {
#if defined(CPPGC_CAGED_HEAP)
    CagedHeapResult result;
    if (!TryGetCagedHeap(first_element, first_element, result)) {
      return;
    }

    if (V8_UNLIKELY(result.caged_heap().is_marking_in_progress)) {
      DijkstraMarkingBarrierRangeSlow(heap_callback(), first_element,
                                      element_size, number_of_elements,
                                      trace_callback);
      return;
    }
#if defined(CPPGC_YOUNG_GENERATION)
    // We pass 0 as value offset to indicate that there's no information about
    // the value.
    GenerationalBarrier(result.caged_heap(), first_element, result.slot_offset,
                        0);
#endif
#else
    if (V8_LIKELY(!ProcessHeap::IsAnyIncrementalOrConcurrentMarking())) return;

    // TODO(1056170): Inline a check for is marking on the API if necessary.
    DijkstraMarkingBarrierRangeSlow(heap_callback(), first_element,
                                    element_size, number_of_elements,
                                    trace_callback);
#endif  // CPPGC_CAGED_HEAP
  }

  static V8_INLINE void SteeleMarkingBarrier(const void* object) {
#if defined(CPPGC_CAGED_HEAP)
    CagedHeapResult result;
    // The passed slot here is contained within object's header, resulting in
    // `result` referring to the interiors of `object`.
    if (!TryGetCagedHeap(object, object, result)) {
      return;
    }

    if (V8_UNLIKELY(result.caged_heap().is_marking_in_progress)) {
      SteeleMarkingBarrierSlow(object);
      return;
    }
#if defined(CPPGC_YOUNG_GENERATION)
    // We pass 0 as value offset to indicate that there's no information about
    // the value.
    GenerationalBarrier(result.caged_heap(), object, result.slot_offset, 0);
#endif
#else
    if (V8_LIKELY(!ProcessHeap::IsAnyIncrementalOrConcurrentMarking())) return;

    SteeleMarkingBarrierSlowWithSentinelCheck(object);
#endif  // CPPGC_CAGED_HEAP
  }

 private:
  WriteBarrier() = delete;

#if defined(CPPGC_CAGED_HEAP)
  struct CagedHeapResult {
    uintptr_t start;
    uintptr_t slot_offset;

    CagedHeapLocalData& caged_heap() const {
      return *reinterpret_cast<CagedHeapLocalData*>(start);
    }
  };

  // The contents of `result` are only valid if `TryCagedHeap()` returns true.
  static V8_INLINE bool TryGetCagedHeap(const void* slot, const void* value,
                                        CagedHeapResult& result) {
    result.start = reinterpret_cast<uintptr_t>(value) &
                   ~(api_constants::kCagedHeapReservationAlignment - 1);
    result.slot_offset = reinterpret_cast<uintptr_t>(slot) - result.start;
    if (result.slot_offset > api_constants::kCagedHeapReservationSize) {
      // Check if slot is on stack or value is sentinel or nullptr. This relies
      // on the fact that kSentinelPointer is encoded as 0x1.
      return false;
    }
    return true;
  }
#endif  // CPPGC_CAGED_HEAP

  static void DijkstraMarkingBarrierSlow(const void* value);
  static void DijkstraMarkingBarrierSlowWithSentinelCheck(const void* value);
  static void DijkstraMarkingBarrierRangeSlow(HeapHandle& heap_handle,
                                              const void* first_element,
                                              size_t element_size,
                                              size_t number_of_elements,
                                              TraceCallback trace_callback);
  static void SteeleMarkingBarrierSlow(const void* value);
  static void SteeleMarkingBarrierSlowWithSentinelCheck(const void* value);

#if defined(CPPGC_YOUNG_GENERATION)
  static V8_INLINE void GenerationalBarrier(CagedHeapLocalData& local_data,
                                            const void* slot,
                                            uintptr_t slot_offset,
                                            uintptr_t value_offset) {
    const AgeTable& age_table = local_data.age_table;

    // Bail out if the slot is in young generation.
    if (V8_LIKELY(age_table[slot_offset] == AgeTable::Age::kYoung)) return;

    GenerationalBarrierSlow(local_data, age_table, slot, value_offset);
  }

  static void GenerationalBarrierSlow(CagedHeapLocalData& local_data,
                                      const AgeTable& ageTable,
                                      const void* slot, uintptr_t value_offset);
#endif  // CPPGC_YOUNG_GENERATION
};

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_
