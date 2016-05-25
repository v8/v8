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
class Page;
class JSArrayBuffer;

// LocalArrayBufferTracker is tracker for live and dead JSArrayBuffer objects.
//
// It consists of two sets, a live, and a not yet discovered set of buffers.
// Upon registration (in the ArrayBufferTracker) the buffers are added to both
// sets. When a buffer is encountered as live (or added is live) it is removed
// from the not yet discovered set. Finally, after each round (sometime during
// GC) the left over not yet discovered buffers are cleaned up. Upon starting
// a new round the not yet discovered buffers are initialized from the live set.
//
// Caveats:
// - Between cleaning up the buffers using |Free| we always need a |Reset| and
//   thus another marking phase.
// - LocalArrayBufferTracker is completely unlocked. Calls need to ensure
//   exclusive access.
class LocalArrayBufferTracker {
 public:
  typedef std::pair<void*, size_t> Value;
  typedef JSArrayBuffer* Key;

  enum LivenessIndicator { kForwardingPointer, kMarkBit };
  enum CallbackResult { kKeepEntry, kKeepAndUpdateEntry, kRemoveEntry };

  explicit LocalArrayBufferTracker(Heap* heap) : heap_(heap), started_(false) {}
  ~LocalArrayBufferTracker();

  void Add(Key key, const Value& value);
  void AddLive(Key key, const Value& value);
  Value Remove(Key key);
  void MarkLive(Key key);
  bool IsEmpty();

  // Resets the tracking set, i.e., not yet discovered buffers are initialized
  // from the remaining live set of buffers.
  void Reset();

  // Frees up any dead backing stores of not yet discovered array buffers.
  // Requires that the buffers have been properly marked using MarkLive.
  void FreeDead();

  // Scans the whole tracker and decides based on liveness_indicator whether
  // a JSArrayBuffer is still considered live.
  template <LivenessIndicator liveness_indicator>
  inline void ScanAndFreeDead();

  bool IsTracked(Key key) { return live_.find(key) != live_.end(); }

 private:
  // TODO(mlippautz): Switch to unordered_map once it is supported on all
  // platforms.
  typedef std::map<Key, Value> TrackingMap;

  // Processes buffers one by one. The CallbackResult decides whether the buffer
  // will be dropped or not.
  //
  // Callback should be of type:
  //   CallbackResult fn(JSArrayBuffer*, JSArrayBuffer**);
  template <typename Callback>
  inline void Process(Callback callback);

  Heap* heap_;

  // |live_| maps tracked JSArrayBuffers to the internally allocated backing
  // store and length. For each GC round |not_yet_discovered_| is initialized
  // as a copy of |live_|. Upon finding a JSArrayBuffer during GC, the buffer
  // is removed from |not_yet_discovered_|. At the end of a GC, we free up the
  // remaining JSArrayBuffers in |not_yet_discovered_|.
  TrackingMap live_;
  TrackingMap not_yet_discovered_;

  bool started_;
};

class ArrayBufferTracker {
 public:
  explicit ArrayBufferTracker(Heap* heap) : heap_(heap) {}
  ~ArrayBufferTracker();

  // The following methods are used to track raw C++ pointers to externally
  // allocated memory used as backing store in live array buffers.

  // Register/unregister a new JSArrayBuffer |buffer| for tracking.
  void RegisterNew(JSArrayBuffer* buffer);
  void Unregister(JSArrayBuffer* buffer);

  // Frees all backing store pointers for dead JSArrayBuffers in new space.
  void FreeDeadInNewSpace();

  void FreeDead(Page* page);

  template <LocalArrayBufferTracker::LivenessIndicator liveness_indicator>
  void ScanAndFreeDeadArrayBuffers(Page* page);

  // A live JSArrayBuffer was discovered during marking.
  void MarkLive(JSArrayBuffer* buffer);

  // Resets all trackers in old space. Is required to be called from the main
  // thread.
  void ResetTrackersInOldSpace();

 private:
  Heap* heap_;
};

}  // namespace internal
}  // namespace v8
#endif  // V8_HEAP_ARRAY_BUFFER_TRACKER_H_
