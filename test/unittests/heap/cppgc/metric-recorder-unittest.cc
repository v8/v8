// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/metric-recorder.h"

#include "src/heap/cppgc/stats-collector.h"
#include "test/unittests/heap/cppgc/tests.h"

namespace cppgc {
namespace internal {

namespace {
class MetricRecorderImpl final : public MetricRecorder {
 public:
  void AddMainThreadEvent(const CppGCCycleEndMetricSamples& event) final {
    CppGCCycleEndMetricSamples_event = event;
    CppGCCycleEndMetricSamples_callcount++;
  }
  void AddMainThreadEvent(const CppGCIncrementalMarkMetricSample& event) final {
    CppGCIncrementalMarkMetricSample_event = event;
    CppGCIncrementalMarkMetricSample_callcount++;
  }
  void AddMainThreadEvent(
      const CppGCIncrementalSweepMetricSample& event) final {
    CppGCIncrementalSweepMetricSample_event = event;
    CppGCIncrementalSweepMetricSample_callcount++;
  }

  static size_t CppGCCycleEndMetricSamples_callcount;
  static CppGCCycleEndMetricSamples CppGCCycleEndMetricSamples_event;
  static size_t CppGCIncrementalMarkMetricSample_callcount;
  static CppGCIncrementalMarkMetricSample
      CppGCIncrementalMarkMetricSample_event;
  static size_t CppGCIncrementalSweepMetricSample_callcount;
  static CppGCIncrementalSweepMetricSample
      CppGCIncrementalSweepMetricSample_event;
};

// static
size_t MetricRecorderImpl::CppGCCycleEndMetricSamples_callcount = 0u;
MetricRecorderImpl::CppGCCycleEndMetricSamples
    MetricRecorderImpl::CppGCCycleEndMetricSamples_event;
size_t MetricRecorderImpl::CppGCIncrementalMarkMetricSample_callcount = 0u;
MetricRecorderImpl::CppGCIncrementalMarkMetricSample
    MetricRecorderImpl::CppGCIncrementalMarkMetricSample_event;
size_t MetricRecorderImpl::CppGCIncrementalSweepMetricSample_callcount = 0u;
MetricRecorderImpl::CppGCIncrementalSweepMetricSample
    MetricRecorderImpl::CppGCIncrementalSweepMetricSample_event;

class MetricRecorderTest : public testing::TestWithHeap {
 public:
  MetricRecorderTest() : stats(Heap::From(GetHeap())->stats_collector()) {
    stats->SetMetricRecorderForTesting(std::make_unique<MetricRecorderImpl>());
  }

  void StartGC() {
    stats->NotifyMarkingStarted(
        GarbageCollector::Config::CollectionType::kMajor,
        GarbageCollector::Config::IsForcedGC::kNotForced);
  }
  void EndGC(size_t marked_bytes) {
    stats->NotifyMarkingCompleted(marked_bytes);
    stats->NotifySweepingCompleted();
  }

  StatsCollector* stats;
};
}  // namespace

TEST_F(MetricRecorderTest, IncrementalScopesReportedImmediately) {
  MetricRecorderImpl::CppGCCycleEndMetricSamples_callcount = 0u;
  MetricRecorderImpl::CppGCIncrementalMarkMetricSample_callcount = 0u;
  MetricRecorderImpl::CppGCIncrementalSweepMetricSample_callcount = 0u;
  StartGC();
  {
    EXPECT_EQ(0u,
              MetricRecorderImpl::CppGCIncrementalMarkMetricSample_callcount);
    {
      StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                         StatsCollector::kIncrementalMark);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EXPECT_EQ(1u,
              MetricRecorderImpl::CppGCIncrementalMarkMetricSample_callcount);
    EXPECT_LT(
        0u,
        MetricRecorderImpl::CppGCIncrementalMarkMetricSample_event.duration_ms);
  }
  {
    EXPECT_EQ(0u,
              MetricRecorderImpl::CppGCIncrementalSweepMetricSample_callcount);
    {
      StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                         StatsCollector::kIncrementalSweep);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EXPECT_EQ(1u,
              MetricRecorderImpl::CppGCIncrementalSweepMetricSample_callcount);
    EXPECT_LT(0u, MetricRecorderImpl::CppGCIncrementalSweepMetricSample_event
                      .duration_ms);
  }
  EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_callcount);
  EndGC(0);
}

TEST_F(MetricRecorderTest, AtomicScopesNotReportedImmediately) {
  MetricRecorderImpl::CppGCCycleEndMetricSamples_callcount = 0u;
  MetricRecorderImpl::CppGCIncrementalMarkMetricSample_callcount = 0u;
  MetricRecorderImpl::CppGCIncrementalSweepMetricSample_callcount = 0u;
  StartGC();
  {
    StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                       StatsCollector::kAtomicMark);
  }
  {
    StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                       StatsCollector::kAtomicWeak);
  }
  {
    StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                       StatsCollector::kAtomicCompact);
  }
  {
    StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                       StatsCollector::kAtomicSweep);
  }
  EXPECT_EQ(0u, MetricRecorderImpl::CppGCIncrementalMarkMetricSample_callcount);
  EXPECT_EQ(0u,
            MetricRecorderImpl::CppGCIncrementalSweepMetricSample_callcount);
  EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_callcount);
  EndGC(0);
}

