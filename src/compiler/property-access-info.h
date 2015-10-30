// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PROPERTY_ACCESS_INFO_H_
#define V8_COMPILER_PROPERTY_ACCESS_INFO_H_

#include <iosfwd>

#include "src/field-index.h"
#include "src/objects.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {

// Forward declarations.
class CompilationDependencies;
class Factory;
class TypeCache;


namespace compiler {

// Whether we are loading a property or storing to a property.
enum class PropertyAccessMode { kLoad, kStore };

std::ostream& operator<<(std::ostream&, PropertyAccessMode);


// This class encapsulates all information required to access a certain
// object property, either on the object itself or on the prototype chain.
class PropertyAccessInfo final {
 public:
  enum Kind { kInvalid, kDataConstant, kDataField };

  static PropertyAccessInfo DataConstant(Type* receiver_type,
                                         Handle<Object> constant,
                                         MaybeHandle<JSObject> holder) {
    return PropertyAccessInfo(holder, constant, receiver_type);
  }
  static PropertyAccessInfo DataField(
      Type* receiver_type, FieldIndex field_index, Type* field_type,
      MaybeHandle<JSObject> holder = MaybeHandle<JSObject>(),
      MaybeHandle<Map> transition_map = MaybeHandle<Map>()) {
    return PropertyAccessInfo(holder, transition_map, field_index, field_type,
                              receiver_type);
  }

  PropertyAccessInfo();

  bool IsDataConstant() const { return kind() == kDataConstant; }
  bool IsDataField() const { return kind() == kDataField; }

  Kind kind() const { return kind_; }
  MaybeHandle<JSObject> holder() const { return holder_; }
  MaybeHandle<Map> transition_map() const { return transition_map_; }
  Handle<Object> constant() const { return constant_; }
  FieldIndex field_index() const { return field_index_; }
  Type* field_type() const { return field_type_; }
  Type* receiver_type() const { return receiver_type_; }

  bool HasTransitionMap() const { return !transition_map().is_null(); }

 private:
  PropertyAccessInfo(MaybeHandle<JSObject> holder, Handle<Object> constant,
                     Type* receiver_type);
  PropertyAccessInfo(MaybeHandle<JSObject> holder,
                     MaybeHandle<Map> transition_map, FieldIndex field_index,
                     Type* field_type, Type* receiver_type);

  Kind kind_;
  Type* receiver_type_;
  Handle<Object> constant_;
  MaybeHandle<Map> transition_map_;
  MaybeHandle<JSObject> holder_;
  FieldIndex field_index_;
  Type* field_type_;
};


// Factory class for {PropertyAccessInfo}s.
class PropertyAccessInfoFactory final {
 public:
  PropertyAccessInfoFactory(CompilationDependencies* dependencies,
                            Handle<Context> native_context, Zone* zone);

  bool ComputePropertyAccessInfo(Handle<Map> map, Handle<Name> name,
                                 PropertyAccessMode access_mode,
                                 PropertyAccessInfo* access_info);
  bool ComputePropertyAccessInfos(MapHandleList const& maps, Handle<Name> name,
                                  PropertyAccessMode access_mode,
                                  ZoneVector<PropertyAccessInfo>* access_infos);

 private:
  bool LookupSpecialFieldAccessor(Handle<Map> map, Handle<Name> name,
                                  PropertyAccessInfo* access_info);
  bool LookupTransition(Handle<Map> map, Handle<Name> name,
                        MaybeHandle<JSObject> holder,
                        PropertyAccessInfo* access_info);

  CompilationDependencies* dependencies() const { return dependencies_; }
  Factory* factory() const;
  Isolate* isolate() const { return isolate_; }
  Handle<Context> native_context() const { return native_context_; }
  Zone* zone() const { return zone_; }

  CompilationDependencies* const dependencies_;
  Handle<Context> const native_context_;
  Isolate* const isolate_;
  TypeCache const& type_cache_;
  Zone* const zone_;

  DISALLOW_COPY_AND_ASSIGN(PropertyAccessInfoFactory);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PROPERTY_ACCESS_INFO_H_
