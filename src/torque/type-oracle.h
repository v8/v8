// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_TYPE_ORACLE_H_
#define V8_TORQUE_TYPE_ORACLE_H_

#include "src/torque/declarable.h"
#include "src/torque/declarations.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

class TypeOracle {
 public:
  explicit TypeOracle(Declarations* declarations)
      : declarations_(declarations) {}

  void RegisterImplicitConversion(Type to, Type from) {
    implicit_conversions_.push_back(std::make_pair(to, from));
  }

  Type GetArgumentsType() { return GetBuiltinType(ARGUMENTS_TYPE_STRING); }

  Type GetBoolType() { return GetBuiltinType(BOOL_TYPE_STRING); }

  Type GetConstexprBoolType() {
    return GetBuiltinType(CONSTEXPR_BOOL_TYPE_STRING);
  }

  Type GetVoidType() { return GetBuiltinType(VOID_TYPE_STRING); }

  Type GetObjectType() { return GetBuiltinType(OBJECT_TYPE_STRING); }

  Type GetStringType() { return GetBuiltinType(STRING_TYPE_STRING); }

  Type GetIntPtrType() { return GetBuiltinType(INTPTR_TYPE_STRING); }

  Type GetNeverType() { return GetBuiltinType(NEVER_TYPE_STRING); }

  Type GetConstInt31Type() { return GetBuiltinType(CONST_INT31_TYPE_STRING); }

  bool IsAssignableFrom(Type to, Type from) {
    if (to == from) return true;
    if (to.IsSubclass(from) && !from.IsConstexpr()) return true;
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
  Type GetBuiltinType(const std::string& name) {
    Declarable* declarable = declarations_->Lookup(name);
    DCHECK(declarable != nullptr);
    return Type(TypeImpl::cast(declarable));
  }

  Declarations* declarations_;
  std::vector<std::pair<Type, Type>> implicit_conversions_;
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_TYPE_ORACLE_H_
