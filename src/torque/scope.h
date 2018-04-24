// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_SCOPE_H_
#define V8_TORQUE_SCOPE_H_

#include <string>

#include "./antlr4-runtime.h"
#include "src/torque/ast.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

class Builtin;
class Callable;
class Declarable;
class GlobalContext;
class Macro;
class Parameter;
class Runtime;
class Variable;

class Scope {
 public:
  explicit Scope(GlobalContext& global_context);

  Macro* DeclareMacro(SourcePosition pos, const std::string& name, Scope* scope,
                      const Signature& signature);

  Builtin* DeclareBuiltin(SourcePosition pos, const std::string& name,
                          Builtin::Kind kind, Scope* scope,
                          const Signature& signature);

  Runtime* DeclareRuntime(SourcePosition pos, const std::string& name,
                          Scope* scope, const Signature& signature);

  Variable* DeclareVariable(SourcePosition pos, const std::string& var,
                            Type type);

  Parameter* DeclareParameter(SourcePosition pos, const std::string& name,
                              const std::string& mangled_name, Type type);

  Label* DeclareLabel(SourcePosition pos, const std::string& name);

  Label* DeclarePrivateLabel(SourcePosition pos, const std::string& name);

  void DeclareConstant(SourcePosition pos, const std::string& name, Type type,
                       const std::string& value);

  Declarable* Lookup(const std::string& name) {
    auto i = lookup_.find(name);
    if (i == lookup_.end()) {
      return nullptr;
    }
    return i->second.get();
  }

  void Stream(std::ostream& stream) const {
    stream << "scope " << std::to_string(scope_number_) << " {";
    for (auto& c : lookup_) {
      stream << c.first << ",";
    }
    stream << "}";
  }

  GlobalContext& global_context() const { return global_context_; }

  void AddLiveVariables(std::set<const Variable*>& set);

  void Print();

  class Activator;

 private:
  void CheckAlreadyDeclared(SourcePosition pos, const std::string& name,
                            const char* new_type);

  GlobalContext& global_context_;
  int scope_number_;
  int private_label_number_;
  std::map<std::string, std::unique_ptr<Declarable>> lookup_;
};

class Scope::Activator {
 public:
  explicit Activator(GlobalContext& global_context, const AstNode* node);
  explicit Activator(Scope* scope);
  ~Activator();

 private:
  Scope* scope_;
};

inline std::ostream& operator<<(std::ostream& os, const Scope& scope) {
  scope.Stream(os);
  return os;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_SCOPE_H_
