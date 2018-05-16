// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_DECLARATIONS_H_
#define V8_TORQUE_DECLARATIONS_H_

#include <string>

#include "src/torque/declarable.h"
#include "src/torque/scope.h"
#include "src/torque/utils.h"

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

  Declarable* LookupGlobalScope(const std::string& name) {
    return chain_.LookupGlobalScope(name);
  }

  Declarable* LookupGlobalScope(SourcePosition pos, const std::string& name) {
    Declarable* d = chain_.LookupGlobalScope(name);
    if (d == nullptr) {
      std::stringstream s;
      s << "cannot find \"" << name << "\" in global scope at "
        << PositionAsString(pos);
      ReportError(s.str());
    }
    return d;
  }

  const Type* LookupType(SourcePosition pos, const std::string& name);
  const Type* LookupGlobalType(const std::string& name);
  const Type* LookupGlobalType(SourcePosition pos, const std::string& name);
  const Type* GetType(SourcePosition pos, TypeExpression* type_expression);

  const Type* GetFunctionPointerType(SourcePosition pos,
                                     TypeVector argument_types,
                                     const Type* return_type);

  Builtin* FindSomeInternalBuiltinWithType(const FunctionPointerType* type);

  Value* LookupValue(SourcePosition pos, const std::string& name);

  Macro* LookupMacro(SourcePosition pos, const std::string& name,
                     const TypeVector& types);

  Builtin* LookupBuiltin(SourcePosition pos, const std::string& name);

  Label* LookupLabel(SourcePosition pos, const std::string& name);

  Generic* LookupGeneric(const SourcePosition& pos, const std::string& name);

  const AbstractType* DeclareAbstractType(SourcePosition pos,
                                          const std::string& name,
                                          const std::string& generated,
                                          const std::string* parent = nullptr);

  void DeclareTypeAlias(SourcePosition pos, const std::string& name,
                        const Type* aliased_type);

  Label* DeclareLabel(SourcePosition pos, const std::string& name);

  Macro* DeclareMacro(SourcePosition pos, const std::string& name,
                      const Signature& signature);

  Builtin* DeclareBuiltin(SourcePosition pos, const std::string& name,
                          Builtin::Kind kind, bool external,
                          const Signature& signature);

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

  template <class T>
  T* RegisterDeclarable(std::unique_ptr<T> d) {
    T* ptr = d.get();
    declarables_.push_back(std::move(d));
    return ptr;
  }

  void Declare(const std::string& name, std::unique_ptr<Declarable> d) {
    chain_.Declare(name, RegisterDeclarable(std::move(d)));
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
  Deduplicator<FunctionPointerType> function_pointer_types_;
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
