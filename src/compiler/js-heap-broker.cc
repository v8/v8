// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-heap-broker.h"

#include "src/objects-inl.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/module-inl.h"
#include "src/utils.h"

namespace v8 {
namespace internal {
namespace compiler {

#define FORWARD_DECL(Name) class Name##Data;
HEAP_BROKER_OBJECT_LIST(FORWARD_DECL)
#undef FORWARD_DECL

// TODO(neis): It would be nice to share the serialized data for read-only
// objects.

class ObjectData : public ZoneObject {
 public:
  static ObjectData* Serialize(JSHeapBroker* broker, Handle<Object> object);

  ObjectData(JSHeapBroker* broker_, Handle<Object> object_, bool is_smi_)
      : broker(broker_), object(object_), is_smi(is_smi_) {
    broker->AddData(object, this);
  }

#define DECLARE_IS_AND_AS(Name) \
  bool Is##Name() const;        \
  Name##Data* As##Name();
  HEAP_BROKER_OBJECT_LIST(DECLARE_IS_AND_AS)
#undef DECLARE_IS_AND_AS

  JSHeapBroker* const broker;
  Handle<Object> const object;
  bool const is_smi;
};

// TODO(neis): Perhaps add a boolean that indicates whether serialization of an
// object has completed. That could be used to add safety checks.

#define GET_OR_CREATE(name) \
  broker->GetOrCreateData(handle(object_->name(), broker->isolate()))

class HeapObjectData : public ObjectData {
 public:
  static HeapObjectData* Serialize(JSHeapBroker* broker,
                                   Handle<HeapObject> object);

  HeapObjectType const type;
  ObjectData* const map;

  HeapObjectData(JSHeapBroker* broker_, Handle<HeapObject> object_,
                 HeapObjectType type_)
      : ObjectData(broker_, object_, false),
        type(type_),
        map(GET_OR_CREATE(map)) {
    CHECK(broker_->SerializingAllowed());
  }
};

class PropertyCellData : public HeapObjectData {};
class JSObjectData : public HeapObjectData {
 public:
  using HeapObjectData::HeapObjectData;
};

class JSFunctionData : public JSObjectData {
 public:
  ObjectData* const global_proxy;
  ObjectData* const prototype_or_initial_map;
  ObjectData* const shared;

  JSFunctionData(JSHeapBroker* broker_, Handle<JSFunction> object_,
                 HeapObjectType type_)
      : JSObjectData(broker_, object_, type_),
        global_proxy(GET_OR_CREATE(global_proxy)),
        prototype_or_initial_map(object_->map()->has_prototype_slot()
                                     ? GET_OR_CREATE(prototype_or_initial_map)
                                     : nullptr),
        shared(GET_OR_CREATE(shared)) {}
};

class JSRegExpData : public JSObjectData {};
class HeapNumberData : public HeapObjectData {};
class MutableHeapNumberData : public HeapObjectData {};

class ContextData : public HeapObjectData {
 public:
  ContextData(JSHeapBroker* broker_, Handle<Context> object_,
              HeapObjectType type_)
      : HeapObjectData(broker_, object_, type_) {}
};

#define NATIVE_CONTEXT_DATA(V)  \
  V(fast_aliased_arguments_map) \
  V(initial_array_iterator_map) \
  V(iterator_result_map)        \
  V(js_array_fast_elements_map) \
  V(map_key_iterator_map)       \
  V(map_key_value_iterator_map) \
  V(map_value_iterator_map)     \
  V(set_key_value_iterator_map) \
  V(set_value_iterator_map)     \
  V(sloppy_arguments_map)       \
  V(strict_arguments_map)       \
  V(string_iterator_map)

class NativeContextData : public ContextData {
 public:
#define DECL_MEMBER(name) ObjectData* const name;
  NATIVE_CONTEXT_DATA(DECL_MEMBER)
#undef DECL_MEMBER

