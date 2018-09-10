// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-heap-broker.h"

#include "src/boxed-float.h"
#include "src/compiler/graph-reducer.h"
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

  ObjectData(JSHeapBroker* broker, Handle<Object> object, bool is_smi)
      : broker_(broker), object_(object), is_smi_(is_smi) {
    broker->AddData(object, this);
  }

#define DECLARE_IS_AND_AS(Name) \
  bool Is##Name() const;        \
  Name##Data* As##Name();
  HEAP_BROKER_OBJECT_LIST(DECLARE_IS_AND_AS)
#undef DECLARE_IS_AND_AS

  JSHeapBroker* broker() const { return broker_; }
  Handle<Object> object() const { return object_; }
  bool is_smi() const { return is_smi_; }

 private:
  JSHeapBroker* const broker_;
  Handle<Object> const object_;
  bool const is_smi_;
};

class HeapObjectData : public ObjectData {
 public:
  static HeapObjectData* Serialize(JSHeapBroker* broker,
                                   Handle<HeapObject> object);

  HeapObjectType type() const { return type_; }
  MapData* map() const { return map_; }

  HeapObjectData(JSHeapBroker* broker, Handle<HeapObject> object,
                 HeapObjectType type);

 private:
  HeapObjectType const type_;
  MapData* const map_;
};

