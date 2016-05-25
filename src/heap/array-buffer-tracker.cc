// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-tracker.h"
#include "src/heap/array-buffer-tracker-inl.h"
#include "src/heap/heap.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

LocalArrayBufferTracker::~LocalArrayBufferTracker() {
  size_t freed_memory = 0;
  for (auto& buffer : live_) {
    heap_->isolate()->array_buffer_allocator()->Free(buffer.second.first,
                                                     buffer.second.second);
    freed_memory += buffer.second.second;
  }
  if (freed_memory > 0) {
    heap_->update_amount_of_external_allocated_freed_memory(
        static_cast<intptr_t>(freed_memory));
  }
  live_.clear();
  not_yet_discovered_.clear();
}

void LocalArrayBufferTracker::Add(Key key, const Value& value) {
  live_[key] = value;
  not_yet_discovered_[key] = value;
}

void LocalArrayBufferTracker::AddLive(Key key, const Value& value) {
  DCHECK_EQ(not_yet_discovered_.count(key), 0);
  live_[key] = value;
}

void LocalArrayBufferTracker::MarkLive(Key key) {
  DCHECK_EQ(live_.count(key), 1);
  not_yet_discovered_.erase(key);
}

LocalArrayBufferTracker::Value LocalArrayBufferTracker::Remove(Key key) {
  DCHECK_EQ(live_.count(key), 1);
  Value value = live_[key];
  live_.erase(key);
  not_yet_discovered_.erase(key);
  return value;
}

void LocalArrayBufferTracker::FreeDead() {
  size_t freed_memory = 0;
  for (TrackingMap::iterator it = not_yet_discovered_.begin();
       it != not_yet_discovered_.end();) {
    heap_->isolate()->array_buffer_allocator()->Free(it->second.first,
                                                     it->second.second);
    freed_memory += it->second.second;
    live_.erase(it->first);
    not_yet_discovered_.erase(it++);
  }
  if (freed_memory > 0) {
    heap_->update_amount_of_external_allocated_freed_memory(
        static_cast<intptr_t>(freed_memory));
  }
  started_ = false;
}

void LocalArrayBufferTracker::Reset() {
  if (!started_) {
    not_yet_discovered_ = live_;
    started_ = true;
  }
}

bool LocalArrayBufferTracker::IsEmpty() {
  return live_.empty() && not_yet_discovered_.empty();
}

ArrayBufferTracker::~ArrayBufferTracker() {}

void ArrayBufferTracker::RegisterNew(JSArrayBuffer* buffer) {
  void* data = buffer->backing_store();
  if (!data) return;

  size_t length = NumberToSize(heap_->isolate(), buffer->byte_length());
  Page* page = Page::FromAddress(buffer->address());
  LocalArrayBufferTracker* tracker =
      page->local_tracker<Page::kCreateIfNotPresent>();
  DCHECK_NOT_NULL(tracker);
  {
    base::LockGuard<base::Mutex> guard(tracker->mutex());
    if (Marking::IsBlack(Marking::MarkBitFrom(buffer))) {
      tracker->AddLive(buffer, std::make_pair(data, length));
    } else {
      tracker->Add(buffer, std::make_pair(data, length));
    }
  }
  // We may go over the limit of externally allocated memory here. We call the
  // api function to trigger a GC in this case.
  reinterpret_cast<v8::Isolate*>(heap_->isolate())
      ->AdjustAmountOfExternalAllocatedMemory(length);
}


void ArrayBufferTracker::Unregister(JSArrayBuffer* buffer) {
  void* data = buffer->backing_store();
  if (!data) return;

  LocalArrayBufferTracker* tracker =
      Page::FromAddress(buffer->address())->local_tracker<Page::kDontCreate>();
  DCHECK_NOT_NULL(tracker);
  size_t length = 0;
  {
    base::LockGuard<base::Mutex> guard(tracker->mutex());
    length = tracker->Remove(buffer).second;
  }
  heap_->update_amount_of_external_allocated_memory(
      -static_cast<intptr_t>(length));
}

void ArrayBufferTracker::FreeDeadInNewSpace() {
  NewSpacePageIterator from_it(heap_->new_space()->FromSpaceStart(),
                               heap_->new_space()->FromSpaceEnd());
  while (from_it.has_next()) {
    ScanAndFreeDeadArrayBuffers<LocalArrayBufferTracker::kForwardingPointer>(
        from_it.next());
  }
  heap_->account_amount_of_external_allocated_freed_memory();
}

void ArrayBufferTracker::ResetTrackersInOldSpace() {
  heap_->old_space()->ForAllPages([](Page* p) {
    LocalArrayBufferTracker* tracker = p->local_tracker<Page::kDontCreate>();
    if (tracker != nullptr) {
      tracker->Reset();
      if (tracker->IsEmpty()) {
        p->ReleaseLocalTracker();
      }
    }
  });
}

void ArrayBufferTracker::MarkLive(JSArrayBuffer* buffer) {
  if (buffer->is_external()) return;
  void* data = buffer->backing_store();
  if (data == nullptr) return;
  if (data == heap_->undefined_value()) return;

  Page* page = Page::FromAddress(buffer->address());
  LocalArrayBufferTracker* tracker =
      page->local_tracker<Page::kCreateIfNotPresent>();
  DCHECK_NOT_NULL(tracker);
  if (tracker->IsTracked(buffer)) {
    base::LockGuard<base::Mutex> guard(page->mutex());
    tracker->MarkLive((buffer));
  } else {
    RegisterNew(buffer);
  }
}

void ArrayBufferTracker::FreeDead(Page* page) {
  LocalArrayBufferTracker* tracker = page->local_tracker<Page::kDontCreate>();
  if (tracker != nullptr) {
    base::LockGuard<base::Mutex> guard(tracker->mutex());
    tracker->FreeDead();
  }
}

template <LocalArrayBufferTracker::LivenessIndicator liveness_indicator>
void ArrayBufferTracker::ScanAndFreeDeadArrayBuffers(Page* page) {
  LocalArrayBufferTracker* tracker = page->local_tracker<Page::kDontCreate>();
  if (tracker != nullptr) {
    base::LockGuard<base::Mutex> guard(tracker->mutex());
    tracker->ScanAndFreeDead<liveness_indicator>();
  }
}

template void ArrayBufferTracker::ScanAndFreeDeadArrayBuffers<
    LocalArrayBufferTracker::LivenessIndicator::kForwardingPointer>(Page* page);
template void ArrayBufferTracker::ScanAndFreeDeadArrayBuffers<
    LocalArrayBufferTracker::LivenessIndicator::kMarkBit>(Page* page);

}  // namespace internal
}  // namespace v8
