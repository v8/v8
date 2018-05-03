// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/file-visitor.h"

#include "src/torque/declarable.h"

namespace v8 {
namespace internal {
namespace torque {

Signature FileVisitor::MakeSignature(SourcePosition pos,
                                     const ParameterList& parameters,
                                     const std::string& return_type,
                                     const LabelAndTypesVector& labels) {
  LabelDeclarationVector definition_vector;
  for (auto label : labels) {
    LabelDeclaration def = {label.name, GetTypeVector(pos, label.types)};
    definition_vector.push_back(def);
  }
  Signature result{
      parameters.names,
      {GetTypeVector(pos, parameters.types), parameters.has_varargs},
      declarations()->LookupType(pos, return_type),
      definition_vector};
  return result;
}

Callable* FileVisitor::LookupCall(SourcePosition pos, const std::string& name,
                                  const TypeVector& parameter_types) {
  Callable* result = nullptr;
  Declarable* declarable = declarations()->Lookup(pos, name);
  if (declarable->IsBuiltin()) {
    result = Builtin::cast(declarable);
  } else if (declarable->IsRuntimeFunction()) {
    result = RuntimeFunction::cast(declarable);
  } else if (declarable->IsMacroList()) {
    for (auto& m : MacroList::cast(declarable)->list()) {
      if (GetTypeOracle().IsCompatibleSignature(m->signature().parameter_types,
                                                parameter_types)) {
        if (result != nullptr) {
          std::stringstream stream;
          stream << "multiple matching matching parameter list for macro "
                 << name << ": (" << parameter_types << ") and ("
                 << result->signature().parameter_types << ") at "
                 << PositionAsString(pos);
          ReportError(stream.str());
        }
        result = m;
      }
    }
    if (result == nullptr) {
      std::stringstream stream;
      stream << "no matching matching parameter list for macro " << name
             << ": call parameters were (" << parameter_types << ") at "
             << PositionAsString(pos);
      ReportError(stream.str());
    }
  }

  size_t caller_size = parameter_types.size();
  size_t callee_size = result->signature().types().size();
  if (caller_size != callee_size &&
      !result->signature().parameter_types.var_args) {
    std::stringstream stream;
    stream << "parameter count mismatch calling " << *result << ": expected "
           << std::to_string(callee_size) << ", found "
           << std::to_string(caller_size);
    ReportError(stream.str());
  }

  return result;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
