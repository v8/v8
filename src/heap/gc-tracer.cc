// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/gc-tracer.h"

#include <cstdarg>
#include <limits>
#include <optional>

#include "include/v8-metrics.h"
#include "src/base/atomic-utils.h"
#include "src/base/logging.h"
#include "src/base/platform/time.h"
#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/execution/thread-id.h"
#include "src/heap/base/unsafe-json-emitter.h"
#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/cppgc/metric-recorder.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/memory-balancer.h"
#include "src/heap/spaces.h"
#include "src/logging/counters.h"
#include "src/logging/metrics.h"
#include "src/logging/tracing-flags.h"
#include "src/tracing/tracing-category-observer.h"

namespace v8 {
namespace internal {

static size_t CountTotalHolesSize(Heap* heap) {
  size_t holes_size = 0;
  PagedSpaceIterator spaces(heap);
  for (PagedSpace* space = spaces.Next(); space != nullptr;
       space = spaces.Next()) {
    DCHECK_GE(holes_size + space->Waste() + space->Available(), holes_size);
    holes_size += space->Waste() + space->Available();
  }
  return holes_size;
}

namespace {

std::atomic<CollectionEpoch> global_epoch{0};

CollectionEpoch next_epoch() {
  return global_epoch.fetch_add(1, std::memory_order_relaxed) + 1;
}

using BytesAndDuration = ::heap::base::BytesAndDuration;

std::optional<double> BoundedAverageSpeed(
    const base::RingBuffer<BytesAndDuration>& buffer) {
  constexpr size_t kMinNonEmptySpeedInBytesPerMs = 1;
  constexpr size_t kMaxSpeedInBytesPerMs = GB;
  return ::heap::base::AverageSpeed(buffer, BytesAndDuration(), std::nullopt,
                                    kMinNonEmptySpeedInBytesPerMs,
                                    kMaxSpeedInBytesPerMs);
}

double BoundedThroughput(const ::heap::base::SmoothedBytesAndDuration& buffer) {
  constexpr double kMaxSpeedInBytesPerMs = static_cast<double>(GB);
  return std::min(buffer.GetThroughput(), kMaxSpeedInBytesPerMs);
}

}  // namespace

GCTracer::Event::Event(Type type, State state,
                       GarbageCollectionReason gc_reason,
                       const char* collector_reason,
                       GCTracer::Priority priority)
    : type(type),
      state(state),
      gc_reason(gc_reason),
      collector_reason(collector_reason),
      priority(priority) {}

const char* ToString(GCTracer::Event::Type type, bool short_name) {
  switch (type) {
    case GCTracer::Event::Type::SCAVENGER:
      return (short_name) ? "s" : "Scavenge";
    case GCTracer::Event::Type::MARK_COMPACTOR:
    case GCTracer::Event::Type::INCREMENTAL_MARK_COMPACTOR:
      return (short_name) ? "mc" : "Mark-Compact";
    case GCTracer::Event::Type::MINOR_MARK_SWEEPER:
    case GCTracer::Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER:
      return (short_name) ? "mms" : "Minor Mark-Sweep";
    case GCTracer::Event::Type::START:
      return (short_name) ? "st" : "Start";
  }
}

GCTracer::RecordGCPhasesInfo::RecordGCPhasesInfo(
    Heap* heap, GarbageCollector collector, GarbageCollectionReason reason) {
  if (Heap::IsYoungGenerationCollector(collector)) {
    type_timer_ = nullptr;
    type_priority_timer_ = nullptr;
    if (!v8_flags.minor_ms) {
      mode_ = Mode::Scavenger;
      trace_event_name_ = "V8.GCScavenger";
    } else {
      mode_ = Mode::None;
      trace_event_name_ = "V8.GCMinorMS";
    }
  } else {
    DCHECK_EQ(GarbageCollector::MARK_COMPACTOR, collector);
    Counters* counters = heap->isolate()->counters();
    const bool in_background = heap->isolate()->is_backgrounded();
    const bool is_incremental = !heap->incremental_marking()->IsStopped();
    mode_ = Mode::None;
    // The following block selects histogram counters to emit. The trace event
    // name should be changed when metrics are updated.
    //
    // Memory reducing GCs take priority over memory measurement GCs. They can
    // happen at the same time when measuring memory is folded into a memory
    // reducing GC.
    if (is_incremental) {
      if (heap->ShouldReduceMemory()) {
        type_timer_ = counters->gc_finalize_incremental_memory_reducing();
        type_priority_timer_ =
            in_background
                ? counters->gc_finalize_incremental_memory_reducing_background()
                : counters
                      ->gc_finalize_incremental_memory_reducing_foreground();
        trace_event_name_ = "V8.GCFinalizeMCReduceMemory";
      } else if (reason == GarbageCollectionReason::kMeasureMemory) {
        type_timer_ = counters->gc_finalize_incremental_memory_measure();
        type_priority_timer_ =
            in_background
                ? counters->gc_finalize_incremental_memory_measure_background()
                : counters->gc_finalize_incremental_memory_measure_foreground();
        trace_event_name_ = "V8.GCFinalizeMCMeasureMemory";
      } else {
        type_timer_ = counters->gc_finalize_incremental_regular();
        type_priority_timer_ =
            in_background
                ? counters->gc_finalize_incremental_regular_background()
                : counters->gc_finalize_incremental_regular_foreground();
        trace_event_name_ = "V8.GCFinalizeMC";
        mode_ = Mode::Finalize;
      }
    } else {
      trace_event_name_ = "V8.GCCompactor";
      if (heap->ShouldReduceMemory()) {
        type_timer_ = counters->gc_finalize_non_incremental_memory_reducing();
        type_priority_timer_ =
            in_background
                ? counters
                      ->gc_finalize_non_incremental_memory_reducing_background()
                : counters
                      ->gc_finalize_non_incremental_memory_reducing_foreground();
      } else if (reason == GarbageCollectionReason::kMeasureMemory) {
        type_timer_ = counters->gc_finalize_non_incremental_memory_measure();
        type_priority_timer_ =
            in_background
                ? counters
                      ->gc_finalize_non_incremental_memory_measure_background()
                : counters
                      ->gc_finalize_non_incremental_memory_measure_foreground();
      } else {
        type_timer_ = counters->gc_finalize_non_incremental_regular();
        type_priority_timer_ =
            in_background
                ? counters->gc_finalize_non_incremental_regular_background()
                : counters->gc_finalize_non_incremental_regular_foreground();
      }
    }
  }
}

GCTracer::GCTracer(Heap* heap, base::TimeTicks startup_time,
                   GarbageCollectionReason initial_gc_reason)
    : heap_(heap),
      current_(Event::Type::START, Event::State::NOT_RUNNING, initial_gc_reason,
               nullptr, heap_->isolate()->priority()),
      previous_(current_),
      allocation_time_(startup_time),
      previous_mark_compact_end_time_(startup_time)
#if defined(V8_USE_PERFETTO)
      ,
      parent_track_(perfetto::ThreadTrack::Current())
#endif
{
  // All accesses to incremental_marking_scope assume that incremental marking
  // scopes come first.
  static_assert(0 == Scope::FIRST_INCREMENTAL_SCOPE);
  // We assume that MC_INCREMENTAL is the first scope so that we can properly
  // map it to RuntimeCallStats.
  static_assert(0 == Scope::MC_INCREMENTAL);
  // Starting a new cycle will make the current event the previous event.
  // Setting the current end time here allows us to refer back to a previous
  // event's end time to compute time spent in mutator.
  current_.end_time = previous_mark_compact_end_time_;
}

void GCTracer::ResetForTesting() {
  auto* heap = heap_;
  this->~GCTracer();
  new (this)
      GCTracer(heap, base::TimeTicks::Now(), GarbageCollectionReason::kTesting);
}

void GCTracer::StartObservablePause(base::TimeTicks time) {
  DCHECK(!IsInObservablePause());
  start_of_observable_pause_.emplace(time);
}

void GCTracer::UpdateCurrentEvent(GarbageCollectionReason gc_reason,
                                  const char* collector_reason) {
  // For incremental marking, the event has already been created and we just
  // need to update a few fields.
  DCHECK(current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR ||
         current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER);
  DCHECK_EQ(Event::State::ATOMIC, current_.state);
  DCHECK(IsInObservablePause());
  current_.gc_reason = gc_reason;
  current_.collector_reason = collector_reason;
  // TODO(chromium:1154636): The start_time of the current event contains
  // currently the start time of the observable pause. This should be
  // reconsidered.
  current_.start_time = start_of_observable_pause_.value();
  current_.reduce_memory = heap_->ShouldReduceMemory();
}

void GCTracer::StartCycle(GarbageCollector collector,
                          GarbageCollectionReason gc_reason,
                          const char* collector_reason, MarkingType marking) {
  // We cannot start a new cycle while there's another one in its atomic pause.
  DCHECK_NE(Event::State::ATOMIC, current_.state);
  // We cannot start a new cycle while a young generation GC cycle has
  // already interrupted a full GC cycle.
  DCHECK(!young_gc_during_full_gc_sweeping_);

  young_gc_during_full_gc_sweeping_ =
      current_.state != Event::State::NOT_RUNNING;
  CHECK_IMPLIES(young_gc_during_full_gc_sweeping_,
                current_.state == Event::State::SWEEPING);
  if (young_gc_during_full_gc_sweeping_) {
    // The cases for interruption are: Scavenger, MinorMS interrupting sweeping.
    // In both cases we are fine with fetching background counters now and
    // fixing them up later in StopAtomicPause().
    FetchBackgroundCounters();
  }

  DCHECK_IMPLIES(young_gc_during_full_gc_sweeping_,
                 Heap::IsYoungGenerationCollector(collector));
  DCHECK_IMPLIES(young_gc_during_full_gc_sweeping_,
                 !Event::IsYoungGenerationEvent(current_.type));

  Event::Type type;
  switch (collector) {
    case GarbageCollector::SCAVENGER:
      type = Event::Type::SCAVENGER;
      break;
    case GarbageCollector::MINOR_MARK_SWEEPER:
      type = marking == MarkingType::kIncremental
                 ? Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER
                 : Event::Type::MINOR_MARK_SWEEPER;
      break;
    case GarbageCollector::MARK_COMPACTOR:
      type = marking == MarkingType::kIncremental
                 ? Event::Type::INCREMENTAL_MARK_COMPACTOR
                 : Event::Type::MARK_COMPACTOR;
      break;
  }

  DCHECK_IMPLIES(!young_gc_during_full_gc_sweeping_,
                 current_.state == Event::State::NOT_RUNNING);
  DCHECK_EQ(Event::State::NOT_RUNNING, previous_.state);

  previous_ = current_;
  current_ = Event(type, Event::State::MARKING, gc_reason, collector_reason,
                   heap_->isolate()->priority());

  switch (marking) {
    case MarkingType::kAtomic:
      DCHECK(IsInObservablePause());
      // TODO(chromium:1154636): The start_time of the current event contains
      // currently the start time of the observable pause. This should be
      // reconsidered.
      current_.start_time = start_of_observable_pause_.value();
      current_.reduce_memory = heap_->ShouldReduceMemory();
      break;
    case MarkingType::kIncremental:
      // The current event will be updated later.
      DCHECK_IMPLIES(Heap::IsYoungGenerationCollector(collector),
                     (v8_flags.minor_ms &&
                      collector == GarbageCollector::MINOR_MARK_SWEEPER));
      DCHECK(!IsInObservablePause());
      break;
  }
  current_.is_loading = heap_->IsLoading();

  if (collector == GarbageCollector::MARK_COMPACTOR) {
    current_.old_generation_consumed_baseline =
        heap_->OldGenerationConsumedBytesAtLastGC();
    current_.old_generation_consumed_current =
        heap_->OldGenerationConsumedBytes();
    current_.old_generation_consumed_limit =
        heap_->old_generation_allocation_limit();
    current_.global_consumed_baseline = heap_->GlobalConsumedBytesAtLastGC();
    current_.global_consumed_current = heap_->GlobalConsumedBytes();
    current_.global_consumed_limit = heap_->global_allocation_limit();
  }

  if (Heap::IsYoungGenerationCollector(collector)) {
    epoch_young_ = next_epoch();
  } else {
    epoch_full_ = next_epoch();
  }
}

void GCTracer::StartAtomicPause() {
  DCHECK_EQ(Event::State::MARKING, current_.state);
  current_.state = Event::State::ATOMIC;
}

void GCTracer::StartInSafepoint(base::TimeTicks time) {
  SampleAllocation(current_.start_time, heap_->NewSpaceAllocationCounter(),
                   heap_->OldGenerationAllocationCounter(),
                   heap_->EmbedderAllocationCounter());
  current_.start_object_size = heap_->SizeOfObjects();
  current_.start_memory_size = heap_->memory_allocator()->Size();
  current_.start_holes_size = CountTotalHolesSize(heap_);
  size_t new_space_size = (heap_->new_space() ? heap_->new_space()->Size() : 0);
  size_t new_lo_space_size =
      (heap_->new_lo_space() ? heap_->new_lo_space()->SizeOfObjects() : 0);
  current_.young_object_size = new_space_size + new_lo_space_size;
  current_.start_atomic_pause_time = time;
}

void GCTracer::StopInSafepoint(base::TimeTicks time) {
  current_.end_object_size = heap_->SizeOfObjects();
  current_.end_memory_size = heap_->memory_allocator()->Size();
  current_.end_holes_size = CountTotalHolesSize(heap_);
  current_.survived_young_object_size = heap_->SurvivedYoungObjectSize();
  current_.end_atomic_pause_time = time;

  // Do not include the GC pause for calculating the allocation rate. GC pause
  // with heap verification can decrease the allocation rate significantly.
  allocation_time_ = time;

  if (v8_flags.memory_balancer) {
    UpdateMemoryBalancerGCSpeed();
  }
}

void GCTracer::StopObservablePause(GarbageCollector collector,
                                   base::TimeTicks time) {
  DCHECK(IsConsistentWithCollector(collector));
  DCHECK(IsInObservablePause());
  start_of_observable_pause_.reset();

  // TODO(chromium:1154636): The end_time of the current event contains
  // currently the end time of the observable pause. This should be
  // reconsidered.
  current_.end_time = time;

  FetchBackgroundCounters();

  const base::TimeDelta duration = current_.end_time - current_.start_time;
  auto* long_task_stats = heap_->isolate()->GetCurrentLongTaskStats();
  const bool is_young = Heap::IsYoungGenerationCollector(collector);
  if (is_young) {
    recorded_minor_gc_atomic_pause_.Push(
        BytesAndDuration(current_.survived_young_object_size, duration));
    long_task_stats->gc_young_wall_clock_duration_us +=
        duration.InMicroseconds();
  } else {
    if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
      RecordIncrementalMarkingSpeed(current_.incremental_marking_bytes,
                                    current_.incremental_marking_duration);
      recorded_incremental_mark_compacts_.Push(
          BytesAndDuration(current_.end_object_size, duration));
      for (int i = 0; i < Scope::NUMBER_OF_INCREMENTAL_SCOPES; i++) {
        current_.incremental_scopes[i] = incremental_scopes_[i];
        current_.scopes[i] = incremental_scopes_[i].duration;
      }
    } else {
      recorded_mark_compacts_.Push(
          BytesAndDuration(current_.end_object_size, duration));
      DCHECK_EQ(0u, current_.incremental_marking_bytes);
      DCHECK(current_.incremental_marking_duration.IsZero());
    }
    RecordGCSumCounters();
    combined_mark_compact_speed_cache_ = std::nullopt;
    long_task_stats->gc_full_atomic_wall_clock_duration_us +=
        duration.InMicroseconds();
    RecordMutatorUtilization(current_.end_time,
                             duration + current_.incremental_marking_duration);
  }

