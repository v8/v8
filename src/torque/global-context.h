// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_GLOBAL_CONTEXT_H_
#define V8_TORQUE_GLOBAL_CONTEXT_H_

#include "src/torque/TorqueLexer.h"
#include "src/torque/TorqueParser.h"
#include "src/torque/declarable.h"
#include "src/torque/scope.h"
#include "src/torque/type-oracle.h"

namespace v8 {
namespace internal {
namespace torque {

class GlobalContext;
class Scope;
class TypeOracle;

class Module {
 public:
  Module(const std::string& name, GlobalContext& context)
      : name_(name), scope_(context) {}
  const std::string& name() const { return name_; }
  std::ostream& source_stream() { return source_stream_; }
  std::ostream& header_stream() { return header_stream_; }
  std::string source() { return source_stream_.str(); }
  std::string header() { return header_stream_.str(); }
  Scope* scope() { return &scope_; }

 private:
  std::string name_;
  Scope scope_;
  std::stringstream header_stream_;
  std::stringstream source_stream_;
};

class OperationHandler {
 public:
  std::string macro_name;
  ParameterTypes parameter_types;
  Type result_type;
};

struct SourceFileContext {
  std::string name;
  antlr4::ANTLRFileStream* stream;
  TorqueLexer* lexer;
  antlr4::CommonTokenStream* tokens;
  TorqueParser* parser;
  TorqueParser::FileContext* file;

  std::string sourceFileAndLineNumber(antlr4::ParserRuleContext* context) {
    antlr4::misc::Interval i = context->getSourceInterval();
    auto token = tokens->get(i.a);
    size_t line = token->getLine();
    size_t pos = token->getCharPositionInLine();
    return name + ":" + std::to_string(line) + ":" + std::to_string(pos);
  }
};

class GlobalContext {
 public:
  explicit GlobalContext(Ast ast)
      : verbose_(false),
        next_scope_number_(0),
        next_label_number_(0),
        default_module_(GetModule("base")),
        ast_(std::move(ast)) {}
  Module* GetDefaultModule() { return default_module_; }
  Module* GetModule(const std::string& name) {
    auto i = modules_.find(name);
    if (i != modules_.end()) {
      return i->second;
    }
    Module* module = new Module(name, *this);
    modules_[name] = module;
    return module;
  }
  int GetNextScopeNumber() { return next_scope_number_++; }
  int GetNextLabelNumber() { return next_label_number_++; }

  const std::map<std::string, Module*>& GetModules() const { return modules_; }

  Scope* GetParserRuleContextScope(const AstNode* context) {
    auto i = context_scopes_.find(context);
    if (i != context_scopes_.end()) return i->second;
    Scope* new_scope = new Scope(*this);
    context_scopes_[context] = new_scope;
    return new_scope;
  }

  Scope* TopScope() const { return scopes_.back(); }

  Declarable* Lookup(const std::string& name) const {
    auto e = scopes_.rend();
    auto c = scopes_.rbegin();
    while (c != e) {
      Declarable* result = (*c)->Lookup(name);
      if (result != nullptr) return result;
      ++c;
    }

    return nullptr;
  }

  void PushScope(Scope* scope) { scopes_.push_back(scope); }

  void PopScope() { scopes_.pop_back(); }

  std::set<const Variable*> GetLiveTypeVariables() {
    std::set<const Variable*> result;
    for (auto scope : scopes_) {
      scope->AddLiveVariables(result);
    }
    return result;
  }

  void SetVerbose() { verbose_ = true; }
  bool verbose() const { return verbose_; }

  void AddControlSplitChangedVariables(const AstNode* node,
                                       const std::set<const Variable*>& vars) {
    control_split_changed_variables_[node] = vars;
  }

  const std::set<const Variable*>& GetControlSplitChangedVariables(
      const AstNode* node) {
    assert(control_split_changed_variables_.find(node) !=
           control_split_changed_variables_.end());
    return control_split_changed_variables_.find(node)->second;
  }

  void MarkVariableChanged(const AstNode* node, Variable* var) {
    control_split_changed_variables_[node].insert(var);
  }

  friend class CurrentCallActivator;

  TypeOracle& GetTypeOracle() { return type_oracle_; }

  Callable* GetCurrentCallable() const { return current_callable_; }

  std::map<std::string, std::vector<OperationHandler>> op_handlers_;

  void PrintScopeChain() {
    for (auto s : scopes_) {
      s->Print();
    }
  }

  std::string PositionAsString(SourcePosition pos) {
    return ast_.PositionAsString(pos);
  }

  Ast* ast() { return &ast_; }

 private:
  bool verbose_;
  int next_scope_number_;
  int next_label_number_;
  std::map<std::string, Module*> modules_;
  Module* default_module_;
  std::vector<Scope*> scopes_;
  TypeOracle type_oracle_;
  Callable* current_callable_;
  std::map<const AstNode*, std::set<const Variable*>>
      control_split_changed_variables_;
  std::map<const AstNode*, Scope*> context_scopes_;
  Ast ast_;
};

class CurrentCallActivator {
 public:
  CurrentCallActivator(GlobalContext& context, Callable* callable)
      : context_(context) {
    context_.current_callable_ = callable;
  }
  ~CurrentCallActivator() { context_.current_callable_ = nullptr; }

 private:
  GlobalContext& context_;
};

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_GLOBAL_CONTEXT_H_