class PropertyCellData : public HeapObjectData {
 public:
  PropertyCellData(JSHeapBroker* broker, Handle<PropertyCell> object,
                   HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

class JSObjectField {
 public:
  bool IsDouble() const { return object_ == nullptr; }
  double AsDouble() const {
    CHECK(IsDouble());
    return number_;
  }

  bool IsObject() const { return object_ != nullptr; }
  ObjectData* AsObject() const {
    CHECK(IsObject());
    return object_;
  }

  explicit JSObjectField(double value) : number_(value) {}
  explicit JSObjectField(ObjectData* value) : object_(value) {}

 private:
  ObjectData* object_ = nullptr;
  double number_ = 0;
};

class JSObjectData : public HeapObjectData {
 public:
  JSObjectData(JSHeapBroker* broker, Handle<JSObject> object,
               HeapObjectType type);

  // Recursively serializes all reachable JSObjects.
  void SerializeAsBoilerplate();
  // Shallow serialization of {elements}.
  void SerializeElements();

  const JSObjectField& GetInobjectField(int property_index) const;
  FixedArrayBaseData* elements() const;

  // This method is only used to assert our invariants.
  bool cow_or_empty_elements_tenured() const;

 private:
  void SerializeRecursive(int max_depths);

  FixedArrayBaseData* elements_ = nullptr;
  bool cow_or_empty_elements_tenured_ = false;
  // The {serialized_as_boilerplate} flag is set when all recursively
  // reachable JSObjects are serialized.
  bool serialized_as_boilerplate_ = false;
  bool serialized_elements_ = false;

  ZoneVector<JSObjectField> inobject_fields_;
};

class JSFunctionData : public JSObjectData {
 public:
  bool has_initial_map() const { return has_initial_map_; }
  bool has_prototype() const { return has_prototype_; }
  bool PrototypeRequiresRuntimeLookup() const {
    return PrototypeRequiresRuntimeLookup_;
  }

  JSGlobalProxyData* global_proxy() const { return global_proxy_; }
  MapData* initial_map() const { return initial_map_; }
  ObjectData* prototype() const { return prototype_; }
  SharedFunctionInfoData* shared() const { return shared_; }

  JSFunctionData(JSHeapBroker* broker, Handle<JSFunction> object,
                 HeapObjectType type);
  void Serialize();

 private:
  bool has_initial_map_;
  bool has_prototype_;
  bool PrototypeRequiresRuntimeLookup_;

  bool serialized_ = false;

  JSGlobalProxyData* global_proxy_ = nullptr;
  MapData* initial_map_ = nullptr;
  ObjectData* prototype_ = nullptr;
  SharedFunctionInfoData* shared_ = nullptr;
};

class JSRegExpData : public JSObjectData {
 public:
  JSRegExpData(JSHeapBroker* broker, Handle<JSRegExp> object,
               HeapObjectType type)
      : JSObjectData(broker, object, type) {}

  void SerializeAsRegExpBoilerplate();

  ObjectData* raw_properties_or_hash() const { return raw_properties_or_hash_; }
  ObjectData* data() const { return data_; }
  ObjectData* source() const { return source_; }
  ObjectData* flags() const { return flags_; }
  ObjectData* last_index() const { return last_index_; }

 private:
  bool serialized_as_reg_exp_boilerplate_ = false;

  ObjectData* raw_properties_or_hash_ = nullptr;
  ObjectData* data_ = nullptr;
  ObjectData* source_ = nullptr;
  ObjectData* flags_ = nullptr;
  ObjectData* last_index_ = nullptr;
};

class HeapNumberData : public HeapObjectData {
 public:
  HeapNumberData(JSHeapBroker* broker, Handle<HeapNumber> object,
                 HeapObjectType type)
      : HeapObjectData(broker, object, type), value_(object->value()) {}

  double value() const { return value_; }

 private:
  double const value_;
};

class MutableHeapNumberData : public HeapObjectData {
 public:
  MutableHeapNumberData(JSHeapBroker* broker, Handle<MutableHeapNumber> object,
                        HeapObjectType type)
      : HeapObjectData(broker, object, type), value_(object->value()) {}

  double value() const { return value_; }

 private:
  double const value_;
};

class ContextData : public HeapObjectData {
 public:
  ContextData(JSHeapBroker* broker, Handle<Context> object, HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

class NativeContextData : public ContextData {
 public:
#define DECL_ACCESSOR(type, name) \
  type##Data* name() const { return name##_; }
  BROKER_NATIVE_CONTEXT_FIELDS(DECL_ACCESSOR)
#undef DECL_ACCESSOR

  NativeContextData(JSHeapBroker* broker, Handle<NativeContext> object,
                    HeapObjectType type);
  void Serialize();

 private:
  bool serialized_ = false;
#define DECL_MEMBER(type, name) type##Data* name##_ = nullptr;
  BROKER_NATIVE_CONTEXT_FIELDS(DECL_MEMBER)
#undef DECL_MEMBER
};

class NameData : public HeapObjectData {
 public:
  NameData(JSHeapBroker* broker, Handle<Name> object, HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

class StringData : public NameData {
 public:
  int length() const { return length_; }
  uint16_t first_char() const { return first_char_; }
  base::Optional<double> to_number() const { return to_number_; }

  StringData(JSHeapBroker* broker, Handle<String> object, HeapObjectType type)
      : NameData(broker, object, type),
        length_(object->length()),
        first_char_(length_ > 0 ? object->Get(0) : 0) {
    int flags = ALLOW_HEX | ALLOW_OCTAL | ALLOW_BINARY;
    if (length_ <= kMaxLengthForDoubleConversion) {
      to_number_ = StringToDouble(
          broker->isolate(), broker->isolate()->unicode_cache(), object, flags);
    }
  }

 private:
  int const length_;
  uint16_t const first_char_;
  base::Optional<double> to_number_;

  static constexpr int kMaxLengthForDoubleConversion = 23;
};

class InternalizedStringData : public StringData {
 public:
  InternalizedStringData(JSHeapBroker* broker,
                         Handle<InternalizedString> object, HeapObjectType type)
      : StringData(broker, object, type) {}
};

namespace {

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
bool IsInlinableFastLiteral(Handle<JSObject> boilerplate) {
  int max_properties = kMaxFastLiteralProperties;
  return IsFastLiteralHelper(boilerplate, kMaxFastLiteralDepth,
                             &max_properties);
}

}  // namespace

class AllocationSiteData : public HeapObjectData {
 public:
  AllocationSiteData(JSHeapBroker* broker, Handle<AllocationSite> object,
                     HeapObjectType type);
  void SerializeBoilerplate();

  bool PointsToLiteral() const { return PointsToLiteral_; }
  PretenureFlag GetPretenureMode() const { return GetPretenureMode_; }
  ObjectData* nested_site() const { return nested_site_; }
  bool IsFastLiteral() const { return IsFastLiteral_; }
  JSObjectData* boilerplate() const { return boilerplate_; }

  // These are only valid if PointsToLiteral is false.
  ElementsKind GetElementsKind() const { return GetElementsKind_; }
  bool CanInlineCall() const { return CanInlineCall_; }

 private:
  bool const PointsToLiteral_;
  PretenureFlag const GetPretenureMode_;
  ObjectData* nested_site_ = nullptr;
  bool IsFastLiteral_ = false;
  JSObjectData* boilerplate_ = nullptr;
  ElementsKind GetElementsKind_ = NO_ELEMENTS;
  bool CanInlineCall_ = false;
  bool serialized_boilerplate_ = false;
};

// Only used in JSNativeContextSpecialization.
class ScriptContextTableData : public HeapObjectData {
 public:
  ScriptContextTableData(JSHeapBroker* broker,
                         Handle<ScriptContextTable> object, HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

struct PropertyDescriptor {
  NameData* key = nullptr;
  PropertyDetails details = PropertyDetails::Empty();
  FieldIndex field_index;
  MapData* field_owner = nullptr;
  ObjectData* field_type = nullptr;
};

class MapData : public HeapObjectData {
 public:
  InstanceType instance_type() const { return instance_type_; }
  int instance_size() const { return instance_size_; }
  byte bit_field() const { return bit_field_; }
  byte bit_field2() const { return bit_field2_; }
  uint32_t bit_field3() const { return bit_field3_; }

  MapData(JSHeapBroker* broker, Handle<Map> object, HeapObjectType type);

  // Extra information.

  void SerializeElementsKindGeneralizations();
  const ZoneVector<MapData*>& elements_kind_generalizations() const {
    CHECK(serialized_elements_kind_generalizations_);
    return elements_kind_generalizations_;
  }

  // Serialize the descriptor array and, recursively, that of any field owner.
  void SerializeDescriptors();
  const ZoneVector<PropertyDescriptor>& descriptors() const {
    CHECK(serialized_descriptors_);
    return descriptors_;
  }

 private:
  InstanceType const instance_type_;
  int const instance_size_;
  byte const bit_field_;
  byte const bit_field2_;
  uint32_t const bit_field3_;

  bool serialized_elements_kind_generalizations_ = false;
  ZoneVector<MapData*> elements_kind_generalizations_;

  bool serialized_descriptors_ = false;
  ZoneVector<PropertyDescriptor> descriptors_;
};

AllocationSiteData::AllocationSiteData(JSHeapBroker* broker,
                                       Handle<AllocationSite> object,
                                       HeapObjectType type)
    : HeapObjectData(broker, object, type),
      PointsToLiteral_(object->PointsToLiteral()),
      GetPretenureMode_(object->GetPretenureMode()) {
  if (PointsToLiteral_) {
    IsFastLiteral_ = IsInlinableFastLiteral(
        handle(object->boilerplate(), broker->isolate()));
  } else {
    GetElementsKind_ = object->GetElementsKind();
    CanInlineCall_ = object->CanInlineCall();
  }
}

void AllocationSiteData::SerializeBoilerplate() {
  if (serialized_boilerplate_) return;
  serialized_boilerplate_ = true;

  Handle<AllocationSite> site = Handle<AllocationSite>::cast(object());

  CHECK(IsFastLiteral_);
  DCHECK_NULL(boilerplate_);
  boilerplate_ = broker()->GetOrCreateData(site->boilerplate())->AsJSObject();
  boilerplate_->SerializeAsBoilerplate();

  DCHECK_NULL(nested_site_);
  nested_site_ = broker()->GetOrCreateData(site->nested_site());
  if (nested_site_->IsAllocationSite()) {
    nested_site_->AsAllocationSite()->SerializeBoilerplate();
  }
}

HeapObjectData::HeapObjectData(JSHeapBroker* broker, Handle<HeapObject> object,
                               HeapObjectType type)
    : ObjectData(broker, object, false),
      type_(type),
      map_(broker->GetOrCreateData(object->map())->AsMap()) {
  CHECK(broker->SerializingAllowed());
}

MapData::MapData(JSHeapBroker* broker, Handle<Map> object, HeapObjectType type)
    : HeapObjectData(broker, object, type),
      instance_type_(object->instance_type()),
      instance_size_(object->instance_size()),
      bit_field_(object->bit_field()),
      bit_field2_(object->bit_field2()),
      bit_field3_(object->bit_field3()),
      elements_kind_generalizations_(broker->zone()),
      descriptors_(broker->zone()) {}

JSFunctionData::JSFunctionData(JSHeapBroker* broker, Handle<JSFunction> object,
                               HeapObjectType type)
    : JSObjectData(broker, object, type),
      has_initial_map_(object->has_prototype_slot() &&
                       object->has_initial_map()),
      has_prototype_(object->has_prototype_slot() && object->has_prototype()),
      PrototypeRequiresRuntimeLookup_(
          object->PrototypeRequiresRuntimeLookup()) {}

void JSFunctionData::Serialize() {
  if (serialized_) return;
  serialized_ = true;

  Handle<JSFunction> function = Handle<JSFunction>::cast(object());

  DCHECK_NULL(global_proxy_);
  DCHECK_NULL(initial_map_);
  DCHECK_NULL(prototype_);
  DCHECK_NULL(shared_);

  global_proxy_ =
      broker()->GetOrCreateData(function->global_proxy())->AsJSGlobalProxy();
  shared_ =
      broker()->GetOrCreateData(function->shared())->AsSharedFunctionInfo();
  initial_map_ =
      has_initial_map()
          ? broker()->GetOrCreateData(function->initial_map())->AsMap()
          : nullptr;
  prototype_ = has_prototype()
                   ? broker()->GetOrCreateData(function->prototype())
                   : nullptr;

  if (initial_map_ != nullptr &&
      initial_map_->instance_type() == JS_ARRAY_TYPE) {
    initial_map_->SerializeElementsKindGeneralizations();
  }
}

void MapData::SerializeElementsKindGeneralizations() {
  if (serialized_elements_kind_generalizations_) return;
  serialized_elements_kind_generalizations_ = true;

  broker()->Trace("Computing ElementsKind generalizations of %p.\n", *object());
  DCHECK_EQ(instance_type(), JS_ARRAY_TYPE);
  MapRef self(this);
  ElementsKind from_kind = self.elements_kind();
  DCHECK(elements_kind_generalizations_.empty());
  for (int i = FIRST_FAST_ELEMENTS_KIND; i <= LAST_FAST_ELEMENTS_KIND; i++) {
    ElementsKind to_kind = static_cast<ElementsKind>(i);
    if (IsMoreGeneralElementsKindTransition(from_kind, to_kind)) {
      Handle<Map> target =
          Map::AsElementsKind(broker()->isolate(), self.object<Map>(), to_kind);
      elements_kind_generalizations_.push_back(
          broker()->GetOrCreateData(target)->AsMap());
    }
  }
}

class FeedbackVectorData : public HeapObjectData {
 public:
  const ZoneVector<ObjectData*>& feedback() { return feedback_; }

  FeedbackVectorData(JSHeapBroker* broker, Handle<FeedbackVector> object,
                     HeapObjectType type);

  void SerializeSlots();

 private:
  bool serialized_ = false;
  ZoneVector<ObjectData*> feedback_;
};

FeedbackVectorData::FeedbackVectorData(JSHeapBroker* broker,
                                       Handle<FeedbackVector> object,
                                       HeapObjectType type)
    : HeapObjectData(broker, object, type), feedback_(broker->zone()) {}

void FeedbackVectorData::SerializeSlots() {
  if (serialized_) return;
  serialized_ = true;

  Handle<FeedbackVector> vector = Handle<FeedbackVector>::cast(object());
  DCHECK(feedback_.empty());
  feedback_.reserve(vector->length());
  for (int i = 0; i < vector->length(); ++i) {
    MaybeObject* value = vector->get(i);
    ObjectData* slot_value = value->IsObject()
                                 ? broker()->GetOrCreateData(value->ToObject())
                                 : nullptr;
    feedback_.push_back(slot_value);
    if (slot_value == nullptr) continue;

    if (slot_value->IsAllocationSite() &&
        slot_value->AsAllocationSite()->IsFastLiteral()) {
      slot_value->AsAllocationSite()->SerializeBoilerplate();
    } else if (slot_value->IsJSRegExp()) {
      slot_value->AsJSRegExp()->SerializeAsRegExpBoilerplate();
    }
  }
  DCHECK_EQ(vector->length(), feedback_.size());
}

class FixedArrayBaseData : public HeapObjectData {
 public:
  FixedArrayBaseData(JSHeapBroker* broker, Handle<FixedArrayBase> object,
                     HeapObjectType type)
      : HeapObjectData(broker, object, type), length_(object->length()) {}

  int length() const { return length_; }

 private:
  int const length_;
};

JSObjectData::JSObjectData(JSHeapBroker* broker, Handle<JSObject> object,
                           HeapObjectType type)
    : HeapObjectData(broker, object, type), inobject_fields_(broker->zone()) {}

class FixedArrayData : public FixedArrayBaseData {
 public:
  FixedArrayData(JSHeapBroker* broker, Handle<FixedArray> object,
                 HeapObjectType type);

  // Creates all elements of the fixed array.
  void SerializeContents();

  ObjectData* Get(int i) const;

 private:
  bool serialized_contents_ = false;
  ZoneVector<ObjectData*> contents_;
};

void FixedArrayData::SerializeContents() {
  if (serialized_contents_) return;
  serialized_contents_ = true;

  Handle<FixedArray> array = Handle<FixedArray>::cast(object());
  CHECK_EQ(array->length(), length());
  CHECK(contents_.empty());
  contents_.reserve(static_cast<size_t>(length()));

  for (int i = 0; i < length(); i++) {
    Handle<Object> value = handle(array->get(i), broker()->isolate());
    contents_.push_back(broker()->GetOrCreateData(value));
  }
}

FixedArrayData::FixedArrayData(JSHeapBroker* broker, Handle<FixedArray> object,
                               HeapObjectType type)
    : FixedArrayBaseData(broker, object, type), contents_(broker->zone()) {}

class FixedDoubleArrayData : public FixedArrayBaseData {
 public:
  FixedDoubleArrayData(JSHeapBroker* broker, Handle<FixedDoubleArray> object,
                       HeapObjectType type);

  // Serializes all elements of the fixed array.
  void SerializeContents();

  Float64 Get(int i) const;

 private:
  bool serialized_contents_ = false;
  ZoneVector<Float64> contents_;
};

FixedDoubleArrayData::FixedDoubleArrayData(JSHeapBroker* broker,
                                           Handle<FixedDoubleArray> object,
                                           HeapObjectType type)
    : FixedArrayBaseData(broker, object, type), contents_(broker->zone()) {}

void FixedDoubleArrayData::SerializeContents() {
  if (serialized_contents_) return;
  serialized_contents_ = true;

  Handle<FixedDoubleArray> self = Handle<FixedDoubleArray>::cast(object());
  CHECK_EQ(self->length(), length());
  CHECK(contents_.empty());
  contents_.reserve(static_cast<size_t>(length()));

  for (int i = 0; i < length(); i++) {
    contents_.push_back(Float64::FromBits(self->get_representation(i)));
  }
}

class BytecodeArrayData : public FixedArrayBaseData {
 public:
  int register_count() const { return register_count_; }

  BytecodeArrayData(JSHeapBroker* broker, Handle<BytecodeArray> object,
                    HeapObjectType type)
      : FixedArrayBaseData(broker, object, type),
        register_count_(object->register_count()) {}

 private:
  int const register_count_;
};

class JSArrayData : public JSObjectData {
 public:
  JSArrayData(JSHeapBroker* broker, Handle<JSArray> object, HeapObjectType type)
      : JSObjectData(broker, object, type) {}
};

class ScopeInfoData : public HeapObjectData {
 public:
  ScopeInfoData(JSHeapBroker* broker, Handle<ScopeInfo> object,
                HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

class SharedFunctionInfoData : public HeapObjectData {
 public:
  int builtin_id() const { return builtin_id_; }
  BytecodeArrayData* GetBytecodeArray() const { return GetBytecodeArray_; }
#define DECL_ACCESSOR(type, name) \
  type name() const { return name##_; }
  BROKER_SFI_FIELDS(DECL_ACCESSOR)
#undef DECL_ACCESSOR

  SharedFunctionInfoData(JSHeapBroker* broker,
                         Handle<SharedFunctionInfo> object, HeapObjectType type)
      : HeapObjectData(broker, object, type),
        builtin_id_(object->HasBuiltinId() ? object->builtin_id()
                                           : Builtins::kNoBuiltinId),
        GetBytecodeArray_(
            object->HasBytecodeArray()
                ? broker->GetOrCreateData(object->GetBytecodeArray())
                      ->AsBytecodeArray()
                : nullptr)
#define INIT_MEMBER(type, name) , name##_(object->name())
            BROKER_SFI_FIELDS(INIT_MEMBER)
#undef INIT_MEMBER
  {
    DCHECK_EQ(HasBuiltinId_, builtin_id_ != Builtins::kNoBuiltinId);
    DCHECK_EQ(HasBytecodeArray_, GetBytecodeArray_ != nullptr);
  }

 private:
  int const builtin_id_;
  BytecodeArrayData* const GetBytecodeArray_;
#define DECL_MEMBER(type, name) type const name##_;
  BROKER_SFI_FIELDS(DECL_MEMBER)
#undef DECL_MEMBER
};

class ModuleData : public HeapObjectData {
 public:
  ModuleData(JSHeapBroker* broker, Handle<Module> object, HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

class CellData : public HeapObjectData {
 public:
  CellData(JSHeapBroker* broker, Handle<Cell> object, HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

class JSGlobalProxyData : public JSObjectData {
 public:
  JSGlobalProxyData(JSHeapBroker* broker, Handle<JSGlobalProxy> object,
                    HeapObjectType type)
      : JSObjectData(broker, object, type) {}
};

class CodeData : public HeapObjectData {
 public:
  CodeData(JSHeapBroker* broker, Handle<Code> object, HeapObjectType type)
      : HeapObjectData(broker, object, type) {}
};

#define DEFINE_IS_AND_AS(Name)                                            \
  bool ObjectData::Is##Name() const {                                     \
    if (broker()->mode() == JSHeapBroker::kDisabled) {                    \
      AllowHandleDereference allow_handle_dereference;                    \
      return object()->Is##Name();                                        \
    }                                                                     \
    if (is_smi()) return false;                                           \
    InstanceType instance_type =                                          \
        static_cast<const HeapObjectData*>(this)->type().instance_type(); \
    return InstanceTypeChecker::Is##Name(instance_type);                  \
  }                                                                       \
  Name##Data* ObjectData::As##Name() {                                    \
    CHECK_NE(broker()->mode(), JSHeapBroker::kDisabled);                  \
    CHECK(Is##Name());                                                    \
    return static_cast<Name##Data*>(this);                                \
  }
HEAP_BROKER_OBJECT_LIST(DEFINE_IS_AND_AS)
#undef DEFINE_IS_AND_AS

const JSObjectField& JSObjectData::GetInobjectField(int property_index) const {
  CHECK_LT(static_cast<size_t>(property_index), inobject_fields_.size());
  return inobject_fields_[property_index];
}

bool JSObjectData::cow_or_empty_elements_tenured() const {
  return cow_or_empty_elements_tenured_;
}

FixedArrayBaseData* JSObjectData::elements() const { return elements_; }

void JSObjectData::SerializeAsBoilerplate() {
  SerializeRecursive(kMaxFastLiteralDepth);
}

void JSObjectData::SerializeElements() {
  if (serialized_elements_) return;
  serialized_elements_ = true;

  Handle<JSObject> boilerplate = Handle<JSObject>::cast(object());
  Handle<FixedArrayBase> elements_object(boilerplate->elements(),
                                         broker()->isolate());
  DCHECK_NULL(elements_);
  elements_ = broker()->GetOrCreateData(elements_object)->AsFixedArrayBase();
}

void MapData::SerializeDescriptors() {
  if (serialized_descriptors_) return;
  serialized_descriptors_ = true;

  Handle<Map> map = Handle<Map>::cast(object());
  Isolate* const isolate = broker()->isolate();
  Handle<DescriptorArray> descriptors(map->instance_descriptors(), isolate);
  // We copy all descriptors (not only the own) in order to support
  // FindFieldOwner, which is used by the FieldType compilation dependency.
  int const number_of_descriptors = descriptors->number_of_descriptors();
  DCHECK(descriptors_.empty());
  descriptors_.reserve(number_of_descriptors);

  for (int i = 0; i < number_of_descriptors; ++i) {
    PropertyDescriptor d;
    d.key = broker()->GetOrCreateData(descriptors->GetKey(i))->AsName();
    d.details = descriptors->GetDetails(i);
    if (d.details.location() == kField) {
      d.field_index = FieldIndex::ForDescriptor(*map, i);
      d.field_owner =
          broker()->GetOrCreateData(map->FindFieldOwner(isolate, i))->AsMap();
      d.field_type = broker()->GetOrCreateData(descriptors->GetFieldType(i));
      d.field_owner->SerializeDescriptors();
    }
    descriptors_.push_back(d);
  }
}

void JSObjectData::SerializeRecursive(int depth) {
  if (serialized_as_boilerplate_) return;
  serialized_as_boilerplate_ = true;

  Handle<JSObject> boilerplate = Handle<JSObject>::cast(object());

  // We only serialize boilerplates that pass the IsInlinableFastLiteral
  // check, so we only do a sanity check on the depth here.
  CHECK_GT(depth, 0);
  CHECK(!boilerplate->map()->is_deprecated());

  // Serialize the elements.
  Isolate* const isolate = broker()->isolate();
  Handle<FixedArrayBase> elements_object(boilerplate->elements(), isolate);

  // Boilerplates need special serialization - we need to make sure COW arrays
  // are tenured. Boilerplate objects should only be reachable from their
  // allocation site, so it is safe to assume that the elements have not been
  // serialized yet.

  bool const empty_or_cow =
      elements_object->length() == 0 ||
      elements_object->map() == ReadOnlyRoots(isolate).fixed_cow_array_map();
  if (empty_or_cow) {
    // We need to make sure copy-on-write elements are tenured.
    if (Heap::InNewSpace(*elements_object)) {
      elements_object =
          broker()->isolate()->factory()->CopyAndTenureFixedCOWArray(
              Handle<FixedArray>::cast(elements_object));
      boilerplate->set_elements(*elements_object);
    }
    cow_or_empty_elements_tenured_ = true;
  }

  DCHECK_NULL(elements_);
  elements_ = broker()->GetOrCreateData(elements_object)->AsFixedArrayBase();

  if (empty_or_cow) {
    // No need to do anything here. Empty or copy-on-write elements
    // do not need to be serialized because we only need to store the elements
    // reference to the allocated object.
  } else if (boilerplate->HasSmiOrObjectElements()) {
    elements_->AsFixedArray()->SerializeContents();
    Handle<FixedArray> fast_elements =
        Handle<FixedArray>::cast(elements_object);
    int length = elements_object->length();
    for (int i = 0; i < length; i++) {
      Handle<Object> value(fast_elements->get(i), isolate);
      if (value->IsJSObject()) {
        ObjectData* value_data = broker()->GetOrCreateData(value);
        value_data->AsJSObject()->SerializeRecursive(depth - 1);
      }
    }
  } else {
    CHECK(boilerplate->HasDoubleElements());
    CHECK_LE(elements_object->Size(), kMaxRegularHeapObjectSize);
    elements_->AsFixedDoubleArray()->SerializeContents();
  }

  // TODO(turbofan): Do we want to support out-of-object properties?
  CHECK(boilerplate->HasFastProperties() &&
        boilerplate->property_array()->length() == 0);
  CHECK_EQ(inobject_fields_.size(), 0u);

  // Check the in-object properties.
  Handle<DescriptorArray> descriptors(
      boilerplate->map()->instance_descriptors(), isolate);
  int const limit = boilerplate->map()->NumberOfOwnDescriptors();
  for (int i = 0; i < limit; i++) {
    PropertyDetails details = descriptors->GetDetails(i);
    if (details.location() != kField) continue;
    DCHECK_EQ(kData, details.kind());

    FieldIndex field_index = FieldIndex::ForDescriptor(boilerplate->map(), i);
    // Make sure {field_index} agrees with {inobject_properties} on the index of
    // this field.
    DCHECK_EQ(field_index.property_index(),
              static_cast<int>(inobject_fields_.size()));
    if (boilerplate->IsUnboxedDoubleField(field_index)) {
      double value = boilerplate->RawFastDoublePropertyAt(field_index);
      inobject_fields_.push_back(JSObjectField{value});
    } else {
      Handle<Object> value(boilerplate->RawFastPropertyAt(field_index),
                           isolate);
      ObjectData* value_data = broker()->GetOrCreateData(value);
      if (value->IsJSObject()) {
        value_data->AsJSObject()->SerializeRecursive(depth - 1);
      }
      inobject_fields_.push_back(JSObjectField{value_data});
    }
  }

  map()->SerializeDescriptors();
}

void JSRegExpData::SerializeAsRegExpBoilerplate() {
  if (serialized_as_reg_exp_boilerplate_) return;
  serialized_as_reg_exp_boilerplate_ = true;

  SerializeElements();

  Handle<JSRegExp> boilerplate = Handle<JSRegExp>::cast(object());
  raw_properties_or_hash_ =
      broker()->GetOrCreateData(boilerplate->raw_properties_or_hash());
  data_ = broker()->GetOrCreateData(boilerplate->data());
  source_ = broker()->GetOrCreateData(boilerplate->source());
  flags_ = broker()->GetOrCreateData(boilerplate->flags());
  last_index_ = broker()->GetOrCreateData(boilerplate->last_index());
}

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

#define RETURN_CREATE_DATA_IF_MATCH(name)                     \
  if (object->Is##name()) {                                   \
    return new (broker->zone())                               \
        name##Data(broker, Handle<name>::cast(object), type); \
  }
  HEAP_BROKER_OBJECT_LIST(RETURN_CREATE_DATA_IF_MATCH)
#undef RETURN_CREATE_DATA_IF_MATCH
  UNREACHABLE();
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
  if (mode() == kDisabled) return;

  Trace("Serializing standard objects.\n");

  Builtins* const b = isolate()->builtins();
  Factory* const f = isolate()->factory();

  // Stuff used by JSGraph:
  GetOrCreateData(f->empty_fixed_array());

  // Stuff used by JSCreateLowering:
  GetOrCreateData(isolate()->native_context())->AsNativeContext()->Serialize();
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

  Trace("Finished serializing standard objects.\n");
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
    // TODO(neis): Inline Serializehere, now that we have subclass-specific
    // Serialize methods.
    data = ObjectData::Serialize(this, object);
  }
  CHECK_NOT_NULL(data);
  return data;
}

ObjectData* JSHeapBroker::GetOrCreateData(Object* object) {
  return GetOrCreateData(handle(object, isolate()));
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

bool ObjectRef::IsSmi() const { return data()->is_smi(); }

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
    return data()->AsHeapObject()->type();
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

base::Optional<MapRef> MapRef::AsElementsKind(ElementsKind kind) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHeapAllocation heap_allocation;
    AllowHandleDereference allow_handle_dereference;
    return MapRef(broker(), Map::AsElementsKind(broker()->isolate(),
                                                object<Map>(), kind));
  } else {
    if (kind == elements_kind()) return *this;
    const ZoneVector<MapData*>& elements_kind_generalizations =
        data()->AsMap()->elements_kind_generalizations();
    for (auto data : elements_kind_generalizations) {
      MapRef map(data);
      if (map.elements_kind() == kind) return map;
    }
    return base::Optional<MapRef>();
  }
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
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference handle_dereference;
    Handle<Object> value(object<FeedbackVector>()->Get(slot)->ToObject(),
                         broker()->isolate());
    return ObjectRef(broker(), value);
  }
  int i = FeedbackVector::GetIndex(slot);
  return ObjectRef(data()->AsFeedbackVector()->feedback().at(i));
}

double JSObjectRef::RawFastDoublePropertyAt(FieldIndex index) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference handle_dereference;
    return object<JSObject>()->RawFastDoublePropertyAt(index);
  } else {
    JSObjectData* object_data = data()->AsJSObject();
    CHECK(map().IsUnboxedDoubleField(index));
    CHECK(index.is_inobject());
    return object_data->GetInobjectField(index.property_index()).AsDouble();
  }
}

ObjectRef JSObjectRef::RawFastPropertyAt(FieldIndex index) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference handle_dereference;
    return ObjectRef(broker(),
                     handle(object<JSObject>()->RawFastPropertyAt(index),
                            broker()->isolate()));
  } else {
    JSObjectData* object_data = data()->AsJSObject();
    CHECK(!map().IsUnboxedDoubleField(index));
    CHECK(index.is_inobject());
    return ObjectRef(
        object_data->GetInobjectField(index.property_index()).AsObject());
  }
}

bool AllocationSiteRef::IsFastLiteral() const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHeapAllocation
        allow_heap_allocation;  // This is needed for TryMigrateInstance.
    AllowHandleAllocation allow_handle_allocation;
    AllowHandleDereference allow_handle_dereference;
    return IsInlinableFastLiteral(
        handle(object<AllocationSite>()->boilerplate(), broker()->isolate()));
  } else {
    return data()->AsAllocationSite()->IsFastLiteral();
  }
}

void JSObjectRef::EnsureElementsTenured() {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation allow_handle_allocation;
    AllowHandleDereference allow_handle_dereference;
    AllowHeapAllocation allow_heap_allocation;

    Handle<FixedArrayBase> object_elements =
        elements().object<FixedArrayBase>();
    if (Heap::InNewSpace(*object_elements)) {
      // If we would like to pretenure a fixed cow array, we must ensure that
      // the array is already in old space, otherwise we'll create too many
      // old-to-new-space pointers (overflowing the store buffer).
      object_elements =
          broker()->isolate()->factory()->CopyAndTenureFixedCOWArray(
              Handle<FixedArray>::cast(object_elements));
      object<JSObject>()->set_elements(*object_elements);
    }
  } else {
    CHECK(data()->AsJSObject()->cow_or_empty_elements_tenured());
  }
}

FieldIndex MapRef::GetFieldIndexFor(int descriptor_index) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    return FieldIndex::ForDescriptor(*object<Map>(), descriptor_index);
  }
  return data()->AsMap()->descriptors().at(descriptor_index).field_index;
}

int MapRef::GetInObjectPropertyOffset(int i) const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->GetInObjectPropertyOffset(i);
}