  heap_->UpdateTotalGCTime(duration);

  if (v8_flags.trace_gc_ignore_scavenger && is_young) return;

  if (v8_flags.trace_gc_nvp) {
    PrintNVP();
  } else {
    Print();
  }

  // Reset here because Print() still uses these scopes.
  if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
    for (int i = 0; i < Scope::NUMBER_OF_INCREMENTAL_SCOPES; i++) {
      new (&incremental_scopes_[i]) IncrementalInfos;
    }
  }

  if (v8_flags.trace_gc) {
    heap_->PrintShortHeapStatistics();
  }

  if (V8_UNLIKELY(TracingFlags::gc.load(std::memory_order_relaxed) &
                  v8::tracing::TracingCategoryObserver::ENABLED_BY_TRACING)) {
    TRACE_GC_NOTE("V8.GC_HEAP_DUMP_STATISTICS");
    std::stringstream heap_stats;
    heap_->DumpJSONHeapStatistics(heap_stats);

    TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("v8.gc"), "V8.GC_Heap_Stats",
                         TRACE_EVENT_SCOPE_THREAD, "stats",
                         TRACE_STR_COPY(heap_stats.str().c_str()));
  }
}

void GCTracer::UpdateMemoryBalancerGCSpeed() {
  DCHECK(v8_flags.memory_balancer);
  size_t major_gc_bytes = current_.start_object_size;
  const base::TimeDelta atomic_pause_duration =
      current_.end_atomic_pause_time - current_.start_atomic_pause_time;
  const base::TimeDelta blocked_time_taken =
      atomic_pause_duration + current_.incremental_marking_duration;
  base::TimeDelta concurrent_gc_time;
  {
    base::MutexGuard guard(&background_scopes_mutex_);
    concurrent_gc_time =
        background_scopes_[Scope::MC_BACKGROUND_EVACUATE_COPY] +
        background_scopes_[Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS] +
        background_scopes_[Scope::MC_BACKGROUND_MARKING] +
        background_scopes_[Scope::MC_BACKGROUND_SWEEPING];
  }
  const base::TimeDelta major_gc_duration =
      blocked_time_taken + concurrent_gc_time;
  const base::TimeDelta major_allocation_duration =
      (current_.end_atomic_pause_time - previous_mark_compact_end_time_) -
      blocked_time_taken;
  CHECK_GE(major_allocation_duration, base::TimeDelta());

  heap_->mb_->UpdateGCSpeed(major_gc_bytes, major_gc_duration);
}

void GCTracer::StopAtomicPause() {
  DCHECK_EQ(Event::State::ATOMIC, current_.state);
  current_.state = Event::State::SWEEPING;
}

namespace {

// Estimate of young generation wall time across all threads up to and including
// the atomic pause.
constexpr v8::base::TimeDelta YoungGenerationWallTime(
    const GCTracer::Event& event) {
  return
      // Scavenger events.
      event.scopes[GCTracer::Scope::SCAVENGER] +
      event.scopes[GCTracer::Scope::SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL] +
      // Minor MS events.
      event.scopes[GCTracer::Scope::MINOR_MS] +
      event.scopes[GCTracer::Scope::MINOR_MS_BACKGROUND_MARKING];
}

}  // namespace

void GCTracer::StopCycle(GarbageCollector collector) {
  DCHECK_EQ(Event::State::SWEEPING, current_.state);
  current_.state = Event::State::NOT_RUNNING;

  DCHECK(IsConsistentWithCollector(collector));

  FetchBackgroundCounters();

  if (Heap::IsYoungGenerationCollector(collector)) {
    ReportYoungCycleToRecorder();

    const v8::base::TimeDelta per_thread_wall_time =
        YoungGenerationWallTime(current_) / current_.concurrency_estimate;
    recorded_minor_gc_per_thread_.Push(BytesAndDuration(
        current_.survived_young_object_size, per_thread_wall_time));

    // If a young generation GC interrupted an unfinished full GC cycle, restore
    // the event corresponding to the full GC cycle.
    if (young_gc_during_full_gc_sweeping_) {
      // Sweeping for full GC could have occured during the young GC. Copy over
      // any sweeping scope values to the previous_ event. The full GC sweeping
      // scopes are never reported by young cycles.
      previous_.scopes[Scope::MC_SWEEP] += current_.scopes[Scope::MC_SWEEP];
      previous_.scopes[Scope::MC_BACKGROUND_SWEEPING] +=
          current_.scopes[Scope::MC_BACKGROUND_SWEEPING];
      std::swap(current_, previous_);
      young_gc_during_full_gc_sweeping_ = false;
    }
  } else {
    ReportFullCycleToRecorder();

    heap_->isolate()->counters()->mark_compact_reason()->AddSample(
        static_cast<int>(current_.gc_reason));

    if (v8_flags.trace_gc_freelists) {
      PrintIsolate(heap_->isolate(),
                   "FreeLists statistics before collection:\n");
      heap_->PrintFreeListsStats();
    }
  }
}

void GCTracer::StopFullCycleIfFinished() {
  if (current_.state != Event::State::SWEEPING) return;
  if (!notified_full_sweeping_completed_) return;
  if (heap_->cpp_heap() && !notified_full_cppgc_completed_) return;
  StopCycle(GarbageCollector::MARK_COMPACTOR);
  notified_full_sweeping_completed_ = false;
  notified_full_cppgc_completed_ = false;
  full_cppgc_completed_during_minor_gc_ = false;
}

void GCTracer::StopYoungCycleIfFinished() {
  DCHECK(Event::IsYoungGenerationEvent(current_.type));
  if (current_.state != Event::State::SWEEPING) return;
  if (!notified_young_sweeping_completed_) return;
  // Check if young cppgc was scheduled but hasn't completed yet.
  if (heap_->cpp_heap() && notified_young_cppgc_running_ &&
      !notified_young_cppgc_completed_)
    return;
  bool was_young_gc_during_full_gc_sweeping_ =
      young_gc_during_full_gc_sweeping_;
  StopCycle(current_.type == Event::Type::SCAVENGER
                ? GarbageCollector::SCAVENGER
                : GarbageCollector::MINOR_MARK_SWEEPER);
  notified_young_sweeping_completed_ = false;
  notified_young_cppgc_running_ = false;
  notified_young_cppgc_completed_ = false;
  if (was_young_gc_during_full_gc_sweeping_) {
    // Check if the full gc cycle is ready to be stopped.
    StopFullCycleIfFinished();
  }
}

