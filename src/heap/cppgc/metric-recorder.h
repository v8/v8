// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_METRIC_RECORDER_H_
#define V8_HEAP_CPPGC_METRIC_RECORDER_H_

#include <cstdint>

namespace cppgc {
namespace internal {

class StatsCollector;

/**
 * Base class used for reporting GC statistics histograms. Embedders interested
 * in collecting histgorams should implement the virtual AddMainThreadEvent
 * methods below and pass an instance of the implementation during Heap
 * creation.
 */
class MetricRecorder {
 public:
  struct CppGCCycleEndMetricSamples {
    int64_t atomic_mark_ms;
    int64_t atomic_weak_ms;
    int64_t atomic_compact_ms;
    int64_t atomic_sweep_ms;
    int64_t incremental_mark_ms;
    int64_t incremental_sweep_ms;
    int64_t concurrent_mark_ms;
    int64_t concurrent_sweep_ms;

    int64_t objects_before_bytes;
    int64_t objects_after_bytes;
    int64_t objects_freed_bytes;
    int64_t memory_freed_bytes;
  };

  struct CppGCIncrementalMarkMetricSample {
    int64_t duration_ms;
  };

  struct CppGCIncrementalSweepMetricSample {
    int64_t duration_ms;
  };

  virtual ~MetricRecorder() = default;

  virtual void AddMainThreadEvent(const CppGCCycleEndMetricSamples& event) {}
  virtual void AddMainThreadEvent(
      const CppGCIncrementalMarkMetricSample& event) {}
  virtual void AddMainThreadEvent(
      const CppGCIncrementalSweepMetricSample& event) {}
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_METRIC_RECORDER_H_
