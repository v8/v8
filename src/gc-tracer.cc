// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/gc-tracer.h"

namespace v8 {
namespace internal {

static intptr_t CountTotalHolesSize(Heap* heap) {
  intptr_t holes_size = 0;
  OldSpaces spaces(heap);
  for (OldSpace* space = spaces.next(); space != NULL; space = spaces.next()) {
    holes_size += space->Waste() + space->Available();
  }
  return holes_size;
}


GCTracer::Event::Event(Type type, const char* gc_reason,
                       const char* collector_reason)
    : type(type),
      gc_reason(gc_reason),
      collector_reason(collector_reason),
      start_time(0.0),
      end_time(0.0),
      start_object_size(0),
      end_object_size(0),
      start_memory_size(0),
      end_memory_size(0),
      start_holes_size(0),
      end_holes_size(0),
      incremental_marking_steps(0),
      incremental_marking_duration(0.0) {
  for (int i = 0; i < Scope::NUMBER_OF_SCOPES; i++) {
    scopes[i] = 0;
  }
}


const char* GCTracer::Event::TypeName(bool short_name) const {
  switch (type) {
    case SCAVENGER:
      if (short_name) {
        return "s";
      } else {
        return "Scavenge";
      }
    case MARK_COMPACTOR:
      if (short_name) {
        return "ms";
      } else {
        return "Mark-sweep";
      }
    case START:
      if (short_name) {
        return "st";
      } else {
        return "Start";
      }
  }
  return "Unknown Event Type";
}


GCTracer::GCTracer(Heap* heap)
    : heap_(heap),
      incremental_marking_steps_(0),
      incremental_marking_duration_(0.0),
      longest_incremental_marking_step_(0.0) {
  current_ = Event(Event::START, NULL, NULL);
  current_.end_time = base::OS::TimeCurrentMillis();
  previous_ = previous_mark_compactor_event_ = current_;
}


void GCTracer::Start(GarbageCollector collector, const char* gc_reason,
                     const char* collector_reason) {
  previous_ = current_;
  if (current_.type == Event::MARK_COMPACTOR)
    previous_mark_compactor_event_ = current_;

  if (collector == SCAVENGER) {
    current_ = Event(Event::SCAVENGER, gc_reason, collector_reason);
  } else {
    current_ = Event(Event::MARK_COMPACTOR, gc_reason, collector_reason);
  }

  current_.start_time = base::OS::TimeCurrentMillis();
  current_.start_object_size = heap_->SizeOfObjects();
  current_.start_memory_size = heap_->isolate()->memory_allocator()->Size();
  current_.start_holes_size = CountTotalHolesSize(heap_);

  current_.incremental_marking_steps = incremental_marking_steps_;
  current_.incremental_marking_duration = incremental_marking_duration_;
  current_.longest_incremental_marking_step = longest_incremental_marking_step_;

  for (int i = 0; i < Scope::NUMBER_OF_SCOPES; i++) {
    current_.scopes[i] = 0;
  }
}


void GCTracer::Stop() {
  current_.end_time = base::OS::TimeCurrentMillis();
  current_.end_object_size = heap_->SizeOfObjects();
  current_.end_memory_size = heap_->isolate()->memory_allocator()->Size();
  current_.end_holes_size = CountTotalHolesSize(heap_);

  if (current_.type == Event::SCAVENGER) {
    scavenger_events_.push_front(current_);
  } else {
    mark_compactor_events_.push_front(current_);
  }

  if (current_.type == Event::MARK_COMPACTOR)
    longest_incremental_marking_step_ = 0.0;

  // TODO(ernstm): move the code below out of GCTracer.

  if (!FLAG_trace_gc && !FLAG_print_cumulative_gc_stat) return;

  double duration = current_.end_time - current_.start_time;
  double spent_in_mutator = Max(current_.start_time - previous_.end_time, 0.0);

  heap_->UpdateCumulativeGCStatistics(duration, spent_in_mutator,
                                      current_.scopes[Scope::MC_MARK]);

  if (current_.type == Event::SCAVENGER && FLAG_trace_gc_ignore_scavenger)
    return;

  if (FLAG_trace_gc) {
    if (FLAG_trace_gc_nvp)
      PrintNVP();
    else
      Print();

    heap_->PrintShortHeapStatistics();
  }
}


void GCTracer::AddIncrementalMarkingStep(double duration) {
  incremental_marking_steps_++;
  incremental_marking_duration_ += duration;
  longest_incremental_marking_step_ =
      Max(longest_incremental_marking_step_, duration);
}


void GCTracer::Print() const {
  PrintPID("%8.0f ms: ", heap_->isolate()->time_millis_since_init());

  PrintF("%s %.1f (%.1f) -> %.1f (%.1f) MB, ", current_.TypeName(false),
         static_cast<double>(current_.start_object_size) / MB,
         static_cast<double>(current_.start_memory_size) / MB,
         static_cast<double>(current_.end_object_size) / MB,
         static_cast<double>(current_.end_memory_size) / MB);

  int external_time = static_cast<int>(current_.scopes[Scope::EXTERNAL]);
  if (external_time > 0) PrintF("%d / ", external_time);

  double duration = current_.end_time - current_.start_time;
  PrintF("%.1f ms", duration);
  if (current_.type == Event::SCAVENGER) {
    int steps = current_.incremental_marking_steps -
                previous_.incremental_marking_steps;
    if (steps > 0) {
      PrintF(" (+ %.1f ms in %d steps since last GC)",
             current_.incremental_marking_duration -
                 previous_.incremental_marking_duration,
             steps);
    }
  } else {
    int steps = current_.incremental_marking_steps -
                previous_mark_compactor_event_.incremental_marking_steps;
    if (steps > 0) {
      PrintF(
          " (+ %.1f ms in %d steps since start of marking, "
          "biggest step %.1f ms)",
          current_.incremental_marking_duration -
              previous_mark_compactor_event_.incremental_marking_duration,
          steps, current_.longest_incremental_marking_step);
    }
  }

  if (current_.gc_reason != NULL) {
    PrintF(" [%s]", current_.gc_reason);
  }

  if (current_.collector_reason != NULL) {
    PrintF(" [%s]", current_.collector_reason);
  }

  PrintF(".\n");
}


void GCTracer::PrintNVP() const {
  PrintPID("%8.0f ms: ", heap_->isolate()->time_millis_since_init());

  double duration = current_.end_time - current_.start_time;
  double spent_in_mutator = current_.start_time - previous_.end_time;

  PrintF("pause=%.1f ", duration);
  PrintF("mutator=%.1f ", spent_in_mutator);
  PrintF("gc=%s ", current_.TypeName(true));

  PrintF("external=%.1f ", current_.scopes[Scope::EXTERNAL]);
  PrintF("mark=%.1f ", current_.scopes[Scope::MC_MARK]);
  PrintF("sweep=%.2f ", current_.scopes[Scope::MC_SWEEP]);
  PrintF("sweepns=%.2f ", current_.scopes[Scope::MC_SWEEP_NEWSPACE]);
  PrintF("sweepos=%.2f ", current_.scopes[Scope::MC_SWEEP_OLDSPACE]);
  PrintF("sweepcode=%.2f ", current_.scopes[Scope::MC_SWEEP_CODE]);
  PrintF("sweepcell=%.2f ", current_.scopes[Scope::MC_SWEEP_CELL]);
  PrintF("sweepmap=%.2f ", current_.scopes[Scope::MC_SWEEP_MAP]);
  PrintF("evacuate=%.1f ", current_.scopes[Scope::MC_EVACUATE_PAGES]);
  PrintF("new_new=%.1f ",
         current_.scopes[Scope::MC_UPDATE_NEW_TO_NEW_POINTERS]);
  PrintF("root_new=%.1f ",
         current_.scopes[Scope::MC_UPDATE_ROOT_TO_NEW_POINTERS]);
  PrintF("old_new=%.1f ",
         current_.scopes[Scope::MC_UPDATE_OLD_TO_NEW_POINTERS]);
  PrintF("compaction_ptrs=%.1f ",
         current_.scopes[Scope::MC_UPDATE_POINTERS_TO_EVACUATED]);
  PrintF("intracompaction_ptrs=%.1f ",
         current_.scopes[Scope::MC_UPDATE_POINTERS_BETWEEN_EVACUATED]);
  PrintF("misc_compaction=%.1f ",
         current_.scopes[Scope::MC_UPDATE_MISC_POINTERS]);
  PrintF("weakcollection_process=%.1f ",
         current_.scopes[Scope::MC_WEAKCOLLECTION_PROCESS]);
  PrintF("weakcollection_clear=%.1f ",
         current_.scopes[Scope::MC_WEAKCOLLECTION_CLEAR]);

  PrintF("total_size_before=%" V8_PTR_PREFIX "d ", current_.start_object_size);
  PrintF("total_size_after=%" V8_PTR_PREFIX "d ", current_.end_object_size);
  PrintF("holes_size_before=%" V8_PTR_PREFIX "d ", current_.start_holes_size);
  PrintF("holes_size_after=%" V8_PTR_PREFIX "d ", current_.end_holes_size);

  intptr_t allocated_since_last_gc =
      current_.start_object_size - previous_.end_object_size;
  PrintF("allocated=%" V8_PTR_PREFIX "d ", allocated_since_last_gc);
  PrintF("promoted=%" V8_PTR_PREFIX "d ", heap_->promoted_objects_size_);
  PrintF("semi_space_copied=%" V8_PTR_PREFIX "d ",
         heap_->semi_space_copied_object_size_);
  PrintF("nodes_died_in_new=%d ", heap_->nodes_died_in_new_space_);
  PrintF("nodes_copied_in_new=%d ", heap_->nodes_copied_in_new_space_);
  PrintF("nodes_promoted=%d ", heap_->nodes_promoted_);
  PrintF("promotion_rate=%.1f%% ", heap_->promotion_rate_);
  PrintF("semi_space_copy_rate=%.1f%% ", heap_->semi_space_copied_rate_);

  if (current_.type == Event::SCAVENGER) {
    PrintF("stepscount=%d ", current_.incremental_marking_steps -
                                 previous_.incremental_marking_steps);
    PrintF("stepstook=%.1f ", current_.incremental_marking_duration -
                                  previous_.incremental_marking_duration);
  } else {
    PrintF("stepscount=%d ",
           current_.incremental_marking_steps -
               previous_mark_compactor_event_.incremental_marking_steps);
    PrintF("stepstook=%.1f ",
           current_.incremental_marking_duration -
               previous_mark_compactor_event_.incremental_marking_duration);
    PrintF("longeststep=%.1f ", current_.longest_incremental_marking_step);
  }

  PrintF("\n");
}


double GCTracer::MeanDuration(const EventBuffer& events) const {
  if (events.empty()) return 0.0;

  double mean = 0.0;
  EventBuffer::const_iterator iter = events.begin();
  while (iter != events.end()) {
    mean += iter->end_time - iter->start_time;
    ++iter;
  }

  return mean / events.size();
}


double GCTracer::MaxDuration(const EventBuffer& events) const {
  if (events.empty()) return 0.0;

  double maximum = 0.0f;
  EventBuffer::const_iterator iter = events.begin();
  while (iter != events.end()) {
    maximum = Max(iter->end_time - iter->start_time, maximum);
    ++iter;
  }

  return maximum;
}


double GCTracer::MeanIncrementalMarkingDuration() const {
  if (mark_compactor_events_.empty()) return 0.0;

  EventBuffer::const_iterator last_mc = mark_compactor_events_.begin();
  return last_mc->incremental_marking_duration /
         last_mc->incremental_marking_steps;
}


double GCTracer::MaxIncrementalMarkingDuration() const {
  if (mark_compactor_events_.empty()) return 0.0;

  EventBuffer::const_iterator last_mc = mark_compactor_events_.begin();
  return last_mc->longest_incremental_marking_step;
}
}
}  // namespace v8::internal