void GCTracer::NotifyFullSweepingCompletedAndStopCycleIfFinished() {
  // Notifying twice that V8 sweeping is finished for the same cycle is possible
  // only if Oilpan sweeping is still in progress.
  DCHECK_IMPLIES(
      notified_full_sweeping_completed_,
      !notified_full_cppgc_completed_ || full_cppgc_completed_during_minor_gc_);

  if (Event::IsYoungGenerationEvent(current_.type)) {
    bool was_young_gc_during_full_gc_sweeping =
        young_gc_during_full_gc_sweeping_;
    bool was_full_sweeping_notified = notified_full_sweeping_completed_;
    NotifyYoungSweepingCompletedAndStopCycleIfFinished();
    // NotifyYoungSweepingCompletedAndStopCycleIfFinished checks if the full
    // cycle needs to be stopped as well. If full sweeping was already notified,
    // nothing more needs to be done here.
    if (!was_young_gc_during_full_gc_sweeping || was_full_sweeping_notified)
      return;
  }

  DCHECK(!Event::IsYoungGenerationEvent(current_.type));
  // Sweeping finalization can also be triggered from inside a full GC cycle's
  // atomic pause.
  DCHECK(current_.state == Event::State::SWEEPING ||
         current_.state == Event::State::ATOMIC);

  // Stop a full GC cycle only when both v8 and cppgc (if available) GCs have
  // finished sweeping. This method is invoked by v8.
  if (v8_flags.trace_gc_freelists) {
    PrintIsolate(heap_->isolate(),
                 "FreeLists statistics after sweeping completed:\n");
    heap_->PrintFreeListsStats();
  }
  notified_full_sweeping_completed_ = true;
  StopFullCycleIfFinished();
}

void GCTracer::NotifyYoungSweepingCompleted() {
  DCHECK(Event::IsYoungGenerationEvent(current_.type));
  if (v8_flags.verify_heap || v8_flags.code_stats) {
    // If heap verification or code stats are enabled, sweeping finalization can
    // also be triggered from inside a full GC cycle's atomic pause.
    DCHECK(current_.type == Event::Type::MINOR_MARK_SWEEPER ||
           current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER ||
           current_.type == Event::Type::SCAVENGER);
    DCHECK(current_.state == Event::State::SWEEPING ||
           current_.state == Event::State::ATOMIC);
  } else {
    DCHECK(IsSweepingInProgress() || (current_.type == Event::Type::SCAVENGER));
  }

  DCHECK(!notified_young_sweeping_completed_);
  notified_young_sweeping_completed_ = true;
}

void GCTracer::NotifyYoungSweepingCompletedAndStopCycleIfFinished() {
  if (!Event::IsYoungGenerationEvent(current_.type)) return;
  NotifyYoungSweepingCompleted();
  StopYoungCycleIfFinished();
}

void GCTracer::NotifyFullCppGCCompleted() {
  // Stop a full GC cycle only when both v8 and cppgc (if available) GCs have
  // finished sweeping. This method is invoked by cppgc.
  DCHECK(heap_->cpp_heap());
  const auto* metric_recorder =
      CppHeap::From(heap_->cpp_heap())->GetMetricRecorder();
  USE(metric_recorder);
  DCHECK(metric_recorder->FullGCMetricsReportPending());
  DCHECK(!notified_full_cppgc_completed_);
  notified_full_cppgc_completed_ = true;
  // Cppgc sweeping may finalize during MinorMS sweeping. In that case, delay
  // stopping the cycle until the nested MinorMS cycle is stopped.
  if (Event::IsYoungGenerationEvent(current_.type)) {
    DCHECK(young_gc_during_full_gc_sweeping_);
    full_cppgc_completed_during_minor_gc_ = true;
    return;
  }
  StopFullCycleIfFinished();
}

void GCTracer::NotifyYoungCppGCCompleted() {
  // Stop a young GC cycle only when both v8 and cppgc (if available) GCs have
  // finished sweeping. This method is invoked by cppgc.
  DCHECK(heap_->cpp_heap());
  DCHECK(notified_young_cppgc_running_);
  const auto* metric_recorder =
      CppHeap::From(heap_->cpp_heap())->GetMetricRecorder();
  USE(metric_recorder);
  DCHECK(metric_recorder->YoungGCMetricsReportPending());
  DCHECK(!notified_young_cppgc_completed_);
  notified_young_cppgc_completed_ = true;
  StopYoungCycleIfFinished();
}

void GCTracer::NotifyYoungCppGCRunning() {
  DCHECK(!notified_young_cppgc_running_);
  notified_young_cppgc_running_ = true;
}

void GCTracer::SampleAllocation(base::TimeTicks current,
                                size_t new_space_counter_bytes,
                                size_t old_generation_counter_bytes,
                                size_t embedder_counter_bytes) {
  int64_t new_space_allocated_bytes = std::max<int64_t>(
      new_space_counter_bytes - new_space_allocation_counter_bytes_, 0);
  int64_t old_generation_allocated_bytes = std::max<int64_t>(
      old_generation_counter_bytes - old_generation_allocation_counter_bytes_,
      0);
  int64_t embedder_allocated_bytes = std::max<int64_t>(
      embedder_counter_bytes - embedder_allocation_counter_bytes_, 0);
  const base::TimeDelta allocation_duration = current - allocation_time_;
  allocation_time_ = current;

  new_space_allocation_counter_bytes_ = new_space_counter_bytes;
  old_generation_allocation_counter_bytes_ = old_generation_counter_bytes;
  embedder_allocation_counter_bytes_ = embedder_counter_bytes;

  new_generation_allocations_.Update(
      BytesAndDuration(new_space_allocated_bytes, allocation_duration));
  old_generation_allocations_.Update(
      BytesAndDuration(old_generation_allocated_bytes, allocation_duration));
  embedder_generation_allocations_.Update(
      BytesAndDuration(embedder_allocated_bytes, allocation_duration));

  if (v8_flags.memory_balancer) {
    heap_->mb_->UpdateAllocationRate(old_generation_allocated_bytes,
                                     allocation_duration);
  }

#if defined(V8_USE_PERFETTO)
  TRACE_COUNTER(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                perfetto::CounterTrack("OldGenerationAllocationThroughput",
                                       parent_track_),
                OldGenerationAllocationThroughputInBytesPerMillisecond());
  TRACE_COUNTER(
      TRACE_DISABLED_BY_DEFAULT("v8.gc"),
      perfetto::CounterTrack("EmbedderAllocationThroughput", parent_track_),
      EmbedderAllocationThroughputInBytesPerMillisecond());
  TRACE_COUNTER(
      TRACE_DISABLED_BY_DEFAULT("v8.gc"),
      perfetto::CounterTrack("NewSpaceAllocationThroughput", parent_track_),
      NewSpaceAllocationThroughputInBytesPerMillisecond());
#endif
}

void GCTracer::SampleConcurrencyEsimate(size_t concurrency) {
  // For now, we only expect a single sample.
  DCHECK_EQ(current_.concurrency_estimate, 1);
  DCHECK_GT(concurrency, 0);
  current_.concurrency_estimate = concurrency;
}

void GCTracer::NotifyMarkingStart() {
  const auto marking_start = base::TimeTicks::Now();

  // Handle code flushing time deltas. Times are incremented conservatively:
  // 1. The first delta is 0s.
  // 2. Any delta is rounded downwards to a full second.
  // 3. 0s-deltas are carried over to the next GC with their precise diff. This
  //    allows for frequent GCs (within a single second) to be attributed
  //    correctly later on.
  // 4. The first non-zero increment after a reset always just increments by 1s.
  using SFIAgeType = decltype(code_flushing_increase_s_);
  static_assert(SharedFunctionInfo::kAgeSize == sizeof(SFIAgeType));
  static constexpr auto kMaxDeltaForSFIAge =
      base::TimeDelta::FromSeconds(std::numeric_limits<SFIAgeType>::max());
  SFIAgeType code_flushing_increase_s = 0;
  if (last_marking_start_time_for_code_flushing_.has_value()) {
    const auto diff =
        marking_start - last_marking_start_time_for_code_flushing_.value();
    if (diff > kMaxDeltaForSFIAge) {
      code_flushing_increase_s = std::numeric_limits<SFIAgeType>::max();
    } else {
      code_flushing_increase_s = static_cast<SFIAgeType>(diff.InSeconds());
    }
  }
  DCHECK_LE(code_flushing_increase_s, std::numeric_limits<SFIAgeType>::max());
  code_flushing_increase_s_ = code_flushing_increase_s;
  if (!last_marking_start_time_for_code_flushing_.has_value() ||
      code_flushing_increase_s > 0) {
    last_marking_start_time_for_code_flushing_ = marking_start;
  }
  if (V8_UNLIKELY(v8_flags.trace_flush_code)) {
    PrintIsolate(heap_->isolate(), "code flushing: increasing time: %u s\n",
                 code_flushing_increase_s_);
  }
}

uint16_t GCTracer::CodeFlushingIncrease() const {
  return code_flushing_increase_s_;
}

void GCTracer::AddCompactionEvent(double duration,
                                  size_t live_bytes_compacted) {
  recorded_compactions_.Push(BytesAndDuration(
      live_bytes_compacted, base::TimeDelta::FromMillisecondsD(duration)));
}

void GCTracer::AddSurvivalRatio(double promotion_ratio) {
  recorded_survival_ratios_.Push(promotion_ratio);
}

void GCTracer::AddIncrementalMarkingStep(double duration, size_t bytes) {
  if (bytes > 0) {
    current_.incremental_marking_bytes += bytes;
    current_.incremental_marking_duration +=
        base::TimeDelta::FromMillisecondsD(duration);
  }
  ReportIncrementalMarkingStepToRecorder(duration);
}

void GCTracer::AddIncrementalSweepingStep(double duration) {
  ReportIncrementalSweepingStepToRecorder(duration);
}

void GCTracer::Output(const char* format, ...) const {
  if (v8_flags.trace_gc) {
    va_list arguments;
    va_start(arguments, format);
    base::OS::VPrint(format, arguments);
    va_end(arguments);
  }

  const int kBufferSize = 256;
  char raw_buffer[kBufferSize];
  base::Vector<char> buffer(raw_buffer, kBufferSize);
  va_list arguments2;
  va_start(arguments2, format);
  base::VSNPrintF(buffer, format, arguments2);
  va_end(arguments2);

  heap_->AddToRingBuffer(buffer.begin());
}

void GCTracer::Print() const {
  const base::TimeDelta duration = current_.end_time - current_.start_time;
  const size_t kIncrementalStatsSize = 128;
  char incremental_buffer[kIncrementalStatsSize] = {0};

  if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
    base::OS::SNPrintF(
        incremental_buffer, kIncrementalStatsSize,
        " (+ %.1f ms in %d steps since start of marking, "
        "biggest step %.1f ms, walltime since start of marking %.f ms)",
        current_scope(Scope::MC_INCREMENTAL),
        incremental_scope(Scope::MC_INCREMENTAL).steps,
        incremental_scope(Scope::MC_INCREMENTAL).longest_step.InMillisecondsF(),
        (current_.end_time - current_.incremental_marking_start_time)
            .InMillisecondsF());
  }

  const double total_external_time =
      current_scope(Scope::HEAP_EXTERNAL_WEAK_GLOBAL_HANDLES) +
      current_scope(Scope::HEAP_EXTERNAL_EPILOGUE) +
      current_scope(Scope::HEAP_EXTERNAL_PROLOGUE) +
      current_scope(Scope::MC_INCREMENTAL_EXTERNAL_EPILOGUE) +
      current_scope(Scope::MC_INCREMENTAL_EXTERNAL_PROLOGUE);

  // Avoid PrintF as Output also appends the string to the tracing ring buffer
  // that gets printed on OOM failures.
  DCHECK_IMPLIES(young_gc_during_full_gc_sweeping_,
                 Event::IsYoungGenerationEvent(current_.type));
  Output(
      "[%d:%p] "
      "%8.0f ms: "
      "%s%s%s %.1f (%.1f) -> %.1f (%.1f) MB, "
      "pooled: %.1f MB, "
      "%.2f / %.2f ms%s (average mu = %.3f, current mu = %.3f) %s; %s\n",
      base::OS::GetCurrentProcessId(),
      reinterpret_cast<void*>(heap_->isolate()),
      heap_->isolate()->time_millis_since_init(),
      ToString(current_.type, false), current_.reduce_memory ? " (reduce)" : "",
      young_gc_during_full_gc_sweeping_ ? " (during sweeping)" : "",
      static_cast<double>(current_.start_object_size) / MB,
      static_cast<double>(current_.start_memory_size) / MB,
      static_cast<double>(current_.end_object_size) / MB,
      static_cast<double>(current_.end_memory_size) / MB,
      static_cast<double>(heap_->memory_allocator()->GetPooledChunksCount() *
                          PageMetadata::kPageSize) /
          MB,
      duration.InMillisecondsF(), total_external_time, incremental_buffer,
      AverageMarkCompactMutatorUtilization(),
      CurrentMarkCompactMutatorUtilization(), ToString(current_.gc_reason),
      current_.collector_reason != nullptr ? current_.collector_reason : "");
}

