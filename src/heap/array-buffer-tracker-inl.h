// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/array-buffer-tracker.h"
#include "src/heap/heap.h"
#include "src/heap/mark-compact.h"
#include "src/isolate.h"

namespace v8 {
namespace internal {

template <typename Callback>
void LocalArrayBufferTracker::Process(Callback callback) {
  JSArrayBuffer* new_buffer = nullptr;
  size_t freed_memory = 0;
  for (TrackingMap::iterator it = live_.begin(); it != live_.end();) {
    switch (callback(it->first, &new_buffer)) {
      case kKeepEntry:
        it++;
        break;
      case kKeepAndUpdateEntry:
        DCHECK_NOT_NULL(new_buffer);
        Page::FromAddress(new_buffer->address())
            ->local_tracker()
            ->AddLive(new_buffer, it->second);
        live_.erase(it++);
        break;
      case kRemoveEntry:
        heap_->isolate()->array_buffer_allocator()->Free(it->second.first,
                                                         it->second.second);
        freed_memory += it->second.second;
        live_.erase(it++);
        break;
      default:
        UNREACHABLE();
    }
  }
  if (freed_memory > 0) {
    heap_->update_amount_of_external_allocated_freed_memory(
        static_cast<intptr_t>(freed_memory));
  }
  not_yet_discovered_.clear();
  started_ = false;
}

template <LocalArrayBufferTracker::LivenessIndicator liveness_indicator>
void LocalArrayBufferTracker::ScanAndFreeDead() {
  switch (liveness_indicator) {
    case kForwardingPointer:
      Process([](JSArrayBuffer* old_buffer, JSArrayBuffer** new_buffer) {
        MapWord map_word = old_buffer->map_word();
        if (map_word.IsForwardingAddress()) {
          *new_buffer = JSArrayBuffer::cast(map_word.ToForwardingAddress());
          return LocalArrayBufferTracker::kKeepAndUpdateEntry;
        }
        return LocalArrayBufferTracker::kRemoveEntry;
      });
      break;
    case kMarkBit:
      Process([](JSArrayBuffer* old_buffer, JSArrayBuffer**) {
        if (Marking::IsBlackOrGrey(Marking::MarkBitFrom(old_buffer))) {
          return LocalArrayBufferTracker::kKeepEntry;
        }
        return LocalArrayBufferTracker::kRemoveEntry;
      });
      break;
    default:
      UNREACHABLE();
  }
}

}  // namespace internal
}  // namespace v8
