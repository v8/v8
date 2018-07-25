// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-heap-broker.h"

#include "src/compiler/compilation-dependencies.h"
#include "src/objects-inl.h"
#include "src/objects/js-regexp-inl.h"
#include "src/objects/module-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

// TODO(neis): When do we need NON_EXPORTED_BASE?
class ObjectData : public ZoneObject {
 public:
  ObjectData(JSHeapBroker* broker_, Handle<Object> object_)
      : broker(broker_), object(object_) {}

  JSHeapBroker* broker;
  Handle<Object> object;

#define DEFINE_IS(Name)                              \
  bool Is##Name() const {                            \
    AllowHandleDereference allow_handle_dereference; \
    return object->Is##Name();                       \
  }
  HEAP_BROKER_NORMAL_OBJECT_LIST(DEFINE_IS)
  DEFINE_IS(Smi)
#undef DEFINE_IS
};

MapRef HeapObjectRef::map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                handle(object<HeapObject>()->map(), broker()->isolate()));
}

double HeapNumberRef::value() const {
  AllowHandleDereference allow_handle_dereference;
  return object<HeapNumber>()->value();
}

double MutableHeapNumberRef::value() const {
  AllowHandleDereference allow_handle_dereference;
  return object<MutableHeapNumber>()->value();
}

int ObjectRef::AsSmi() const { return object<Smi>()->value(); }

bool ObjectRef::equals(const ObjectRef& other) const {
  DCHECK_EQ(data_ == other.data_, data_->object.equals(other.data_->object));
  return data_ == other.data_;
}

StringRef ObjectRef::TypeOf() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  return StringRef(broker(),
                   Object::TypeOf(broker()->isolate(), object<Object>()));
}

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
    : isolate_(isolate), zone_(zone), refs_(zone_) {}

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

#define DEFINE_IS_AND_AS(Name)                                    \
  bool ObjectRef::Is##Name() const { return data()->Is##Name(); } \
  Name##Ref ObjectRef::As##Name() const {                         \
    DCHECK(Is##Name());                                           \
    return Name##Ref(data());                                     \
  }
HEAP_BROKER_NORMAL_OBJECT_LIST(DEFINE_IS_AND_AS)
#undef DEFINE_IS_AND_AS

bool ObjectRef::IsSmi() const { return data()->IsSmi(); }

FieldTypeRef ObjectRef::AsFieldType() const {
  return FieldTypeRef(broker(), object<Object>());
}

HeapObjectType HeapObjectRef::type() const {
  AllowHandleDereference allow_handle_dereference;
  // TODO(neis): When this gets called via OptimizingCompileDispatcher ->
  // LoadElimination -> TypeNarrowingReducer, we don't have a HandleScope. Why?
  return broker()->HeapObjectTypeFromMap(object<HeapObject>()->map());
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

bool HeapObjectRef::IsSeqString() const {
  AllowHandleDereference allow_handle_dereference;
  return object<HeapObject>()->IsSeqString();
}

bool HeapObjectRef::IsExternalString() const {
  AllowHandleDereference allow_handle_dereference;
  return object<HeapObject>()->IsExternalString();
}

bool JSFunctionRef::HasBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSFunction>()->shared()->HasBuiltinFunctionId();
}

BuiltinFunctionId JSFunctionRef::GetBuiltinFunctionId() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSFunction>()->shared()->builtin_function_id();
}

bool JSFunctionRef::IsConstructor() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSFunction>()->IsConstructor();
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
MapRef MapRef::AsElementsKind(ElementsKind kind) const {
  AllowHandleAllocation handle_allocation;
  AllowHeapAllocation heap_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                Map::AsElementsKind(broker()->isolate(), object<Map>(), kind));
}

SlackTrackingResult JSFunctionRef::FinishSlackTracking() const {
  AllowHandleDereference allow_handle_dereference;
  AllowHandleAllocation handle_allocation;
  object<JSFunction>()->CompleteInobjectSlackTrackingIfActive();
  int instance_size = object<JSFunction>()->initial_map()->instance_size();
  int inobject_property_count =
      object<JSFunction>()->initial_map()->GetInObjectProperties();
  return SlackTrackingResult(instance_size, inobject_property_count);
}

bool JSFunctionRef::has_initial_map() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSFunction>()->has_initial_map();
}

MapRef JSFunctionRef::initial_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(), handle(object<JSFunction>()->initial_map(),
                                 broker()->isolate()));
}

SharedFunctionInfoRef JSFunctionRef::shared() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return SharedFunctionInfoRef(
      broker(), handle(object<JSFunction>()->shared(), broker()->isolate()));
}

JSGlobalProxyRef JSFunctionRef::global_proxy() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return JSGlobalProxyRef(broker(), handle(object<JSFunction>()->global_proxy(),
                                           broker()->isolate()));
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

ScriptContextTableRef NativeContextRef::script_context_table() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  return ScriptContextTableRef(
      broker(),
      handle(object<Context>()->script_context_table(), broker()->isolate()));
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