void GCTracer::PrintNVP() const {
  const base::TimeDelta duration = current_.end_time - current_.start_time;
  const base::TimeDelta spent_in_mutator =
      current_.start_time - previous_.end_time;
  size_t allocated_since_last_gc =
      current_.start_object_size - previous_.end_object_size;

  base::TimeDelta incremental_walltime_duration;
  if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
    incremental_walltime_duration =
        current_.end_time - current_.incremental_marking_start_time;
  }

  // Avoid data races when printing the background scopes.
  base::MutexGuard guard(&background_scopes_mutex_);

  ::heap::base::UnsafeJsonEmitter json;

  json.object_start()
      .p("pause", duration.InMillisecondsF())
      .p("mutator", spent_in_mutator.InMillisecondsF())
      .p("gc", ToString(current_.type, true))
      .p("reduce_memory", current_.reduce_memory)
      .p("time_to_safepoint",
         current_.scopes[Scope::TIME_TO_SAFEPOINT].InMillisecondsF())
      .p("stack", heap_->IsGCWithStack())
      .p("reason", ToString(current_.gc_reason))
      .p("start_object_size", current_.start_object_size)
      .p("end_object_size", current_.end_object_size)
      .p("start_memory_size", current_.start_memory_size)
      .p("end_memory_size", current_.end_memory_size)
      .p("start_holes_size", current_.start_holes_size)
      .p("end_holes_size", current_.end_holes_size)
      .p("pool_local_chunks", heap_->memory_allocator()->GetPooledChunksCount())
      .p("pool_shared_chunks",
         heap_->memory_allocator()->GetSharedPooledChunksCount())
      .p("pool_total_chunks",
         heap_->memory_allocator()->GetTotalPooledChunksCount())
      .p("new_space_capacity",
         heap_->new_space() ? heap_->new_space()->TotalCapacity() : 0)
      .p("old_gen_allocation_limit", heap_->old_generation_allocation_limit())
      .p("global_allocation_limit", heap_->global_allocation_limit())
      .p("allocation_throughput", AllocationThroughputInBytesPerMillisecond())
      .p("new_space_allocation_throughput",
         NewSpaceAllocationThroughputInBytesPerMillisecond())
      .p("new_space_survive_rate", heap_->new_space_surviving_rate_)
      .p("allocated", allocated_since_last_gc)
      .p("promoted", heap_->promoted_objects_size())
      .p("new_space_survived", heap_->new_space_surviving_object_size())
      .p("nodes_died_in_new", heap_->nodes_died_in_new_space_)
      .p("nodes_copied_in_new", heap_->nodes_copied_in_new_space_)
      .p("nodes_promoted", heap_->nodes_promoted_)
      .p("promotion_ratio", heap_->promotion_ratio_)
      .p("average_survival_ratio", AverageSurvivalRatio())
      .p("promotion_rate", heap_->promotion_rate_);

  switch (current_.type) {
    case Event::Type::SCAVENGER:
      json.p("during_sweeping", young_gc_during_full_gc_sweeping_)
          .p("heap.prologue", current_scope(Scope::HEAP_PROLOGUE))
          .p("heap.epilogue", current_scope(Scope::HEAP_EPILOGUE))
          .p("heap.external.prologue",
             current_scope(Scope::HEAP_EXTERNAL_PROLOGUE))
          .p("heap.external.epilogue",
             current_scope(Scope::HEAP_EXTERNAL_EPILOGUE))
          .p("heap.external_weak_global_handles",
             current_scope(Scope::HEAP_EXTERNAL_WEAK_GLOBAL_HANDLES))
          .p("complete.sweep_array_buffers",
             current_scope(Scope::SCAVENGER_COMPLETE_SWEEP_ARRAY_BUFFERS))
          .p("scavenge", current_scope(Scope::SCAVENGER_SCAVENGE))
          .p("scavenge.free_remembered_set",
             current_scope(Scope::SCAVENGER_FREE_REMEMBERED_SET))
          .p("scavenge.roots", current_scope(Scope::SCAVENGER_SCAVENGE_ROOTS))
          .p("scavenge.weak", current_scope(Scope::SCAVENGER_SCAVENGE_WEAK))
          .p("scavenge.weak_global_handles.identify",
             current_scope(
                 Scope::SCAVENGER_SCAVENGE_WEAK_GLOBAL_HANDLES_IDENTIFY))
          .p("scavenge.weak_global_handles.process",
             current_scope(
                 Scope::SCAVENGER_SCAVENGE_WEAK_GLOBAL_HANDLES_PROCESS))
          .p("scavenge.parallel",
             current_scope(Scope::SCAVENGER_SCAVENGE_PARALLEL))
          .p("scavenge.update_refs",
             current_scope(Scope::SCAVENGER_SCAVENGE_UPDATE_REFS))
          .p("scavenge.pin_objects",
             current_scope(Scope::SCAVENGER_SCAVENGE_PIN_OBJECTS))
          .p("scavenge.restore_pinned",
             current_scope(
                 Scope::SCAVENGER_SCAVENGE_RESTORE_AND_QUARANTINE_PINNED))
          .p("scavenge.sweep_array_buffers",
             current_scope(Scope::SCAVENGER_SWEEP_ARRAY_BUFFERS))
          .p("scavenge.resize_new_space",
             current_scope(Scope::SCAVENGER_RESIZE_NEW_SPACE))
          .p("background.scavenge.parallel",
             current_scope(Scope::SCAVENGER_BACKGROUND_SCAVENGE_PARALLEL))
          .p("incremental.steps_count",
             incremental_scope(GCTracer::Scope::MC_INCREMENTAL).steps)
          .p("incremental.steps_took", current_scope(Scope::MC_INCREMENTAL))
          .p("scavenge_throughput",
             YoungGenerationSpeedInBytesPerMillisecond(
                 YoungGenerationSpeedMode::kOnlyAtomicPause)
                 .value_or(0.0))
          .p("quarantined_size",
             heap_->semi_space_new_space()->QuarantinedSize())
          .p("quarantined_pages",
             heap_->semi_space_new_space()->QuarantinedPageCount());
      break;
    case Event::Type::MINOR_MARK_SWEEPER:
    case Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER:
      json.p("minor_ms", current_scope(Scope::MINOR_MS))
          .p("mark", current_scope(Scope::MINOR_MS_MARK))
          .p("mark.incremental_seed",
             current_scope(Scope::MINOR_MS_MARK_INCREMENTAL_SEED))
          .p("mark.finish_incremental",
             current_scope(Scope::MINOR_MS_MARK_FINISH_INCREMENTAL))
          .p("mark.seed", current_scope(Scope::MINOR_MS_MARK_SEED))
          .p("mark.traced_handles",
             current_scope(Scope::MINOR_MS_MARK_TRACED_HANDLES))
          .p("mark.closure_parallel",
             current_scope(Scope::MINOR_MS_MARK_CLOSURE_PARALLEL))
          .p("mark.closure", current_scope(Scope::MINOR_MS_MARK_CLOSURE))
          .p("mark.conservative_stack",
             current_scope(Scope::MINOR_MS_MARK_CONSERVATIVE_STACK))
          .p("clear", current_scope(Scope::MINOR_MS_CLEAR))
          .p("clear.string_forwarding_table",
             current_scope(Scope::MINOR_MS_CLEAR_STRING_FORWARDING_TABLE))
          .p("clear.string_table",
             current_scope(Scope::MINOR_MS_CLEAR_STRING_TABLE))
          .p("clear.global_handles",
             current_scope(Scope::MINOR_MS_CLEAR_WEAK_GLOBAL_HANDLES))
          .p("complete.sweep_array_buffers",
             current_scope(Scope::MINOR_MS_COMPLETE_SWEEP_ARRAY_BUFFERS))
          .p("complete.sweeping",
             current_scope(Scope::MINOR_MS_COMPLETE_SWEEPING))
          .p("sweep", current_scope(Scope::MINOR_MS_SWEEP))
          .p("sweep.new", current_scope(Scope::MINOR_MS_SWEEP_NEW))
          .p("sweep.new_lo", current_scope(Scope::MINOR_MS_SWEEP_NEW_LO))
          .p("sweep.update_string_table",
             current_scope(Scope::MINOR_MS_SWEEP_UPDATE_STRING_TABLE))
          .p("sweep.start_jobs",
             current_scope(Scope::MINOR_MS_SWEEP_START_JOBS))
          .p("sweep.array_buffers",
             current_scope(Scope::YOUNG_ARRAY_BUFFER_SWEEP))
          .p("finish", current_scope(Scope::MINOR_MS_FINISH))
          .p("finish.ensure_capacity",
             current_scope(Scope::MINOR_MS_FINISH_ENSURE_CAPACITY))
          .p("finish.sweep_array_buffers",
             current_scope(Scope::MINOR_MS_FINISH_SWEEP_ARRAY_BUFFERS))
          .p("background.mark",
             current_scope(Scope::MINOR_MS_BACKGROUND_MARKING))
          .p("background.sweep",
             current_scope(Scope::MINOR_MS_BACKGROUND_SWEEPING))
          .p("background.sweep.array_buffers",
             current_scope(Scope::BACKGROUND_YOUNG_ARRAY_BUFFER_SWEEP))
          .p("conservative_stack_scanning",
             current_scope(Scope::CONSERVATIVE_STACK_SCANNING));
      break;
    case Event::Type::MARK_COMPACTOR:
    case Event::Type::INCREMENTAL_MARK_COMPACTOR:
      json.p("heap.prologue", current_scope(Scope::HEAP_PROLOGUE))
          .p("heap.embedder_tracing_epilogue",
             current_scope(Scope::HEAP_EMBEDDER_TRACING_EPILOGUE))
          .p("heap.epilogue", current_scope(Scope::HEAP_EPILOGUE))
          .p("heap.external.prologue",
             current_scope(Scope::HEAP_EXTERNAL_PROLOGUE))
          .p("heap.external.epilogue",
             current_scope(Scope::HEAP_EXTERNAL_EPILOGUE))
          .p("heap.external.weak_global_handles",
             current_scope(Scope::HEAP_EXTERNAL_WEAK_GLOBAL_HANDLES))
          .p("clear", current_scope(Scope::MC_CLEAR))
          .p("clear.external_string_table",
             current_scope(Scope::MC_CLEAR_EXTERNAL_STRING_TABLE))
          .p("clear.string_forwarding_table",
             current_scope(Scope::MC_CLEAR_STRING_FORWARDING_TABLE))
          .p("clear.weak_global_handles",
             current_scope(Scope::MC_CLEAR_WEAK_GLOBAL_HANDLES))
          .p("clear.dependent_code",
             current_scope(Scope::MC_CLEAR_DEPENDENT_CODE))
          .p("clear.maps", current_scope(Scope::MC_CLEAR_MAPS))
          .p("clear.slots_buffer", current_scope(Scope::MC_CLEAR_SLOTS_BUFFER))
          .p("clear.weak_collections",
             current_scope(Scope::MC_CLEAR_WEAK_COLLECTIONS))
          .p("clear.weak_lists", current_scope(Scope::MC_CLEAR_WEAK_LISTS))
          .p("clear.weak_references_trivial",
             current_scope(Scope::MC_CLEAR_WEAK_REFERENCES_TRIVIAL))
          .p("clear.weak_references_non_trivial",
             current_scope(Scope::MC_CLEAR_WEAK_REFERENCES_NON_TRIVIAL))
          .p("clear.weak_references_filter_non_trivial",
             current_scope(Scope::MC_CLEAR_WEAK_REFERENCES_FILTER_NON_TRIVIAL))
          .p("clear.js_weak_references",
             current_scope(Scope::MC_CLEAR_JS_WEAK_REFERENCES))
          .p("clear.join_filter_job",
             current_scope(Scope::MC_CLEAR_WEAK_REFERENCES_JOIN_FILTER_JOB))
          .p("clear.join_job", current_scope(Scope::MC_CLEAR_JOIN_JOB))
          .p("weakness_handling", current_scope(Scope::MC_WEAKNESS_HANDLING))
          .p("complete.sweep_array_buffers",
             current_scope(Scope::MC_COMPLETE_SWEEP_ARRAY_BUFFERS))
          .p("complete.sweeping", current_scope(Scope::MC_COMPLETE_SWEEPING))
          .p("epilogue", current_scope(Scope::MC_EPILOGUE))
          .p("evacuate", current_scope(Scope::MC_EVACUATE))
          .p("evacuate.pin_pages", current_scope(Scope::MC_EVACUATE_PIN_PAGES))
          .p("evacuate.candidates",
             current_scope(Scope::MC_EVACUATE_CANDIDATES))
          .p("evacuate.clean_up", current_scope(Scope::MC_EVACUATE_CLEAN_UP))
          .p("evacuate.copy", current_scope(Scope::MC_EVACUATE_COPY))
          .p("evacuate.prologue", current_scope(Scope::MC_EVACUATE_PROLOGUE))
          .p("evacuate.epilogue", current_scope(Scope::MC_EVACUATE_EPILOGUE))
          .p("evacuate.rebalance", current_scope(Scope::MC_EVACUATE_REBALANCE))
          .p("evacuate.update_pointers",
             current_scope(Scope::MC_EVACUATE_UPDATE_POINTERS))
          .p("evacuate.update_pointers.to_new_roots",
             current_scope(Scope::MC_EVACUATE_UPDATE_POINTERS_TO_NEW_ROOTS))
          .p("evacuate.update_pointers.slots.main",
             current_scope(Scope::MC_EVACUATE_UPDATE_POINTERS_SLOTS_MAIN))
          .p("evacuate.update_pointers.weak",
             current_scope(Scope::MC_EVACUATE_UPDATE_POINTERS_WEAK))
          .p("finish", current_scope(Scope::MC_FINISH))
          .p("finish.sweep_array_buffers",
             current_scope(Scope::MC_FINISH_SWEEP_ARRAY_BUFFERS))
          .p("mark", current_scope(Scope::MC_MARK))
          .p("mark.finish_incremental",
             current_scope(Scope::MC_MARK_FINISH_INCREMENTAL))
          .p("mark.roots", current_scope(Scope::MC_MARK_ROOTS))
          .p("mark.full_closure_parallel",
             current_scope(Scope::MC_MARK_FULL_CLOSURE_PARALLEL))
          .p("mark.full_closure", current_scope(Scope::MC_MARK_FULL_CLOSURE))
          .p("mark.ephemeron.marking",
             current_scope(Scope::MC_MARK_WEAK_CLOSURE_EPHEMERON_MARKING))
          .p("mark.ephemeron.linear",
             current_scope(Scope::MC_MARK_WEAK_CLOSURE_EPHEMERON_LINEAR))
          .p("mark.embedder_prologue",
             current_scope(Scope::MC_MARK_EMBEDDER_PROLOGUE))
          .p("mark.embedder_tracing",
             current_scope(Scope::MC_MARK_EMBEDDER_TRACING))
          .p("prologue", current_scope(Scope::MC_PROLOGUE))
          .p("sweep", current_scope(Scope::MC_SWEEP))
          .p("sweep.code", current_scope(Scope::MC_SWEEP_CODE))
          .p("sweep.map", current_scope(Scope::MC_SWEEP_MAP))
          .p("sweep.new", current_scope(Scope::MC_SWEEP_NEW))
          .p("sweep.new_lo", current_scope(Scope::MC_SWEEP_NEW_LO))
          .p("sweep.old", current_scope(Scope::MC_SWEEP_OLD))
          .p("sweep.start_jobs", current_scope(Scope::MC_SWEEP_START_JOBS))
          .p("incremental", current_scope(Scope::MC_INCREMENTAL))
          .p("incremental.finalize.external.prologue",
             current_scope(Scope::MC_INCREMENTAL_EXTERNAL_PROLOGUE))
          .p("incremental.finalize.external.epilogue",
             current_scope(Scope::MC_INCREMENTAL_EXTERNAL_EPILOGUE))
          .p("incremental.layout_change",
             current_scope(Scope::MC_INCREMENTAL_LAYOUT_CHANGE))
          .p("incremental.sweep_array_buffers",
             current_scope(Scope::MC_INCREMENTAL_START))
          .p("incremental.sweeping",
             current_scope(Scope::MC_INCREMENTAL_SWEEPING))
          .p("incremental.embedder_tracing",
             current_scope(Scope::MC_INCREMENTAL_EMBEDDER_TRACING))
          .p("incremental_wrapper_tracing_longest_step",
             incremental_scope(Scope::MC_INCREMENTAL_EMBEDDER_TRACING)
                 .longest_step.InMillisecondsF())
          .p("incremental_longest_step",
             incremental_scope(Scope::MC_INCREMENTAL)
                 .longest_step.InMillisecondsF())
          .p("incremental_steps_count",
             incremental_scope(Scope::MC_INCREMENTAL).steps)
          .p("incremental_marking_throughput",
             IncrementalMarkingSpeedInBytesPerMillisecond())
          .p("incremental_walltime_duration",
             incremental_walltime_duration.InMillisecondsF())
          .p("background.mark", current_scope(Scope::MC_BACKGROUND_MARKING))
          .p("background.sweep", current_scope(Scope::MC_BACKGROUND_SWEEPING))
          .p("background.evacuate.copy",
             current_scope(Scope::MC_BACKGROUND_EVACUATE_COPY))
          .p("background.evacuate.update_pointers",
             current_scope(Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS))
          .p("conservative_stack_scanning",
             current_scope(Scope::CONSERVATIVE_STACK_SCANNING))
          .p("compaction_speed",
             CompactionSpeedInBytesPerMillisecond().value_or(0.0));
      break;
    case Event::Type::START:
      break;
  }

  std::string json_str = json.object_end().ToString();
  heap_->isolate()->PrintWithTimestamp("GC: %s\n", json_str.c_str());

