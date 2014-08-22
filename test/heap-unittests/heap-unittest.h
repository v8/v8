// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_UNITTESTS_HEAP_UNITTEST_H_
#define V8_HEAP_UNITTESTS_HEAP_UNITTEST_H_

#include "src/heap/gc-idle-time-handler.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class GCIdleTimeHandlerTest : public ::testing::Test {
 public:
  GCIdleTimeHandlerTest() {}
  virtual ~GCIdleTimeHandlerTest() {}

  GCIdleTimeHandler* handler() { return &handler_; }

  GCIdleTimeHandler::HeapState DefaultHeapState() {
    GCIdleTimeHandler::HeapState result;
    result.contexts_disposed = 0;
    result.size_of_objects = kSizeOfObjects;
    result.incremental_marking_stopped = false;
    result.can_start_incremental_marking = true;
    result.sweeping_in_progress = false;
    result.mark_compact_speed_in_bytes_per_ms = kMarkCompactSpeed;
    result.incremental_marking_speed_in_bytes_per_ms = kMarkingSpeed;
    return result;
  }

  static const size_t kSizeOfObjects = 100 * MB;
  static const size_t kMarkCompactSpeed = 100 * KB;
  static const size_t kMarkingSpeed = 100 * KB;

 private:
  GCIdleTimeHandler handler_;
};
}
}
#endif  // V8_HEAP_UNITTESTS_HEAP_UNITTEST_H_
