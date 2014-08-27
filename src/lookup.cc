// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/bootstrapper.h"
#include "src/deoptimizer.h"
#include "src/lookup.h"
#include "src/lookup-inl.h"

namespace v8 {
namespace internal {


void LookupIterator::Next() {
  DisallowHeapAllocation no_gc;
  has_property_ = false;

  JSReceiver* holder = NULL;
  Map* map = *holder_map_;

  // Perform lookup on current holder.
  state_ = LookupInHolder(map);

  // Continue lookup if lookup on current holder failed.
  while (!IsFound()) {
    JSReceiver* maybe_holder = NextHolder(map);
    if (maybe_holder == NULL) break;
    holder = maybe_holder;
    map = holder->map();
    state_ = LookupInHolder(map);
  }

  // Either was found in the receiver, or the receiver has no prototype.
  if (holder == NULL) return;

  maybe_holder_ = handle(holder, isolate_);
  holder_map_ = handle(map, isolate_);
}


Handle<JSReceiver> LookupIterator::GetRoot() const {
  Handle<Object> receiver = GetReceiver();
  if (receiver->IsJSReceiver()) return Handle<JSReceiver>::cast(receiver);
  Handle<Object> root =
      handle(receiver->GetRootMap(isolate_)->prototype(), isolate_);
  CHECK(!root->IsNull());
  return Handle<JSReceiver>::cast(root);
}


Handle<Map> LookupIterator::GetReceiverMap() const {
  Handle<Object> receiver = GetReceiver();
  if (receiver->IsNumber()) return isolate_->factory()->heap_number_map();
  return handle(Handle<HeapObject>::cast(receiver)->map(), isolate_);
}


Handle<JSObject> LookupIterator::GetStoreTarget() const {
  Handle<JSObject> receiver = Handle<JSObject>::cast(GetReceiver());

  if (receiver->IsJSGlobalProxy()) {
    PrototypeIterator iter(isolate(), receiver);
    if (iter.IsAtEnd()) return receiver;
    return Handle<JSGlobalObject>::cast(PrototypeIterator::GetCurrent(iter));
  }
  return receiver;
}


bool LookupIterator::IsBootstrapping() const {
  return isolate_->bootstrapper()->IsActive();
}


bool LookupIterator::HasAccess(v8::AccessType access_type) const {
  DCHECK_EQ(ACCESS_CHECK, state_);
  DCHECK(is_guaranteed_to_have_holder());
  return isolate_->MayNamedAccess(GetHolder<JSObject>(), name_, access_type);
}


bool LookupIterator::HasProperty() {
  DCHECK_EQ(PROPERTY, state_);
  DCHECK(is_guaranteed_to_have_holder());

  if (property_encoding_ == DICTIONARY) {
    Handle<JSObject> holder = GetHolder<JSObject>();
    number_ = holder->property_dictionary()->FindEntry(name_);
    if (number_ == NameDictionary::kNotFound) return false;

    property_details_ = holder->property_dictionary()->DetailsAt(number_);
    // Holes in dictionary cells are absent values.
    if (holder->IsGlobalObject() &&
        (property_details_.IsDeleted() || FetchValue()->IsTheHole())) {
      return false;
    }
  } else {
    // Can't use descriptor_number() yet because has_property_ is still false.
    property_details_ =
        holder_map_->instance_descriptors()->GetDetails(number_);
  }

  LoadPropertyKind();

  has_property_ = true;
  return true;
}


void LookupIterator::LoadPropertyKind() {
  switch (property_details_.type()) {
    case v8::internal::FIELD:
    case v8::internal::NORMAL:
    case v8::internal::CONSTANT:
      property_kind_ = DATA;
      break;
    case v8::internal::CALLBACKS:
      property_kind_ = ACCESSOR;
      break;
  }
}


void LookupIterator::ReloadPropertyInformation() {
  state_ = BEFORE_PROPERTY;
  state_ = LookupInHolder(*holder_map_);
  DCHECK(IsFound());
  HasProperty();
}


void LookupIterator::PrepareForDataProperty(Handle<Object> value) {
  DCHECK(has_property_);
  DCHECK(HolderIsReceiverOrHiddenPrototype());
  if (property_encoding_ == DICTIONARY) return;
  holder_map_ =
      Map::PrepareForDataProperty(holder_map_, descriptor_number(), value);
  JSObject::MigrateToMap(GetHolder<JSObject>(), holder_map_);
  ReloadPropertyInformation();
}


void LookupIterator::ReconfigureDataProperty(Handle<Object> value,
                                             PropertyAttributes attributes) {
  DCHECK(has_property_);
  DCHECK(HolderIsReceiverOrHiddenPrototype());
  Handle<JSObject> holder = GetHolder<JSObject>();
  if (property_encoding_ != DICTIONARY) {
    holder_map_ = Map::ReconfigureDataProperty(holder_map_, descriptor_number(),
                                               attributes);
    JSObject::MigrateToMap(holder, holder_map_);
  }

  if (holder_map_->is_dictionary_map()) {
    PropertyDetails details(attributes, NORMAL, 0);
    JSObject::SetNormalizedProperty(holder, name(), value, details);
  }

  ReloadPropertyInformation();
}


void LookupIterator::PrepareTransitionToDataProperty(
    Handle<Object> value, PropertyAttributes attributes,
    Object::StoreFromKeyed store_mode) {
  if (state_ == TRANSITION) return;
  DCHECK(!has_property_ || property_kind_ != ACCESSOR);
  DCHECK(!(has_property_ || state_ == JSPROXY) ||
         !HolderIsReceiverOrHiddenPrototype());

  // Can only be called when the receiver is a JSObject. JSProxy has to be
  // handled via a trap. Adding properties to primitive values is not
  // observable.
  Handle<JSObject> receiver = GetStoreTarget();

  if (!name().is_identical_to(isolate()->factory()->hidden_string()) &&
      !receiver->map()->is_extensible()) {
    return;
  }

  transition_map_ = Map::TransitionToDataProperty(
      handle(receiver->map(), isolate_), name_, value, attributes, store_mode);
  state_ = TRANSITION;
}


void LookupIterator::ApplyTransitionToDataProperty() {
  DCHECK_EQ(TRANSITION, state_);

  Handle<JSObject> receiver = GetStoreTarget();
  maybe_holder_ = receiver;
  holder_map_ = transition_map_;
  JSObject::MigrateToMap(receiver, holder_map_);
  ReloadPropertyInformation();
}


void LookupIterator::TransitionToAccessorProperty(
    AccessorComponent component, Handle<Object> accessor,
    PropertyAttributes attributes) {
  DCHECK(!accessor->IsNull());
  // Can only be called when the receiver is a JSObject. JSProxy has to be
  // handled via a trap. Adding properties to primitive values is not
  // observable.
  Handle<JSObject> receiver = GetStoreTarget();
  maybe_holder_ = receiver;
  holder_map_ =
      Map::TransitionToAccessorProperty(handle(receiver->map(), isolate_),
                                        name_, component, accessor, attributes);
  JSObject::MigrateToMap(receiver, holder_map_);

  ReloadPropertyInformation();

  if (!holder_map_->is_dictionary_map()) return;

  // We have to deoptimize since accesses to data properties may have been
  // inlined without a corresponding map-check.
  if (holder_map_->IsGlobalObjectMap()) {
    Deoptimizer::DeoptimizeGlobalObject(*receiver);
  }

  // Install the accessor into the dictionary-mode object.
  PropertyDetails details(attributes, CALLBACKS, 0);
  Handle<AccessorPair> pair;
  if (IsFound() && HasProperty() && property_kind() == ACCESSOR &&
      GetAccessors()->IsAccessorPair()) {
    pair = Handle<AccessorPair>::cast(GetAccessors());
    // If the component and attributes are identical, nothing has to be done.
    if (pair->get(component) == *accessor) {
      if (property_details().attributes() == attributes) return;
    } else {
      pair = AccessorPair::Copy(pair);
      pair->set(component, *accessor);
    }
  } else {
    pair = isolate()->factory()->NewAccessorPair();
    pair->set(component, *accessor);
  }
  JSObject::SetNormalizedProperty(receiver, name_, pair, details);

  JSObject::ReoptimizeIfPrototype(receiver);
  holder_map_ = handle(receiver->map(), isolate_);
  ReloadPropertyInformation();
}


bool LookupIterator::HolderIsReceiverOrHiddenPrototype() const {
  DCHECK(has_property_ || state_ == INTERCEPTOR || state_ == JSPROXY);
  // Optimization that only works if configuration_ is not mutable.
  if (!check_prototype_chain()) return true;
  DisallowHeapAllocation no_gc;
  Handle<Object> receiver = GetReceiver();
  if (!receiver->IsJSReceiver()) return false;
  Object* current = *receiver;
  JSReceiver* holder = *maybe_holder_.ToHandleChecked();
  // JSProxy do not occur as hidden prototypes.
  if (current->IsJSProxy()) {
    return JSReceiver::cast(current) == holder;
  }
  PrototypeIterator iter(isolate(), current,
                         PrototypeIterator::START_AT_RECEIVER);
  do {
    if (JSReceiver::cast(iter.GetCurrent()) == holder) return true;
    DCHECK(!current->IsJSProxy());
    iter.Advance();
  } while (!iter.IsAtEnd(PrototypeIterator::END_AT_NON_HIDDEN));
  return false;
}


Handle<Object> LookupIterator::FetchValue() const {
  Object* result = NULL;
  Handle<JSObject> holder = GetHolder<JSObject>();
  switch (property_encoding_) {
    case DICTIONARY:
      result = holder->property_dictionary()->ValueAt(number_);
      if (holder->IsGlobalObject()) {
        result = PropertyCell::cast(result)->value();
      }
      break;
    case DESCRIPTOR:
      if (property_details_.type() == v8::internal::FIELD) {
        FieldIndex field_index =
            FieldIndex::ForDescriptor(*holder_map_, number_);
        return JSObject::FastPropertyAt(
            holder, property_details_.representation(), field_index);
      }
      result = holder_map_->instance_descriptors()->GetValue(number_);
  }
  return handle(result, isolate_);
}


int LookupIterator::GetConstantIndex() const {
  DCHECK(has_property_);
  DCHECK_EQ(DESCRIPTOR, property_encoding_);
  DCHECK_EQ(v8::internal::CONSTANT, property_details_.type());
  return descriptor_number();
}


FieldIndex LookupIterator::GetFieldIndex() const {
  DCHECK(has_property_);
  DCHECK_EQ(DESCRIPTOR, property_encoding_);
  DCHECK_EQ(v8::internal::FIELD, property_details_.type());
  int index =
      holder_map()->instance_descriptors()->GetFieldIndex(descriptor_number());
  bool is_double = representation().IsDouble();
  return FieldIndex::ForPropertyIndex(*holder_map(), index, is_double);
}


Handle<HeapType> LookupIterator::GetFieldType() const {
  DCHECK(has_property_);
  DCHECK_EQ(DESCRIPTOR, property_encoding_);
  DCHECK_EQ(v8::internal::FIELD, property_details_.type());
  return handle(
      holder_map()->instance_descriptors()->GetFieldType(descriptor_number()),
      isolate_);
}


Handle<PropertyCell> LookupIterator::GetPropertyCell() const {
  Handle<JSObject> holder = GetHolder<JSObject>();
  Handle<GlobalObject> global = Handle<GlobalObject>::cast(holder);
  Object* value = global->property_dictionary()->ValueAt(dictionary_entry());
  return Handle<PropertyCell>(PropertyCell::cast(value));
}


Handle<Object> LookupIterator::GetAccessors() const {
  DCHECK(has_property_);
  DCHECK_EQ(ACCESSOR, property_kind_);
  return FetchValue();
}


Handle<Object> LookupIterator::GetDataValue() const {
  DCHECK(has_property_);
  DCHECK_EQ(DATA, property_kind_);
  Handle<Object> value = FetchValue();
  return value;
}


void LookupIterator::WriteDataValue(Handle<Object> value) {
  DCHECK(is_guaranteed_to_have_holder());
  DCHECK(has_property_);
  Handle<JSObject> holder = GetHolder<JSObject>();
  if (property_encoding_ == DICTIONARY) {
    NameDictionary* property_dictionary = holder->property_dictionary();
    if (holder->IsGlobalObject()) {
      Handle<PropertyCell> cell(
          PropertyCell::cast(property_dictionary->ValueAt(dictionary_entry())));
      PropertyCell::SetValueInferType(cell, value);
    } else {
      property_dictionary->ValueAtPut(dictionary_entry(), *value);
    }
  } else if (property_details_.type() == v8::internal::FIELD) {
    holder->WriteToField(descriptor_number(), *value);
  } else {
    DCHECK_EQ(v8::internal::CONSTANT, property_details_.type());
  }
}


void LookupIterator::InternalizeName() {
  if (name_->IsUniqueName()) return;
  name_ = factory()->InternalizeString(Handle<String>::cast(name_));
}
} }  // namespace v8::internal