#if defined(V8_USE_PERFETTO)
  TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("v8.gc"), "V8.GCTraceGCNVP",
                       TRACE_EVENT_SCOPE_THREAD, "value",
                       TRACE_STR_COPY(json_str.c_str()));
#endif
}

void GCTracer::RecordIncrementalMarkingSpeed(size_t bytes,
                                             base::TimeDelta duration) {
  DCHECK(!Event::IsYoungGenerationEvent(current_.type));
  if (duration.IsZero() || bytes == 0) return;
  double current_speed =
      static_cast<double>(bytes) / duration.InMillisecondsF();
  if (recorded_major_incremental_marking_speed_ == 0) {
    recorded_major_incremental_marking_speed_ = current_speed;
  } else {
    recorded_major_incremental_marking_speed_ =
        (recorded_major_incremental_marking_speed_ + current_speed) / 2;
  }
}

void GCTracer::RecordTimeToIncrementalMarkingTask(
    base::TimeDelta time_to_task) {
  if (!average_time_to_incremental_marking_task_.has_value()) {
    average_time_to_incremental_marking_task_.emplace(time_to_task);
  } else {
    average_time_to_incremental_marking_task_ =
        (average_time_to_incremental_marking_task_.value() + time_to_task) / 2;
  }
}

std::optional<base::TimeDelta> GCTracer::AverageTimeToIncrementalMarkingTask()
    const {
  return average_time_to_incremental_marking_task_;
}

void GCTracer::RecordEmbedderMarkingSpeed(size_t bytes,
                                          base::TimeDelta duration) {
  recorded_embedder_marking_.Push(BytesAndDuration(bytes, duration));
}

void GCTracer::RecordMutatorUtilization(base::TimeTicks mark_compact_end_time,
                                        base::TimeDelta mark_compact_duration) {
  total_duration_since_last_mark_compact_ =
      mark_compact_end_time - previous_mark_compact_end_time_;
  DCHECK_GE(total_duration_since_last_mark_compact_, base::TimeDelta());
  const base::TimeDelta mutator_duration =
      total_duration_since_last_mark_compact_ - mark_compact_duration;
  DCHECK_GE(mutator_duration, base::TimeDelta());
  if (average_mark_compact_duration_ == 0 && average_mutator_duration_ == 0) {
    // This is the first event with mutator and mark-compact durations.
    average_mark_compact_duration_ = mark_compact_duration.InMillisecondsF();
    average_mutator_duration_ = mutator_duration.InMillisecondsF();
  } else {
    average_mark_compact_duration_ = (average_mark_compact_duration_ +
                                      mark_compact_duration.InMillisecondsF()) /
                                     2;
    average_mutator_duration_ =
        (average_mutator_duration_ + mutator_duration.InMillisecondsF()) / 2;
  }
  current_mark_compact_mutator_utilization_ =
      !total_duration_since_last_mark_compact_.IsZero()
          ? mutator_duration.InMillisecondsF() /
                total_duration_since_last_mark_compact_.InMillisecondsF()
          : 0;
  previous_mark_compact_end_time_ = mark_compact_end_time;
}

double GCTracer::AverageMarkCompactMutatorUtilization() const {
  double average_total_duration =
      average_mark_compact_duration_ + average_mutator_duration_;
  if (average_total_duration == 0) return 1.0;
  return average_mutator_duration_ / average_total_duration;
}

double GCTracer::CurrentMarkCompactMutatorUtilization() const {
  return current_mark_compact_mutator_utilization_;
}

