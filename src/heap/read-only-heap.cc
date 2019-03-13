// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/read-only-heap.h"

#include "src/heap/spaces.h"
#include "src/snapshot/read-only-deserializer.h"

namespace v8 {
namespace internal {

// static
void ReadOnlyHeap::SetUp(Isolate* isolate, ReadOnlyDeserializer* des) {
  auto* ro_heap = new ReadOnlyHeap(new ReadOnlySpace(isolate->heap()));
  isolate->heap()->SetUpFromReadOnlyHeap(ro_heap);
  if (des != nullptr) {
    des->DeserializeInto(isolate);
    ro_heap->read_only_space_->MarkAsReadOnly();
  }
}

void ReadOnlyHeap::OnCreateHeapObjectsComplete() {
  read_only_space_->MarkAsReadOnly();
}

void ReadOnlyHeap::OnHeapTearDown() {
  delete read_only_space_;
  delete this;
}

// static
bool ReadOnlyHeap::Contains(Object object) {
  return Page::FromAddress(object.ptr())->owner()->identity() == RO_SPACE;
}

}  // namespace internal
}  // namespace v8
