// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-heap-broker.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

bool ObjectReference::IsSmi() const {
  AllowHandleDereference allow_handle_dereference;
  return object_->IsSmi();
}

int ObjectReference::AsSmi() const {
  AllowHandleDereference allow_handle_dereference;
  return Smi::cast(*object_)->value();
}

HeapReference ObjectReference::AsHeapReference() const {
  AllowHandleDereference allow_handle_dereference;
  return HeapReference(Handle<HeapObject>::cast(object_));
}

base::Optional<ContextHeapReference> ContextHeapReference::previous(
    const JSHeapBroker* broker) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Context* previous = Handle<Context>::cast(object())->previous();
  if (previous == nullptr) return base::Optional<ContextHeapReference>();
  return broker->HeapReferenceForObject(handle(previous, broker->isolate()))
      .AsContext();
}

base::Optional<ObjectReference> ContextHeapReference::get(
    const JSHeapBroker* broker, int index) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Handle<Object> value(Handle<Context>::cast(object())->get(index),
                       broker->isolate());
  return ObjectReference(value);
}

JSHeapBroker::JSHeapBroker(Isolate* isolate) : isolate_(isolate) {}

HeapReferenceType JSHeapBroker::HeapReferenceTypeFromMap(Map* map) const {
  AllowHandleDereference allow_handle_dereference;
  Heap* heap = isolate_->heap();
  HeapReferenceType::OddballType oddball_type = HeapReferenceType::kNone;
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
      oddball_type = HeapReferenceType::kOther;
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
  return HeapReference(Handle<HeapObject>::cast(object));
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

#define HEAP_DATA_FUNCTIONS_DEF(Name)                   \
  Name##HeapReference HeapReference::As##Name() const { \
    AllowHandleDereference allow_handle_dereference;    \
    SLOW_DCHECK(object_->Is##Name());                   \
    return Name##HeapReference(object_);                \
  }
HEAP_BROKER_DATA_LIST(HEAP_DATA_FUNCTIONS_DEF)
#undef HEAP_DATA_FUNCTIONS_DEF

HeapReferenceType HeapReference::type(const JSHeapBroker* broker) const {
  AllowHandleDereference allow_handle_dereference;
  return broker->HeapReferenceTypeFromMap(object_->map());
}

double NumberHeapReference::value() const {
  AllowHandleDereference allow_handle_dereference;
  return object()->Number();
}

bool JSFunctionHeapReference::HasBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return JSFunction::cast(*object())->shared()->HasBuiltinFunctionId();
}

BuiltinFunctionId JSFunctionHeapReference::GetBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return JSFunction::cast(*object())->shared()->builtin_function_id();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