double GCTracer::IncrementalMarkingSpeedInBytesPerMillisecond() const {
  if (recorded_major_incremental_marking_speed_ != 0) {
    return recorded_major_incremental_marking_speed_;
  }
  if (!current_.incremental_marking_duration.IsZero()) {
    return current_.incremental_marking_bytes /
           current_.incremental_marking_duration.InMillisecondsF();
  }
  return kConservativeSpeedInBytesPerMillisecond;
}

std::optional<double> GCTracer::EmbedderSpeedInBytesPerMillisecond() const {
  return BoundedAverageSpeed(recorded_embedder_marking_);
}

std::optional<double> GCTracer::YoungGenerationSpeedInBytesPerMillisecond(
    YoungGenerationSpeedMode mode) const {
  switch (mode) {
    case YoungGenerationSpeedMode::kUpToAndIncludingAtomicPause:
      return BoundedAverageSpeed(recorded_minor_gc_per_thread_);
    case YoungGenerationSpeedMode::kOnlyAtomicPause:
      return BoundedAverageSpeed(recorded_minor_gc_atomic_pause_);
  }
  UNREACHABLE();
}

std::optional<double> GCTracer::CompactionSpeedInBytesPerMillisecond() const {
  return BoundedAverageSpeed(recorded_compactions_);
}

std::optional<double> GCTracer::MarkCompactSpeedInBytesPerMillisecond() const {
  return BoundedAverageSpeed(recorded_mark_compacts_);
}

std::optional<double>
GCTracer::FinalIncrementalMarkCompactSpeedInBytesPerMillisecond() const {
  return BoundedAverageSpeed(recorded_incremental_mark_compacts_);
}

std::optional<double> GCTracer::OldGenerationSpeedInBytesPerMillisecond() {
  if (v8_flags.gc_speed_uses_counters) {
    return BoundedAverageSpeed(recorded_major_totals_);
  }

  const double kMinimumMarkingSpeed = 0.5;
  if (combined_mark_compact_speed_cache_.has_value())
    return combined_mark_compact_speed_cache_;
  // MarkCompact speed is more stable than incremental marking speed, because
  // there might not be many incremental marking steps because of concurrent
  // marking.
  combined_mark_compact_speed_cache_ = MarkCompactSpeedInBytesPerMillisecond();
  if (combined_mark_compact_speed_cache_.has_value())
    return combined_mark_compact_speed_cache_;
  double speed1 = IncrementalMarkingSpeedInBytesPerMillisecond();
  double speed2 =
      FinalIncrementalMarkCompactSpeedInBytesPerMillisecond().value_or(0.0);
  if (speed1 < kMinimumMarkingSpeed || speed2 < kMinimumMarkingSpeed) {
    // No data for the incremental marking speed.
    // Return the non-incremental mark-compact speed.
    combined_mark_compact_speed_cache_ =
        MarkCompactSpeedInBytesPerMillisecond();
  } else {
    // Combine the speed of incremental step and the speed of the final step.
    // 1 / (1 / speed1 + 1 / speed2) = speed1 * speed2 / (speed1 + speed2).
    combined_mark_compact_speed_cache_ = speed1 * speed2 / (speed1 + speed2);
  }
  return combined_mark_compact_speed_cache_;
}

double GCTracer::NewSpaceAllocationThroughputInBytesPerMillisecond() const {
  return BoundedThroughput(new_generation_allocations_);
}

double GCTracer::OldGenerationAllocationThroughputInBytesPerMillisecond()
    const {
  return BoundedThroughput(old_generation_allocations_);
}

double GCTracer::EmbedderAllocationThroughputInBytesPerMillisecond() const {
  return BoundedThroughput(embedder_generation_allocations_);
}

double GCTracer::AllocationThroughputInBytesPerMillisecond() const {
  return NewSpaceAllocationThroughputInBytesPerMillisecond() +
         OldGenerationAllocationThroughputInBytesPerMillisecond();
}

double GCTracer::AverageSurvivalRatio() const {
  if (recorded_survival_ratios_.Empty()) return 0.0;
  double sum = recorded_survival_ratios_.Reduce(
      [](double a, double b) { return a + b; }, 0.0);
  return sum / recorded_survival_ratios_.Size();
}

bool GCTracer::SurvivalEventsRecorded() const {
  return !recorded_survival_ratios_.Empty();
}

void GCTracer::ResetSurvivalEvents() { recorded_survival_ratios_.Clear(); }

void GCTracer::NotifyIncrementalMarkingStart() {
  current_.incremental_marking_start_time = base::TimeTicks::Now();
}

void GCTracer::FetchBackgroundCounters() {
  base::MutexGuard guard(&background_scopes_mutex_);
  for (int i = Scope::FIRST_BACKGROUND_SCOPE; i <= Scope::LAST_BACKGROUND_SCOPE;
       i++) {
    current_.scopes[i] += background_scopes_[i];
    background_scopes_[i] = base::TimeDelta();
  }
}

namespace {

V8_INLINE int TruncateToMs(base::TimeDelta delta) {
  return static_cast<int>(delta.InMilliseconds());
}

}  // namespace

void GCTracer::RecordGCPhasesHistograms(RecordGCPhasesInfo::Mode mode) {
  Counters* counters = heap_->isolate()->counters();
  if (mode == RecordGCPhasesInfo::Mode::Finalize) {
    DCHECK_EQ(Scope::FIRST_TOP_MC_SCOPE, Scope::MC_CLEAR);
    counters->gc_finalize_clear()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_CLEAR]));
    counters->gc_finalize_epilogue()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_EPILOGUE]));
    counters->gc_finalize_evacuate()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_EVACUATE]));
    counters->gc_finalize_finish()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_FINISH]));
    counters->gc_finalize_mark()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_MARK]));
    counters->gc_finalize_prologue()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_PROLOGUE]));
    counters->gc_finalize_sweep()->AddSample(
        TruncateToMs(current_.scopes[Scope::MC_SWEEP]));
    if (!current_.incremental_marking_duration.IsZero()) {
      heap_->isolate()->counters()->incremental_marking_sum()->AddSample(
          TruncateToMs(current_.incremental_marking_duration));
    }
    DCHECK_EQ(Scope::LAST_TOP_MC_SCOPE, Scope::MC_SWEEP);
  } else if (mode == RecordGCPhasesInfo::Mode::Scavenger) {
    counters->gc_scavenger_scavenge_main()->AddSample(
        TruncateToMs(current_.scopes[Scope::SCAVENGER_SCAVENGE_PARALLEL]));
    counters->gc_scavenger_scavenge_roots()->AddSample(
        TruncateToMs(current_.scopes[Scope::SCAVENGER_SCAVENGE_ROOTS]));
  }
}

void GCTracer::RecordGCSumCounters() {
  const base::TimeDelta atomic_pause_duration =
      current_.scopes[Scope::MARK_COMPACTOR];
  const base::TimeDelta incremental_marking =
      incremental_scopes_[Scope::MC_INCREMENTAL_LAYOUT_CHANGE].duration +
      incremental_scopes_[Scope::MC_INCREMENTAL_START].duration +
      current_.incremental_marking_duration;
  const base::TimeDelta incremental_sweeping =
      incremental_scopes_[Scope::MC_INCREMENTAL_SWEEPING].duration;
  const base::TimeDelta overall_duration =
      atomic_pause_duration + incremental_marking + incremental_sweeping;
  const base::TimeDelta atomic_marking_duration =
      current_.scopes[Scope::MC_PROLOGUE] + current_.scopes[Scope::MC_MARK];
  const base::TimeDelta marking_duration =
      atomic_marking_duration + incremental_marking;
  base::TimeDelta background_duration;
  base::TimeDelta marking_background_duration;
  {
    base::MutexGuard guard(&background_scopes_mutex_);
    background_duration =
        background_scopes_[Scope::MC_BACKGROUND_EVACUATE_COPY] +
        background_scopes_[Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS] +
        background_scopes_[Scope::MC_BACKGROUND_MARKING] +
        background_scopes_[Scope::MC_BACKGROUND_SWEEPING];
    marking_background_duration =
        background_scopes_[Scope::MC_BACKGROUND_MARKING];
  }

  recorded_major_totals_.Push(
      BytesAndDuration(current_.end_object_size, overall_duration));

  // Emit trace event counters.
  TRACE_EVENT_INSTANT2(
      TRACE_DISABLED_BY_DEFAULT("v8.gc"), "V8.GCMarkCompactorSummary",
      TRACE_EVENT_SCOPE_THREAD, "duration", overall_duration.InMillisecondsF(),
      "background_duration", background_duration.InMillisecondsF());
  TRACE_EVENT_INSTANT2(
      TRACE_DISABLED_BY_DEFAULT("v8.gc"), "V8.GCMarkCompactorMarkingSummary",
      TRACE_EVENT_SCOPE_THREAD, "duration", marking_duration.InMillisecondsF(),
      "background_duration", marking_background_duration.InMillisecondsF());
  TRACE_EVENT_INSTANT2(TRACE_DISABLED_BY_DEFAULT("v8.gc"), "V8.GCSpeedSummary",
                       TRACE_EVENT_SCOPE_THREAD, "old_generation_speed",
                       OldGenerationSpeedInBytesPerMillisecond().value_or(0.0),
                       "embedder_speed",
                       EmbedderSpeedInBytesPerMillisecond().value_or(0.0));
}

void GCTracer::RecordGCSizeCounters() const {
#if defined(V8_USE_PERFETTO)
  TRACE_COUNTER(
      TRACE_DISABLED_BY_DEFAULT("v8.gc"),
      perfetto::CounterTrack("OldGenerationConsumedBytes", parent_track_),
      heap_->OldGenerationConsumedBytes());
  TRACE_COUNTER(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                perfetto::CounterTrack("GlobalConsumedBytes", parent_track_),
                heap_->GlobalConsumedBytes());
  TRACE_COUNTER(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                perfetto::CounterTrack("ExternalMemoryBytes", parent_track_),
                heap_->external_memory());
  TRACE_COUNTER(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                perfetto::CounterTrack("NewSpaceCapacity", parent_track_),
                heap_->NewSpaceCapacity());
#endif
}

