// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-tracker.h"
#include "src/heap/heap.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "src/objects-inl.h"
#include "src/v8.h"

namespace v8 {
namespace internal {

ArrayBufferTracker::~ArrayBufferTracker() {
  Isolate* isolate = heap()->isolate();
  size_t freed_memory = 0;
  for (auto& buffer : live_old_gen_) {
    isolate->array_buffer_allocator()->Free(buffer.second.first,
                                            buffer.second.second);
    freed_memory += buffer.second.second;
  }
  for (auto& buffer : live_young_gen_) {
    isolate->array_buffer_allocator()->Free(buffer.second.first,
                                            buffer.second.second);
    freed_memory += buffer.second.second;
  }
  live_old_gen_.clear();
  live_young_gen_.clear();
  not_yet_discovered_old_gen_.clear();
  not_yet_discovered_young_gen_.clear();

  if (freed_memory > 0) {
    heap()->update_amount_of_external_allocated_memory(
        -static_cast<int64_t>(freed_memory));
  }
}


void ArrayBufferTracker::RegisterNew(JSArrayBuffer* buffer) {
  void* data = buffer->backing_store();
  if (!data) return;

  bool in_new_space = heap()->InNewSpace(buffer);
  size_t length = NumberToSize(heap()->isolate(), buffer->byte_length());
  if (in_new_space) {
    live_young_gen_[buffer->address()] = std::make_pair(data, length);
    not_yet_discovered_young_gen_[buffer->address()] =
        std::make_pair(data, length);
  } else {
    live_old_gen_[buffer->address()] = std::make_pair(data, length);
    not_yet_discovered_old_gen_[buffer->address()] =
        std::make_pair(data, length);
  }

  // We may go over the limit of externally allocated memory here. We call the
  // api function to trigger a GC in this case.
  reinterpret_cast<v8::Isolate*>(heap()->isolate())
      ->AdjustAmountOfExternalAllocatedMemory(length);
}


void ArrayBufferTracker::Unregister(JSArrayBuffer* buffer) {
  void* data = buffer->backing_store();
  if (!data) return;

  bool in_new_space = heap()->InNewSpace(buffer);
  Key key = buffer->address();
  TrackingMap* live_buffers = in_new_space ? &live_young_gen_ : &live_old_gen_;
  TrackingMap* not_yet_discovered_buffers = in_new_space
                                                ? &not_yet_discovered_young_gen_
                                                : &not_yet_discovered_old_gen_;

  DCHECK(live_buffers->count(key) > 0);

  size_t length = (*live_buffers)[key].second;
  live_buffers->erase(key);
  not_yet_discovered_buffers->erase(key);

  heap()->update_amount_of_external_allocated_memory(
      -static_cast<int64_t>(length));
}

void ArrayBufferTracker::FreeDead(bool from_scavenge) {
  size_t freed_memory = 0;
  Isolate* isolate = heap()->isolate();
  for (auto& buffer : not_yet_discovered_young_gen_) {
    isolate->array_buffer_allocator()->Free(buffer.second.first,
                                            buffer.second.second);
    freed_memory += buffer.second.second;
    live_young_gen_.erase(buffer.first);
  }

  if (!from_scavenge) {
    for (auto& buffer : not_yet_discovered_old_gen_) {
      isolate->array_buffer_allocator()->Free(buffer.second.first,
                                              buffer.second.second);
      freed_memory += buffer.second.second;
      live_old_gen_.erase(buffer.first);
    }
  }

  not_yet_discovered_young_gen_ = live_young_gen_;
  if (!from_scavenge) not_yet_discovered_old_gen_ = live_old_gen_;

  // Do not call through the api as this code is triggered while doing a GC.
  heap()->update_amount_of_external_allocated_memory(
      -static_cast<int64_t>(freed_memory));
}

#define UPDATE_GUARD(buffer, data)               \
  if (buffer->is_external()) return;             \
  data = buffer->backing_store();                \
  if (data == nullptr) return;                   \
  if (data == heap()->undefined_value()) return; \
  base::LockGuard<base::Mutex> guard(&mutex_);

void ArrayBufferTracker::MarkLive(JSArrayBuffer* buffer) {
  void* data = nullptr;
  UPDATE_GUARD(buffer, data);

  if (heap()->InNewSpace(buffer)) {
    not_yet_discovered_young_gen_.erase(buffer->address());
  } else {
    not_yet_discovered_old_gen_.erase(buffer->address());
  }
}

void ArrayBufferTracker::Promote(JSArrayBuffer* new_buffer,
                                 JSArrayBuffer* old_buffer) {
  void* data = nullptr;
  UPDATE_GUARD(new_buffer, data);

  Key new_key = new_buffer->address();
  Key old_key = old_buffer->address();
  DCHECK(live_young_gen_.count(old_key) > 0);
  live_old_gen_[new_key] = live_young_gen_[old_key];
  live_young_gen_.erase(old_key);
  not_yet_discovered_young_gen_.erase(old_key);
}

void ArrayBufferTracker::Compact(JSArrayBuffer* new_buffer,
                                 JSArrayBuffer* old_buffer) {
  void* data = nullptr;
  UPDATE_GUARD(new_buffer, data);

  Key new_key = new_buffer->address();
  Key old_key = old_buffer->address();
  DCHECK_NE(new_key, old_key);
  DCHECK(live_old_gen_.count(old_key) > 0);
  live_old_gen_[new_key] = live_old_gen_[old_key];
  live_old_gen_.erase(old_key);
  not_yet_discovered_old_gen_.erase(old_key);
}

void ArrayBufferTracker::SemiSpaceCopy(JSArrayBuffer* new_buffer,
                                       JSArrayBuffer* old_buffer) {
  void* data = nullptr;
  UPDATE_GUARD(new_buffer, data);

  Key new_key = new_buffer->address();
  Key old_key = old_buffer->address();
  DCHECK(live_young_gen_.count(old_key) > 0);
  live_young_gen_[new_key] = live_young_gen_[old_key];
  live_young_gen_.erase(old_key);
  not_yet_discovered_young_gen_.erase(old_key);
}

#undef UPDATE_GUARD

bool ArrayBufferTracker::IsTrackedInOldGenForTesting(JSArrayBuffer* buffer) {
  return live_old_gen_.find(buffer->address()) != live_old_gen_.end();
}

bool ArrayBufferTracker::IsTrackedInYoungGenForTesting(JSArrayBuffer* buffer) {
  return live_young_gen_.find(buffer->address()) != live_young_gen_.end();
}

}  // namespace internal
}  // namespace v8