  // There's no NativeContext class, so we take object_ as Context.
  NativeContextData(JSHeapBroker* broker_, Handle<Context> object_,
                    HeapObjectType type_)
      : ContextData(broker_, object_, type_)
#define INIT_MEMBER(name) , name(GET_OR_CREATE(name))
            NATIVE_CONTEXT_DATA(INIT_MEMBER)
#undef INIT_MEMBER
  {
    CHECK(object_->IsNativeContext());
  }
};

class NameData : public HeapObjectData {
 public:
  NameData(JSHeapBroker* broker, Handle<Name> object, HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

class StringData : public NameData {
 public:
  StringData(JSHeapBroker* broker, Handle<String> object, HeapObjectType type)
      : NameData(broker, object, type),
        length(object->length()),
        first_char(length > 0 ? object->Get(0) : 0) {
    int flags = ALLOW_HEX | ALLOW_OCTAL | ALLOW_BINARY;
    to_number = StringToDouble(
        broker->isolate(), broker->isolate()->unicode_cache(), object, flags);
  }

  int length;
  uint16_t first_char;
  double to_number;
};

class InternalizedStringData : public StringData {
 public:
  using StringData::StringData;
};

class ScriptContextTableData : public HeapObjectData {};
class FeedbackVectorData : public HeapObjectData {};
class AllocationSiteData : public HeapObjectData {};

class MapData : public HeapObjectData {
 public:
  int const instance_size;
  byte const bit_field;
  byte const bit_field2;
  uint32_t const bit_field3;

  MapData(JSHeapBroker* broker_, Handle<Map> object_, HeapObjectType type_)
      : HeapObjectData(broker_, object_, type_),
        instance_size(object_->instance_size()),
        bit_field(object_->bit_field()),
        bit_field2(object_->bit_field2()),
        bit_field3(object_->bit_field3()) {}
};

class FixedArrayBaseData : public HeapObjectData {};
class FixedArrayData : public FixedArrayBaseData {};
class FixedDoubleArrayData : public FixedArrayBaseData {};
class JSArrayData : public JSObjectData {};
class ScopeInfoData : public HeapObjectData {};
class SharedFunctionInfoData : public HeapObjectData {};
class ModuleData : public HeapObjectData {};
class CellData : public HeapObjectData {};
class JSGlobalProxyData : public JSObjectData {};
class CodeData : public HeapObjectData {};

#define DEFINE_IS_AND_AS(Name)                                          \
  bool ObjectData::Is##Name() const {                                   \
    if (broker->mode() == JSHeapBroker::kDisabled) {                    \
      AllowHandleDereference allow_handle_dereference;                  \
      return object->Is##Name();                                        \
    }                                                                   \
    if (is_smi) return false;                                           \
    InstanceType instance_type =                                        \
        static_cast<const HeapObjectData*>(this)->type.instance_type(); \
    return InstanceTypeChecker::Is##Name(instance_type);                \
  }                                                                     \
  Name##Data* ObjectData::As##Name() {                                  \
    CHECK_NE(broker->mode(), JSHeapBroker::kDisabled);                  \
    CHECK(Is##Name());                                                  \
    return static_cast<Name##Data*>(this);                              \
  }
HEAP_BROKER_OBJECT_LIST(DEFINE_IS_AND_AS)
#undef DEFINE_IS_AND_AS

ObjectData* ObjectData::Serialize(JSHeapBroker* broker, Handle<Object> object) {
  CHECK(broker->SerializingAllowed());
  return object->IsSmi() ? new (broker->zone()) ObjectData(broker, object, true)
                         : HeapObjectData::Serialize(
                               broker, Handle<HeapObject>::cast(object));
}

HeapObjectData* HeapObjectData::Serialize(JSHeapBroker* broker,
                                          Handle<HeapObject> object) {
  CHECK(broker->SerializingAllowed());
  Handle<Map> map(object->map(), broker->isolate());
  HeapObjectType type = broker->HeapObjectTypeFromMap(map);

  HeapObjectData* result;
  // TODO(neis): Do a switch on instance type here.
  if (object->IsJSFunction()) {
    result = new (broker->zone())
        JSFunctionData(broker, Handle<JSFunction>::cast(object), type);
  } else if (object->IsNativeContext()) {
    result = new (broker->zone())
        NativeContextData(broker, Handle<Context>::cast(object), type);
  } else if (object->IsMap()) {
    result =
        new (broker->zone()) MapData(broker, Handle<Map>::cast(object), type);
  } else if (object->IsInternalizedString()) {
    result = new (broker->zone())
        InternalizedStringData(broker, Handle<String>::cast(object), type);
  } else if (object->IsString()) {
    result = new (broker->zone())
        StringData(broker, Handle<String>::cast(object), type);
  } else if (object->IsName()) {
    result =
        new (broker->zone()) NameData(broker, Handle<Name>::cast(object), type);
  } else {
    result = new (broker->zone()) HeapObjectData(broker, object, type);
  }
  return result;
}

bool ObjectRef::equals(const ObjectRef& other) const {
  return data_ == other.data_;
}

StringRef ObjectRef::TypeOf() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  return StringRef(broker(),
                   Object::TypeOf(broker()->isolate(), object<Object>()));
}

Isolate* ObjectRef::isolate() const { return broker()->isolate(); }

base::Optional<ContextRef> ContextRef::previous() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Context* previous = object<Context>()->previous();
  if (previous == nullptr) return base::Optional<ContextRef>();
  return ContextRef(broker(), handle(previous, broker()->isolate()));
}

ObjectRef ContextRef::get(int index) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Handle<Object> value(object<Context>()->get(index), broker()->isolate());
  return ObjectRef(broker(), value);
}

JSHeapBroker::JSHeapBroker(Isolate* isolate, Zone* zone)
    : isolate_(isolate),
      zone_(zone),
      refs_(zone),
      mode_(FLAG_concurrent_compiler_frontend ? kSerializing : kDisabled) {
  Trace("%s", "Constructing heap broker.\n");
}

void JSHeapBroker::Trace(const char* format, ...) const {
  if (FLAG_trace_heap_broker) {
    PrintF("[%p] ", this);
    va_list arguments;
    va_start(arguments, format);
    base::OS::VPrint(format, arguments);
    va_end(arguments);
  }
}

bool JSHeapBroker::SerializingAllowed() const {
  return mode() == kSerializing ||
         (!FLAG_strict_heap_broker && mode() == kSerialized);
}

void JSHeapBroker::SerializeStandardObjects() {
  Trace("Serializing standard objects.\n");

  Builtins* const b = isolate()->builtins();
  Factory* const f = isolate()->factory();

  // Stuff used by JSGraph:
  GetOrCreateData(f->empty_fixed_array());

  // Stuff used by JSCreateLowering:
  GetOrCreateData(f->block_context_map());
  GetOrCreateData(f->catch_context_map());
  GetOrCreateData(f->eval_context_map());
  GetOrCreateData(f->fixed_array_map());
  GetOrCreateData(f->fixed_double_array_map());
  GetOrCreateData(f->function_context_map());
  GetOrCreateData(f->many_closures_cell_map());
  GetOrCreateData(f->mutable_heap_number_map());
  GetOrCreateData(f->name_dictionary_map());
  GetOrCreateData(f->one_pointer_filler_map());
  GetOrCreateData(f->sloppy_arguments_elements_map());
  GetOrCreateData(f->with_context_map());

  // Stuff used by TypedOptimization:
  // Strings produced by typeof:
  GetOrCreateData(f->boolean_string());
  GetOrCreateData(f->number_string());
  GetOrCreateData(f->string_string());
  GetOrCreateData(f->bigint_string());
  GetOrCreateData(f->symbol_string());
  GetOrCreateData(f->undefined_string());
  GetOrCreateData(f->object_string());
  GetOrCreateData(f->function_string());

  // Stuff used by JSTypedLowering:
  GetOrCreateData(f->length_string());
  Builtins::Name builtins[] = {
      Builtins::kArgumentsAdaptorTrampoline,
      Builtins::kCallFunctionForwardVarargs,
      Builtins::kStringAdd_CheckNone_NotTenured,
      Builtins::kStringAdd_CheckNone_Tenured,
      Builtins::kStringAdd_ConvertLeft_NotTenured,
      Builtins::kStringAdd_ConvertRight_NotTenured,
  };
  for (auto id : builtins) {
    GetOrCreateData(b->builtin_handle(id));
  }
  for (int32_t id = 0; id < Builtins::builtin_count; ++id) {
    if (Builtins::KindOf(id) == Builtins::TFJ) {
      GetOrCreateData(b->builtin_handle(id));
    }
  }
}

HeapObjectType JSHeapBroker::HeapObjectTypeFromMap(Map* map) const {
  AllowHandleDereference allow_handle_dereference;
  OddballType oddball_type = OddballType::kNone;
  if (map->instance_type() == ODDBALL_TYPE) {
    ReadOnlyRoots roots(isolate_);
    if (map == roots.undefined_map()) {
      oddball_type = OddballType::kUndefined;
    } else if (map == roots.null_map()) {
      oddball_type = OddballType::kNull;
    } else if (map == roots.boolean_map()) {
      oddball_type = OddballType::kBoolean;
    } else if (map == roots.the_hole_map()) {
      oddball_type = OddballType::kHole;
    } else if (map == roots.uninitialized_map()) {
      oddball_type = OddballType::kUninitialized;
    } else {
      oddball_type = OddballType::kOther;
      DCHECK(map == roots.termination_exception_map() ||
             map == roots.arguments_marker_map() ||
             map == roots.optimized_out_map() ||
             map == roots.stale_register_map());
    }
  }
  HeapObjectType::Flags flags(0);
  if (map->is_undetectable()) flags |= HeapObjectType::kUndetectable;
  if (map->is_callable()) flags |= HeapObjectType::kCallable;

  return HeapObjectType(map->instance_type(), flags, oddball_type);
}

ObjectData* JSHeapBroker::GetData(Handle<Object> object) const {
  auto it = refs_.find(object.address());
  return it != refs_.end() ? it->second : nullptr;
}

ObjectData* JSHeapBroker::GetOrCreateData(Handle<Object> object) {
  CHECK(SerializingAllowed());
  ObjectData* data = GetData(object);
  if (data == nullptr) {
    // TODO(neis): Remove these Allow* once we serialize everything upfront.
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference handle_dereference;
    data = ObjectData::Serialize(this, object);
  }
  CHECK_NOT_NULL(data);
  return data;
}

void JSHeapBroker::AddData(Handle<Object> object, ObjectData* data) {
  Trace("Creating data %p for handle %" V8PRIuPTR " (", data, object.address());
  if (FLAG_trace_heap_broker) {
    object->ShortPrint();
    PrintF(")\n");
  }
  CHECK_NOT_NULL(isolate()->handle_scope_data()->canonical_scope);
  CHECK(refs_.insert({object.address(), data}).second);
}

#define DEFINE_IS_AND_AS(Name)                                    \
  bool ObjectRef::Is##Name() const { return data()->Is##Name(); } \
  Name##Ref ObjectRef::As##Name() const {                         \
    DCHECK(Is##Name());                                           \
    return Name##Ref(data());                                     \
  }
HEAP_BROKER_OBJECT_LIST(DEFINE_IS_AND_AS)
#undef DEFINE_IS_AND_AS

bool ObjectRef::IsSmi() const { return data()->is_smi; }

int ObjectRef::AsSmi() const {
  DCHECK(IsSmi());
  // Handle-dereference is always allowed for Handle<Smi>.
  return object<Smi>()->value();
}

HeapObjectType HeapObjectRef::type() const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    return broker()->HeapObjectTypeFromMap(object<HeapObject>()->map());
  } else {
    return data()->AsHeapObject()->type;
  }
}

