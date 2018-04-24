// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_TYPE_ORACLE_H_
#define V8_TORQUE_TYPE_ORACLE_H_

#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

class TypeOracle {
 public:
  void RegisterTypeImpl(const std::string& name, const std::string& generated,
                        const std::string* parent = nullptr) {
    TypeImpl* parent_class = nullptr;
    if (type_impls_.find(name) != type_impls_.end()) {
      ReportError(std::string("cannot redefine type class ") + name);
    }
    if (parent != nullptr) {
      auto i = type_impls_.find(*parent);
      if (i == type_impls_.end()) {
        std::stringstream s;
        s << "cannot find parent type class " << *parent << " for " << name;
        ReportError(s.str());
      }
      parent_class = i->second.get();
    }
    TypeImpl* new_class = new TypeImpl(parent_class, name, generated);
    type_impls_[name] = std::unique_ptr<TypeImpl>(new_class);
  }

  void RegisterImplicitConversion(Type to, Type from) {
    implicit_conversions_.push_back(std::make_pair(to, from));
  }

  Type GetType(const std::string& type_name) {
    auto i = type_impls_.find(type_name);
    if (i == type_impls_.end()) {
      std::stringstream s;
      s << "no type class found for type " << type_name;
      ReportError(s.str());
    }
    return Type(i->second.get());
  }

  Type GetArgumentsType() { return GetType(ARGUMENTS_TYPE_STRING); }

  Type GetTaggedType() { return GetType(TAGGED_TYPE_STRING); }

  Type GetExceptionType() { return GetType(EXCEPTION_TYPE_STRING); }

  Type GetBranchType() { return GetType(BRANCH_TYPE_STRING); }

  Type GetBitType() { return GetType(BIT_TYPE_STRING); }

  Type GetVoidType() { return GetType(VOID_TYPE_STRING); }

  Type GetObjectType() { return GetType(OBJECT_TYPE_STRING); }

  Type GetStringType() { return GetType(STRING_TYPE_STRING); }

  Type GetIntPtrType() { return GetType(INTPTR_TYPE_STRING); }

  Type GetNeverType() { return GetType(NEVER_TYPE_STRING); }

  Type GetConstInt31Type() { return GetType(CONST_INT31_TYPE_STRING); }

  bool IsException(Type from) { return GetExceptionType().IsSubclass(from); }

  bool IsAssignableFrom(Type to, Type from) {
    if (to.IsSubclass(from)) return true;
    return IsImplicitlyConverableFrom(to, from);
  }

  bool IsImplicitlyConverableFrom(Type to, Type from) {
    for (auto& conversion : implicit_conversions_) {
      if (conversion.first == to && conversion.second == from) {
        return true;
      }
    }
    return false;
  }

  bool IsCompatibleSignature(const ParameterTypes& to, const TypeVector& from) {
    auto i = to.types.begin();
    for (auto current : from) {
      if (i == to.types.end()) {
        if (!to.var_args) return false;
        if (!IsAssignableFrom(GetObjectType(), current)) return false;
      } else {
        if (!IsAssignableFrom(*i++, current)) return false;
      }
    }
    return true;
  }

 private:
  std::map<std::string, std::unique_ptr<TypeImpl>> type_impls_;
  std::vector<std::pair<Type, Type>> implicit_conversions_;
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_TYPE_ORACLE_H_