PropertyDetails MapRef::GetPropertyDetails(int descriptor_index) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    return object<Map>()->instance_descriptors()->GetDetails(descriptor_index);
  }
  return data()->AsMap()->descriptors().at(descriptor_index).details;
}

NameRef MapRef::GetPropertyKey(int descriptor_index) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference allow_handle_dereference;
    return NameRef(
        broker(),
        handle(object<Map>()->instance_descriptors()->GetKey(descriptor_index),
               broker()->isolate()));
  }
  return NameRef(data()->AsMap()->descriptors().at(descriptor_index).key);
}

bool MapRef::IsFixedCowArrayMap() const {
  AllowHandleDereference allow_handle_dereference;
  return *object<Map>() ==
         ReadOnlyRoots(broker()->isolate()).fixed_cow_array_map();
}

MapRef MapRef::FindFieldOwner(int descriptor_index) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference allow_handle_dereference;
    Handle<Map> owner(
        object<Map>()->FindFieldOwner(broker()->isolate(), descriptor_index),
        broker()->isolate());
    return MapRef(broker(), owner);
  }
  return MapRef(
      data()->AsMap()->descriptors().at(descriptor_index).field_owner);
}

ObjectRef MapRef::GetFieldType(int descriptor_index) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference allow_handle_dereference;
    Handle<FieldType> field_type(
        object<Map>()->instance_descriptors()->GetFieldType(descriptor_index),
        broker()->isolate());
    return ObjectRef(broker(), field_type);
  }
  return ObjectRef(
      data()->AsMap()->descriptors().at(descriptor_index).field_type);
}