base::Optional<MapRef> HeapObjectRef::TryGetObjectCreateMap() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  Handle<Map> instance_map;
  if (Map::TryGetObjectCreateMap(broker()->isolate(), object<HeapObject>())
          .ToHandle(&instance_map)) {
    return MapRef(broker(), instance_map);
  } else {
    return base::Optional<MapRef>();
  }
}

void JSFunctionRef::EnsureHasInitialMap() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  AllowHeapAllocation heap_allocation;
  // TODO(jarin) Eventually, we will prepare initial maps for resumable
  // functions (i.e., generators).
  DCHECK(IsResumableFunction(object<JSFunction>()->shared()->kind()));
  JSFunction::EnsureHasInitialMap(object<JSFunction>());
}

// TODO(mslekova): Pre-compute these on the main thread.
base::Optional<MapRef> MapRef::AsElementsKind(ElementsKind kind) const {
  AllowHandleAllocation handle_allocation;
  AllowHeapAllocation heap_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                Map::AsElementsKind(broker()->isolate(), object<Map>(), kind));
}

int JSFunctionRef::InitialMapInstanceSizeWithMinSlack() const {
  AllowHandleDereference allow_handle_dereference;
  AllowHandleAllocation handle_allocation;

  return object<JSFunction>()->ComputeInstanceSizeWithMinSlack(
      broker()->isolate());
}

