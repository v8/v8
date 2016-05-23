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

void ArrayBufferTracker::RegisterNew(JSArrayBuffer* buffer, bool track_live) {
  void* data = buffer->backing_store();
  if (!data) return;

  size_t length = NumberToSize(heap_->isolate(), buffer->byte_length());
  Page* page = Page::FromAddress(buffer->address());
  if (track_live) {
    page->local_tracker()->AddLive(buffer, std::make_pair(data, length));
  } else {
    page->local_tracker()->Add(buffer, std::make_pair(data, length));
  }
  // We may go over the limit of externally allocated memory here. We call the
  // api function to trigger a GC in this case.
  reinterpret_cast<v8::Isolate*>(heap_->isolate())
      ->AdjustAmountOfExternalAllocatedMemory(length);
}


void ArrayBufferTracker::Unregister(JSArrayBuffer* buffer) {
  void* data = buffer->backing_store();
  if (!data) return;

  size_t length = Page::FromAddress(buffer->address())
                      ->local_tracker()
                      ->Remove(buffer)
                      .second;
  heap_->update_amount_of_external_allocated_memory(
      -static_cast<intptr_t>(length));
}

void ArrayBufferTracker::FreeDeadInNewSpace() {
  NewSpacePageIterator from_it(heap_->new_space()->FromSpaceStart(),
                               heap_->new_space()->FromSpaceEnd());
  while (from_it.has_next()) {
    Page* p = from_it.next();
    p->ScanAndFreeDeadArrayBuffers<
        LocalArrayBufferTracker::kForwardingPointer>();
  }
  heap_->account_amount_of_external_allocated_freed_memory();
}

void ArrayBufferTracker::ResetTrackersInOldSpace() {
  heap_->old_space()->ForAllPages([](Page* p) { p->ResetTracker(); });
}

#define UPDATE_GUARD(buffer, data)   \
  if (buffer->is_external()) return; \
  data = buffer->backing_store();    \
  if (data == nullptr) return;       \
  if (data == heap_->undefined_value()) return;

void ArrayBufferTracker::MarkLive(JSArrayBuffer* buffer) {
  void* data = nullptr;
  UPDATE_GUARD(buffer, data);

  LocalArrayBufferTracker* tracker =
      Page::FromAddress(buffer->address())->local_tracker();
  if (tracker->IsTracked(buffer)) {
    tracker->MarkLive((buffer));
  } else {
    heap_->RegisterNewArrayBuffer(buffer);
  }
}

#undef UPDATE_GUARD

}  // namespace internal
}  // namespace v8
