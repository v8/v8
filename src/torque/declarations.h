// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_DECLARATIONS_H_
#define V8_TORQUE_DECLARATIONS_H_

#include <string>

#include "src/torque/declarable.h"
#include "src/torque/scope.h"

namespace v8 {
namespace internal {
namespace torque {

class Declarations {
 public:
  explicit Declarations(SourceFileMap* source_file_map)
      : source_file_map_(source_file_map),
        unique_declaration_number_(0),
        current_generic_specialization_(nullptr) {}

  Declarable* Lookup(const std::string& name) { return chain_.Lookup(name); }

  Declarable* Lookup(SourcePosition pos, const std::string& name) {
    Declarable* d = Lookup(name);
    if (d == nullptr) {
      std::stringstream s;
      s << "cannot find \"" << name << "\" at " << PositionAsString(pos);
      ReportError(s.str());
    }
    return d;
  }

  const Type* LookupType(SourcePosition pos, const std::string& name);

  Value* LookupValue(SourcePosition pos, const std::string& name);

  Macro* LookupMacro(SourcePosition pos, const std::string& name,
                     const TypeVector& types);

  Builtin* LookupBuiltin(SourcePosition pos, const std::string& name);

  Label* LookupLabel(SourcePosition pos, const std::string& name);

  Generic* LookupGeneric(const SourcePosition& pos, const std::string& name);

  const Type* DeclareType(SourcePosition pos, const std::string& name,
                          const std::string& generated,
                          const std::string* parent = nullptr);

  void DeclareTypeAlias(SourcePosition pos, const std::string& name,
                        const Type* aliased_type);

  Label* DeclareLabel(SourcePosition pos, const std::string& name);

  Macro* DeclareMacro(SourcePosition pos, const std::string& name,
                      const Signature& signature);

  Builtin* DeclareBuiltin(SourcePosition pos, const std::string& name,
                          const Builtin::Kind kind, const Signature& signature);

  RuntimeFunction* DeclareRuntimeFunction(SourcePosition pos,
                                          const std::string& name,
                                          const Signature& signature);

  Variable* DeclareVariable(SourcePosition pos, const std::string& var,
                            const Type* type);

  Parameter* DeclareParameter(SourcePosition pos, const std::string& name,
                              const std::string& mangled_name,
                              const Type* type);

  Label* DeclarePrivateLabel(SourcePosition pos, const std::string& name);

  void DeclareConstant(SourcePosition pos, const std::string& name,
                       const Type* type, const std::string& value);

  Generic* DeclareGeneric(SourcePosition pos, const std::string& name,
                          Module* module, GenericDeclaration* generic);

  TypeVector GetCurrentSpecializationTypeNamesVector();

  ScopeChain::Snapshot GetScopeChainSnapshot() { return chain_.TaskSnapshot(); }

  std::set<const Variable*> GetLiveVariables() {
    return chain_.GetLiveVariables();
  }

  std::string PositionAsString(SourcePosition pos) {
    return source_file_map_->PositionAsString(pos);
  }

  Statement* next_body() const { return next_body_; }

  void PrintScopeChain() { chain_.Print(); }

  class NodeScopeActivator;
  class GenericScopeActivator;
  class ScopedGenericInstantiation;

 private:
  Scope* GetNodeScope(const AstNode* node);
  Scope* GetGenericScope(Generic* generic, const TypeVector& types);

  void Declare(const std::string& name, std::unique_ptr<Declarable> d) {
    Declarable* ptr = d.get();
    declarables_.emplace_back(std::move(d));
    chain_.Declare(name, ptr);
  }

  int GetNextUniqueDeclarationNumber() { return unique_declaration_number_++; }

  void CheckAlreadyDeclared(SourcePosition pos, const std::string& name,
                            const char* new_type);

  SourceFileMap* source_file_map_;
  int unique_declaration_number_;
  ScopeChain chain_;
  const SpecializationKey* current_generic_specialization_;
  Statement* next_body_;
  std::vector<std::unique_ptr<Declarable>> declarables_;
  std::map<std::pair<const AstNode*, TypeVector>, Scope*> scopes_;
  std::map<Generic*, ScopeChain::Snapshot> generic_declaration_scopes_;
};

class Declarations::NodeScopeActivator {
 public:
  NodeScopeActivator(Declarations* declarations, AstNode* node)
      : activator_(declarations->GetNodeScope(node)) {}

 private:
  Scope::Activator activator_;
};

class Declarations::GenericScopeActivator {
 public:
  GenericScopeActivator(Declarations* declarations,
                        const SpecializationKey& key)
      : activator_(declarations->GetGenericScope(key.first, key.second)) {}

 private:
  Scope::Activator activator_;
};

class Declarations::ScopedGenericInstantiation {
 public:
  ScopedGenericInstantiation(Declarations* declarations,
                             const SpecializationKey& key)
      : declarations_(declarations),
        restorer_(declarations->generic_declaration_scopes_[key.first]) {
    declarations->current_generic_specialization_ = &key;
  }
  ~ScopedGenericInstantiation() {
    declarations_->current_generic_specialization_ = nullptr;
  }

 private:
  Declarations* declarations_;
  ScopeChain::ScopedSnapshotRestorer restorer_;
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_DECLARATIONS_H_