bool MapRef::IsUnboxedDoubleField(FieldIndex index) const {
  AllowHandleDereference allow_handle_dereference;
  return object<Map>()->IsUnboxedDoubleField(index);
}

uint16_t StringRef::GetFirstChar() {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    return object<String>()->Get(0);
  } else {
    return data()->AsString()->first_char();
  }
}

base::Optional<double> StringRef::ToNumber() {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    AllowHandleAllocation allow_handle_allocation;
    AllowHeapAllocation allow_heap_allocation;
    int flags = ALLOW_HEX | ALLOW_OCTAL | ALLOW_BINARY;
    return StringToDouble(broker()->isolate(),
                          broker()->isolate()->unicode_cache(),
                          object<String>(), flags);
  } else {
    return data()->AsString()->to_number();
  }
}

ObjectRef FixedArrayRef::get(int i) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference allow_handle_dereference;
    return ObjectRef(broker(),
                     handle(object<FixedArray>()->get(i), broker()->isolate()));
  } else {
    return ObjectRef(data()->AsFixedArray()->Get(i));
  }
}

bool FixedDoubleArrayRef::is_the_hole(int i) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    return object<FixedDoubleArray>()->is_the_hole(i);
  } else {
    return data()->AsFixedDoubleArray()->Get(i).is_hole_nan();
  }
}

