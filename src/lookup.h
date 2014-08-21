// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_LOOKUP_H_
#define V8_LOOKUP_H_

#include "src/factory.h"
#include "src/isolate.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

class LookupIterator V8_FINAL BASE_EMBEDDED {
 public:
  enum Configuration {
    // Configuration bits.
    CHECK_HIDDEN_PROPERTY = 1 << 0,
    CHECK_DERIVED_PROPERTY = 1 << 1,
    CHECK_INTERCEPTOR = 1 << 2,
    CHECK_ACCESS_CHECK = 1 << 3,

    // Convience combinations of bits.
    CHECK_PROPERTY = 0,
    CHECK_HIDDEN_SKIP_INTERCEPTOR = CHECK_HIDDEN_PROPERTY | CHECK_ACCESS_CHECK,
    CHECK_DERIVED_SKIP_INTERCEPTOR =
        CHECK_HIDDEN_SKIP_INTERCEPTOR | CHECK_DERIVED_PROPERTY,
    CHECK_DERIVED = CHECK_DERIVED_SKIP_INTERCEPTOR | CHECK_INTERCEPTOR,
    CHECK_HIDDEN = CHECK_HIDDEN_SKIP_INTERCEPTOR | CHECK_INTERCEPTOR
  };

  enum State {
    ACCESS_CHECK,
    INTERCEPTOR,
    JSPROXY,
    NOT_FOUND,
    PROPERTY,
    // Set state_ to BEFORE_PROPERTY to ensure that the next lookup will be a
    // PROPERTY lookup.
    BEFORE_PROPERTY = INTERCEPTOR
  };

  enum PropertyKind {
    DATA,
    ACCESSOR
  };

  enum PropertyEncoding {
    DICTIONARY,
    DESCRIPTOR
  };

  explicit LookupIterator(const LookupIterator* other)
      : configuration_(other->configuration_),
        state_(other->state_),
        property_kind_(other->property_kind_),
        property_encoding_(other->property_encoding_),
        property_details_(other->property_details_),
        isolate_(other->isolate_),
        name_(other->name_),
        holder_map_(other->holder_map_),
        maybe_receiver_(other->maybe_receiver_),
        maybe_holder_(other->maybe_holder_) {}

  LookupIterator(Handle<Object> receiver, Handle<Name> name,
                 Configuration configuration = CHECK_DERIVED)
      : configuration_(ComputeConfiguration(configuration, name)),
        state_(NOT_FOUND),
        property_kind_(DATA),
        property_encoding_(DESCRIPTOR),
        property_details_(NONE, NONEXISTENT, Representation::None()),
        isolate_(name->GetIsolate()),
        name_(name),
        maybe_receiver_(receiver),
        number_(DescriptorArray::kNotFound) {
    Handle<JSReceiver> root = GetRoot();
    holder_map_ = handle(root->map());
    maybe_holder_ = root;
    Next();
  }

  LookupIterator(Handle<Object> receiver, Handle<Name> name,
                 Handle<JSReceiver> holder,
                 Configuration configuration = CHECK_DERIVED)
      : configuration_(ComputeConfiguration(configuration, name)),
        state_(NOT_FOUND),
        property_kind_(DATA),
        property_encoding_(DESCRIPTOR),
        property_details_(NONE, NONEXISTENT, Representation::None()),
        isolate_(name->GetIsolate()),
        name_(name),
        holder_map_(holder->map()),
        maybe_receiver_(receiver),
        maybe_holder_(holder),
        number_(DescriptorArray::kNotFound) {
    Next();
  }

  Isolate* isolate() const { return isolate_; }
  State state() const { return state_; }
  Handle<Name> name() const { return name_; }

  bool IsFound() const { return state_ != NOT_FOUND; }
  void Next();
  void NotFound() {
    has_property_ = false;
    state_ = NOT_FOUND;
  }

  Heap* heap() const { return isolate_->heap(); }
  Factory* factory() const { return isolate_->factory(); }
  Handle<Object> GetReceiver() const {
    return maybe_receiver_.ToHandleChecked();
  }
  Handle<Map> holder_map() const { return holder_map_; }
  template <class T>
  Handle<T> GetHolder() const {
    DCHECK(IsFound());
    return Handle<T>::cast(maybe_holder_.ToHandleChecked());
  }
  Handle<JSReceiver> GetRoot() const;
  bool HolderIsReceiverOrHiddenPrototype() const;
  bool HolderIsNonGlobalHiddenPrototype() const;