JSObjectRef AllocationSiteRef::boilerplate() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Handle<JSObject> value(object<AllocationSite>()->boilerplate(),
                         broker()->isolate());
  return JSObjectRef(broker(), value);
}

ObjectRef AllocationSiteRef::nested_site() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  Handle<Object> obj(object<AllocationSite>()->nested_site(),
                     broker()->isolate());
  return ObjectRef(broker(), obj);
}

bool AllocationSiteRef::PointsToLiteral() const {
  AllowHandleDereference handle_dereference;
  return object<AllocationSite>()->PointsToLiteral();
}

ElementsKind AllocationSiteRef::GetElementsKind() const {
  AllowHandleDereference handle_dereference;
  return object<AllocationSite>()->GetElementsKind();
}

bool AllocationSiteRef::CanInlineCall() const {
  AllowHandleDereference handle_dereference;
  return object<AllocationSite>()->CanInlineCall();
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

ElementsKind JSObjectRef::GetElementsKind() {
  AllowHandleDereference handle_dereference;
  return object<JSObject>()->GetElementsKind();
}

FixedArrayBaseRef JSObjectRef::elements() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference handle_dereference;
  return FixedArrayBaseRef(
      broker(), handle(object<JSObject>()->elements(), broker()->isolate()));
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

PretenureFlag AllocationSiteRef::GetPretenureMode() const {
  AllowHandleDereference allow_handle_dereference;
  return object<AllocationSite>()->GetPretenureMode();
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

bool MapRef::is_deprecated() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->is_deprecated();
}

bool MapRef::CanBeDeprecated() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->CanBeDeprecated();
}

int MapRef::GetInObjectProperties() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->GetInObjectProperties();
}

int MapRef::NumberOfOwnDescriptors() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->NumberOfOwnDescriptors();
}

FieldIndex MapRef::GetFieldIndexFor(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return FieldIndex::ForDescriptor(*object<Map>(), i);
}

int MapRef::GetInObjectPropertyOffset(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->GetInObjectPropertyOffset(i);
}

bool MapRef::is_dictionary_map() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->is_dictionary_map();
}

ObjectRef MapRef::constructor_or_backpointer() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(broker(), handle(object<Map>()->constructor_or_backpointer(),
                                    broker()->isolate()));
}

ElementsKind MapRef::elements_kind() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->elements_kind();
}

int MapRef::instance_size() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->instance_size();
}

InstanceType MapRef::instance_type() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->instance_type();
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

bool MapRef::IsJSArrayMap() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->IsJSArrayMap();
}

bool MapRef::IsInobjectSlackTrackingInProgress() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->IsInobjectSlackTrackingInProgress();
}

bool MapRef::IsFixedCowArrayMap() const {
  AllowHandleDereference allow_handle_dereference;
  return *object<Map>() ==
         ReadOnlyRoots(broker()->isolate()).fixed_cow_array_map();
}

bool MapRef::has_prototype_slot() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->has_prototype_slot();
}

bool MapRef::is_stable() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->is_stable();
}

bool MapRef::CanTransition() const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->CanTransition();
}

MapRef MapRef::FindFieldOwner(int descriptor) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  Handle<Map> owner(
      object<Map>()->FindFieldOwner(broker()->isolate(), descriptor),
      broker()->isolate());
  return MapRef(broker(), owner);
}

FieldTypeRef MapRef::GetFieldType(int descriptor) const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  Handle<FieldType> field_type(
      object<Map>()->instance_descriptors()->GetFieldType(descriptor),
      broker()->isolate());
  return FieldTypeRef(broker(), field_type);
}

ElementsKind JSArrayRef::GetElementsKind() const {
  AllowHandleDereference allow_handle_dereference;
  return object<JSArray>()->GetElementsKind();
}

ObjectRef JSArrayRef::length() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(broker(),
                   handle(object<JSArray>()->length(), broker()->isolate()));
}

int StringRef::length() const {
  AllowHandleDereference allow_handle_dereference;
  return object<String>()->length();
}

uint16_t StringRef::GetFirstChar() {
  AllowHandleDereference allow_handle_dereference;
  return object<String>()->Get(0);
}

double StringRef::ToNumber() {
  AllowHandleDereference allow_handle_dereference;
  AllowHandleAllocation allow_handle_allocation;
  AllowHeapAllocation allow_heap_allocation;
  int flags = ALLOW_HEX | ALLOW_OCTAL | ALLOW_BINARY;
  return StringToDouble(broker()->isolate(),
                        broker()->isolate()->unicode_cache(), object<String>(),
                        flags);
}

ObjectRef JSRegExpRef::raw_properties_or_hash() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(broker(),
                   handle(object<JSRegExp>()->raw_properties_or_hash(),
                          broker()->isolate()));
}

ObjectRef JSRegExpRef::data() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(broker(),
                   handle(object<JSRegExp>()->data(), broker()->isolate()));
}

ObjectRef JSRegExpRef::source() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(broker(),
                   handle(object<JSRegExp>()->source(), broker()->isolate()));
}

ObjectRef JSRegExpRef::flags() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(broker(),
                   handle(object<JSRegExp>()->flags(), broker()->isolate()));
}