base::Optional<ScriptContextTableRef::LookupResult>
ScriptContextTableRef::lookup(const NameRef& name) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  if (!name.IsString()) return {};
  ScriptContextTable::LookupResult lookup_result;
  auto table = object<ScriptContextTable>();
  if (!ScriptContextTable::Lookup(broker()->isolate(), table,
                                  name.object<String>(), &lookup_result)) {
    return {};
  }
  Handle<Context> script_context = ScriptContextTable::GetContext(
      broker()->isolate(), table, lookup_result.context_index);
  LookupResult result{ContextRef(broker(), script_context),
                      lookup_result.mode == VariableMode::kConst,
                      lookup_result.slot_index};
  return result;
}

OddballType ObjectRef::oddball_type() const {
  return IsSmi() ? OddballType::kNone : AsHeapObject().type().oddball_type();
}

ObjectRef FeedbackVectorRef::get(FeedbackSlot slot) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Handle<Object> value(object<FeedbackVector>()->Get(slot)->ToObject(),
                       broker()->isolate());
  return ObjectRef(broker(), value);
}

bool JSObjectRef::IsUnboxedDoubleField(FieldIndex index) const {
  AllowHandleDereference handle_dereference;
  return object<JSObject>()->IsUnboxedDoubleField(index);
}

double JSObjectRef::RawFastDoublePropertyAt(FieldIndex index) const {
  AllowHandleDereference handle_dereference;
  return object<JSObject>()->RawFastDoublePropertyAt(index);
}

ObjectRef JSObjectRef::RawFastPropertyAt(FieldIndex index) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  return ObjectRef(broker(),
                   handle(object<JSObject>()->RawFastPropertyAt(index),
                          broker()->isolate()));
}


