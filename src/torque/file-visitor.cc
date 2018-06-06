// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/torque/file-visitor.h"

#include "src/torque/declarable.h"
#include "src/torque/parameter-difference.h"

namespace v8 {
namespace internal {
namespace torque {

Signature FileVisitor::MakeSignature(const CallableNodeSignature* signature) {
  LabelDeclarationVector definition_vector;
  for (auto label : signature->labels) {
    LabelDeclaration def = {label.name, GetTypeVector(label.types)};
    definition_vector.push_back(def);
  }
  Signature result{signature->parameters.names,
                   {GetTypeVector(signature->parameters.types),
                    signature->parameters.has_varargs},
                   declarations()->GetType(signature->return_type),
                   definition_vector};
  return result;
}

std::string FileVisitor::GetGeneratedCallableName(
    const std::string& name, const TypeVector& specialized_types) {
  std::string result = name;
  for (auto type : specialized_types) {
    std::string type_string = type->MangledName();
    result += std::to_string(type_string.size()) + type_string;
  }
  return result;
}

Callable* FileVisitor::LookupCall(const std::string& name,
                                  const TypeVector& parameter_types) {
  Callable* result = nullptr;
  Declarable* declarable = declarations()->Lookup(name);
  if (declarable->IsBuiltin()) {
    result = Builtin::cast(declarable);
  } else if (declarable->IsRuntimeFunction()) {
    result = RuntimeFunction::cast(declarable);
  } else if (declarable->IsMacroList()) {
    std::vector<Macro*> candidates;
    for (Macro* m : MacroList::cast(declarable)->list()) {
      if (IsCompatibleSignature(m->signature().parameter_types,
                                parameter_types)) {
        candidates.push_back(m);
      }
    }

    auto is_better_candidate = [&](Macro* a, Macro* b) {
      return ParameterDifference(a->signature().parameter_types.types,
                                 parameter_types)
          .StrictlyBetterThan(ParameterDifference(
              b->signature().parameter_types.types, parameter_types));
    };
    if (!candidates.empty()) {
      Macro* best = *std::min_element(candidates.begin(), candidates.end(),
                                      is_better_candidate);
      for (Macro* candidate : candidates) {
        if (candidate != best && !is_better_candidate(best, candidate)) {
          std::stringstream s;
          s << "ambiguous macro \"" << name << "\" with types ("
            << parameter_types << "), candidates:";
          for (Macro* m : candidates) {
            s << "\n    (" << m->signature().parameter_types << ") => "
              << m->signature().return_type;
          }
          ReportError(s.str());
        }
      }
      return best;
    }
    std::stringstream stream;
    stream << "cannot find macro with name \"" << name << "\"";
    ReportError(stream.str());
  } else {
    std::stringstream stream;
    stream << "can't call " << declarable->type_name() << " " << name
           << " because it's not callable"
           << ": call parameters were (" << parameter_types << ")";
    ReportError(stream.str());
  }

  size_t caller_size = parameter_types.size();
  size_t callee_size = result->signature().types().size();
  if (caller_size != callee_size &&
      !result->signature().parameter_types.var_args) {
    std::stringstream stream;
    stream << "parameter count mismatch calling " << *result << " - expected "
           << std::to_string(callee_size) << ", found "
           << std::to_string(caller_size);
    ReportError(stream.str());
  }

  return result;
}

void FileVisitor::QueueGenericSpecialization(
    const SpecializationKey& key, CallableNode* callable,
    const CallableNodeSignature* signature, base::Optional<Statement*> body) {
  pending_specializations_.push_back(
      {key, callable, signature, body, CurrentSourcePosition::Get()});
}

void FileVisitor::SpecializeGeneric(
    const PendingSpecialization& specialization) {
  CurrentSourcePosition::Scope scope(specialization.request_position);
  if (completed_specializations_.find(specialization.key) !=
      completed_specializations_.end()) {
    std::stringstream stream;
    stream << "cannot redeclare specialization of "
           << specialization.key.first->name() << " with types <"
           << specialization.key.second << ">";
    ReportError(stream.str());
  }
  if (!specialization.body) {
    std::stringstream stream;
    stream << "missing specialization of " << specialization.key.first->name()
           << " with types <" << specialization.key.second << ">";
    ReportError(stream.str());
  }
  Declarations::ScopedGenericSpecializationKey instantiation(
      declarations(), specialization.key);
  FileVisitor::ScopedModuleActivator activator(
      this, specialization.key.first->module());
  Specialize(specialization.key, specialization.callable,
             specialization.signature, *specialization.body);
  completed_specializations_.insert(specialization.key);
}

void FileVisitor::DrainSpecializationQueue() {
  while (pending_specializations_.size() != 0) {
    PendingSpecialization specialization(pending_specializations_.front());
    pending_specializations_.pop_front();
    if (completed_specializations_.find(specialization.key) ==
        completed_specializations_.end()) {
      Declarations::ScopedGenericScopeChainSnapshot scope(declarations(),
                                                          specialization.key);
      SpecializeGeneric(specialization);
    }
  }
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