namespace {

void CopyTimeMetrics(
    ::v8::metrics::GarbageCollectionPhases& metrics,
    const cppgc::internal::MetricRecorder::GCCycle::IncrementalPhases&
        cppgc_metrics) {
  // Allow for uninitialized values (-1), in case incremental marking/sweeping
  // were not used.
  DCHECK_LE(-1, cppgc_metrics.mark_duration_us);
  metrics.mark_wall_clock_duration_in_us = cppgc_metrics.mark_duration_us;
  DCHECK_LE(-1, cppgc_metrics.sweep_duration_us);
  metrics.sweep_wall_clock_duration_in_us = cppgc_metrics.sweep_duration_us;
  // The total duration is initialized, even if both incremental
  // marking and sweeping were not used.
  metrics.total_wall_clock_duration_in_us =
      std::max(INT64_C(0), metrics.mark_wall_clock_duration_in_us) +
      std::max(INT64_C(0), metrics.sweep_wall_clock_duration_in_us);
}

void CopyTimeMetrics(
    ::v8::metrics::GarbageCollectionPhases& metrics,
    const cppgc::internal::MetricRecorder::GCCycle::Phases& cppgc_metrics) {
  DCHECK_NE(-1, cppgc_metrics.compact_duration_us);
  metrics.compact_wall_clock_duration_in_us = cppgc_metrics.compact_duration_us;
  DCHECK_NE(-1, cppgc_metrics.mark_duration_us);
  metrics.mark_wall_clock_duration_in_us = cppgc_metrics.mark_duration_us;
  DCHECK_NE(-1, cppgc_metrics.sweep_duration_us);
  metrics.sweep_wall_clock_duration_in_us = cppgc_metrics.sweep_duration_us;
  DCHECK_NE(-1, cppgc_metrics.weak_duration_us);
  metrics.weak_wall_clock_duration_in_us = cppgc_metrics.weak_duration_us;
  metrics.total_wall_clock_duration_in_us =
      metrics.compact_wall_clock_duration_in_us +
      metrics.mark_wall_clock_duration_in_us +
      metrics.sweep_wall_clock_duration_in_us +
      metrics.weak_wall_clock_duration_in_us;
}

void CopySizeMetrics(
    ::v8::metrics::GarbageCollectionSizes& metrics,
    const cppgc::internal::MetricRecorder::GCCycle::Sizes& cppgc_metrics) {
  DCHECK_NE(-1, cppgc_metrics.after_bytes);
  metrics.bytes_after = cppgc_metrics.after_bytes;
  DCHECK_NE(-1, cppgc_metrics.before_bytes);
  metrics.bytes_before = cppgc_metrics.before_bytes;
  DCHECK_NE(-1, cppgc_metrics.freed_bytes);
  metrics.bytes_freed = cppgc_metrics.freed_bytes;
}

::v8::metrics::Recorder::ContextId GetContextId(
    v8::internal::Isolate* isolate) {
  DCHECK_NOT_NULL(isolate);
  if (isolate->context().is_null())
    return v8::metrics::Recorder::ContextId::Empty();
  HandleScope scope(isolate);
  return isolate->GetOrRegisterRecorderContextId(isolate->native_context());
}

template <typename EventType>
void FlushBatchedEvents(
    v8::metrics::GarbageCollectionBatchedEvents<EventType>& batched_events,
    Isolate* isolate) {
  DCHECK_NOT_NULL(isolate->metrics_recorder());
  DCHECK(!batched_events.events.empty());
  isolate->metrics_recorder()->AddMainThreadEvent(std::move(batched_events),
                                                  GetContextId(isolate));
  batched_events = {};
}

}  // namespace

void GCTracer::ReportFullCycleToRecorder() {
  DCHECK(!Event::IsYoungGenerationEvent(current_.type));
  DCHECK_EQ(Event::State::NOT_RUNNING, current_.state);
  auto* cpp_heap = v8::internal::CppHeap::From(heap_->cpp_heap());
  DCHECK_IMPLIES(cpp_heap,
                 cpp_heap->GetMetricRecorder()->FullGCMetricsReportPending());
  const std::shared_ptr<metrics::Recorder>& recorder =
      heap_->isolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) {
    incremental_mark_batched_events_ = {};
    incremental_sweep_batched_events_ = {};
    if (cpp_heap) {
      cpp_heap->GetMetricRecorder()->ClearCachedEvents();
    }
    return;
  }
  if (!incremental_mark_batched_events_.events.empty()) {
    FlushBatchedEvents(incremental_mark_batched_events_, heap_->isolate());
  }
  if (!incremental_sweep_batched_events_.events.empty()) {
    FlushBatchedEvents(incremental_sweep_batched_events_, heap_->isolate());
  }

  v8::metrics::GarbageCollectionFullCycle event;
  event.reason = static_cast<int>(current_.gc_reason);
  event.priority = current_.priority;
  event.reduce_memory = current_.reduce_memory;
  event.is_loading = current_.is_loading;

  // Managed C++ heap statistics:
  if (cpp_heap) {
    cpp_heap->GetMetricRecorder()->FlushBatchedIncrementalEvents();
    const std::optional<cppgc::internal::MetricRecorder::GCCycle>
        optional_cppgc_event =
            cpp_heap->GetMetricRecorder()->ExtractLastFullGcEvent();
    DCHECK(optional_cppgc_event.has_value());
    DCHECK(!cpp_heap->GetMetricRecorder()->FullGCMetricsReportPending());
    const cppgc::internal::MetricRecorder::GCCycle& cppgc_event =
        optional_cppgc_event.value();
    DCHECK_EQ(cppgc_event.type,
              cppgc::internal::MetricRecorder::GCCycle::Type::kMajor);
    CopyTimeMetrics(event.total_cpp, cppgc_event.total);
    CopyTimeMetrics(event.main_thread_cpp, cppgc_event.main_thread);
    CopyTimeMetrics(event.main_thread_atomic_cpp,
                    cppgc_event.main_thread_atomic);
    CopyTimeMetrics(event.main_thread_incremental_cpp,
                    cppgc_event.main_thread_incremental);
    CopySizeMetrics(event.objects_cpp, cppgc_event.objects);
    CopySizeMetrics(event.memory_cpp, cppgc_event.memory);
    DCHECK_NE(-1, cppgc_event.collection_rate_in_percent);
    event.collection_rate_cpp_in_percent =
        cppgc_event.collection_rate_in_percent;
    DCHECK_NE(-1, cppgc_event.efficiency_in_bytes_per_us);
    event.efficiency_cpp_in_bytes_per_us =
        cppgc_event.efficiency_in_bytes_per_us;
    DCHECK_NE(-1, cppgc_event.main_thread_efficiency_in_bytes_per_us);
    event.main_thread_efficiency_cpp_in_bytes_per_us =
        cppgc_event.main_thread_efficiency_in_bytes_per_us;

    if (total_duration_since_last_mark_compact_.IsZero()) {
      event.collection_weight_cpp_in_percent = 0;
      event.main_thread_collection_weight_cpp_in_percent = 0;
    } else {
      event.collection_weight_cpp_in_percent =
          static_cast<double>(event.total_cpp.total_wall_clock_duration_in_us) /
          total_duration_since_last_mark_compact_.InMicroseconds();
      event.main_thread_collection_weight_cpp_in_percent =
          static_cast<double>(
              event.main_thread_cpp.total_wall_clock_duration_in_us) /
          total_duration_since_last_mark_compact_.InMicroseconds();
    }
  }

  // Unified heap statistics:
  const base::TimeDelta atomic_pause_duration =
      current_.scopes[Scope::MARK_COMPACTOR];
  const base::TimeDelta incremental_marking =
      current_.incremental_scopes[Scope::MC_INCREMENTAL_LAYOUT_CHANGE]
          .duration +
      current_.incremental_scopes[Scope::MC_INCREMENTAL_START].duration +
      current_.incremental_marking_duration;
  const base::TimeDelta incremental_sweeping =
      current_.incremental_scopes[Scope::MC_INCREMENTAL_SWEEPING].duration;
  const base::TimeDelta overall_duration =
      atomic_pause_duration + incremental_marking + incremental_sweeping;
  const base::TimeDelta marking_background_duration =
      current_.scopes[Scope::MC_BACKGROUND_MARKING];
  const base::TimeDelta sweeping_background_duration =
      current_.scopes[Scope::MC_BACKGROUND_SWEEPING];
  const base::TimeDelta compact_background_duration =
      current_.scopes[Scope::MC_BACKGROUND_EVACUATE_COPY] +
      current_.scopes[Scope::MC_BACKGROUND_EVACUATE_UPDATE_POINTERS];
  const base::TimeDelta background_duration = marking_background_duration +
                                              sweeping_background_duration +
                                              compact_background_duration;
  const base::TimeDelta atomic_marking_duration =
      current_.scopes[Scope::MC_PROLOGUE] + current_.scopes[Scope::MC_MARK];
  const base::TimeDelta marking_duration =
      atomic_marking_duration + incremental_marking;
  const base::TimeDelta weak_duration = current_.scopes[Scope::MC_CLEAR];
  const base::TimeDelta compact_duration = current_.scopes[Scope::MC_EVACUATE] +
                                           current_.scopes[Scope::MC_FINISH] +
                                           current_.scopes[Scope::MC_EPILOGUE];
  const base::TimeDelta atomic_sweeping_duration =
      current_.scopes[Scope::MC_SWEEP];
  const base::TimeDelta sweeping_duration =
      atomic_sweeping_duration + incremental_sweeping;

  event.main_thread_atomic.total_wall_clock_duration_in_us =
      atomic_pause_duration.InMicroseconds();
  event.main_thread.total_wall_clock_duration_in_us =
      overall_duration.InMicroseconds();
  event.total.total_wall_clock_duration_in_us =
      (overall_duration + background_duration).InMicroseconds();
  event.main_thread_atomic.mark_wall_clock_duration_in_us =
      atomic_marking_duration.InMicroseconds();
  event.main_thread.mark_wall_clock_duration_in_us =
      marking_duration.InMicroseconds();
  event.total.mark_wall_clock_duration_in_us =
      (marking_duration + marking_background_duration).InMicroseconds();
  event.main_thread_atomic.weak_wall_clock_duration_in_us =
      event.main_thread.weak_wall_clock_duration_in_us =
          event.total.weak_wall_clock_duration_in_us =
              weak_duration.InMicroseconds();
  event.main_thread_atomic.compact_wall_clock_duration_in_us =
      event.main_thread.compact_wall_clock_duration_in_us =
          compact_duration.InMicroseconds();
  event.total.compact_wall_clock_duration_in_us =
      (compact_duration + compact_background_duration).InMicroseconds();
  event.main_thread_atomic.sweep_wall_clock_duration_in_us =
      atomic_sweeping_duration.InMicroseconds();
  event.main_thread.sweep_wall_clock_duration_in_us =
      sweeping_duration.InMicroseconds();
  event.total.sweep_wall_clock_duration_in_us =
      (sweeping_duration + sweeping_background_duration).InMicroseconds();
  if (current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR) {
    event.main_thread_incremental.mark_wall_clock_duration_in_us =
        incremental_marking.InMicroseconds();
    event.incremental_marking_start_stop_wall_clock_duration_in_us =
        (current_.start_time - current_.incremental_marking_start_time)
            .InMicroseconds();
  } else {
    DCHECK(incremental_marking.IsZero());
    event.main_thread_incremental.mark_wall_clock_duration_in_us = -1;
  }
  // TODO(chromium:1154636): We always report the value of incremental sweeping,
  // even if it is zero.
  event.main_thread_incremental.sweep_wall_clock_duration_in_us =
      incremental_sweeping.InMicroseconds();

  // Objects:
  event.objects.bytes_before = current_.start_object_size;
  event.objects.bytes_after = current_.end_object_size;
  event.objects.bytes_freed =
      current_.start_object_size - current_.end_object_size;
  // Memory:
  event.memory.bytes_before = current_.start_memory_size;
  event.memory.bytes_after = current_.end_memory_size;
  event.memory.bytes_freed =
      current_.start_memory_size > current_.end_memory_size
          ? current_.start_memory_size - current_.end_memory_size
          : 0U;
  // Old generation Consumed Byes:
  event.old_generation_consumed.bytes_baseline =
      current_.old_generation_consumed_baseline;
  event.old_generation_consumed.bytes_limit =
      current_.old_generation_consumed_limit;
  event.old_generation_consumed.bytes_current =
      current_.old_generation_consumed_current;
  event.old_generation_consumed.growing_bytes =
      current_.old_generation_consumed_current -
      current_.old_generation_consumed_baseline;
  event.old_generation_consumed.growing_factor =
      current_.old_generation_consumed_baseline > 0
          ? static_cast<double>(event.old_generation_consumed.growing_bytes) /
                current_.old_generation_consumed_baseline
          : 0.0;
  // Global Consumed Byes:
  event.global_consumed.bytes_baseline = current_.global_consumed_baseline;
  event.global_consumed.bytes_limit = current_.global_consumed_limit;
  event.global_consumed.bytes_current = current_.global_consumed_current;
  event.global_consumed.growing_bytes =
      current_.global_consumed_current - current_.global_consumed_baseline;
  event.global_consumed.growing_factor =
      current_.global_consumed_baseline > 0
          ? static_cast<double>(event.global_consumed.growing_bytes) /
                current_.global_consumed_baseline
          : 0.0;
  // Collection Rate:
  if (event.objects.bytes_before == 0) {
    event.collection_rate_in_percent = 0;
  } else {
    event.collection_rate_in_percent =
        static_cast<double>(event.objects.bytes_freed) /
        event.objects.bytes_before;
  }
  // Efficiency:
  if (event.objects.bytes_freed == 0) {
    event.efficiency_in_bytes_per_us = 0;
    event.main_thread_efficiency_in_bytes_per_us = 0;
  } else {
    // Here, event.main_thread or even event.total can be
    // zero if the clock resolution is not small enough and the entire GC was
    // very short, so the timed value was zero. This appears to happen on
    // Windows, see crbug.com/1338256 and crbug.com/1339180. In this case, we
    // are only here if the number of freed bytes is nonzero and the division
    // below produces an infinite value.
    event.efficiency_in_bytes_per_us =
        static_cast<double>(event.objects.bytes_freed) /
        event.total.total_wall_clock_duration_in_us;
    event.main_thread_efficiency_in_bytes_per_us =
        static_cast<double>(event.objects.bytes_freed) /
        event.main_thread.total_wall_clock_duration_in_us;
  }
  if (total_duration_since_last_mark_compact_.IsZero()) {
    event.collection_weight_in_percent = 0;
    event.main_thread_collection_weight_in_percent = 0;
  } else {
    event.total_duration_since_last_mark_compact =
        total_duration_since_last_mark_compact_.InMicroseconds();
    event.collection_weight_in_percent =
        static_cast<double>(event.total.total_wall_clock_duration_in_us) /
        total_duration_since_last_mark_compact_.InMicroseconds();
    event.main_thread_collection_weight_in_percent =
        static_cast<double>(event.main_thread.total_wall_clock_duration_in_us) /
        total_duration_since_last_mark_compact_.InMicroseconds();
  }

  recorder->AddMainThreadEvent(event, GetContextId(heap_->isolate()));
}

