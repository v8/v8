// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-tracker.h"
#include "src/heap/array-buffer-tracker-inl.h"
#include "src/heap/heap.h"

namespace v8 {
namespace internal {

LocalArrayBufferTracker::~LocalArrayBufferTracker() {
  CHECK(array_buffers_.empty());
}

template <LocalArrayBufferTracker::FreeMode free_mode>
LocalArrayBufferTracker::ProcessResult LocalArrayBufferTracker::Free() {
  size_t freed_memory = 0;
  for (TrackingData::iterator it = array_buffers_.begin();
       it != array_buffers_.end();) {
    JSArrayBuffer* buffer = reinterpret_cast<JSArrayBuffer*>(it->first);
    if ((free_mode == kFreeAll) ||
        Marking::IsWhite(ObjectMarking::MarkBitFrom(buffer))) {
      const size_t len = it->second;
      heap_->isolate()->array_buffer_allocator()->Free(buffer->backing_store(),
                                                       len);
      freed_memory += len;
      it = array_buffers_.erase(it);
    } else {
      ++it;
    }
  }
  return ProcessResult(freed_memory, 0);
}

template <typename Callback>
LocalArrayBufferTracker::ProcessResult LocalArrayBufferTracker::Process(
    Callback callback) {
  JSArrayBuffer* new_buffer = nullptr;
  size_t freed_memory = 0;
  size_t promoted_memory = 0;
  size_t len = 0;
  Page* target_page = nullptr;
  LocalArrayBufferTracker* tracker = nullptr;
  for (TrackingData::iterator it = array_buffers_.begin();
       it != array_buffers_.end();) {
    switch (callback(it->first, &new_buffer)) {
      case kKeepEntry:
        ++it;
        break;
      case kUpdateEntry:
        DCHECK_NOT_NULL(new_buffer);
        target_page = Page::FromAddress(new_buffer->address());
        // We need to lock the target page because we cannot guarantee
        // exclusive access to new space pages.
        if (target_page->InNewSpace()) target_page->mutex()->Lock();
        tracker = target_page->local_tracker();
        if (tracker == nullptr) {
          target_page->AllocateLocalTracker();
          tracker = target_page->local_tracker();
        }
        DCHECK_NOT_NULL(tracker);
        len = it->second;
        tracker->Add(new_buffer, len);
        if (target_page->InNewSpace()) {
          target_page->mutex()->Unlock();
        } else {
          promoted_memory += len;
        }
        it = array_buffers_.erase(it);
        break;
      case kRemoveEntry:
        len = it->second;
        heap_->isolate()->array_buffer_allocator()->Free(
            it->first->backing_store(), len);
        freed_memory += len;
        it = array_buffers_.erase(it);
        break;
    }
  }
  return ProcessResult(freed_memory, promoted_memory);
}

void ArrayBufferTracker::AccountForConcurrentlyFreedMemory() {
  heap_->update_external_memory(
      static_cast<int64_t>(concurrently_freed_.Value()));
  concurrently_freed_.SetValue(0);
}

void ArrayBufferTracker::FreeDeadInNewSpace() {
  DCHECK_EQ(heap_->gc_state(), Heap::HeapState::SCAVENGE);
  for (Page* page : NewSpacePageRange(heap_->new_space()->FromSpaceStart(),
                                      heap_->new_space()->FromSpaceEnd())) {
    bool empty = ProcessBuffers(page, kUpdateForwardedRemoveOthers);
    CHECK(empty);
  }
  AccountForConcurrentlyFreedMemory();
}

void ArrayBufferTracker::FreeDead(Page* page) {
  // Callers need to ensure having the page lock.
  LocalArrayBufferTracker* tracker = page->local_tracker();
  if (tracker == nullptr) return;
  DCHECK(!page->SweepingDone());
  LocalArrayBufferTracker::ProcessResult result =
      tracker->Free<LocalArrayBufferTracker::kFreeDead>();
  if (page->InNewSpace()) {
    retained_from_new_space_.Decrement(result.freed);
  } else {
    retained_from_old_space_.Decrement(result.freed);
  }
  if (tracker->IsEmpty()) {
    page->ReleaseLocalTracker();
  }
}

void ArrayBufferTracker::FreeAll(Page* page) {
  LocalArrayBufferTracker* tracker = page->local_tracker();
  if (tracker == nullptr) return;
  LocalArrayBufferTracker::ProcessResult result =
      tracker->Free<LocalArrayBufferTracker::kFreeAll>();
  concurrently_freed_.Increment(result.freed);
  if (page->InNewSpace()) {
    retained_from_new_space_.Decrement(result.freed);
  } else {
    retained_from_old_space_.Decrement(result.freed);
  }
  if (tracker->IsEmpty()) {
    page->ReleaseLocalTracker();
  }
}

bool ArrayBufferTracker::ProcessBuffers(Page* page, ProcessingMode mode) {
  LocalArrayBufferTracker* tracker = page->local_tracker();
  if (tracker == nullptr) return true;

  DCHECK(page->SweepingDone());
  LocalArrayBufferTracker::ProcessResult result = tracker->Process(
      [mode](JSArrayBuffer* old_buffer, JSArrayBuffer** new_buffer) {
        MapWord map_word = old_buffer->map_word();
        if (map_word.IsForwardingAddress()) {
          *new_buffer = JSArrayBuffer::cast(map_word.ToForwardingAddress());
          return LocalArrayBufferTracker::kUpdateEntry;
        }
        return mode == kUpdateForwardedKeepOthers
                   ? LocalArrayBufferTracker::kKeepEntry
                   : LocalArrayBufferTracker::kRemoveEntry;
      });
  concurrently_freed_.Increment(result.freed);
  if (page->InNewSpace()) {
    retained_from_new_space_.Decrement(result.freed + result.promoted);
  } else {
    retained_from_old_space_.Decrement(result.freed);
  }
  retained_from_old_space_.Increment(result.promoted);
  return tracker->IsEmpty();
}

bool ArrayBufferTracker::IsTracked(JSArrayBuffer* buffer) {
  Page* page = Page::FromAddress(buffer->address());
  {
    base::LockGuard<base::Mutex> guard(page->mutex());
    LocalArrayBufferTracker* tracker = page->local_tracker();
    if (tracker == nullptr) return false;
    return tracker->IsTracked(buffer);
  }
}

}  // namespace internal
}  // namespace v8