namespace {

// Determines whether the given array or object literal boilerplate satisfies
// all limits to be considered for fast deep-copying and computes the total
// size of all objects that are part of the graph.
bool IsFastLiteralHelper(Handle<JSObject> boilerplate, int max_depth,
                         int* max_properties) {
  DCHECK_GE(max_depth, 0);
  DCHECK_GE(*max_properties, 0);

  // Make sure the boilerplate map is not deprecated.
  if (!JSObject::TryMigrateInstance(boilerplate)) return false;

  // Check for too deep nesting.
  if (max_depth == 0) return false;

  // Check the elements.
  Isolate* const isolate = boilerplate->GetIsolate();
  Handle<FixedArrayBase> elements(boilerplate->elements(), isolate);
  if (elements->length() > 0 &&
      elements->map() != ReadOnlyRoots(isolate).fixed_cow_array_map()) {
    if (boilerplate->HasSmiOrObjectElements()) {
      Handle<FixedArray> fast_elements = Handle<FixedArray>::cast(elements);
      int length = elements->length();
      for (int i = 0; i < length; i++) {
        if ((*max_properties)-- == 0) return false;
        Handle<Object> value(fast_elements->get(i), isolate);
        if (value->IsJSObject()) {
          Handle<JSObject> value_object = Handle<JSObject>::cast(value);
          if (!IsFastLiteralHelper(value_object, max_depth - 1,
                                   max_properties)) {
            return false;
          }
        }
      }
    } else if (boilerplate->HasDoubleElements()) {
      if (elements->Size() > kMaxRegularHeapObjectSize) return false;
    } else {
      return false;
    }
  }

  // TODO(turbofan): Do we want to support out-of-object properties?
  if (!(boilerplate->HasFastProperties() &&
        boilerplate->property_array()->length() == 0)) {
    return false;
  }

  // Check the in-object properties.
  Handle<DescriptorArray> descriptors(
      boilerplate->map()->instance_descriptors(), isolate);
  int limit = boilerplate->map()->NumberOfOwnDescriptors();
  for (int i = 0; i < limit; i++) {
    PropertyDetails details = descriptors->GetDetails(i);
    if (details.location() != kField) continue;
    DCHECK_EQ(kData, details.kind());
    if ((*max_properties)-- == 0) return false;
    FieldIndex field_index = FieldIndex::ForDescriptor(boilerplate->map(), i);
    if (boilerplate->IsUnboxedDoubleField(field_index)) continue;
    Handle<Object> value(boilerplate->RawFastPropertyAt(field_index), isolate);
    if (value->IsJSObject()) {
      Handle<JSObject> value_object = Handle<JSObject>::cast(value);
      if (!IsFastLiteralHelper(value_object, max_depth - 1, max_properties)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

// Maximum depth and total number of elements and properties for literal
// graphs to be considered for fast deep-copying. The limit is chosen to
// match the maximum number of inobject properties, to ensure that the
// performance of using object literals is not worse than using constructor
// functions, see crbug.com/v8/6211 for details.
const int kMaxFastLiteralDepth = 3;
const int kMaxFastLiteralProperties = JSObject::kMaxInObjectProperties;

// Determines whether the given array or object literal boilerplate satisfies
// all limits to be considered for fast deep-copying and computes the total
// size of all objects that are part of the graph.
bool AllocationSiteRef::IsFastLiteral() const {
  AllowHeapAllocation
      allow_heap_allocation;  // This is needed for TryMigrateInstance.
  AllowHandleAllocation allow_handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  int max_properties = kMaxFastLiteralProperties;
  Handle<JSObject> boilerplate(object<AllocationSite>()->boilerplate(),
                               broker()->isolate());
  return IsFastLiteralHelper(boilerplate, kMaxFastLiteralDepth,
                             &max_properties);
}

void JSObjectRef::EnsureElementsTenured() {
  // TODO(jarin) Eventually, we will pretenure the boilerplates before
  // the compilation job starts.
  AllowHandleAllocation allow_handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  AllowHeapAllocation allow_heap_allocation;

  Handle<FixedArrayBase> object_elements = elements().object<FixedArrayBase>();
  if (Heap::InNewSpace(*object_elements)) {
    // If we would like to pretenure a fixed cow array, we must ensure that
    // the array is already in old space, otherwise we'll create too many
    // old-to-new-space pointers (overflowing the store buffer).
    object_elements =
        broker()->isolate()->factory()->CopyAndTenureFixedCOWArray(
            Handle<FixedArray>::cast(object_elements));
    object<JSObject>()->set_elements(*object_elements);
  }
}

FieldIndex MapRef::GetFieldIndexFor(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return FieldIndex::ForDescriptor(*object<Map>(), i);
}

int MapRef::GetInObjectPropertyOffset(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->GetInObjectPropertyOffset(i);
}

PropertyDetails MapRef::GetPropertyDetails(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->instance_descriptors()->GetDetails(i);
}

NameRef MapRef::GetPropertyKey(int i) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return NameRef(broker(),
                 handle(object<Map>()->instance_descriptors()->GetKey(i),
                        broker()->isolate()));
}

bool MapRef::IsFixedCowArrayMap() const {
  AllowHandleDereference allow_handle_dereference;
  return *object<Map>() ==
         ReadOnlyRoots(broker()->isolate()).fixed_cow_array_map();
}

MapRef MapRef::FindFieldOwner(int descriptor) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  Handle<Map> owner(
      object<Map>()->FindFieldOwner(broker()->isolate(), descriptor),
      broker()->isolate());
  return MapRef(broker(), owner);
}

ObjectRef MapRef::GetFieldType(int descriptor) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  Handle<FieldType> field_type(
      object<Map>()->instance_descriptors()->GetFieldType(descriptor),
      broker()->isolate());
  return ObjectRef(broker(), field_type);
}

int StringRef::length() const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    return object<String>()->length();
  } else {
    return data()->AsString()->length;
  }
}

