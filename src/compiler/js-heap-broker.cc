// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-heap-broker.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

HeapObjectRef::HeapObjectRef(Handle<Object> object) : ObjectRef(object) {
  AllowHandleDereference handle_dereference;
  SLOW_DCHECK(object->IsHeapObject());
}

JSFunctionRef::JSFunctionRef(Handle<Object> object) : HeapObjectRef(object) {
  AllowHandleDereference handle_dereference;
  SLOW_DCHECK(object->IsJSFunction());
}

HeapNumberRef::HeapNumberRef(Handle<Object> object) : HeapObjectRef(object) {
  AllowHandleDereference handle_dereference;
  SLOW_DCHECK(object->IsHeapNumber());
}

ContextRef::ContextRef(Handle<Object> object) : HeapObjectRef(object) {
  AllowHandleDereference handle_dereference;
  SLOW_DCHECK(object->IsContext());
}

NativeContextRef::NativeContextRef(Handle<Object> object) : ContextRef(object) {
  AllowHandleDereference handle_dereference;
  SLOW_DCHECK(object->IsNativeContext());
}

bool ObjectRef::IsSmi() const {
  AllowHandleDereference allow_handle_dereference;
  return object_->IsSmi();
}

int ObjectRef::AsSmi() const { return object<Smi>()->value(); }

base::Optional<ContextRef> ContextRef::previous(
    const JSHeapBroker* broker) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Context* previous = object<Context>()->previous();
  if (previous == nullptr) return base::Optional<ContextRef>();
  return ContextRef(handle(previous, broker->isolate()));
}

ObjectRef ContextRef::get(const JSHeapBroker* broker, int index) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Handle<Object> value(object<Context>()->get(index), broker->isolate());
  return ObjectRef(value);
}

JSHeapBroker::JSHeapBroker(Isolate* isolate) : isolate_(isolate) {}

HeapObjectType JSHeapBroker::HeapObjectTypeFromMap(Map* map) const {
  AllowHandleDereference allow_handle_dereference;
  Heap* heap = isolate_->heap();
  OddballType oddball_type = OddballType::kNone;
  if (map->instance_type() == ODDBALL_TYPE) {
    if (map == heap->undefined_map()) {
      oddball_type = OddballType::kUndefined;
    } else if (map == heap->null_map()) {
      oddball_type = OddballType::kNull;
    } else if (map == heap->boolean_map()) {
      oddball_type = OddballType::kBoolean;
    } else if (map == heap->the_hole_map()) {
      oddball_type = OddballType::kHole;
    } else {
      oddball_type = OddballType::kOther;
      DCHECK(map == heap->uninitialized_map() ||
             map == heap->termination_exception_map() ||
             map == heap->arguments_marker_map() ||
             map == heap->optimized_out_map() ||
             map == heap->stale_register_map());
    }
  }
  HeapObjectType::Flags flags(0);
  if (map->is_undetectable()) flags |= HeapObjectType::kUndetectable;
  if (map->is_callable()) flags |= HeapObjectType::kCallable;

  return HeapObjectType(map->instance_type(), flags, oddball_type);
}

// static
base::Optional<int> JSHeapBroker::TryGetSmi(Handle<Object> object) {
  AllowHandleDereference allow_handle_dereference;
  if (!object->IsSmi()) return base::Optional<int>();
  return Smi::cast(*object)->value();
}

#define HEAP_KIND_FUNCTIONS_DEF(Name)                \
  bool ObjectRef::Is##Name() const {                 \
    AllowHandleDereference allow_handle_dereference; \
    return object<Object>()->Is##Name();             \
  }
HEAP_BROKER_KIND_LIST(HEAP_KIND_FUNCTIONS_DEF)
#undef HEAP_KIND_FUNCTIONS_DEF

#define HEAP_DATA_FUNCTIONS_DEF(Name)       \
  Name##Ref ObjectRef::As##Name() const {   \
    SLOW_DCHECK(Is##Name());                \
    return Name##Ref(object<HeapObject>()); \
  }
HEAP_BROKER_DATA_LIST(HEAP_DATA_FUNCTIONS_DEF)
#undef HEAP_DATA_FUNCTIONS_DEF

HeapObjectType HeapObjectRef::type(const JSHeapBroker* broker) const {
  AllowHandleDereference allow_handle_dereference;
  return broker->HeapObjectTypeFromMap(object<HeapObject>()->map());
}

double HeapNumberRef::value() const {
  AllowHandleDereference allow_handle_dereference;
  return object<HeapObject>()->Number();
}

bool JSFunctionRef::HasBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSFunction>()->shared()->HasBuiltinFunctionId();
}

BuiltinFunctionId JSFunctionRef::GetBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSFunction>()->shared()->builtin_function_id();
}

NameRef::NameRef(Handle<Object> object) : HeapObjectRef(object) {
  AllowHandleDereference handle_dereference;
  SLOW_DCHECK(object->IsName());
}

ScriptContextTableRef::ScriptContextTableRef(Handle<Object> object)
    : HeapObjectRef(object) {
  AllowHandleDereference handle_dereference;
  SLOW_DCHECK(object->IsScriptContextTable());
}

base::Optional<ScriptContextTableRef::LookupResult>
ScriptContextTableRef::lookup(const NameRef& name) const {
  AllowHandleDereference handle_dereference;
  if (!name.IsString()) return {};
  ScriptContextTable::LookupResult lookup_result;
  auto table = object<ScriptContextTable>();
  if (!ScriptContextTable::Lookup(table, name.object<String>(),
                                  &lookup_result)) {
    return {};
  }
  Handle<Context> script_context =
      ScriptContextTable::GetContext(table, lookup_result.context_index);
  LookupResult result{ContextRef(script_context),
                      lookup_result.mode == VariableMode::kConst,
                      lookup_result.slot_index};
  return result;
}

ScriptContextTableRef NativeContextRef::script_context_table(
    const JSHeapBroker* broker) const {
  AllowHandleDereference handle_dereference;
  return ScriptContextTableRef(
      handle(object<Context>()->script_context_table(), broker->isolate()));
}

OddballType ObjectRef::oddball_type(const JSHeapBroker* broker) const {
  return IsSmi() ? OddballType::kNone
                 : AsHeapObject().type(broker).oddball_type();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
