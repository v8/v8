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
    kAccessCheck = 1 << 0,
    kHidden = 1 << 1,
    kInterceptor = 1 << 2,
    kPrototypeChain = 1 << 3,

    // Convience combinations of bits.
    OWN_PROPERTY = 0,
    OWN_SKIP_INTERCEPTOR = kAccessCheck,
    OWN = kAccessCheck | kInterceptor,
    HIDDEN_PROPERTY = kHidden,
    HIDDEN_SKIP_INTERCEPTOR = kAccessCheck | kHidden,
    HIDDEN = kAccessCheck | kHidden | kInterceptor,
    PROTOTYPE_CHAIN_PROPERTY = kHidden | kPrototypeChain,
    PROTOTYPE_CHAIN_SKIP_INTERCEPTOR = kAccessCheck | kHidden | kPrototypeChain,
    PROTOTYPE_CHAIN = kAccessCheck | kHidden | kPrototypeChain | kInterceptor
  };

  enum State {
    ACCESS_CHECK,
    INTERCEPTOR,
    JSPROXY,
    NOT_FOUND,
    PROPERTY,
    TRANSITION,
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

  LookupIterator(Handle<Object> receiver, Handle<Name> name,
                 Configuration configuration = PROTOTYPE_CHAIN)
      : configuration_(ComputeConfiguration(configuration, name)),
        state_(NOT_FOUND),
        property_kind_(DATA),
        property_encoding_(DESCRIPTOR),
        property_details_(NONE, NORMAL, Representation::None()),
        isolate_(name->GetIsolate()),
        name_(name),
        maybe_receiver_(receiver),
        number_(DescriptorArray::kNotFound) {
    Handle<JSReceiver> root = GetRoot();
    holder_map_ = handle(root->map(), isolate_);
    maybe_holder_ = root;
    Next();
  }

  LookupIterator(Handle<Object> receiver, Handle<Name> name,
                 Handle<JSReceiver> holder,
                 Configuration configuration = PROTOTYPE_CHAIN)
      : configuration_(ComputeConfiguration(configuration, name)),
        state_(NOT_FOUND),
        property_kind_(DATA),
        property_encoding_(DESCRIPTOR),
        property_details_(NONE, NORMAL, Representation::None()),
        isolate_(name->GetIsolate()),
        name_(name),
        holder_map_(holder->map(), isolate_),
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

  Factory* factory() const { return isolate_->factory(); }
  Handle<Object> GetReceiver() const {
    return maybe_receiver_.ToHandleChecked();
  }
  Handle<JSObject> GetStoreTarget() const;
  Handle<Map> holder_map() const { return holder_map_; }
  Handle<Map> transition_map() const {
    DCHECK_EQ(TRANSITION, state_);
    return transition_map_;
  }
  template <class T>
  Handle<T> GetHolder() const {
    DCHECK(IsFound());
    return Handle<T>::cast(maybe_holder_.ToHandleChecked());
  }
  Handle<JSReceiver> GetRoot() const;
  bool HolderIsReceiverOrHiddenPrototype() const;

  /* ACCESS_CHECK */
  bool HasAccess(v8::AccessType access_type) const;

  /* PROPERTY */
  // HasProperty needs to be called before any of the other PROPERTY methods
  // below can be used. It ensures that we are able to provide a definite
  // answer, and loads extra information about the property.
  bool HasProperty();
  void PrepareForDataProperty(Handle<Object> value);
  void PrepareTransitionToDataProperty(Handle<Object> value,
                                       PropertyAttributes attributes,
                                       Object::StoreFromKeyed store_mode);
  bool IsCacheableTransition() {
    bool cacheable =
        state_ == TRANSITION && transition_map()->GetBackPointer()->IsMap();
    if (cacheable) {
      property_details_ = transition_map_->GetLastDescriptorDetails();
      LoadPropertyKind();
      has_property_ = true;
    }
    return cacheable;
  }
  void ApplyTransitionToDataProperty();
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
  bool IsConfigurable() const { return property_details().IsConfigurable(); }
  bool IsReadOnly() const { return property_details().IsReadOnly(); }
  Representation representation() const {
    return property_details().representation();
  }
  FieldIndex GetFieldIndex() const;
  Handle<HeapType> GetFieldType() const;
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
  void LoadPropertyKind();

  bool IsBootstrapping() const;

  // Methods that fetch data from the holder ensure they always have a holder.
  // This means the receiver needs to be present as opposed to just the receiver
  // map. Other objects in the prototype chain are transitively guaranteed to be
  // present via the receiver map.
  bool is_guaranteed_to_have_holder() const {
    return !maybe_receiver_.is_null();
  }
  bool check_access_check() const {
    return (configuration_ & kAccessCheck) != 0;
  }
  bool check_hidden() const { return (configuration_ & kHidden) != 0; }
  bool check_interceptor() const {
    return !IsBootstrapping() && (configuration_ & kInterceptor) != 0;
  }
  bool check_prototype_chain() const {
    return (configuration_ & kPrototypeChain) != 0;
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
      return static_cast<Configuration>(configuration & HIDDEN);
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
  Handle<Map> transition_map_;
  MaybeHandle<Object> maybe_receiver_;
  MaybeHandle<JSReceiver> maybe_holder_;

  int number_;
};


} }  // namespace v8::internal

#endif  // V8_LOOKUP_H_
