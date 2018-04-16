// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/file-visitor.h"

#include "src/torque/declarable.h"

namespace v8 {
namespace internal {
namespace torque {

Signature FileVisitor::MakeSignature(const ParameterList& parameters,
                                     const std::string& return_type,
                                     const LabelAndTypesVector& labels) {
  LabelDeclarationVector definition_vector;
  for (auto label : labels) {
    LabelDeclaration def = {label.name, GetTypeVector(label.types)};
    definition_vector.push_back(def);
  }
  Signature result{parameters.names,
                   {GetTypeVector(parameters.types), parameters.has_varargs},
                   GetType(return_type),
                   definition_vector,
                   {}};
  for (auto label : labels) {
    auto label_params = GetTypeVector(label.types);
    std::vector<Variable*> vars;
    size_t i = 0;
    for (auto var_type : label_params) {
      std::string name = label.name + std::to_string(i++);
      Variable* var = new Variable(label.name, name, var_type);
      vars.push_back(var);
    }
    result.labels.push_back(new Label(label.name, vars));
  }
  return result;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
