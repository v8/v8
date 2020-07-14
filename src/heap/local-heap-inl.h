// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_HEAP_INL_H_
#define V8_HEAP_LOCAL_HEAP_INL_H_

#include "src/handles/persistent-handles.h"
#include "src/heap/local-heap.h"

namespace v8 {
namespace internal {

template <typename T>
Handle<T> LocalHeap::NewPersistentHandle(T object) {
  if (!persistent_handles_) {
    EnsurePersistentHandles();
  }
  return persistent_handles_->NewHandle(object);
}

template <typename T>
Handle<T> LocalHeap::NewPersistentHandle(Handle<T> object) {
  return NewPersistentHandle(*object);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_HEAP_INL_H_