ObjectRef JSRegExpRef::last_index() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(
      broker(), handle(object<JSRegExp>()->last_index(), broker()->isolate()));
}

int FixedArrayBaseRef::length() const {
  AllowHandleDereference allow_handle_dereference;
  return object<FixedArrayBase>()->length();
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

int ScopeInfoRef::ContextLength() const {
  AllowHandleDereference allow_handle_dereference;
  return object<ScopeInfo>()->ContextLength();
}

int SharedFunctionInfoRef::internal_formal_parameter_count() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->internal_formal_parameter_count();
}

int SharedFunctionInfoRef::function_map_index() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->function_map_index();
}

bool SharedFunctionInfoRef::has_duplicate_parameters() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->has_duplicate_parameters();
}

FunctionKind SharedFunctionInfoRef::kind() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->kind();
}

LanguageMode SharedFunctionInfoRef::language_mode() {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->language_mode();
}

bool SharedFunctionInfoRef::native() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->native();
}

bool SharedFunctionInfoRef::HasBreakInfo() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->HasBreakInfo();
}

bool SharedFunctionInfoRef::HasBuiltinId() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->HasBuiltinId();
}

int SharedFunctionInfoRef::builtin_id() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->builtin_id();
}

bool SharedFunctionInfoRef::construct_as_builtin() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->construct_as_builtin();
}

bool SharedFunctionInfoRef::HasBytecodeArray() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->HasBytecodeArray();
}

int SharedFunctionInfoRef::GetBytecodeArrayRegisterCount() const {
  AllowHandleDereference allow_handle_dereference;
  return object<SharedFunctionInfo>()->GetBytecodeArray()->register_count();
}

MapRef NativeContextRef::fast_aliased_arguments_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                handle(object<Context>()->fast_aliased_arguments_map(),
                       broker()->isolate()));
}

MapRef NativeContextRef::sloppy_arguments_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(), handle(object<Context>()->sloppy_arguments_map(),
                                 broker()->isolate()));
}

MapRef NativeContextRef::strict_arguments_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(), handle(object<Context>()->strict_arguments_map(),
                                 broker()->isolate()));
}

MapRef NativeContextRef::js_array_fast_elements_map_index() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                handle(object<Context>()->js_array_fast_elements_map_index(),
                       broker()->isolate()));
}

MapRef NativeContextRef::initial_array_iterator_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                handle(object<Context>()->initial_array_iterator_map(),
                       broker()->isolate()));
}

MapRef NativeContextRef::set_value_iterator_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(), handle(object<Context>()->set_value_iterator_map(),
                                 broker()->isolate()));
}

MapRef NativeContextRef::set_key_value_iterator_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                handle(object<Context>()->set_key_value_iterator_map(),
                       broker()->isolate()));
}

MapRef NativeContextRef::map_key_iterator_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(), handle(object<Context>()->map_key_iterator_map(),
                                 broker()->isolate()));
}

MapRef NativeContextRef::map_value_iterator_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(), handle(object<Context>()->map_value_iterator_map(),
                                 broker()->isolate()));
}

MapRef NativeContextRef::map_key_value_iterator_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                handle(object<Context>()->map_key_value_iterator_map(),
                       broker()->isolate()));
}

MapRef NativeContextRef::iterator_result_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(), handle(object<Context>()->iterator_result_map(),
                                 broker()->isolate()));
}

MapRef NativeContextRef::string_iterator_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(), handle(object<Context>()->string_iterator_map(),
                                 broker()->isolate()));
}

MapRef NativeContextRef::promise_function_initial_map() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return MapRef(broker(),
                handle(object<Context>()->promise_function()->initial_map(),
                       broker()->isolate()));
}

JSFunctionRef NativeContextRef::array_function() const {
  AllowHandleAllocation handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return JSFunctionRef(broker(), handle(object<Context>()->array_function(),
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

ObjectRef PropertyCellRef::value() const {
  AllowHandleAllocation allow_handle_allocation;
  AllowHandleDereference allow_handle_dereference;
  return ObjectRef(
      broker(), handle(object<PropertyCell>()->value(), broker()->isolate()));
}

PropertyDetails PropertyCellRef::property_details() const {
  AllowHandleDereference allow_handle_dereference;
  return object<PropertyCell>()->property_details();
}

ObjectRef::ObjectRef(JSHeapBroker* broker, Handle<Object> object)
    : data_(nullptr) {
  DisallowHeapAccess no_heap_access;
  // TODO(neis): After serialization pass, only read from the hash table.
  auto x = broker->refs_.insert({object.address(), nullptr});
  bool const inserted = x.second;
  if (inserted) {
    x.first->second = new (broker->zone_) ObjectData(broker, object);
  }
  data_ = x.first->second;
  CHECK_NOT_NULL(data_);
}

Handle<Object> ObjectRef::object() const { return data_->object; }

JSHeapBroker* ObjectRef::broker() const { return data_->broker; }

ObjectData* ObjectRef::data() const { return data_; }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
