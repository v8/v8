// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/parameter-initializer-rewriter.h"

#include "src/ast/ast.h"
#include "src/ast/ast-expression-visitor.h"
#include "src/ast/scopes.h"

namespace v8 {
namespace internal {

namespace {


class Rewriter final : public AstExpressionVisitor {
 public:
  Rewriter(uintptr_t stack_limit, Expression* initializer, Scope* param_scope)
      : AstExpressionVisitor(stack_limit, initializer),
        param_scope_(param_scope) {}

 private:
  void VisitExpression(Expression* expr) override {}

  void VisitFunctionLiteral(FunctionLiteral* expr) override;
  void VisitClassLiteral(ClassLiteral* expr) override;
  void VisitVariableProxy(VariableProxy* expr) override;

  void VisitBlock(Block* stmt) override;
  void VisitTryCatchStatement(TryCatchStatement* stmt) override;
  void VisitWithStatement(WithStatement* stmt) override;

  Scope* param_scope_;
};

void Rewriter::VisitFunctionLiteral(FunctionLiteral* function_literal) {
  function_literal->scope()->ReplaceOuterScope(param_scope_);
}


void Rewriter::VisitClassLiteral(ClassLiteral* class_literal) {
  class_literal->scope()->ReplaceOuterScope(param_scope_);
  if (class_literal->extends() != nullptr) {
    Visit(class_literal->extends());
  }
  // No need to visit the constructor since it will have the class
  // scope on its scope chain.
  ZoneList<ObjectLiteralProperty*>* props = class_literal->properties();
  for (int i = 0; i < props->length(); ++i) {
    ObjectLiteralProperty* prop = props->at(i);
    if (!prop->key()->IsLiteral()) {
      Visit(prop->key());
    }
    // No need to visit the values, since all values are functions with
    // the class scope on their scope chain.
    DCHECK(prop->value()->IsFunctionLiteral());
  }
}


void Rewriter::VisitVariableProxy(VariableProxy* proxy) {
  if (!proxy->is_resolved()) {
    if (param_scope_->outer_scope()->RemoveUnresolved(proxy)) {
      param_scope_->AddUnresolved(proxy);
    }
  } else {
    // Ensure that temporaries we find are already in the correct scope.
    DCHECK(proxy->var()->mode() != TEMPORARY ||
           proxy->var()->scope() == param_scope_->ClosureScope());
  }
}


void Rewriter::VisitBlock(Block* stmt) {
  if (stmt->scope() != nullptr)
    stmt->scope()->ReplaceOuterScope(param_scope_);
  else
    VisitStatements(stmt->statements());
}


void Rewriter::VisitTryCatchStatement(TryCatchStatement* stmt) {
  Visit(stmt->try_block());
  stmt->scope()->ReplaceOuterScope(param_scope_);
}


void Rewriter::VisitWithStatement(WithStatement* stmt) {
  Visit(stmt->expression());
  stmt->scope()->ReplaceOuterScope(param_scope_);
}


}  // anonymous namespace

void ReparentParameterExpressionScope(uintptr_t stack_limit, Expression* expr,
                                      Scope* param_scope) {
  // The only case that uses this code is block scopes for parameters containing
  // sloppy eval.
  DCHECK(param_scope->is_block_scope());
  DCHECK(param_scope->is_declaration_scope());
  DCHECK(param_scope->calls_sloppy_eval());
  DCHECK(param_scope->outer_scope()->is_function_scope());

  Rewriter rewriter(stack_limit, expr, param_scope);
  rewriter.Run();
}


}  // namespace internal
}  // namespace v8
