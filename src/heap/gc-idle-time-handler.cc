// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <climits>

#include "src/v8.h"

#include "src/heap/gc-idle-time-handler.h"

namespace v8 {
namespace internal {


const double GCIdleTimeHandler::kConservativeTimeRatio = 0.9;


intptr_t GCIdleTimeHandler::EstimateMarkingStepSize(
    int idle_time_in_ms, intptr_t marking_speed_in_bytes_per_ms) {
  DCHECK(idle_time_in_ms > 0);

  if (marking_speed_in_bytes_per_ms == 0) {
    marking_speed_in_bytes_per_ms =
        GCIdleTimeHandler::kInitialConservativeMarkingSpeed;
  }

  intptr_t marking_step_size = marking_speed_in_bytes_per_ms * idle_time_in_ms;
  if (static_cast<intptr_t>(marking_step_size / idle_time_in_ms) !=
      marking_speed_in_bytes_per_ms) {
    // In the case of an overflow we return maximum marking step size.
    return INT_MAX;
  }

  return static_cast<intptr_t>(marking_step_size *
                               GCIdleTimeHandler::kConservativeTimeRatio);
}
}
}
