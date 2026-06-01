// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECT_PREDICATES_INL_H_
#define V8_OBJECTS_OBJECT_PREDICATES_INL_H_

#include "src/objects/object-predicates.h"
// Include the non-inl header before the rest of the headers.

#include "src/common/globals.h"
#include "src/heap/heap-layout-inl.h"
#include "src/objects/casting.h"
#include "src/objects/heap-object-inl.h"
#include "src/objects/smi.h"
#include "src/objects/tagged-index.h"
#include "src/roots/roots.h"

namespace v8::internal {

bool IsTaggedIndex(Tagged<Object> obj) {
  return IsSmi(obj) &&
         TaggedIndex::IsValid(Tagged<TaggedIndex>(obj.ptr()).value());
}

bool IsZero(Tagged<Object> obj) { return obj == Smi::zero(); }

bool IsNoSharedNameSentinel(Tagged<Object> obj) {
  return obj == SharedFunctionInfo::kNoSharedNameSentinel;
}

bool IsJSObjectThatCanBeTrackedAsPrototype(Tagged<Object> obj) {
  return IsHeapObject(obj) &&
         IsJSObjectThatCanBeTrackedAsPrototype(Cast<HeapObject>(obj));
}

bool IsJSObjectThatCanBeTrackedAsPrototype(Tagged<HeapObject> obj) {
  return IsJSObject(obj) && !HeapLayout::InWritableSharedSpace(obj);
}

bool IsAnyObjectThatCanBeTrackedAsPrototype(Tagged<Object> obj) {
  return IsHeapObject(obj) &&
         IsAnyObjectThatCanBeTrackedAsPrototype(Cast<HeapObject>(obj));
}

bool IsAnyObjectThatCanBeTrackedAsPrototype(Tagged<HeapObject> obj) {
  return (IsJSObject(obj) || IsWasmObject(obj)) &&
         !HeapLayout::InWritableSharedSpace(obj);
}

bool IsNumber(Tagged<Object> obj) {
  if (IsSmi(obj)) return true;
  return IsHeapNumber(Cast<HeapObject>(obj));
}

bool IsNumeric(Tagged<Object> obj) {
  if (IsSmi(obj)) return true;
  Tagged<HeapObject> heap_object = Cast<HeapObject>(obj);
  return IsHeapNumber(heap_object) || IsBigInt(heap_object);
}

}  // namespace v8::internal

#endif  // V8_OBJECTS_OBJECT_PREDICATES_INL_H_
