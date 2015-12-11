// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEAP_UTILS_H_
#define HEAP_UTILS_H_

#include "src/factory.h"
#include "src/heap/heap-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/mark-compact.h"
#include "src/isolate.h"


namespace v8 {
namespace internal {

static int LenFromSize(int size) {
  return (size - i::FixedArray::kHeaderSize) / i::kPointerSize;
}


static inline void CreatePadding(i::Heap* heap, int padding_size,
                                 i::PretenureFlag tenure) {
  const int max_number_of_objects = 20;
  v8::internal::Handle<v8::internal::FixedArray>
      big_objects[max_number_of_objects];
  i::Isolate* isolate = heap->isolate();
  int allocate_memory;
  int length;
  int free_memory = padding_size;
  if (tenure == i::TENURED) {
    int current_free_memory =
        static_cast<int>(*heap->old_space()->allocation_limit_address() -
                         *heap->old_space()->allocation_top_address());
    CHECK(padding_size <= current_free_memory || current_free_memory == 0);
  } else {
    heap->new_space()->DisableInlineAllocationSteps();
    int current_free_memory =
        static_cast<int>(*heap->new_space()->allocation_limit_address() -
                         *heap->new_space()->allocation_top_address());
    CHECK(padding_size <= current_free_memory || current_free_memory == 0);
  }
  for (int i = 0; i < max_number_of_objects && free_memory > 0; i++) {
    if (free_memory > i::Page::kMaxRegularHeapObjectSize) {
      allocate_memory = i::Page::kMaxRegularHeapObjectSize;
      length = LenFromSize(allocate_memory);
    } else {
      allocate_memory = free_memory;
      length = LenFromSize(allocate_memory);
      if (length <= 0) {
        // Not enough room to create another fixed array. Let's create a filler.
        heap->CreateFillerObjectAt(*heap->old_space()->allocation_top_address(),
                                   free_memory);
        break;
      }
    }
    big_objects[i] = isolate->factory()->NewFixedArray(length, tenure);
    CHECK((tenure == i::NOT_TENURED && heap->InNewSpace(*big_objects[i])) ||
          (tenure == i::TENURED && heap->InOldSpace(*big_objects[i])));
    free_memory -= allocate_memory;
  }
}


// Helper function that simulates a full new-space in the heap.
static inline bool FillUpOnePage(v8::internal::NewSpace* space) {
  space->DisableInlineAllocationSteps();
  int space_remaining = static_cast<int>(*space->allocation_limit_address() -
                                         *space->allocation_top_address());
  if (space_remaining == 0) return false;
  CreatePadding(space->heap(), space_remaining, i::NOT_TENURED);
  return true;
}


// Helper function that simulates a fill new-space in the heap.
static inline void AllocateAllButNBytes(v8::internal::NewSpace* space,
                                        int extra_bytes) {
  space->DisableInlineAllocationSteps();
  int space_remaining = static_cast<int>(*space->allocation_limit_address() -
                                         *space->allocation_top_address());
  CHECK(space_remaining >= extra_bytes);
  int new_linear_size = space_remaining - extra_bytes;
  if (new_linear_size == 0) return;
  CreatePadding(space->heap(), new_linear_size, i::NOT_TENURED);
}


static inline void FillCurrentPage(v8::internal::NewSpace* space) {
  AllocateAllButNBytes(space, 0);
}


static inline void SimulateFullSpace(v8::internal::NewSpace* space) {
  FillCurrentPage(space);
  while (FillUpOnePage(space)) {
  }
}


// Helper function that simulates a full old-space in the heap.
static inline void SimulateFullSpace(v8::internal::PagedSpace* space) {
  space->EmptyAllocationInfo();
  space->ResetFreeList();
  space->ClearStats();
}


// Helper function that simulates many incremental marking steps until
// marking is completed.
static inline void SimulateIncrementalMarking(i::Heap* heap,
                                              bool force_completion = true) {
  i::MarkCompactCollector* collector = heap->mark_compact_collector();
  i::IncrementalMarking* marking = heap->incremental_marking();
  if (collector->sweeping_in_progress()) {
    collector->EnsureSweepingCompleted();
  }
  CHECK(marking->IsMarking() || marking->IsStopped());
  if (marking->IsStopped()) {
    heap->StartIncrementalMarking();
  }
  CHECK(marking->IsMarking());
  if (!force_completion) return;

  while (!marking->IsComplete()) {
    marking->Step(i::MB, i::IncrementalMarking::NO_GC_VIA_STACK_GUARD);
    if (marking->IsReadyToOverApproximateWeakClosure()) {
      marking->FinalizeIncrementally();
    }
  }
  CHECK(marking->IsComplete());
}

}  // namespace internal
}  // namespace v8

#endif  // HEAP_UTILS_H_
