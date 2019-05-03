// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/read-only-heap.h"

#include <cstring>

#include "src/base/once.h"
#include "src/heap/heap-inl.h"
#include "src/heap/spaces.h"
#include "src/objects-inl.h"
#include "src/objects/heap-object-inl.h"
#include "src/snapshot/read-only-deserializer.h"

namespace v8 {
namespace internal {

#ifdef V8_SHARED_RO_HEAP
V8_DECLARE_ONCE(setup_ro_heap_once);
ReadOnlyHeap* shared_ro_heap = nullptr;
#endif

// static
void ReadOnlyHeap::SetUp(Isolate* isolate, ReadOnlyDeserializer* des) {
#ifdef V8_SHARED_RO_HEAP
  void* isolate_ro_roots = reinterpret_cast<void*>(
      isolate->roots_table().read_only_roots_begin().address());
  base::CallOnce(&setup_ro_heap_once, [isolate, des, isolate_ro_roots]() {
    shared_ro_heap = Init(isolate, des);
    if (des != nullptr) {
      std::memcpy(shared_ro_heap->read_only_roots_, isolate_ro_roots,
                  kEntriesCount * sizeof(Address));
    }
  });

  isolate->heap()->SetUpFromReadOnlyHeap(shared_ro_heap);
  if (des != nullptr) {
    std::memcpy(isolate_ro_roots, shared_ro_heap->read_only_roots_,
                kEntriesCount * sizeof(Address));
  }
#else
  Init(isolate, des);
#endif  // V8_SHARED_RO_HEAP
}

void ReadOnlyHeap::OnCreateHeapObjectsComplete() {
  DCHECK(!deserializing_);
#ifdef V8_SHARED_RO_HEAP
  read_only_space_->Forget();
#endif
  read_only_space_->MarkAsReadOnly();
}

// static
ReadOnlyHeap* ReadOnlyHeap::Init(Isolate* isolate, ReadOnlyDeserializer* des) {
  auto* ro_heap = new ReadOnlyHeap(new ReadOnlySpace(isolate->heap()));
  isolate->heap()->SetUpFromReadOnlyHeap(ro_heap);
  if (des != nullptr) {
    des->DeserializeInto(isolate);
    ro_heap->deserializing_ = true;
#ifdef V8_SHARED_RO_HEAP
    ro_heap->read_only_space_->Forget();
#endif
    ro_heap->read_only_space_->MarkAsReadOnly();
  }
  return ro_heap;
}

void ReadOnlyHeap::OnHeapTearDown() {
#ifndef V8_SHARED_RO_HEAP
  delete read_only_space_;
  delete this;
#endif
}

// static
bool ReadOnlyHeap::Contains(HeapObject object) {
  return Page::FromAddress(object.ptr())->owner()->identity() == RO_SPACE;
}

ReadOnlyHeapIterator::ReadOnlyHeapIterator(ReadOnlyHeap* ro_heap)
    : ReadOnlyHeapIterator(ro_heap->read_only_space()) {}

ReadOnlyHeapIterator::ReadOnlyHeapIterator(ReadOnlySpace* ro_space)
    : ro_space_(ro_space),
      current_page_(ro_space->first_page()),
      current_addr_(current_page_->area_start()) {}

HeapObject ReadOnlyHeapIterator::next() {
  if (current_page_ == nullptr) {
    return HeapObject();
  }

  for (;;) {
    DCHECK_LE(current_addr_, current_page_->area_end());
    if (current_addr_ == current_page_->area_end()) {
      // Progress to the next page.
      current_page_ = current_page_->next_page();
      if (current_page_ == nullptr) {
        return HeapObject();
      }
      current_addr_ = current_page_->area_start();
    }

    if (current_addr_ == ro_space_->top() &&
        current_addr_ != ro_space_->limit()) {
      current_addr_ = ro_space_->limit();
      continue;
    }
    HeapObject object = HeapObject::FromAddress(current_addr_);
    const int object_size = object->Size();
    current_addr_ += object_size;

    if (object->IsFiller()) {
      continue;
    }

    DCHECK_OBJECT_SIZE(object_size);
    return object;
  }
}

}  // namespace internal
}  // namespace v8
