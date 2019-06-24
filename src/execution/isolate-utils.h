// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ISOLATE_UTILS_H_
#define V8_EXECUTION_ISOLATE_UTILS_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

V8_INLINE Heap* GetHeapFromWritableObject(HeapObject object);

V8_INLINE Isolate* GetIsolateFromWritableObject(HeapObject object);

// Returns true if it succeeded to obtain isolate from given object.
// If it fails then the object is definitely a read-only object but it may also
// succeed for read only objects if pointer compression is enabled.
V8_INLINE bool GetIsolateFromHeapObject(HeapObject object, Isolate** isolate);

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ISOLATE_UTILS_H_
