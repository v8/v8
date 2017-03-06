// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/v8.h"

#include "src/heap/concurrent-marking.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
TEST(ConcurrentMarking) {
  i::FLAG_concurrent_marking = true;
  CcTest::InitializeVM();
  Heap* heap = CcTest::heap();
  ConcurrentMarking* concurrent_marking = new ConcurrentMarking(heap);
  for (int i = 0; i < 10; i++) {
    concurrent_marking->EnqueueObject(heap->undefined_value());
  }
  concurrent_marking->StartMarkingTasks(3);
  while (!concurrent_marking->IsQueueEmpty()) {
  }
  concurrent_marking->WaitForTasksToComplete();
  delete concurrent_marking;
}

}  // namespace internal
}  // namespace v8
