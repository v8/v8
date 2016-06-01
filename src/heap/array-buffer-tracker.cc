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
  size_t freed_memory = 0;
  for (auto& buffer : array_buffers_) {
    heap_->isolate()->array_buffer_allocator()->Free(buffer.second.first,
                                                     buffer.second.second);
    freed_memory += buffer.second.second;
  }
  if (freed_memory > 0) {
    heap_->update_amount_of_external_allocated_freed_memory(
        static_cast<intptr_t>(freed_memory));
  }
  array_buffers_.clear();
}

void LocalArrayBufferTracker::Add(Key key, const Value& value) {
  array_buffers_[key] = value;
}

LocalArrayBufferTracker::Value LocalArrayBufferTracker::Remove(Key key) {
  DCHECK_EQ(array_buffers_.count(key), 1);
  Value value = array_buffers_[key];
  array_buffers_.erase(key);
  return value;
}

void LocalArrayBufferTracker::FreeDead() {
  size_t freed_memory = 0;
  for (TrackingMap::iterator it = array_buffers_.begin();
       it != array_buffers_.end();) {
    if (Marking::IsWhite(Marking::MarkBitFrom(it->first))) {
      heap_->isolate()->array_buffer_allocator()->Free(it->second.first,
                                                       it->second.second);
      freed_memory += it->second.second;
      array_buffers_.erase(it++);
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
    switch (callback(it->first, &new_buffer)) {
      case kKeepEntry:
        it++;
        break;
      case kUpdateEntry:
        DCHECK_NOT_NULL(new_buffer);
        Page::FromAddress(new_buffer->address())
            ->local_tracker<Page::kCreateIfNotPresent>()
            ->Add(new_buffer, it->second);
        array_buffers_.erase(it++);
        break;
      case kRemoveEntry:
        heap_->isolate()->array_buffer_allocator()->Free(it->second.first,
                                                         it->second.second);
        freed_memory += it->second.second;
        array_buffers_.erase(it++);
        break;
      default:
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
  LocalArrayBufferTracker* tracker =
      page->local_tracker<Page::kCreateIfNotPresent>();
  DCHECK_NOT_NULL(tracker);
  {
    base::LockGuard<base::Mutex> guard(page->mutex());
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
  LocalArrayBufferTracker* tracker = page->local_tracker<Page::kDontCreate>();
  DCHECK_NOT_NULL(tracker);
  size_t length = 0;
  {
    base::LockGuard<base::Mutex> guard(page->mutex());
    length = tracker->Remove(buffer).second;
  }
  heap->update_amount_of_external_allocated_memory(
      -static_cast<intptr_t>(length));
}

void ArrayBufferTracker::FreeDeadInNewSpace(Heap* heap) {
  NewSpacePageIterator from_it(heap->new_space()->FromSpaceStart(),
                               heap->new_space()->FromSpaceEnd());
  while (from_it.has_next()) {
    ProcessBuffers(from_it.next(), kUpdateForwardedRemoveOthers);
  }
  heap->account_amount_of_external_allocated_freed_memory();
}

void ArrayBufferTracker::FreeDead(Page* page) {
  // Only called from the sweeper, which already holds the page lock.
  LocalArrayBufferTracker* tracker = page->local_tracker<Page::kDontCreate>();
  if (tracker == nullptr) return;
  DCHECK(!page->SweepingDone());
  tracker->FreeDead();
  if (tracker->IsEmpty()) {
    page->ReleaseLocalTracker();
  }
}

void ArrayBufferTracker::ProcessBuffers(Page* page, ProcessingMode mode) {
  LocalArrayBufferTracker* tracker = page->local_tracker<Page::kDontCreate>();
  if (tracker == nullptr) return;
  {
    base::LockGuard<base::Mutex> guard(page->mutex());
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
  }
}

bool ArrayBufferTracker::IsTracked(JSArrayBuffer* buffer) {
  Page* page = Page::FromAddress(buffer->address());
  LocalArrayBufferTracker* tracker =
      page->local_tracker<Page::kCreateIfNotPresent>();
  {
    base::LockGuard<base::Mutex> guard(page->mutex());
    return tracker->IsTracked(buffer);
  }
}

}  // namespace internal
}  // namespace v8
