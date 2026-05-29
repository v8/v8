// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_OBJECT_PREDICATES_INL_H_
#define V8_OBJECTS_OBJECT_PREDICATES_INL_H_

#include "src/objects/object-predicates.h"
// Include the non-inl header before the rest of the headers.

#include "src/common/globals.h"
#include "src/objects/casting.h"
#include "src/objects/instance-type-checker.h"
#include "src/objects/name-inl.h"
#include "src/objects/oddball-predicates-inl.h"
#include "src/objects/smi.h"
#include "src/objects/tagged-index.h"
#include "src/roots/roots.h"

namespace v8::internal {

bool IsTaggedIndex(Tagged<Object> obj) {
  return IsSmi(obj) &&
         TaggedIndex::IsValid(Tagged<TaggedIndex>(obj.ptr()).value());
}

#define IS_TYPE_FUNCTION_DEF(type_)                        \
  inline bool Is##type_(Tagged<Object> obj) {              \
    Tagged<HeapObject> ho;                                 \
    return TryCast<HeapObject>(obj, &ho) && Is##type_(ho); \
  }                                                        \
  inline bool Is##type_(const HeapObject* obj) {           \
    return Is##type_(Tagged<HeapObject>(obj));             \
  }
HEAP_OBJECT_TYPE_LIST(IS_TYPE_FUNCTION_DEF)
IS_TYPE_FUNCTION_DEF(HashTableBase)
IS_TYPE_FUNCTION_DEF(SmallOrderedHashTable)
IS_TYPE_FUNCTION_DEF(PropertyDictionary)
IS_TYPE_FUNCTION_DEF(AnyHole)
#undef IS_TYPE_FUNCTION_DEF

bool IsZero(Tagged<Object> obj) { return obj == Smi::zero(); }

bool IsPublicSymbol(Tagged<Object> obj) {
  Tagged<Symbol> symbol;
  return TryCast<Symbol>(obj, &symbol) && !symbol->is_any_private();
}
bool IsPrivateSymbol(Tagged<Object> obj) {
  Tagged<Symbol> symbol;
  return TryCast<Symbol>(obj, &symbol) && symbol->is_any_private();
}

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

bool IsMetaMap(Tagged<Map> map) {
  return InstanceTypeChecker::IsMap(map->instance_type());
}

bool IsMetaMap(Tagged<HeapObject> obj) {
  if (!IsMap(obj)) return false;
  return IsMetaMap(UncheckedCast<Map>(obj));
}

bool IsExtendedMap(Tagged<Map> map) {
  DCHECK_IMPLIES(map->is_extended_map(), !IsMetaMap(map));
  return map->is_extended_map();
}

bool IsExtendedMap(Tagged<HeapObject> obj) {
  if (!IsMap(obj)) return false;
  return IsExtendedMap(UncheckedCast<Map>(obj));
}

bool IsJSInterceptorMap(Tagged<Map> map) {
  return IsExtendedMap(map) && (UncheckedCast<ExtendedMap>(map)->map_kind() ==
                                ExtendedMapKind::kJSInterceptorMap);
}

bool IsJSInterceptorMap(Tagged<HeapObject> obj) {
  if (!IsMap(obj)) return false;
  return IsJSInterceptorMap(UncheckedCast<Map>(obj));
}

bool IsPrimitive(Tagged<Object> obj) {
  if (obj.IsSmi()) return true;
  return IsPrimitiveMap(Cast<HeapObject>(obj)->map());
}

#define MAKE_STRUCT_PREDICATE(NAME, Name, name)                  \
  inline bool Is##Name(Tagged<Object> obj) {                     \
    return IsHeapObject(obj) && Is##Name(Cast<HeapObject>(obj)); \
  }                                                              \
  inline bool Is##Name(const HeapObject* obj) {                  \
    return Is##Name(Tagged<HeapObject>(obj));                    \
  }
STRUCT_LIST(MAKE_STRUCT_PREDICATE)
#undef MAKE_STRUCT_PREDICATE

}  // namespace v8::internal

#endif  // V8_OBJECTS_OBJECT_PREDICATES_INL_H_
