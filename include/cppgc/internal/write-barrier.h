// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_
#define INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_

#include "cppgc/internal/process-heap.h"
#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {
namespace internal {

class BasePage;
class Heap;

class V8_EXPORT WriteBarrier final {
 public:
  static V8_INLINE void MarkingBarrier(const void* slot, const void* value) {
    if (V8_LIKELY(!ProcessHeap::IsAnyIncrementalOrConcurrentMarking())) return;

    MarkingBarrierSlow(slot, value);
  }

 private:
  WriteBarrier() = delete;

  static void MarkingBarrierSlow(const void* slot, const void* value);
};

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_WRITE_BARRIER_H_
