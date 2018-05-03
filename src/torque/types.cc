// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>

#include "src/torque/declarable.h"
#include "src/torque/types.h"

namespace v8 {
namespace internal {
namespace torque {

bool Type::Is(const std::string& name) const { return name == impl_->name(); }

const std::string& Type::name() const { return impl_->name(); }

bool Type::IsSubclass(Type from) {
  TypeImpl* to_class = type_impl();
  TypeImpl* from_class = from.type_impl();
  while (from_class != nullptr) {
    if (to_class == from_class) return true;
    from_class = from_class->parent();
  }
  return false;
}

const std::string& Type::GetGeneratedTypeName() const {
  return type_impl()->generated_type();
}

std::string Type::GetGeneratedTNodeTypeName() const {
  std::string result = type_impl()->generated_type();
  DCHECK_EQ(result.substr(0, 6), "TNode<");
  result = result.substr(6, result.length() - 7);
  return result;
}

std::ostream& operator<<(std::ostream& os, const Signature& sig) {
  os << "(";
  for (size_t i = 0; i < sig.parameter_names.size(); ++i) {
    if (i > 0) os << ", ";
    if (!sig.parameter_names.empty()) os << sig.parameter_names[i] << ": ";
    os << sig.parameter_types.types[i];
  }
  if (sig.parameter_types.var_args) {
    if (sig.parameter_names.size()) os << ", ";
    os << "...";
  }
  os << ")";
  if (!sig.return_type.IsVoid()) {
    os << ": " << sig.return_type;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const TypeVector& types) {
  for (size_t i = 0; i < types.size(); ++i) {
    if (i > 0) os << ", ";
    os << types[i];
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const ParameterTypes& p) {
  for (size_t i = 0; i < p.types.size(); ++i) {
    if (i > 0) os << ", ";
    os << p.types[i];
  }
  if (p.var_args) {
    if (p.types.size() > 0) os << ", ";
    os << "...";
  }
  return os;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