double FixedDoubleArrayRef::get_scalar(int i) const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleDereference allow_handle_dereference;
    return object<FixedDoubleArray>()->get_scalar(i);
  } else {
    CHECK(!data()->AsFixedDoubleArray()->Get(i).is_hole_nan());
    return data()->AsFixedDoubleArray()->Get(i).get_scalar();
  }
}

#define IF_BROKER_DISABLED_ACCESS_HANDLE_C(holder, name) \
  if (broker()->mode() == JSHeapBroker::kDisabled) {     \
    AllowHandleAllocation handle_allocation;             \
    AllowHandleDereference allow_handle_dereference;     \
    return object<holder>()->name();                     \
  }

#define IF_BROKER_DISABLED_ACCESS_HANDLE(holder, result, name)                 \
  if (broker()->mode() == JSHeapBroker::kDisabled) {                           \
    AllowHandleAllocation handle_allocation;                                   \
    AllowHandleDereference allow_handle_dereference;                           \
    return result##Ref(broker(),                                               \
                       handle(object<holder>()->name(), broker()->isolate())); \
  }

// Macros for definining a const getter that, depending on the broker mode,
// either looks into the handle or into the serialized data. The first one is
// used for the rare case of a XYZRef class that does not have a corresponding
// XYZ class in objects.h. The second one is used otherwise.
#define BIMODAL_ACCESSOR(holder, result, name)                   \
  result##Ref holder##Ref::name() const {                        \
    IF_BROKER_DISABLED_ACCESS_HANDLE(holder, result, name);      \
    return result##Ref(ObjectRef::data()->As##holder()->name()); \
  }

