// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HANDLES_PERSISTENT_HANDLES_INL_H_
#define V8_HANDLES_PERSISTENT_HANDLES_INL_H_

#include "src/handles/persistent-handles.h"

namespace v8 {
namespace internal {

template <typename T>
Handle<T> PersistentHandles::NewHandle(T obj) {
#ifdef DEBUG
  CheckOwnerIsParked();
#endif
  return Handle<T>(GetHandle(obj.ptr()));
}

template <typename T>
Handle<T> PersistentHandles::NewHandle(Handle<T> obj) {
  return NewHandle(*obj);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HANDLES_PERSISTENT_HANDLES_INL_H_
