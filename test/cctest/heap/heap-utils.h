// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEAP_HEAP_UTILS_H_
#define HEAP_HEAP_UTILS_H_

#include "src/api/api-inl.h"
#include "src/heap/heap.h"

namespace v8::internal {

#if V8_STATIC_READ_ONLY_HEAP_LIMIT_BOOL

class ReadOnlySpaceTesting {
 public:
  static ReadOnlySpaceTesting allow_create_pages_above_limit() {
    return ReadOnlySpaceTesting(true);
  }

  ~ReadOnlySpaceTesting() {
    ReadOnlySpace::ForTesting::allow_create_pages_above_limit_ =
        old_allow_create_pages_above_limit_;
  }

 private:
  ReadOnlySpaceTesting() = delete;

  bool old_allow_create_pages_above_limit_;

  explicit ReadOnlySpaceTesting(bool allow_create_pages_above_limit)
      : old_allow_create_pages_above_limit_(
            ReadOnlySpace::ForTesting::allow_create_pages_above_limit_) {
    ReadOnlySpace::ForTesting::allow_create_pages_above_limit_ =
        allow_create_pages_above_limit;
  }
};

#endif  // V8_STATIC_READ_ONLY_HEAP_LIMIT

namespace heap {

void SealCurrentObjects(Heap* heap);

int FixedArrayLenFromSize(int size);

// Fill a page with fixed arrays leaving remainder behind. The function does
// not create additional fillers and assumes that the space has just been
// sealed.
std::vector<Handle<FixedArray>> FillOldSpacePageWithFixedArrays(Heap* heap,
                                                                int remainder);

std::vector<Handle<FixedArray>> CreatePadding(
    Heap* heap, int padding_size, AllocationType allocation,
    int object_size = kMaxRegularHeapObjectSize);

void FillCurrentPage(v8::internal::NewSpace* space,
                     std::vector<Handle<FixedArray>>* out_handles = nullptr);

void FillCurrentPageButNBytes(
    v8::internal::NewSpace* space, int extra_bytes,
    std::vector<Handle<FixedArray>>* out_handles = nullptr);

// Helper function that simulates many incremental marking steps until
// marking is completed.
void SimulateIncrementalMarking(i::Heap* heap, bool force_completion = true);

// Helper function that simulates a full old-space in the heap.
void SimulateFullSpace(v8::internal::PagedSpace* space);

void AbandonCurrentlyFreeMemory(PagedSpace* space);

void GcAndSweep(Heap* heap, AllocationSpace space);

void ForceEvacuationCandidate(Page* page);

void InvokeScavenge(Isolate* isolate = nullptr);

void InvokeMarkSweep(Isolate* isolate = nullptr);

void GrowNewSpace(Heap* heap);

void GrowNewSpaceToMaximumCapacity(Heap* heap);

template <typename GlobalOrPersistent>
bool InYoungGeneration(v8::Isolate* isolate, const GlobalOrPersistent& global) {
  v8::HandleScope scope(isolate);
  auto tmp = global.Get(isolate);
  return i::Heap::InYoungGeneration(*v8::Utils::OpenHandle(*tmp));
}

bool InCorrectGeneration(HeapObject object);

template <typename GlobalOrPersistent>
bool InCorrectGeneration(v8::Isolate* isolate,
                         const GlobalOrPersistent& global) {
  v8::HandleScope scope(isolate);
  auto tmp = global.Get(isolate);
  return InCorrectGeneration(*v8::Utils::OpenHandle(*tmp));
}

}  // namespace heap
}  // namespace v8::internal

#endif  // HEAP_HEAP_UTILS_H_