uint16_t StringRef::GetFirstChar() {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    return object<String>()->Get(0);
  } else {
    return data()->AsString()->first_char;
  }
}

double StringRef::ToNumber() {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    AllowHandleAllocation allow_handle_allocation;
    AllowHeapAllocation allow_heap_allocation;
    int flags = ALLOW_HEX | ALLOW_OCTAL | ALLOW_BINARY;
    return StringToDouble(broker()->isolate(),
                          broker()->isolate()->unicode_cache(),
                          object<String>(), flags);
  } else {
    return data()->AsString()->to_number;
  }
}

bool FixedArrayRef::is_the_hole(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return object<FixedArray>()->is_the_hole(broker()->isolate(), i);
}

ObjectRef FixedArrayRef::get(int i) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(broker(),
                   handle(object<FixedArray>()->get(i), broker()->isolate()));
}

bool FixedDoubleArrayRef::is_the_hole(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return object<FixedDoubleArray>()->is_the_hole(i);
}

double FixedDoubleArrayRef::get_scalar(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return object<FixedDoubleArray>()->get_scalar(i);
}

int SharedFunctionInfoRef::GetBytecodeArrayRegisterCount() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->GetBytecodeArray()->register_count();
}

// Macros for definining a const getter that, depending on the broker mode,
// either looks into the handle or into the serialized data. The first one is
// used for the rare case of a XYZRef class that does not have a corresponding
// XYZ class in objects.h. The second one is used otherwise.
#define BIMODAL_ACCESSOR_(holder, v8class, result, name)                     \
  result##Ref holder##Ref::name() const {                                    \
    if (broker()->mode() == JSHeapBroker::kDisabled) {                       \
      AllowHandleAllocation handle_allocation;                               \
      AllowHandleDereference allow_handle_dereference;                       \
      return result##Ref(                                                    \
          broker(), handle(object<v8class>()->name(), broker()->isolate())); \
    } else {                                                                 \
      return result##Ref(data()->As##holder()->name);                        \
    }                                                                        \
  }
#define BIMODAL_ACCESSOR(holder, result, name) \
  BIMODAL_ACCESSOR_(holder, holder, result, name)

// Like HANDLE_ACCESSOR except that the result type is not an XYZRef.
#define BIMODAL_ACCESSOR_C(holder, result, name)       \
  result holder##Ref::name() const {                   \
    if (broker()->mode() == JSHeapBroker::kDisabled) { \
      AllowHandleAllocation handle_allocation;         \
      AllowHandleDereference allow_handle_dereference; \
      return object<holder>()->name();                 \
    } else {                                           \
      return data()->As##holder()->name;               \
    }                                                  \
  }

// Like HANDLE_ACCESSOR_C but for BitFields.
#define BIMODAL_ACCESSOR_B(holder, field, name, BitField)   \
  typename BitField::FieldType holder##Ref::name() const {  \
    if (broker()->mode() == JSHeapBroker::kDisabled) {      \
      AllowHandleAllocation handle_allocation;              \
      AllowHandleDereference allow_handle_dereference;      \
      return object<holder>()->name();                      \
    } else {                                                \
      return BitField::decode(data()->As##holder()->field); \
    }                                                       \
  }

// Macros for definining a const getter that always looks into the handle.
// (These will go away once we serialize everything.) The first one is used for
// the rare case of a XYZRef class that does not have a corresponding XYZ class
// in objects.h. The second one is used otherwise.
#define HANDLE_ACCESSOR_(holder, v8class, result, name)                    \
  result##Ref holder##Ref::name() const {                                  \
    AllowHandleAllocation handle_allocation;                               \
    AllowHandleDereference allow_handle_dereference;                       \
    return result##Ref(                                                    \
        broker(), handle(object<v8class>()->name(), broker()->isolate())); \
  }
#define HANDLE_ACCESSOR(holder, result, name) \
  HANDLE_ACCESSOR_(holder, holder, result, name)

// Like HANDLE_ACCESSOR except that the result type is not an XYZRef.
#define HANDLE_ACCESSOR_C(holder, result, name)      \
  result holder##Ref::name() const {                 \
    AllowHandleAllocation handle_allocation;         \
    AllowHandleDereference allow_handle_dereference; \
    return object<holder>()->name();                 \
  }

