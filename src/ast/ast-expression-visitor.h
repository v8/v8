// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_AST_EXPRESSION_VISITOR_H_
#define V8_AST_AST_EXPRESSION_VISITOR_H_

#include "src/allocation.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/type-info.h"
#include "src/zone.h"

namespace v8 {
namespace internal {

// A Visitor over a CompilationInfo's AST that invokes
// VisitExpression on each expression node.

class AstExpressionVisitor : public AstTraversalVisitor {
 public:
  AstExpressionVisitor(Isolate* isolate, Expression* root);
  AstExpressionVisitor(uintptr_t stack_limit, Expression* root);
  void Run();

 protected:
  virtual void VisitExpression(Expression* expression) = 0;

 private:
#define DECLARE_VISIT(type) void Visit##type(type* node) override;
  EXPRESSION_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

  Expression* root_;

  DISALLOW_COPY_AND_ASSIGN(AstExpressionVisitor);
};
}  // namespace internal
}  // namespace v8

#endif  // V8_AST_AST_EXPRESSION_VISITOR_H_