  /* ACCESS_CHECK */
  bool HasAccess(v8::AccessType access_type) const;

  /* PROPERTY */
  // HasProperty needs to be called before any of the other PROPERTY methods
  // below can be used. It ensures that we are able to provide a definite
  // answer, and loads extra information about the property.
  bool HasProperty();
  void PrepareForDataProperty(Handle<Object> value);
  void TransitionToDataProperty(Handle<Object> value,
                                PropertyAttributes attributes,
                                Object::StoreFromKeyed store_mode);
  void ReconfigureDataProperty(Handle<Object> value,
                               PropertyAttributes attributes);
  void TransitionToAccessorProperty(AccessorComponent component,
                                    Handle<Object> accessor,
                                    PropertyAttributes attributes);
  PropertyKind property_kind() const {
    DCHECK(has_property_);
    return property_kind_;
  }
  PropertyEncoding property_encoding() const {
    DCHECK(has_property_);
    return property_encoding_;
  }
  PropertyDetails property_details() const {
    DCHECK(has_property_);
    return property_details_;
  }
  bool IsConfigurable() const { return !property_details().IsDontDelete(); }
  bool IsReadOnly() const { return property_details().IsReadOnly(); }
  Representation representation() const {
    return property_details().representation();
  }
  FieldIndex GetFieldIndex() const;
  int GetConstantIndex() const;
  Handle<PropertyCell> GetPropertyCell() const;
  Handle<Object> GetAccessors() const;
  Handle<Object> GetDataValue() const;
  void WriteDataValue(Handle<Object> value);

  void InternalizeName();

 private:
  Handle<Map> GetReceiverMap() const;

  MUST_USE_RESULT inline JSReceiver* NextHolder(Map* map);
  inline State LookupInHolder(Map* map);
  Handle<Object> FetchValue() const;
  void ReloadPropertyInformation();

  bool IsBootstrapping() const;

  // Methods that fetch data from the holder ensure they always have a holder.
  // This means the receiver needs to be present as opposed to just the receiver
  // map. Other objects in the prototype chain are transitively guaranteed to be
  // present via the receiver map.
  bool is_guaranteed_to_have_holder() const {
    return !maybe_receiver_.is_null();
  }
  bool check_interceptor() const {
    return !IsBootstrapping() && (configuration_ & CHECK_INTERCEPTOR) != 0;
  }
  bool check_derived() const {
    return (configuration_ & CHECK_DERIVED_PROPERTY) != 0;
  }
  bool check_hidden() const {
    return (configuration_ & CHECK_HIDDEN_PROPERTY) != 0;
  }
  bool check_access_check() const {
    return (configuration_ & CHECK_ACCESS_CHECK) != 0;
  }
  int descriptor_number() const {
    DCHECK(has_property_);
    DCHECK_EQ(DESCRIPTOR, property_encoding_);
    return number_;
  }
  int dictionary_entry() const {
    DCHECK(has_property_);
    DCHECK_EQ(DICTIONARY, property_encoding_);
    return number_;
  }

  static Configuration ComputeConfiguration(
      Configuration configuration, Handle<Name> name) {
    if (name->IsOwn()) {
      return static_cast<Configuration>(configuration & CHECK_HIDDEN);
    } else {
      return configuration;
    }
  }

  // If configuration_ becomes mutable, update
  // HolderIsReceiverOrHiddenPrototype.
  Configuration configuration_;
  State state_;
  bool has_property_;
  PropertyKind property_kind_;
  PropertyEncoding property_encoding_;
  PropertyDetails property_details_;
  Isolate* isolate_;
  Handle<Name> name_;
  Handle<Map> holder_map_;
  MaybeHandle<Object> maybe_receiver_;
  MaybeHandle<JSReceiver> maybe_holder_;

  int number_;
};


} }  // namespace v8::internal

#endif  // V8_LOOKUP_H_
