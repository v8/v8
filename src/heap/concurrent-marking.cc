// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/concurrent-marking.h"

#include <stack>
#include <unordered_map>

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/marking.h"
#include "src/isolate.h"
#include "src/locked-queue-inl.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

class ConcurrentMarkingMarkbits {
 public:
  ConcurrentMarkingMarkbits() {}
  ~ConcurrentMarkingMarkbits() {
    for (auto chunk_bitmap : bitmap_) {
      FreeBitmap(chunk_bitmap.second);
    }
  }
  bool Mark(HeapObject* obj) {
    Address address = obj->address();
    MemoryChunk* chunk = MemoryChunk::FromAddress(address);
    if (bitmap_.count(chunk) == 0) {
      bitmap_[chunk] = AllocateBitmap();
    }
    MarkBit mark_bit =
        bitmap_[chunk]->MarkBitFromIndex(chunk->AddressToMarkbitIndex(address));
    if (mark_bit.Get()) return false;
    mark_bit.Set();
    return true;
  }

  Bitmap* AllocateBitmap() {
    return static_cast<Bitmap*>(calloc(1, Bitmap::kSize));
  }

  void FreeBitmap(Bitmap* bitmap) { free(bitmap); }

 private:
  std::unordered_map<MemoryChunk*, Bitmap*> bitmap_;
};

class ConcurrentMarkingVisitor : public ObjectVisitor {
 public:
  ConcurrentMarkingVisitor() {}

  void VisitPointers(Object** start, Object** end) override {
    for (Object** p = start; p < end; p++) {
      if (!(*p)->IsHeapObject()) continue;
      MarkObject(HeapObject::cast(*p));
    }
  }

  void MarkObject(HeapObject* obj) {
    if (markbits_.Mark(obj)) {
      marking_stack_.push(obj);
    }
  }

  void MarkTransitively() {
    while (!marking_stack_.empty()) {
      HeapObject* obj = marking_stack_.top();
      marking_stack_.pop();
      obj->Iterate(this);
    }
  }

 private:
  std::stack<HeapObject*> marking_stack_;
  ConcurrentMarkingMarkbits markbits_;
};

class ConcurrentMarking::Task : public CancelableTask {
 public:
  Task(Heap* heap, std::vector<HeapObject*>* root_set,
       base::Semaphore* on_finish)
      : CancelableTask(heap->isolate()),
        heap_(heap),
        on_finish_(on_finish),
        root_set_(root_set) {}

  virtual ~Task() {}

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override {
    USE(heap_);
    for (HeapObject* obj : *root_set_) {
      marking_visitor_.MarkObject(obj);
    }
    marking_visitor_.MarkTransitively();
    on_finish_->Signal();
  }

  Heap* heap_;
  base::Semaphore* on_finish_;
  ConcurrentMarkingVisitor marking_visitor_;
  std::vector<HeapObject*>* root_set_;
  DISALLOW_COPY_AND_ASSIGN(Task);
};

ConcurrentMarking::ConcurrentMarking(Heap* heap)
    : heap_(heap), pending_task_(0) {}

ConcurrentMarking::~ConcurrentMarking() {}

void ConcurrentMarking::AddRoot(HeapObject* object) {
  root_set_.push_back(object);
}

void ConcurrentMarking::StartMarkingTask() {
  if (!FLAG_concurrent_marking) return;

  V8::GetCurrentPlatform()->CallOnBackgroundThread(
      new Task(heap_, &root_set_, &pending_task_),
      v8::Platform::kShortRunningTask);
}

void ConcurrentMarking::WaitForTaskToComplete() {
  if (!FLAG_concurrent_marking) return;
  pending_task_.Wait();
}

}  // namespace internal
}  // namespace v8
