// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/type-oracle.h"
#include "src/torque/type-visitor.h"

namespace v8 {
namespace internal {
namespace torque {

DEFINE_CONTEXTUAL_VARIABLE(TypeOracle)

// static
const std::vector<std::unique_ptr<AggregateType>>*
TypeOracle::GetAggregateTypes() {
  return &Get().aggregate_types_;
}

// static
void TypeOracle::FinalizeAggregateTypes() {
  for (const std::unique_ptr<AggregateType>& p : Get().aggregate_types_) {
    p->Finalize();
  }
}

// static
const Type* TypeOracle::GetGenericTypeInstance(GenericType* generic_type,
                                               TypeVector arg_types) {
  auto& params = generic_type->generic_parameters();
  auto& specializations = generic_type->specializations();

  if (params.size() != arg_types.size()) {
    ReportError("Generic struct takes ", params.size(), " parameters, but ",
                arg_types.size(), " were given");
  }

  if (auto specialization = specializations.Get(arg_types)) {
    return *specialization;
  } else {
    CurrentScope::Scope generic_scope(generic_type->ParentScope());
    auto type = TypeVisitor::ComputeType(generic_type->declaration(),
                                         {{generic_type, arg_types}});
    specializations.Add(arg_types, type);
    return type;
  }
}

// static
Namespace* TypeOracle::CreateGenericTypeInstatiationNamespace() {
  Get().generic_type_instantiation_namespaces_.push_back(
      std::make_unique<Namespace>(GENERIC_TYPE_INSTANTIATION_NAMESPACE_STRING));
  return Get().generic_type_instantiation_namespaces_.back().get();
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
