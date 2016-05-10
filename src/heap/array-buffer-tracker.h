// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ARRAY_BUFFER_TRACKER_H_
#define V8_HEAP_ARRAY_BUFFER_TRACKER_H_

#include <map>

#include "src/base/platform/mutex.h"
#include "src/globals.h"

namespace v8 {
namespace internal {

// Forward declarations.
class Heap;
class JSArrayBuffer;

class ArrayBufferTracker {
 public:
  typedef void* Key;

  enum CallbackResult { kKeepEntry, kRemoveEntry };
  enum ListType { kNewSpace, kOldSpace };

  explicit ArrayBufferTracker(Heap* heap) : heap_(heap) {}
  ~ArrayBufferTracker();

  // The following methods are used to track raw C++ pointers to externally
  // allocated memory used as backing store in live array buffers.

  // A new ArrayBuffer was created with |data| as backing store.
  void RegisterNew(JSArrayBuffer* buffer);

  // The backing store |data| is no longer owned by V8.
  void Unregister(JSArrayBuffer* buffer);

  // A live ArrayBuffer was discovered during marking/scavenge.
  void MarkLive(JSArrayBuffer* buffer);

  // Frees all backing store pointers that weren't discovered in the previous
  // marking or scavenge phase.
  void FreeDead(bool from_scavenge);

  // Update methods used to update the tracking state of given ArrayBuffers.
  void Promote(JSArrayBuffer* new_buffer, JSArrayBuffer* old_buffer);
  void SemiSpaceCopy(JSArrayBuffer* new_buffer, JSArrayBuffer* old_buffer);
  void Compact(JSArrayBuffer* new_buffer, JSArrayBuffer* old_buffer);

  // Callback should be of type:
  //   CallbackResult fn(Key);
  template <typename Callback>
  void IterateNotYetDiscoveredEntries(ListType list, Key from, Key to,
                                      Callback callback) {
    TrackingMap::iterator it =
        list == kNewSpace ? not_yet_discovered_young_gen_.lower_bound(from)
                          : not_yet_discovered_old_gen_.lower_bound(from);
    const TrackingMap::iterator end =
        list == kNewSpace ? not_yet_discovered_young_gen_.upper_bound(to)
                          : not_yet_discovered_old_gen_.upper_bound(to);
    {
      base::LockGuard<base::Mutex> guard(&mutex_);
      while (it != end) {
        if (callback(it->first) == kKeepEntry) {
          ++it;
        } else {
          live_old_gen_.erase(it++);
        }
      }
    }
  }

  bool IsTrackedInOldGenForTesting(JSArrayBuffer* buffer);
  bool IsTrackedInYoungGenForTesting(JSArrayBuffer* buffer);

 private:
  typedef std::map<Key, std::pair<void*, size_t>> TrackingMap;

  inline Heap* heap() { return heap_; }

  base::Mutex mutex_;
  Heap* heap_;

  // |live_*| maps tracked JSArrayBuffers to the internally allocated backing
  // store and length.
  // For each GC round (Scavenger, or incremental/full MC)
  // |not_yet_discovered_*| is initialized as a copy of |live_*|. Upon finding
  // a JSArrayBuffer during GC, the buffer is removed from
  // |not_yet_discovered_*|. At the end of a GC, we free up the remaining
  // JSArrayBuffers in |not_yet_discovered_*|.
  TrackingMap live_old_gen_;
  TrackingMap not_yet_discovered_old_gen_;
  TrackingMap live_young_gen_;
  TrackingMap not_yet_discovered_young_gen_;
};

}  // namespace internal
}  // namespace v8
#endif  // V8_HEAP_ARRAY_BUFFER_TRACKER_H_
