// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/ast/ast-expression-visitor.h"

#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/codegen.h"

namespace v8 {
namespace internal {

AstExpressionVisitor::AstExpressionVisitor(Isolate* isolate, Expression* root)
    : AstTraversalVisitor(isolate), root_(root) {}

AstExpressionVisitor::AstExpressionVisitor(uintptr_t stack_limit,
                                           Expression* root)
    : AstTraversalVisitor(stack_limit), root_(root) {}

void AstExpressionVisitor::Run() { Visit(root_); }

void AstExpressionVisitor::VisitFunctionLiteral(FunctionLiteral* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitFunctionLiteral(expr);
}

void AstExpressionVisitor::VisitNativeFunctionLiteral(
    NativeFunctionLiteral* expr) {
  AstTraversalVisitor::VisitNativeFunctionLiteral(expr);
}

void AstExpressionVisitor::VisitDoExpression(DoExpression* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitDoExpression(expr);
}

void AstExpressionVisitor::VisitConditional(Conditional* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitConditional(expr);
}

void AstExpressionVisitor::VisitVariableProxy(VariableProxy* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitVariableProxy(expr);
}

void AstExpressionVisitor::VisitLiteral(Literal* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitLiteral(expr);
}

void AstExpressionVisitor::VisitRegExpLiteral(RegExpLiteral* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitRegExpLiteral(expr);
}

void AstExpressionVisitor::VisitObjectLiteral(ObjectLiteral* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitObjectLiteral(expr);
}

void AstExpressionVisitor::VisitArrayLiteral(ArrayLiteral* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitArrayLiteral(expr);
}

void AstExpressionVisitor::VisitAssignment(Assignment* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitAssignment(expr);
}

void AstExpressionVisitor::VisitYield(Yield* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitYield(expr);
}

void AstExpressionVisitor::VisitThrow(Throw* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitThrow(expr);
}

void AstExpressionVisitor::VisitProperty(Property* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitProperty(expr);
}

void AstExpressionVisitor::VisitCall(Call* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitCall(expr);
}

void AstExpressionVisitor::VisitCallNew(CallNew* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitCallNew(expr);
}

void AstExpressionVisitor::VisitCallRuntime(CallRuntime* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitCallRuntime(expr);
}

void AstExpressionVisitor::VisitUnaryOperation(UnaryOperation* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitUnaryOperation(expr);
}

void AstExpressionVisitor::VisitCountOperation(CountOperation* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitCountOperation(expr);
}

void AstExpressionVisitor::VisitBinaryOperation(BinaryOperation* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitBinaryOperation(expr);
}

void AstExpressionVisitor::VisitCompareOperation(CompareOperation* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitCompareOperation(expr);
}

void AstExpressionVisitor::VisitThisFunction(ThisFunction* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitThisFunction(expr);
}

void AstExpressionVisitor::VisitClassLiteral(ClassLiteral* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitClassLiteral(expr);
}

void AstExpressionVisitor::VisitSpread(Spread* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitSpread(expr);
}

void AstExpressionVisitor::VisitSuperPropertyReference(
    SuperPropertyReference* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitSuperPropertyReference(expr);
}

void AstExpressionVisitor::VisitSuperCallReference(SuperCallReference* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitSuperCallReference(expr);
}

void AstExpressionVisitor::VisitCaseClause(CaseClause* expr) {
  AstTraversalVisitor::VisitCaseClause(expr);
}

void AstExpressionVisitor::VisitEmptyParentheses(EmptyParentheses* expr) {
  AstTraversalVisitor::VisitEmptyParentheses(expr);
}

void AstExpressionVisitor::VisitRewritableExpression(
    RewritableExpression* expr) {
  VisitExpression(expr);
  AstTraversalVisitor::VisitRewritableExpression(expr);
}


}  // namespace internal
}  // namespace v8