// Like HANDLE_ACCESSOR except that the result type is not an XYZRef.
#define BIMODAL_ACCESSOR_C(holder, result, name)      \
  result holder##Ref::name() const {                  \
    IF_BROKER_DISABLED_ACCESS_HANDLE_C(holder, name); \
    return ObjectRef::data()->As##holder()->name();   \
  }

// Like HANDLE_ACCESSOR_C but for BitFields.
#define BIMODAL_ACCESSOR_B(holder, field, name, BitField)              \
  typename BitField::FieldType holder##Ref::name() const {             \
    IF_BROKER_DISABLED_ACCESS_HANDLE_C(holder, name);                  \
    return BitField::decode(ObjectRef::data()->As##holder()->field()); \
  }

// Macros for definining a const getter that always looks into the handle.
// (These will go away once we serialize everything.) The first one is used for
// the rare case of a XYZRef class that does not have a corresponding XYZ class
// in objects.h. The second one is used otherwise.
#define HANDLE_ACCESSOR(holder, result, name)                                  \
  result##Ref holder##Ref::name() const {                                      \
    AllowHandleAllocation handle_allocation;                                   \
    AllowHandleDereference allow_handle_dereference;                           \
    return result##Ref(broker(),                                               \
                       handle(object<holder>()->name(), broker()->isolate())); \
  }

