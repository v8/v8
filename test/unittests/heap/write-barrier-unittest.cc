// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/globals.h"
#include "src/heap/heap-write-barrier.h"
#include "src/heap/local-heap.h"
#include "test/unittests/test-utils.h"

namespace v8::internal {

using HeapWriteBarrierTest = TestWithIsolate;

#if V8_VERIFY_WRITE_BARRIERS

TEST_F(HeapWriteBarrierTest, NoSafepointInWriteBarrierModeScope) {
  LocalHeap* local_heap = isolate()->main_thread_local_heap();
  EXPECT_DEATH_IF_SUPPORTED(
      {
        WriteBarrierModeScope scope(
            *isolate()->roots_table().empty_fixed_array(),
            SKIP_WRITE_BARRIER_SCOPE);
        local_heap->Safepoint();
      },
      "");
}

TEST_F(HeapWriteBarrierTest, NoAllocationInWriteBarrierModeScope) {
  HandleScope handle_scope(isolate());
  EXPECT_DEATH_IF_SUPPORTED(
      {
        WriteBarrierModeScope scope(
            *isolate()->roots_table().empty_fixed_array(),
            SKIP_WRITE_BARRIER_SCOPE);
        isolate()->factory()->NewFixedArray(1);
      },
      "");
}

#endif  // V8_VERIFY_WRITE_BARRIERS

}  // namespace v8::internal