HANDLE_ACCESSOR(AllocationSite, JSObject, boilerplate)
HANDLE_ACCESSOR(AllocationSite, Object, nested_site)
HANDLE_ACCESSOR_C(AllocationSite, bool, CanInlineCall)
HANDLE_ACCESSOR_C(AllocationSite, bool, PointsToLiteral)
HANDLE_ACCESSOR_C(AllocationSite, ElementsKind, GetElementsKind)
HANDLE_ACCESSOR_C(AllocationSite, PretenureFlag, GetPretenureMode)

HANDLE_ACCESSOR_C(FixedArrayBase, int, length)

BIMODAL_ACCESSOR(HeapObject, Map, map)
HANDLE_ACCESSOR_C(HeapObject, bool, IsExternalString)
HANDLE_ACCESSOR_C(HeapObject, bool, IsSeqString)

HANDLE_ACCESSOR_C(HeapNumber, double, value)

HANDLE_ACCESSOR_C(JSArray, ElementsKind, GetElementsKind)
HANDLE_ACCESSOR(JSArray, Object, length)

HANDLE_ACCESSOR_C(JSFunction, bool, has_initial_map)
HANDLE_ACCESSOR_C(JSFunction, bool, IsConstructor)
HANDLE_ACCESSOR(JSFunction, JSGlobalProxy, global_proxy)
HANDLE_ACCESSOR(JSFunction, Map, initial_map)
HANDLE_ACCESSOR(JSFunction, SharedFunctionInfo, shared)

HANDLE_ACCESSOR_C(JSObject, ElementsKind, GetElementsKind)
HANDLE_ACCESSOR(JSObject, FixedArrayBase, elements)

HANDLE_ACCESSOR(JSRegExp, Object, data)
HANDLE_ACCESSOR(JSRegExp, Object, flags)
HANDLE_ACCESSOR(JSRegExp, Object, last_index)
HANDLE_ACCESSOR(JSRegExp, Object, raw_properties_or_hash)
HANDLE_ACCESSOR(JSRegExp, Object, source)

BIMODAL_ACCESSOR_B(Map, bit_field2, elements_kind, Map::ElementsKindBits)
BIMODAL_ACCESSOR_B(Map, bit_field3, is_deprecated, Map::IsDeprecatedBit)
BIMODAL_ACCESSOR_B(Map, bit_field3, is_dictionary_map, Map::IsDictionaryMapBit)
BIMODAL_ACCESSOR_B(Map, bit_field, has_prototype_slot, Map::HasPrototypeSlotBit)
BIMODAL_ACCESSOR_C(Map, int, instance_size)
HANDLE_ACCESSOR_C(Map, bool, CanBeDeprecated)
HANDLE_ACCESSOR_C(Map, bool, CanTransition)
HANDLE_ACCESSOR_C(Map, bool, IsInobjectSlackTrackingInProgress)
HANDLE_ACCESSOR_C(Map, bool, IsJSArrayMap)
HANDLE_ACCESSOR_C(Map, bool, is_stable)
HANDLE_ACCESSOR_C(Map, InstanceType, instance_type)
HANDLE_ACCESSOR_C(Map, int, GetInObjectProperties)
HANDLE_ACCESSOR_C(Map, int, GetInObjectPropertiesStartInWords)
HANDLE_ACCESSOR_C(Map, int, NumberOfOwnDescriptors)
HANDLE_ACCESSOR(Map, Object, constructor_or_backpointer)

HANDLE_ACCESSOR_C(MutableHeapNumber, double, value)

BIMODAL_ACCESSOR_(NativeContext, Context, Map, fast_aliased_arguments_map)
BIMODAL_ACCESSOR_(NativeContext, Context, Map, js_array_fast_elements_map)
BIMODAL_ACCESSOR_(NativeContext, Context, Map, sloppy_arguments_map)
BIMODAL_ACCESSOR_(NativeContext, Context, Map, strict_arguments_map)
HANDLE_ACCESSOR_(NativeContext, Context, JSFunction, array_function)
HANDLE_ACCESSOR_(NativeContext, Context, Map, initial_array_iterator_map)
HANDLE_ACCESSOR_(NativeContext, Context, Map, iterator_result_map)
HANDLE_ACCESSOR_(NativeContext, Context, Map, map_key_iterator_map)
HANDLE_ACCESSOR_(NativeContext, Context, Map, map_key_value_iterator_map)
HANDLE_ACCESSOR_(NativeContext, Context, Map, map_value_iterator_map)
HANDLE_ACCESSOR_(NativeContext, Context, Map, set_key_value_iterator_map)
HANDLE_ACCESSOR_(NativeContext, Context, Map, set_value_iterator_map)
HANDLE_ACCESSOR_(NativeContext, Context, Map, string_iterator_map)
HANDLE_ACCESSOR_(NativeContext, Context, ScriptContextTable,
                 script_context_table)

