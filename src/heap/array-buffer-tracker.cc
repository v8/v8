// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-tracker.h"
#include "src/heap/heap.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

LocalArrayBufferTracker::~LocalArrayBufferTracker() {
  CHECK(array_buffers_.empty());
}

void LocalArrayBufferTracker::Add(Key key, const Value& value) {
  auto ret = array_buffers_.insert(std::make_pair(key, value));
  USE(ret);
  // Check that we indeed inserted a new value and did not overwrite an existing
  // one (which would be a bug).
  DCHECK(ret.second);
}

LocalArrayBufferTracker::Value LocalArrayBufferTracker::Remove(Key key) {
  TrackingMap::iterator it = array_buffers_.find(key);
  DCHECK(it != array_buffers_.end());
  Value value = it->second;
  array_buffers_.erase(it);
  return value;
}

template <LocalArrayBufferTracker::FreeMode free_mode>
void LocalArrayBufferTracker::Free() {
  size_t freed_memory = 0;
  for (TrackingMap::iterator it = array_buffers_.begin();
       it != array_buffers_.end();) {
    if ((free_mode == kFreeAll) ||
        Marking::IsWhite(Marking::MarkBitFrom(it->first))) {
      heap_->isolate()->array_buffer_allocator()->Free(it->second.first,
                                                       it->second.second);
      freed_memory += it->second.second;
      it = array_buffers_.erase(it);
    } else {
      it++;
    }
  }
  if (freed_memory > 0) {
    heap_->update_amount_of_external_allocated_freed_memory(
        static_cast<intptr_t>(freed_memory));
  }
}

template <typename Callback>
void LocalArrayBufferTracker::Process(Callback callback) {
  JSArrayBuffer* new_buffer = nullptr;
  size_t freed_memory = 0;
  for (TrackingMap::iterator it = array_buffers_.begin();
       it != array_buffers_.end();) {
    const CallbackResult result = callback(it->first, &new_buffer);
    if (result == kKeepEntry) {
      it++;
    } else if (result == kUpdateEntry) {
      DCHECK_NOT_NULL(new_buffer);
      Page* target_page = Page::FromAddress(new_buffer->address());
      // We need to lock the target page because we cannot guarantee
      // exclusive access to new space pages.
      if (target_page->InNewSpace()) target_page->mutex()->Lock();
      LocalArrayBufferTracker* tracker = target_page->local_tracker();
      if (tracker == nullptr) {
        target_page->AllocateLocalTracker();
        tracker = target_page->local_tracker();
      }
      DCHECK_NOT_NULL(tracker);
      tracker->Add(new_buffer, it->second);
      if (target_page->InNewSpace()) target_page->mutex()->Unlock();
      it = array_buffers_.erase(it);
    } else if (result == kRemoveEntry) {
      heap_->isolate()->array_buffer_allocator()->Free(it->second.first,
                                                       it->second.second);
      freed_memory += it->second.second;
      it = array_buffers_.erase(it);
    } else {
      UNREACHABLE();
    }
  }
  if (freed_memory > 0) {
    heap_->update_amount_of_external_allocated_freed_memory(
        static_cast<intptr_t>(freed_memory));
  }
}

void ArrayBufferTracker::RegisterNew(Heap* heap, JSArrayBuffer* buffer) {
  void* data = buffer->backing_store();
  if (!data) return;

  size_t length = NumberToSize(heap->isolate(), buffer->byte_length());
  Page* page = Page::FromAddress(buffer->address());
  {
    base::LockGuard<base::Mutex> guard(page->mutex());
    LocalArrayBufferTracker* tracker = page->local_tracker();
    if (tracker == nullptr) {
      page->AllocateLocalTracker();
      tracker = page->local_tracker();
    }
    DCHECK_NOT_NULL(tracker);
    tracker->Add(buffer, std::make_pair(data, length));
  }
  // We may go over the limit of externally allocated memory here. We call the
  // api function to trigger a GC in this case.
  reinterpret_cast<v8::Isolate*>(heap->isolate())
      ->AdjustAmountOfExternalAllocatedMemory(length);
}

void ArrayBufferTracker::Unregister(Heap* heap, JSArrayBuffer* buffer) {
  void* data = buffer->backing_store();
  if (!data) return;

  Page* page = Page::FromAddress(buffer->address());
  size_t length = 0;
  {
    base::LockGuard<base::Mutex> guard(page->mutex());
    LocalArrayBufferTracker* tracker = page->local_tracker();
    DCHECK_NOT_NULL(tracker);
    length = tracker->Remove(buffer).second;
  }
  heap->update_amount_of_external_allocated_memory(
      -static_cast<intptr_t>(length));
}

void ArrayBufferTracker::FreeDeadInNewSpace(Heap* heap) {
  DCHECK_EQ(heap->gc_state(), Heap::HeapState::SCAVENGE);
  NewSpacePageIterator from_it(heap->new_space()->FromSpaceStart(),
                               heap->new_space()->FromSpaceEnd());
  while (from_it.has_next()) {
    Page* page = from_it.next();
    bool empty = ProcessBuffers(page, kUpdateForwardedRemoveOthers);
    CHECK(empty);
  }
  heap->account_amount_of_external_allocated_freed_memory();
}

void ArrayBufferTracker::FreeDead(Page* page) {
  // Callers need to ensure having the page lock.
  LocalArrayBufferTracker* tracker = page->local_tracker();
  if (tracker == nullptr) return;
  DCHECK(!page->SweepingDone());
  tracker->Free<LocalArrayBufferTracker::kFreeDead>();
  if (tracker->IsEmpty()) {
    page->ReleaseLocalTracker();
  }
}

void ArrayBufferTracker::FreeAll(Page* page) {
  LocalArrayBufferTracker* tracker = page->local_tracker();
  if (tracker == nullptr) return;
  tracker->Free<LocalArrayBufferTracker::kFreeAll>();
  if (tracker->IsEmpty()) {
    page->ReleaseLocalTracker();
  }
}

bool ArrayBufferTracker::ProcessBuffers(Page* page, ProcessingMode mode) {
  LocalArrayBufferTracker* tracker = page->local_tracker();
  if (tracker == nullptr) return true;

  DCHECK(page->SweepingDone());
  tracker->Process(
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