TEST_F(MetricRecorderTest, CycleEndMetricsReportedOnGcEnd) {
  MetricRecorderImpl::CppGCCycleEndMetricSamples_callcount = 0u;
  MetricRecorderImpl::CppGCIncrementalMarkMetricSample_callcount = 0u;
  MetricRecorderImpl::CppGCIncrementalSweepMetricSample_callcount = 0u;
  StartGC();
  EndGC(0);
  EXPECT_EQ(0u, MetricRecorderImpl::CppGCIncrementalMarkMetricSample_callcount);
  EXPECT_EQ(0u,
            MetricRecorderImpl::CppGCIncrementalSweepMetricSample_callcount);
  EXPECT_EQ(1u, MetricRecorderImpl::CppGCCycleEndMetricSamples_callcount);
}

TEST_F(MetricRecorderTest, CycleEndHistogramReportsValuesForAtomicScopes) {
  {
    StartGC();
    EndGC(0);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_mark_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_weak_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_compact_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_sweep_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_mark_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_sweep_ms);
  }
  {
    StartGC();
    {
      StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                         StatsCollector::kAtomicMark);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EndGC(0);
    EXPECT_LT(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_mark_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_weak_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_compact_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_sweep_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_mark_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_sweep_ms);
  }
  {
    StartGC();
    {
      StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                         StatsCollector::kAtomicWeak);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EndGC(0);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_mark_ms);
    EXPECT_LT(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_weak_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_compact_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_sweep_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_mark_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_sweep_ms);
  }
  {
    StartGC();
    {
      StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                         StatsCollector::kAtomicCompact);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EndGC(0);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_mark_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_weak_ms);
    EXPECT_LT(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_compact_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_sweep_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_mark_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_sweep_ms);
  }
  {
    StartGC();
    {
      StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                         StatsCollector::kAtomicSweep);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EndGC(0);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_mark_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_weak_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_compact_ms);
    EXPECT_LT(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_sweep_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_mark_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_sweep_ms);
  }
  {
    StartGC();
    {
      StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                         StatsCollector::kIncrementalMark);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EndGC(0);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_mark_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_weak_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_compact_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_sweep_ms);
    EXPECT_LT(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_mark_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_sweep_ms);
  }
  {
    StartGC();
    {
      StatsCollector::EnabledScope scope(*Heap::From(GetHeap()),
                                         StatsCollector::kIncrementalSweep);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EndGC(0);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_mark_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_weak_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_compact_ms);
    EXPECT_EQ(
        0u,
        MetricRecorderImpl::CppGCCycleEndMetricSamples_event.atomic_sweep_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_mark_ms);
    EXPECT_LT(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .incremental_sweep_ms);
  }
}

TEST_F(MetricRecorderTest, ConcurrentSamplesAreReported) {
  {
    StartGC();
    EndGC(0);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .concurrent_mark_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .concurrent_sweep_ms);
  }
  {
    StartGC();
    {
      StatsCollector::EnabledConcurrentScope scope(
          *Heap::From(GetHeap()), StatsCollector::kConcurrentMark);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EndGC(0);
    EXPECT_LT(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .concurrent_mark_ms);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .concurrent_sweep_ms);
  }
  {
    StartGC();
    {
      StatsCollector::EnabledConcurrentScope scope(
          *Heap::From(GetHeap()), StatsCollector::kConcurrentSweep);
      scope.DecreaseStartTimeForTesting(
          v8::base::TimeDelta::FromMilliseconds(1));
    }
    EndGC(0);
    EXPECT_EQ(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .concurrent_mark_ms);
    EXPECT_LT(0u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                      .concurrent_sweep_ms);
  }
}

TEST_F(MetricRecorderTest, ObjectSizeMetricsNoAllocations) {
  // Populate previous event.
  StartGC();
  EndGC(1000);
  // Populate current event.
  StartGC();
  EndGC(800);
  EXPECT_EQ(1000u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                       .objects_before_bytes);
  EXPECT_EQ(
      800u,
      MetricRecorderImpl::CppGCCycleEndMetricSamples_event.objects_after_bytes);
  EXPECT_EQ(
      200u,
      MetricRecorderImpl::CppGCCycleEndMetricSamples_event.objects_freed_bytes);
  EXPECT_EQ(
      0u,
      MetricRecorderImpl::CppGCCycleEndMetricSamples_event.memory_freed_bytes);
}

TEST_F(MetricRecorderTest, ObjectSizeMetricsWithAllocations) {
  // Populate previous event.
  StartGC();
  EndGC(1000);
  // Populate current event.
  StartGC();
  stats->NotifyAllocation(300);
  stats->NotifyFreedMemory(700);
  stats->NotifyMarkingCompleted(800);
  stats->NotifyAllocation(150);
  stats->NotifyFreedMemory(400);
  stats->NotifySweepingCompleted();
  EXPECT_EQ(1300u, MetricRecorderImpl::CppGCCycleEndMetricSamples_event
                       .objects_before_bytes);
  EXPECT_EQ(
      800,
      MetricRecorderImpl::CppGCCycleEndMetricSamples_event.objects_after_bytes);
  EXPECT_EQ(
      500u,
      MetricRecorderImpl::CppGCCycleEndMetricSamples_event.objects_freed_bytes);
  EXPECT_EQ(
      400u,
      MetricRecorderImpl::CppGCCycleEndMetricSamples_event.memory_freed_bytes);
}

}  // namespace internal
}  // namespace cppgc
