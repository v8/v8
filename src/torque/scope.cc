// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>
#include <string>

#include "src/torque/global-context.h"

#include "src/torque/scope.h"

namespace v8 {
namespace internal {
namespace torque {

Scope::Scope(GlobalContext& global_context)
    : global_context_(global_context),
      scope_number_(global_context.GetNextScopeNumber()) {}

Macro* Scope::DeclareMacro(SourcePosition pos, const std::string& name,
                           Scope* scope, const Signature& signature) {
  auto i = lookup_.find(name);
  if (i == lookup_.end()) {
    lookup_[name] = new MacroList();
    i = lookup_.find(name);
  } else if (i->second->kind() != Declarable::kMacroList) {
    std::stringstream s;
    s << "cannot redeclare " << name << " as a non-macro at "
      << global_context_.PositionAsString(pos);
    ReportError(s.str());
  }
  MacroList* macro_list = MacroList::cast(i->second);
  for (auto macro : macro_list->list()) {
    if (signature.parameter_types.types ==
            macro->signature().parameter_types.types &&
        signature.parameter_types.var_args ==
            macro->signature().parameter_types.var_args) {
      std::stringstream s;
      s << "cannot redeclare " << name
        << " as a macro with identical parameter list "
        << signature.parameter_types << global_context_.PositionAsString(pos);
      ReportError(s.str());
    }
  }
  Macro* result = new Macro(name, scope, signature);
  macro_list->AddMacro(result);
  if (global_context_.verbose()) {
    std::cout << "declared " << *result << "\n";
  }
  return result;
}

Builtin* Scope::DeclareBuiltin(SourcePosition pos, const std::string& name,
                               bool java_script, Scope* scope,
                               const Signature& signature) {
  CheckAlreadyDeclared(pos, name, "builtin");
  Builtin* result = new Builtin(name, java_script, scope, signature);
  lookup_[name] = result;
  if (global_context_.verbose()) {
    std::cout << "declared " << *result << "\n";
  }
  return result;
}

Runtime* Scope::DeclareRuntime(SourcePosition pos, const std::string& name,
                               Scope* scope, const Signature& signature) {
  CheckAlreadyDeclared(pos, name, "runtime");
  Runtime* result = new Runtime(name, scope, signature);
  lookup_[name] = result;
  if (global_context_.verbose()) {
    std::cout << "declared " << *result << "\n";
  }
  return result;
}

Variable* Scope::DeclareVariable(SourcePosition pos, const std::string& var,
                                 Type type) {
  std::string name(std::string("v") + "_" + var +
                   std::to_string(scope_number_));
  CheckAlreadyDeclared(pos, var, "variable");
  Variable* result = new Variable(var, name, type);
  lookup_[var] = result;
  if (global_context_.verbose()) {
    std::cout << "declared " << var << " (type " << type << ")\n";
  }
  return result;
}

Parameter* Scope::DeclareParameter(SourcePosition pos, const std::string& name,
                                   const std::string& var_name, Type type) {
  CheckAlreadyDeclared(pos, name, "parameter");
  Parameter* result = new Parameter(name, type, var_name);
  lookup_[name] = result;
  return result;
}

Label* Scope::DeclareLabel(SourcePosition pos, const std::string& name,
                           Label* already_defined) {
  CheckAlreadyDeclared(pos, name, "label");
  Label* result =
      already_defined == nullptr ? new Label(name) : already_defined;
  lookup_[name] = result;
  return result;
}

void Scope::DeclareConstant(SourcePosition pos, const std::string& name,
                            Type type, const std::string& value) {
  CheckAlreadyDeclared(pos, name, "constant, parameter or arguments");
  lookup_[name] = new Constant(name, type, value);
}

void Scope::AddLiveVariables(std::set<const Variable*>& set) {
  for (auto current : lookup_) {
    if (current.second->IsVariable()) {
      set.insert(Variable::cast(current.second));
    }
  }
}

void Scope::CheckAlreadyDeclared(SourcePosition pos, const std::string& name,
                                 const char* new_type) {
  auto i = lookup_.find(name);
  if (i != lookup_.end()) {
    std::stringstream s;
    s << "cannot redeclare " << name << " (type " << new_type << ") at "
      << global_context_.PositionAsString(pos)
      << " (it's already declared as a " << i->second->type_name() << ")\n";
    ReportError(s.str());
  }
}

void Scope::Print() {
  std::cout << "scope #" << std::to_string(scope_number_) << "\n";
  for (auto i : lookup_) {
    std::cout << i.first << ": " << i.second << "\n";
  }
}

Scope::Activator::Activator(Scope* scope) : scope_(scope) {
  scope->global_context().PushScope(scope);
}

Scope::Activator::Activator(GlobalContext& global_context, const AstNode* node)
    : Activator(global_context.GetParserRuleContextScope(node)) {}

Scope::Activator::~Activator() { scope_->global_context().PopScope(); }

}  // namespace torque
}  // namespace internal
}  // namespace v8
