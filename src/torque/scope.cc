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
      scope_number_(global_context.GetNextScopeNumber()),
      private_label_number_(0) {
  global_context.RegisterScope(this);
}

Macro* Scope::DeclareMacro(SourcePosition pos, const std::string& name,
                           Scope* scope, const Signature& signature) {
  auto i = lookup_.find(name);
  if (i == lookup_.end()) {
    lookup_[name] = std::unique_ptr<MacroList>(new MacroList());
    i = lookup_.find(name);
  } else if (i->second->kind() != Declarable::kMacroList) {
    std::stringstream s;
    s << "cannot redeclare " << name << " as a non-macro at "
      << global_context_.PositionAsString(pos);
    ReportError(s.str());
  }
  MacroList* macro_list = MacroList::cast(i->second.get());
  for (auto& macro : macro_list->list()) {
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
  Macro* result = macro_list->AddMacro(
      std::unique_ptr<Macro>(new Macro(name, scope, signature)));
  if (global_context_.verbose()) {
    std::cout << "declared " << *result << "\n";
  }
  return result;
}

Builtin* Scope::DeclareBuiltin(SourcePosition pos, const std::string& name,
                               Builtin::Kind kind, Scope* scope,
                               const Signature& signature) {
  CheckAlreadyDeclared(pos, name, "builtin");
  Builtin* result = new Builtin(name, kind, scope, signature);
  lookup_[name] = std::unique_ptr<Builtin>(result);
  if (global_context_.verbose()) {
    std::cout << "declared " << *result << "\n";
  }
  return result;
}

Runtime* Scope::DeclareRuntime(SourcePosition pos, const std::string& name,
                               Scope* scope, const Signature& signature) {
  CheckAlreadyDeclared(pos, name, "runtime");
  Runtime* result = new Runtime(name, scope, signature);
  lookup_[name] = std::unique_ptr<Runtime>(result);
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
  lookup_[var] = std::unique_ptr<Variable>(result);
  if (global_context_.verbose()) {
    std::cout << "declared " << var << " (type " << type << ")\n";
  }
  return result;
}

Parameter* Scope::DeclareParameter(SourcePosition pos, const std::string& name,
                                   const std::string& var_name, Type type) {
  CheckAlreadyDeclared(pos, name, "parameter");
  Parameter* result = new Parameter(name, type, var_name);
  lookup_[name] = std::unique_ptr<Parameter>(result);
  return result;
}

Label* Scope::DeclareLabel(SourcePosition pos, const std::string& name) {
  CheckAlreadyDeclared(pos, name, "label");
  Label* result = new Label(name);
  lookup_[name] = std::unique_ptr<Label>(result);
  return result;
}

Label* Scope::DeclarePrivateLabel(SourcePosition pos,
                                  const std::string& raw_name) {
  std::string name = raw_name + "_" + std::to_string(private_label_number_++);
  CheckAlreadyDeclared(pos, name, "label");
  Label* result = new Label(name);
  lookup_[name] = std::unique_ptr<Label>(result);
  return result;
}

void Scope::DeclareConstant(SourcePosition pos, const std::string& name,
                            Type type, const std::string& value) {
  CheckAlreadyDeclared(pos, name, "constant, parameter or arguments");
  lookup_[name] = std::unique_ptr<Constant>(new Constant(name, type, value));
}

void Scope::AddLiveVariables(std::set<const Variable*>& set) {
  for (auto& current : lookup_) {
    if (current.second->IsVariable()) {
      set.insert(Variable::cast(current.second.get()));
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
  for (auto& i : lookup_) {
    std::cout << i.first << ": " << i.second.get() << "\n";
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