HANDLE_ACCESSOR(PropertyCell, Object, value)
HANDLE_ACCESSOR_C(PropertyCell, PropertyDetails, property_details)

HANDLE_ACCESSOR_C(ScopeInfo, int, ContextLength)

HANDLE_ACCESSOR_C(SharedFunctionInfo, bool, construct_as_builtin)
HANDLE_ACCESSOR_C(SharedFunctionInfo, bool, HasBreakInfo)
HANDLE_ACCESSOR_C(SharedFunctionInfo, bool, HasBuiltinId)
HANDLE_ACCESSOR_C(SharedFunctionInfo, bool, HasBytecodeArray)
HANDLE_ACCESSOR_C(SharedFunctionInfo, bool, has_duplicate_parameters)
HANDLE_ACCESSOR_C(SharedFunctionInfo, bool, native)
HANDLE_ACCESSOR_C(SharedFunctionInfo, FunctionKind, kind)
HANDLE_ACCESSOR_C(SharedFunctionInfo, int, builtin_id)
HANDLE_ACCESSOR_C(SharedFunctionInfo, int, function_map_index)
HANDLE_ACCESSOR_C(SharedFunctionInfo, int, internal_formal_parameter_count)
HANDLE_ACCESSOR_C(SharedFunctionInfo, LanguageMode, language_mode)

// TODO(neis): Provide StringShape() on StringRef.

bool JSFunctionRef::HasBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSFunction>()->shared()->HasBuiltinFunctionId();
}

BuiltinFunctionId JSFunctionRef::GetBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSFunction>()->shared()->builtin_function_id();
}

MapRef NativeContextRef::promise_function_initial_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                handle(object<Context>()->promise_function()->initial_map(),
                       broker()->isolate()));
}

MapRef NativeContextRef::GetFunctionMapFromIndex(int index) const {
  DCHECK_LE(index, Context::LAST_FUNCTION_MAP_INDEX);
  DCHECK_GE(index, Context::FIRST_FUNCTION_MAP_INDEX);
  return get(index).AsMap();
}

MapRef NativeContextRef::ObjectLiteralMapFromCache() const {
  AllowHeapAllocation heap_allocation;
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  Factory* factory = broker()->isolate()->factory();
  Handle<Map> map = factory->ObjectLiteralMapFromCache(object<Context>(), 0);
  return MapRef(broker(), map);
}

MapRef NativeContextRef::GetInitialJSArrayMap(ElementsKind kind) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  Handle<Map> map(object<Context>()->GetInitialJSArrayMap(kind),
                  broker()->isolate());
  return MapRef(broker(), map);
}

bool ObjectRef::BooleanValue() {
  AllowHandleDereference allow_handle_dereference;
  return object<Object>()->BooleanValue(broker()->isolate());
}

double ObjectRef::OddballToNumber() const {
  OddballType type = oddball_type();

  switch (type) {
    case OddballType::kBoolean: {
      ObjectRef true_ref(broker(),
                         broker()->isolate()->factory()->true_value());
      return this->equals(true_ref) ? 1 : 0;
      break;
    }
    case OddballType::kUndefined: {
      return std::numeric_limits<double>::quiet_NaN();
      break;
    }
    case OddballType::kNull: {
      return 0;
      break;
    }
    default: {
      UNREACHABLE();
      break;
    }
  }
}

CellRef ModuleRef::GetCell(int cell_index) {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return CellRef(broker(), handle(object<Module>()->GetCell(cell_index),
                                  broker()->isolate()));
}

ObjectRef::ObjectRef(JSHeapBroker* broker, Handle<Object> object) {
  switch (broker->mode()) {
    case JSHeapBroker::kSerialized:
      data_ = FLAG_strict_heap_broker ? broker->GetData(object)
                                      : broker->GetOrCreateData(object);
      break;
    case JSHeapBroker::kSerializing:
      data_ = broker->GetOrCreateData(object);
      break;
    case JSHeapBroker::kDisabled:
      data_ = broker->GetData(object);
      if (data_ == nullptr) {
        AllowHandleDereference handle_dereference;
        data_ =
            new (broker->zone()) ObjectData(broker, object, object->IsSmi());
      }
      break;
  }
  CHECK_NOT_NULL(data_);
}

Handle<Object> ObjectRef::object() const { return data_->object; }

JSHeapBroker* ObjectRef::broker() const { return data_->broker; }

ObjectData* ObjectRef::data() const { return data_; }

#undef BIMODAL_ACCESSOR
#undef BIMODAL_ACCESSOR_
#undef BIMODAL_ACCESSOR_B
#undef BIMODAL_ACCESSOR_C
#undef GET_OR_CREATE
#undef HANDLE_ACCESSOR
#undef HANDLE_ACCESSOR_
#undef HANDLE_ACCESSOR_C
#undef NATIVE_CONTEXT_DATA

}  // namespace compiler
}  // namespace internal
}  // namespace v8
