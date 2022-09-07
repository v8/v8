// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/heap/heap-utils.h"

#include "src/heap/incremental-marking.h"
#include "src/heap/mark-compact.h"
#include "src/heap/safepoint.h"

namespace v8 {
namespace internal {

void HeapInternalsBase::SimulateIncrementalMarking(Heap* heap,
                                                   bool force_completion) {
  constexpr double kStepSizeInMs = 100;
  CHECK(v8_flags.incremental_marking);
  i::IncrementalMarking* marking = heap->incremental_marking();
  i::MarkCompactCollector* collector = heap->mark_compact_collector();

  if (collector->sweeping_in_progress()) {
    SafepointScope scope(heap);
    collector->EnsureSweepingCompleted(
        MarkCompactCollector::SweepingForcedFinalizationMode::kV8Only);
  }

  if (marking->IsStopped()) {
    heap->StartIncrementalMarking(i::Heap::kNoGCFlags,
                                  i::GarbageCollectionReason::kTesting);
  }
  CHECK(marking->IsMarking());
  if (!force_completion) return;

  while (!marking->IsMajorMarkingComplete()) {
    marking->AdvanceForTesting(kStepSizeInMs);
  }
}

void HeapInternalsBase::SimulateFullSpace(
    v8::internal::PagedNewSpace* space,
    std::vector<Handle<FixedArray>>* out_handles) {
  // If you see this check failing, disable the flag at the start of your test:
  // v8_flags.stress_concurrent_allocation = false;
  // Background thread allocating concurrently interferes with this function.
  CHECK(!v8_flags.stress_concurrent_allocation);
  Heap* heap = space->heap();
  if (heap->mark_compact_collector()->sweeping_in_progress()) {
    heap->mark_compact_collector()->EnsureSweepingCompleted(
        MarkCompactCollector::SweepingForcedFinalizationMode::kV8Only);
  }
  // MinorMC is atomic so need to ensure it is completed.

  Map unchecked_fixed_array_map =
      ReadOnlyRoots(heap).unchecked_fixed_array_map();
  PagedSpaceBase* paged_space = space->paged_space();
  paged_space->FreeLinearAllocationArea();
  FreeList* free_list = paged_space->free_list();
  free_list->ForAllFreeListCategories(
      [heap, paged_space, free_list, unchecked_fixed_array_map,
       out_handles](FreeListCategory* category) {
        // Remove category from the free list to remove it from the available
        // bytes count.
        free_list->RemoveCategory(category);
        // Create FixedArray objects in all free list entries.
        while (!category->is_empty()) {
          size_t node_size;
          FreeSpace node = category->PickNodeFromList(0, &node_size);
          DCHECK_LT(0, node_size);
          DCHECK_LE(node_size, std::numeric_limits<int>::max());
          // Zero the memory to "initialize" it for the FixedArray.
          memset(reinterpret_cast<void*>(node.address()), 0, node_size);
          Address address = node.address();
          Page* page = Page::FromAddress(address);
          // Fixedarray requires at least 2*kTaggedSize memory.
          while (node_size >= 2 * kTaggedSize) {
            // Don't create FixedArrays bigger than max normal object size.
            int array_size = std::min(static_cast<int>(node_size),
                                      kMaxRegularHeapObjectSize);
            // Convert the free space to a FixedArray
            HeapObject heap_object(HeapObject::FromAddress(address));
            heap_object.set_map_after_allocation(unchecked_fixed_array_map,
                                                 SKIP_WRITE_BARRIER);
            FixedArray arr(FixedArray::cast(heap_object));
            arr.set_length((array_size - FixedArray::SizeFor(0)) / kTaggedSize);
            DCHECK_EQ(array_size, arr.AllocatedSize());
            if (out_handles)
              out_handles->push_back(handle(arr, heap->isolate()));
            // Update allocated bytes statistics for the page and the space.
            page->IncreaseAllocatedBytes(array_size);
            paged_space->IncreaseAllocatedBytes(array_size, page);
            node_size -= array_size;
            address += array_size;
          }
          if (node_size > 0) {
            // Create a filler in any remaining memory.
            DCHECK_GT(2 * kTaggedSize, node_size);
            heap->CreateFillerObjectAt(address, static_cast<int>(node_size));
          }
        }
      });
  paged_space->ResetFreeList();
}

void HeapInternalsBase::SimulateFullSpace(
    v8::internal::NewSpace* space,
    std::vector<Handle<FixedArray>>* out_handles) {
  // If you see this check failing, disable the flag at the start of your test:
  // v8_flags.stress_concurrent_allocation = false;
  // Background thread allocating concurrently interferes with this function.
  CHECK(!v8_flags.stress_concurrent_allocation);
  if (v8_flags.minor_mc) {
    SimulateFullSpace(PagedNewSpace::From(space), out_handles);
  } else {
    while (FillCurrentPage(space, out_handles) || space->AddFreshPage()) {
    }
  }
}

void HeapInternalsBase::SimulateFullSpace(v8::internal::PagedSpace* space) {
  // If you see this check failing, disable the flag at the start of your test:
  // v8_flags.stress_concurrent_allocation = false;
  // Background thread allocating concurrently interferes with this function.
  CHECK(!v8_flags.stress_concurrent_allocation);
  CodePageCollectionMemoryModificationScopeForTesting code_scope(space->heap());
  i::MarkCompactCollector* collector = space->heap()->mark_compact_collector();
  if (collector->sweeping_in_progress()) {
    collector->EnsureSweepingCompleted(
        MarkCompactCollector::SweepingForcedFinalizationMode::kV8Only);
  }
  space->FreeLinearAllocationArea();
  space->ResetFreeList();
}

bool HeapInternalsBase::FillCurrentPage(
    v8::internal::NewSpace* space,
    std::vector<Handle<FixedArray>>* out_handles) {
  return FillCurrentPageButNBytes(space, 0, out_handles);
}

namespace {
int GetSpaceRemainingOnCurrentPage(v8::internal::NewSpace* space) {
  Address top = space->top();
  if ((top & kPageAlignmentMask) == 0) {
    // `top` points to the start of a page signifies that there is not room in
    // the current page.
    return 0;
  }
  return static_cast<int>(Page::FromAddress(space->top())->area_end() - top);
}
}  // namespace

bool HeapInternalsBase::FillCurrentPageButNBytes(
    v8::internal::NewSpace* space, int extra_bytes,
    std::vector<Handle<FixedArray>>* out_handles) {
  PauseAllocationObserversScope pause_observers(space->heap());
  // We cannot rely on `space->limit()` to point to the end of the current page
  // in the case where inline allocations are disabled, it actually points to
  // the current allocation pointer.
  DCHECK_IMPLIES(!space->IsInlineAllocationEnabled(),
                 space->limit() == space->top());
  int space_remaining = GetSpaceRemainingOnCurrentPage(space);
  CHECK(space_remaining >= extra_bytes);
  int new_linear_size = space_remaining - extra_bytes;
  if (new_linear_size == 0) return false;
  std::vector<Handle<FixedArray>> handles =
      CreatePadding(space->heap(), space_remaining, i::AllocationType::kYoung);
  if (out_handles != nullptr) {
    out_handles->insert(out_handles->end(), handles.begin(), handles.end());
  }
  return true;
}

int HeapInternalsBase::FixedArrayLenFromSize(int size) {
  return std::min({(size - FixedArray::kHeaderSize) / kTaggedSize,
                   FixedArray::kMaxRegularLength});
}

std::vector<Handle<FixedArray>> HeapInternalsBase::CreatePadding(
    Heap* heap, int padding_size, AllocationType allocation, int object_size) {
  std::vector<Handle<FixedArray>> handles;
  Isolate* isolate = heap->isolate();
  int allocate_memory;
  int length;
  int free_memory = padding_size;
  if (allocation == i::AllocationType::kOld) {
    heap->old_space()->FreeLinearAllocationArea();
    int overall_free_memory = static_cast<int>(heap->old_space()->Available());
    CHECK(padding_size <= overall_free_memory || overall_free_memory == 0);
  } else {
    int overall_free_memory = static_cast<int>(heap->new_space()->Available());
    CHECK(padding_size <= overall_free_memory || overall_free_memory == 0);
  }
  while (free_memory > 0) {
    if (free_memory > object_size) {
      allocate_memory = object_size;
      length = FixedArrayLenFromSize(allocate_memory);
    } else {
      allocate_memory = free_memory;
      length = FixedArrayLenFromSize(allocate_memory);
      if (length <= 0) {
        // Not enough room to create another FixedArray, so create a filler.
        if (allocation == i::AllocationType::kOld) {
          heap->CreateFillerObjectAt(
              *heap->old_space()->allocation_top_address(), free_memory);
        } else {
          heap->CreateFillerObjectAt(
              *heap->new_space()->allocation_top_address(), free_memory);
        }
        break;
      }
    }
    handles.push_back(isolate->factory()->NewFixedArray(length, allocation));
    CHECK((allocation == AllocationType::kYoung &&
           heap->new_space()->Contains(*handles.back())) ||
          (allocation == AllocationType::kOld &&
           heap->InOldSpace(*handles.back())) ||
          v8_flags.single_generation);
    free_memory -= handles.back()->Size();
  }
  return handles;
}

bool IsNewObjectInCorrectGeneration(HeapObject object) {
  return v8_flags.single_generation ? !i::Heap::InYoungGeneration(object)
                                    : i::Heap::InYoungGeneration(object);
}

}  // namespace internal
}  // namespace v8
