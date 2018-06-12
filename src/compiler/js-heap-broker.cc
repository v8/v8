// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-heap-broker.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

JSHeapBroker::JSHeapBroker(Isolate* isolate) : isolate_(isolate) {}

HeapReferenceType JSHeapBroker::HeapReferenceTypeFromMap(Map* map) const {
  AllowHandleDereference allow_handle_dereference;
  Heap* heap = isolate_->heap();
  HeapReferenceType::OddballType oddball_type = HeapReferenceType::kUnknown;
  if (map->instance_type() == ODDBALL_TYPE) {
    if (map == heap->undefined_map()) {
      oddball_type = HeapReferenceType::kUndefined;
    } else if (map == heap->null_map()) {
      oddball_type = HeapReferenceType::kNull;
    } else if (map == heap->boolean_map()) {
      oddball_type = HeapReferenceType::kBoolean;
    } else if (map == heap->the_hole_map()) {
      oddball_type = HeapReferenceType::kHole;
    } else {
      DCHECK(map == heap->uninitialized_map() ||
             map == heap->termination_exception_map() ||
             map == heap->arguments_marker_map() ||
             map == heap->optimized_out_map() ||
             map == heap->stale_register_map());
    }
  }
  HeapReferenceType::Flags flags(0);
  if (map->is_undetectable()) flags |= HeapReferenceType::kUndetectable;
  if (map->is_callable()) flags |= HeapReferenceType::kCallable;

  return HeapReferenceType(map->instance_type(), flags, oddball_type);
}

HeapReference JSHeapBroker::HeapReferenceForObject(
    Handle<Object> object) const {
  AllowHandleDereference allow_handle_dereference;
  Handle<HeapObject> heap_object = Handle<HeapObject>::cast(object);
  HeapReferenceType type = HeapReferenceTypeFromMap(heap_object->map());
  return HeapReference(heap_object, type);
}

// static
base::Optional<int> JSHeapBroker::TryGetSmi(Handle<Object> object) {
  AllowHandleDereference allow_handle_dereference;
  if (!object->IsSmi()) return base::Optional<int>();
  return Smi::cast(*object)->value();
}

#define HEAP_KIND_FUNCTIONS_DEF(Name)                \
  bool HeapReference::Is##Name() const {             \
    AllowHandleDereference allow_handle_dereference; \
    return object_->Is##Name();                      \
  }
HEAP_BROKER_KIND_LIST(HEAP_KIND_FUNCTIONS_DEF)
#undef HEAP_KIND_FUNCTIONS_DEF

NumberHeapData HeapReference::AsNumber() const {
  AllowHandleDereference allow_handle_dereference;
  return NumberHeapData(object_->Number());
}

JSFunctionHeapData HeapReference::AsJSFunction() const {
  AllowHandleDereference allow_handle_dereference;
  return JSFunctionHeapData(Handle<JSFunction>::cast(object_));
}

bool JSFunctionHeapData::HasBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return function_->shared()->HasBuiltinFunctionId();
}

BuiltinFunctionId JSFunctionHeapData::GetBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return function_->shared()->builtin_function_id();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
