// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_GC_IDLE_TIME_HANDLER_H_
#define V8_HEAP_GC_IDLE_TIME_HANDLER_H_

#include "src/globals.h"

namespace v8 {
namespace internal {

// The idle time handler makes decisions about which garbage collection
// operations are executing during IdleNotification.
class GCIdleTimeHandler {
 public:
  static intptr_t EstimateMarkingStepSize(
      int idle_time_in_ms, intptr_t marking_speed_in_bytes_per_ms);

  // If we haven't recorded any incremental marking events yet, we carefully
  // mark with a conservative lower bound for the marking speed.
  static const intptr_t kInitialConservativeMarkingSpeed = 100 * KB;

  // We have to make sure that we finish the IdleNotification before
  // idle_time_in_ms. Hence, we conservatively prune our workload estimate.
  static const double kConservativeTimeRatio;

 private:
  DISALLOW_COPY_AND_ASSIGN(GCIdleTimeHandler);
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_GC_IDLE_TIME_HANDLER_H_
