// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_MEASUREMENT_INL_H_
#define V8_HEAP_MEMORY_MEASUREMENT_INL_H_

#include "src/heap/memory-measurement.h"
#include "src/objects/contexts-inl.h"
#include "src/objects/contexts.h"
#include "src/objects/map-inl.h"
#include "src/objects/map.h"

namespace v8 {
namespace internal {

bool NativeContextInferrer::Infer(Isolate* isolate, Map map, HeapObject object,
                                  Address* native_context) {
  switch (map.visitor_id()) {
    case kVisitContext:
      *native_context = Context::cast(object).native_context().ptr();
      return true;
    case kVisitNativeContext:
      *native_context = object.ptr();
      return true;
    case kVisitJSFunction:
      return InferForJSFunction(JSFunction::cast(object), native_context);
    case kVisitJSApiObject:
    case kVisitJSArrayBuffer:
    case kVisitJSObject:
    case kVisitJSObjectFast:
    case kVisitJSTypedArray:
    case kVisitJSWeakCollection:
      return InferForJSObject(isolate, map, JSObject::cast(object),
                              native_context);
    default:
      return false;
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_MEASUREMENT_INL_H_