void GCTracer::ReportIncrementalMarkingStepToRecorder(double v8_duration) {
  DCHECK_EQ(Event::Type::INCREMENTAL_MARK_COMPACTOR, current_.type);
  static constexpr int kMaxBatchedEvents =
      CppHeap::MetricRecorderAdapter::kMaxBatchedEvents;
  const std::shared_ptr<metrics::Recorder>& recorder =
      heap_->isolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) return;
  incremental_mark_batched_events_.events.emplace_back();
  if (heap_->cpp_heap()) {
    const std::optional<
        cppgc::internal::MetricRecorder::MainThreadIncrementalMark>
        cppgc_event = v8::internal::CppHeap::From(heap_->cpp_heap())
                          ->GetMetricRecorder()
                          ->ExtractLastIncrementalMarkEvent();
    if (cppgc_event.has_value()) {
      DCHECK_NE(-1, cppgc_event.value().duration_us);
      incremental_mark_batched_events_.events.back()
          .cpp_wall_clock_duration_in_us = cppgc_event.value().duration_us;
    }
  }
  incremental_mark_batched_events_.events.back().wall_clock_duration_in_us =
      static_cast<int64_t>(v8_duration *
                           base::Time::kMicrosecondsPerMillisecond);
  if (incremental_mark_batched_events_.events.size() == kMaxBatchedEvents) {
    FlushBatchedEvents(incremental_mark_batched_events_, heap_->isolate());
  }
}

void GCTracer::ReportIncrementalSweepingStepToRecorder(double v8_duration) {
  static constexpr int kMaxBatchedEvents =
      CppHeap::MetricRecorderAdapter::kMaxBatchedEvents;
  const std::shared_ptr<metrics::Recorder>& recorder =
      heap_->isolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  if (!recorder->HasEmbedderRecorder()) return;
  incremental_sweep_batched_events_.events.emplace_back();
  incremental_sweep_batched_events_.events.back().wall_clock_duration_in_us =
      static_cast<int64_t>(v8_duration *
                           base::Time::kMicrosecondsPerMillisecond);
  if (incremental_sweep_batched_events_.events.size() == kMaxBatchedEvents) {
    FlushBatchedEvents(incremental_sweep_batched_events_, heap_->isolate());
  }
}

void GCTracer::ReportYoungCycleToRecorder() {
  DCHECK(Event::IsYoungGenerationEvent(current_.type));
  DCHECK_EQ(Event::State::NOT_RUNNING, current_.state);
  const std::shared_ptr<metrics::Recorder>& recorder =
      heap_->isolate()->metrics_recorder();
  DCHECK_NOT_NULL(recorder);
  auto* cpp_heap = v8::internal::CppHeap::From(heap_->cpp_heap());
  if (!recorder->HasEmbedderRecorder()) {
    if (cpp_heap) {
      cpp_heap->GetMetricRecorder()->ClearCachedYoungEvents();
    }
    return;
  }

  v8::metrics::GarbageCollectionYoungCycle event;
  // Reason:
  event.reason = static_cast<int>(current_.gc_reason);
  event.priority = current_.priority;
#if defined(CPPGC_YOUNG_GENERATION)
  // Managed C++ heap statistics:
  if (cpp_heap && cpp_heap->generational_gc_supported()) {
    auto* metric_recorder = cpp_heap->GetMetricRecorder();
    const std::optional<cppgc::internal::MetricRecorder::GCCycle>
        optional_cppgc_event = metric_recorder->ExtractLastYoungGcEvent();
    // We bail out from Oilpan's young GC if the full GC is already in progress.
    // Check here if the young generation event was reported.
    if (optional_cppgc_event) {
      DCHECK(!metric_recorder->YoungGCMetricsReportPending());
      const cppgc::internal::MetricRecorder::GCCycle& cppgc_event =
          optional_cppgc_event.value();
      DCHECK_EQ(cppgc_event.type,
                cppgc::internal::MetricRecorder::GCCycle::Type::kMinor);
      CopyTimeMetrics(event.total_cpp, cppgc_event.total);
      CopySizeMetrics(event.objects_cpp, cppgc_event.objects);
      CopySizeMetrics(event.memory_cpp, cppgc_event.memory);
      DCHECK_NE(-1, cppgc_event.collection_rate_in_percent);
      event.collection_rate_cpp_in_percent =
          cppgc_event.collection_rate_in_percent;
      DCHECK_NE(-1, cppgc_event.efficiency_in_bytes_per_us);
      event.efficiency_cpp_in_bytes_per_us =
          cppgc_event.efficiency_in_bytes_per_us;
      DCHECK_NE(-1, cppgc_event.main_thread_efficiency_in_bytes_per_us);
      event.main_thread_efficiency_cpp_in_bytes_per_us =
          cppgc_event.main_thread_efficiency_in_bytes_per_us;
    }
  }
#endif  // defined(CPPGC_YOUNG_GENERATION)

  // Total:
  const base::TimeDelta total_wall_clock_duration =
      YoungGenerationWallTime(current_);

  // TODO(chromium:1154636): Consider adding BACKGROUND_YOUNG_ARRAY_BUFFER_SWEEP
  // (both for the case of the scavenger and the minor mark-sweeper).
  event.total_wall_clock_duration_in_us =
      total_wall_clock_duration.InMicroseconds();
  // MainThread:
  const base::TimeDelta main_thread_wall_clock_duration =
      current_.scopes[Scope::SCAVENGER] +
      current_.scopes[Scope::MINOR_MARK_SWEEPER];
  event.main_thread_wall_clock_duration_in_us =
      main_thread_wall_clock_duration.InMicroseconds();
  // Collection Rate:
  if (current_.young_object_size == 0) {
    event.collection_rate_in_percent = 0;
  } else {
    event.collection_rate_in_percent =
        static_cast<double>(current_.survived_young_object_size) /
        current_.young_object_size;
  }
  // Efficiency:
  //
  // It's possible that time durations are rounded/clamped to zero, in which
  // case we report infinity efficiency.
  const double freed_bytes = static_cast<double>(
      current_.young_object_size - current_.survived_young_object_size);
  event.efficiency_in_bytes_per_us =
      total_wall_clock_duration.IsZero()
          ? std::numeric_limits<double>::infinity()
          : freed_bytes / total_wall_clock_duration.InMicroseconds();
  event.main_thread_efficiency_in_bytes_per_us =
      main_thread_wall_clock_duration.IsZero()
          ? std::numeric_limits<double>::infinity()
          : freed_bytes / main_thread_wall_clock_duration.InMicroseconds();
  recorder->AddMainThreadEvent(event, GetContextId(heap_->isolate()));
}

GarbageCollector GCTracer::GetCurrentCollector() const {
  switch (current_.type) {
    case Event::Type::SCAVENGER:
      return GarbageCollector::SCAVENGER;
    case Event::Type::MARK_COMPACTOR:
    case Event::Type::INCREMENTAL_MARK_COMPACTOR:
      return GarbageCollector::MARK_COMPACTOR;
    case Event::Type::MINOR_MARK_SWEEPER:
    case Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER:
      return GarbageCollector::MINOR_MARK_SWEEPER;
    case Event::Type::START:
      UNREACHABLE();
  }
}

void GCTracer::UpdateCurrentEventPriority(GCTracer::Priority priority) {
  // If the priority is changed, reset the priority field to denote a mixed
  // priority cycle.
  if (!current_.priority.has_value() || (current_.priority == priority)) {
    return;
  }
  current_.priority = std::nullopt;
}

#ifdef DEBUG
bool GCTracer::IsInObservablePause() const {
  return start_of_observable_pause_.has_value();
}

bool GCTracer::IsInAtomicPause() const {
  return current_.state == Event::State::ATOMIC;
}

bool GCTracer::IsConsistentWithCollector(GarbageCollector collector) const {
  switch (collector) {
    case GarbageCollector::SCAVENGER:
      return current_.type == Event::Type::SCAVENGER;
    case GarbageCollector::MARK_COMPACTOR:
      return current_.type == Event::Type::MARK_COMPACTOR ||
             current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR;
    case GarbageCollector::MINOR_MARK_SWEEPER:
      return current_.type == Event::Type::MINOR_MARK_SWEEPER ||
             current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER;
  }
}

bool GCTracer::IsSweepingInProgress() const {
  return (current_.type == Event::Type::MARK_COMPACTOR ||
          current_.type == Event::Type::INCREMENTAL_MARK_COMPACTOR ||
          current_.type == Event::Type::MINOR_MARK_SWEEPER ||
          current_.type == Event::Type::INCREMENTAL_MINOR_MARK_SWEEPER) &&
         current_.state == Event::State::SWEEPING;
}
#endif

}  // namespace internal
}  // namespace v8