// Like HANDLE_ACCESSOR except that the result type is not an XYZRef.
#define HANDLE_ACCESSOR_C(holder, result, name)      \
  result holder##Ref::name() const {                 \
    AllowHandleAllocation handle_allocation;         \
    AllowHandleDereference allow_handle_dereference; \
    return object<holder>()->name();                 \
  }

BIMODAL_ACCESSOR(AllocationSite, Object, nested_site)
BIMODAL_ACCESSOR_C(AllocationSite, bool, CanInlineCall)
BIMODAL_ACCESSOR_C(AllocationSite, bool, PointsToLiteral)
BIMODAL_ACCESSOR_C(AllocationSite, ElementsKind, GetElementsKind)
BIMODAL_ACCESSOR_C(AllocationSite, PretenureFlag, GetPretenureMode)

BIMODAL_ACCESSOR_C(BytecodeArray, int, register_count)

BIMODAL_ACCESSOR(HeapObject, Map, map)
HANDLE_ACCESSOR_C(HeapObject, bool, IsExternalString)
HANDLE_ACCESSOR_C(HeapObject, bool, IsSeqString)

HANDLE_ACCESSOR(JSArray, Object, length)

BIMODAL_ACCESSOR_C(JSFunction, bool, has_prototype)
BIMODAL_ACCESSOR_C(JSFunction, bool, has_initial_map)
BIMODAL_ACCESSOR_C(JSFunction, bool, PrototypeRequiresRuntimeLookup)
BIMODAL_ACCESSOR(JSFunction, JSGlobalProxy, global_proxy)
BIMODAL_ACCESSOR(JSFunction, Map, initial_map)
BIMODAL_ACCESSOR(JSFunction, Object, prototype)
BIMODAL_ACCESSOR(JSFunction, SharedFunctionInfo, shared)
HANDLE_ACCESSOR_C(JSFunction, bool, IsConstructor)

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

#define DEF_NATIVE_CONTEXT_ACCESSOR(type, name) \
  BIMODAL_ACCESSOR(NativeContext, type, name)
BROKER_NATIVE_CONTEXT_FIELDS(DEF_NATIVE_CONTEXT_ACCESSOR)
#undef DEF_NATIVE_CONTEXT_ACCESSOR

HANDLE_ACCESSOR(PropertyCell, Object, value)
HANDLE_ACCESSOR_C(PropertyCell, PropertyDetails, property_details)

HANDLE_ACCESSOR_C(ScopeInfo, int, ContextLength)

BIMODAL_ACCESSOR_C(SharedFunctionInfo, int, builtin_id)
BIMODAL_ACCESSOR(SharedFunctionInfo, BytecodeArray, GetBytecodeArray)
#define DEF_SFI_ACCESSOR(type, name) \
  BIMODAL_ACCESSOR_C(SharedFunctionInfo, type, name)
BROKER_SFI_FIELDS(DEF_SFI_ACCESSOR)
#undef DEF_SFI_ACCESSOR

BIMODAL_ACCESSOR_C(String, int, length)

// TODO(neis): Provide StringShape() on StringRef.

MapRef NativeContextRef::GetFunctionMapFromIndex(int index) const {
  DCHECK_LE(index, Context::LAST_FUNCTION_MAP_INDEX);
  DCHECK_GE(index, Context::FIRST_FUNCTION_MAP_INDEX);
  return get(index).AsMap();
}

