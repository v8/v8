// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_BARRIER_INL_H_
#define V8_HEAP_MARKING_BARRIER_INL_H_

#include "src/heap/incremental-marking-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/marking-barrier.h"

namespace v8 {
namespace internal {

bool MarkingBarrier::MarkValue(HeapObject host, HeapObject value) {
  DCHECK(is_activated_);
  DCHECK(!marking_state_.IsImpossible(value));
  DCHECK(!marking_state_.IsImpossible(host));
  if (!V8_CONCURRENT_MARKING_BOOL && marking_state_.IsBlack(host)) {
    // The value will be marked and the slot will be recorded when the marker
    // visits the host object.
    return false;
  }
  if (WhiteToGreyAndPush(value)) {
    incremental_marking_->RestartIfNotMarking();
  }
  return true;
}

bool MarkingBarrier::WhiteToGreyAndPush(HeapObject obj) {
  if (marking_state_.WhiteToGrey(obj)) {
    collector_->marking_worklists()->Push(obj);
    return true;
  }
  return false;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_BARRIER_INL_H_