MapRef NativeContextRef::GetInitialJSArrayMap(ElementsKind kind) const {
  switch (kind) {
    case PACKED_SMI_ELEMENTS:
      return js_array_packed_smi_elements_map();
    case HOLEY_SMI_ELEMENTS:
      return js_array_holey_smi_elements_map();
    case PACKED_DOUBLE_ELEMENTS:
      return js_array_packed_double_elements_map();
    case HOLEY_DOUBLE_ELEMENTS:
      return js_array_holey_double_elements_map();
    case PACKED_ELEMENTS:
      return js_array_packed_elements_map();
    case HOLEY_ELEMENTS:
      return js_array_holey_elements_map();
    default:
      UNREACHABLE();
  }
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

double HeapNumberRef::value() const {
  IF_BROKER_DISABLED_ACCESS_HANDLE_C(HeapNumber, value);
  return data()->AsHeapNumber()->value();
}

double MutableHeapNumberRef::value() const {
  IF_BROKER_DISABLED_ACCESS_HANDLE_C(MutableHeapNumber, value);
  return data()->AsMutableHeapNumber()->value();
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

base::Optional<JSObjectRef> AllocationSiteRef::boilerplate() const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference allow_handle_dereference;
    return JSObjectRef(broker(), handle(object<AllocationSite>()->boilerplate(),
                                        broker()->isolate()));
  } else {
    JSObjectData* boilerplate = data()->AsAllocationSite()->boilerplate();
    if (boilerplate) {
      return JSObjectRef(boilerplate);
    } else {
      return base::nullopt;
    }
  }
}

ElementsKind JSObjectRef::GetElementsKind() const {
  return map().elements_kind();
}

FixedArrayBaseRef JSObjectRef::elements() const {
  if (broker()->mode() == JSHeapBroker::kDisabled) {
    AllowHandleAllocation handle_allocation;
    AllowHandleDereference allow_handle_dereference;
    return FixedArrayBaseRef(
        broker(), handle(object<JSObject>()->elements(), broker()->isolate()));
  } else {
    return FixedArrayBaseRef(data()->AsJSObject()->elements());
  }
}

int FixedArrayBaseRef::length() const {
  IF_BROKER_DISABLED_ACCESS_HANDLE_C(FixedArrayBase, length);
  return data()->AsFixedArrayBase()->length();
}

ObjectData* FixedArrayData::Get(int i) const {
  CHECK_LT(i, static_cast<int>(contents_.size()));
  CHECK_NOT_NULL(contents_[i]);
  return contents_[i];
}

Float64 FixedDoubleArrayData::Get(int i) const {
  CHECK_LT(i, static_cast<int>(contents_.size()));
  return contents_[i];
}

void FeedbackVectorRef::SerializeSlots() {
  data()->AsFeedbackVector()->SerializeSlots();
}

ObjectRef JSRegExpRef::data() const {
  IF_BROKER_DISABLED_ACCESS_HANDLE(JSRegExp, Object, data);
  return ObjectRef(ObjectRef::data()->AsJSRegExp()->data());
}

ObjectRef JSRegExpRef::flags() const {
  IF_BROKER_DISABLED_ACCESS_HANDLE(JSRegExp, Object, flags);
  return ObjectRef(ObjectRef::data()->AsJSRegExp()->flags());
}

ObjectRef JSRegExpRef::last_index() const {
  IF_BROKER_DISABLED_ACCESS_HANDLE(JSRegExp, Object, last_index);
  return ObjectRef(ObjectRef::data()->AsJSRegExp()->last_index());
}

ObjectRef JSRegExpRef::raw_properties_or_hash() const {
  IF_BROKER_DISABLED_ACCESS_HANDLE(JSRegExp, Object, raw_properties_or_hash);
  return ObjectRef(ObjectRef::data()->AsJSRegExp()->raw_properties_or_hash());
}

ObjectRef JSRegExpRef::source() const {
  IF_BROKER_DISABLED_ACCESS_HANDLE(JSRegExp, Object, source);
  return ObjectRef(ObjectRef::data()->AsJSRegExp()->source());
}

Handle<Object> ObjectRef::object() const { return data()->object(); }

JSHeapBroker* ObjectRef::broker() const { return data()->broker(); }

ObjectData* ObjectRef::data() const { return data_; }

Reduction NoChangeBecauseOfMissingData(JSHeapBroker* broker,
                                       const char* function, int line) {
  if (FLAG_trace_heap_broker) {
    PrintF("[%p] Skipping optimization in %s at line %d due to missing data\n",
           broker, function, line);
  }
  return AdvancedReducer::NoChange();
}

NativeContextData::NativeContextData(JSHeapBroker* broker,
                                     Handle<NativeContext> object,
                                     HeapObjectType type)
    : ContextData(broker, object, type) {}

void NativeContextData::Serialize() {
  if (serialized_) return;
  serialized_ = true;

  Handle<NativeContext> context = Handle<NativeContext>::cast(object());
#define SERIALIZE_MEMBER(type, name)                                \
  DCHECK_NULL(name##_);                                             \
  name##_ = broker()->GetOrCreateData(context->name())->As##type(); \
  if (name##_->IsJSFunction()) name##_->AsJSFunction()->Serialize();
  BROKER_NATIVE_CONTEXT_FIELDS(SERIALIZE_MEMBER)
#undef SERIALIZE_MEMBER
}

void JSFunctionRef::Serialize() {
  if (broker()->mode() == JSHeapBroker::kDisabled) return;
  CHECK_EQ(broker()->mode(), JSHeapBroker::kSerializing);
  data()->AsJSFunction()->Serialize();
}

void MapRef::SerializeDescriptors() {
  if (broker()->mode() == JSHeapBroker::kDisabled) return;
  CHECK_EQ(broker()->mode(), JSHeapBroker::kSerializing);
  data()->AsMap()->SerializeDescriptors();
}

#undef BIMODAL_ACCESSOR
#undef BIMODAL_ACCESSOR_B
#undef BIMODAL_ACCESSOR_C
#undef HANDLE_ACCESSOR
#undef HANDLE_ACCESSOR_C
#undef IF_BROKER_DISABLED_ACCESS_HANDLE
#undef IF_BROKER_DISABLED_ACCESS_HANDLE_C

}  // namespace compiler
}  // namespace internal
}  // namespace v8
