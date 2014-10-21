// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#include "src/ast.h"
#include "src/ast-numbering.h"
#include "src/compiler.h"
#include "src/scopes.h"

namespace v8 {
namespace internal {


class AstNumberingVisitor FINAL : public AstVisitor {
 public:
  explicit AstNumberingVisitor(Zone* zone)
      : AstVisitor(), next_id_(BailoutId::FirstUsable().ToInt()) {
    InitializeAstVisitor(zone);
  }

  void Renumber(FunctionLiteral* node);

 private:
// AST node visitor interface.
#define DEFINE_VISIT(type) virtual void Visit##type(type* node);
  AST_NODE_LIST(DEFINE_VISIT)
#undef DEFINE_VISIT

  void VisitStatements(ZoneList<Statement*>* statements);
  void VisitDeclarations(ZoneList<Declaration*>* declarations);
  void VisitArguments(ZoneList<Expression*>* arguments);
  void VisitObjectLiteralProperty(ObjectLiteralProperty* property);

  int ReserveIdRange(int n) {
    int tmp = next_id_;
    next_id_ += n;
    return tmp;
  }

  int next_id_;

  DEFINE_AST_VISITOR_SUBCLASS_MEMBERS();
  DISALLOW_COPY_AND_ASSIGN(AstNumberingVisitor);
};


void AstNumberingVisitor::VisitVariableDeclaration(VariableDeclaration* node) {
  VisitVariableProxy(node->proxy());
}


void AstNumberingVisitor::VisitExportDeclaration(ExportDeclaration* node) {
  VisitVariableProxy(node->proxy());
}


void AstNumberingVisitor::VisitModuleUrl(ModuleUrl* node) {}


void AstNumberingVisitor::VisitEmptyStatement(EmptyStatement* node) {}


void AstNumberingVisitor::VisitContinueStatement(ContinueStatement* node) {}


void AstNumberingVisitor::VisitBreakStatement(BreakStatement* node) {}


void AstNumberingVisitor::VisitDebuggerStatement(DebuggerStatement* node) {
  node->set_base_id(ReserveIdRange(DebuggerStatement::num_ids()));
}


void AstNumberingVisitor::VisitNativeFunctionLiteral(
    NativeFunctionLiteral* node) {
  node->set_base_id(ReserveIdRange(NativeFunctionLiteral::num_ids()));
}


void AstNumberingVisitor::VisitLiteral(Literal* node) {
  node->set_base_id(ReserveIdRange(Literal::num_ids()));
}


void AstNumberingVisitor::VisitRegExpLiteral(RegExpLiteral* node) {
  node->set_base_id(ReserveIdRange(RegExpLiteral::num_ids()));
}


void AstNumberingVisitor::VisitVariableProxy(VariableProxy* node) {
  node->set_base_id(ReserveIdRange(VariableProxy::num_ids()));
}


void AstNumberingVisitor::VisitThisFunction(ThisFunction* node) {
  node->set_base_id(ReserveIdRange(ThisFunction::num_ids()));
}


void AstNumberingVisitor::VisitSuperReference(SuperReference* node) {
  node->set_base_id(ReserveIdRange(SuperReference::num_ids()));
  Visit(node->this_var());
}


void AstNumberingVisitor::VisitModuleDeclaration(ModuleDeclaration* node) {
  VisitVariableProxy(node->proxy());
  Visit(node->module());
}


void AstNumberingVisitor::VisitImportDeclaration(ImportDeclaration* node) {
  VisitVariableProxy(node->proxy());
  Visit(node->module());
}


void AstNumberingVisitor::VisitModuleVariable(ModuleVariable* node) {
  Visit(node->proxy());
}


void AstNumberingVisitor::VisitModulePath(ModulePath* node) {
  Visit(node->module());
}


void AstNumberingVisitor::VisitModuleStatement(ModuleStatement* node) {
  Visit(node->body());
}


void AstNumberingVisitor::VisitExpressionStatement(ExpressionStatement* node) {
  Visit(node->expression());
}


void AstNumberingVisitor::VisitReturnStatement(ReturnStatement* node) {
  Visit(node->expression());
}


void AstNumberingVisitor::VisitYield(Yield* node) {
  node->set_base_id(ReserveIdRange(Yield::num_ids()));
  Visit(node->generator_object());
  Visit(node->expression());
}


void AstNumberingVisitor::VisitThrow(Throw* node) {
  node->set_base_id(ReserveIdRange(Throw::num_ids()));
  Visit(node->exception());
}


void AstNumberingVisitor::VisitUnaryOperation(UnaryOperation* node) {
  node->set_base_id(ReserveIdRange(UnaryOperation::num_ids()));
  Visit(node->expression());
}


void AstNumberingVisitor::VisitCountOperation(CountOperation* node) {
  node->set_base_id(ReserveIdRange(CountOperation::num_ids()));
  Visit(node->expression());
}


void AstNumberingVisitor::VisitBlock(Block* node) {
  node->set_base_id(ReserveIdRange(Block::num_ids()));
  if (node->scope() != NULL) VisitDeclarations(node->scope()->declarations());
  VisitStatements(node->statements());
}


void AstNumberingVisitor::VisitFunctionDeclaration(FunctionDeclaration* node) {
  VisitVariableProxy(node->proxy());
  VisitFunctionLiteral(node->fun());
}


void AstNumberingVisitor::VisitModuleLiteral(ModuleLiteral* node) {
  VisitBlock(node->body());
}


void AstNumberingVisitor::VisitCallRuntime(CallRuntime* node) {
  node->set_base_id(ReserveIdRange(CallRuntime::num_ids()));
  VisitArguments(node->arguments());
}


void AstNumberingVisitor::VisitWithStatement(WithStatement* node) {
  Visit(node->expression());
  Visit(node->statement());
}


void AstNumberingVisitor::VisitDoWhileStatement(DoWhileStatement* node) {
  node->set_base_id(ReserveIdRange(DoWhileStatement::num_ids()));
  Visit(node->body());
  Visit(node->cond());
}


void AstNumberingVisitor::VisitWhileStatement(WhileStatement* node) {
  node->set_base_id(ReserveIdRange(WhileStatement::num_ids()));
  Visit(node->cond());
  Visit(node->body());
}


void AstNumberingVisitor::VisitTryCatchStatement(TryCatchStatement* node) {
  Visit(node->try_block());
  Visit(node->catch_block());
}


void AstNumberingVisitor::VisitTryFinallyStatement(TryFinallyStatement* node) {
  Visit(node->try_block());
  Visit(node->finally_block());
}


void AstNumberingVisitor::VisitProperty(Property* node) {
  node->set_base_id(ReserveIdRange(Property::num_ids()));
  Visit(node->key());
  Visit(node->obj());
}


void AstNumberingVisitor::VisitAssignment(Assignment* node) {
  node->set_base_id(ReserveIdRange(Assignment::num_ids()));
  if (node->is_compound()) VisitBinaryOperation(node->binary_operation());
  Visit(node->target());
  Visit(node->value());
}


void AstNumberingVisitor::VisitBinaryOperation(BinaryOperation* node) {
  node->set_base_id(ReserveIdRange(BinaryOperation::num_ids()));
  Visit(node->left());
  Visit(node->right());
}


void AstNumberingVisitor::VisitCompareOperation(CompareOperation* node) {
  node->set_base_id(ReserveIdRange(CompareOperation::num_ids()));
  Visit(node->left());
  Visit(node->right());
}


void AstNumberingVisitor::VisitForInStatement(ForInStatement* node) {
  node->set_base_id(ReserveIdRange(ForInStatement::num_ids()));
  Visit(node->each());
  Visit(node->enumerable());
  Visit(node->body());
}


void AstNumberingVisitor::VisitForOfStatement(ForOfStatement* node) {
  node->set_base_id(ReserveIdRange(ForOfStatement::num_ids()));
  Visit(node->assign_iterator());
  Visit(node->next_result());
  Visit(node->result_done());
  Visit(node->assign_each());
  Visit(node->body());
}


void AstNumberingVisitor::VisitConditional(Conditional* node) {
  node->set_base_id(ReserveIdRange(Conditional::num_ids()));
  Visit(node->condition());
  Visit(node->then_expression());
  Visit(node->else_expression());
}


void AstNumberingVisitor::VisitIfStatement(IfStatement* node) {
  node->set_base_id(ReserveIdRange(IfStatement::num_ids()));
  Visit(node->condition());
  Visit(node->then_statement());
  if (node->HasElseStatement()) {
    Visit(node->else_statement());
  }
}


void AstNumberingVisitor::VisitSwitchStatement(SwitchStatement* node) {
  node->set_base_id(ReserveIdRange(SwitchStatement::num_ids()));
  Visit(node->tag());
  ZoneList<CaseClause*>* cases = node->cases();
  for (int i = 0; i < cases->length(); i++) {
    VisitCaseClause(cases->at(i));
  }
}


void AstNumberingVisitor::VisitCaseClause(CaseClause* node) {
  node->set_base_id(ReserveIdRange(CaseClause::num_ids()));
  if (!node->is_default()) Visit(node->label());
  VisitStatements(node->statements());
}


void AstNumberingVisitor::VisitForStatement(ForStatement* node) {
  node->set_base_id(ReserveIdRange(ForStatement::num_ids()));
  if (node->init() != NULL) Visit(node->init());
  if (node->cond() != NULL) Visit(node->cond());
  if (node->next() != NULL) Visit(node->next());
  Visit(node->body());
}


void AstNumberingVisitor::VisitClassLiteral(ClassLiteral* node) {
  node->set_base_id(ReserveIdRange(ClassLiteral::num_ids()));
  if (node->extends()) Visit(node->extends());
  if (node->constructor()) Visit(node->constructor());
  for (int i = 0; i < node->properties()->length(); i++) {
    VisitObjectLiteralProperty(node->properties()->at(i));
  }
}


void AstNumberingVisitor::VisitObjectLiteral(ObjectLiteral* node) {
  node->set_base_id(ReserveIdRange(ObjectLiteral::num_ids()));
  for (int i = 0; i < node->properties()->length(); i++) {
    VisitObjectLiteralProperty(node->properties()->at(i));
  }
}


void AstNumberingVisitor::VisitObjectLiteralProperty(
    ObjectLiteralProperty* node) {
  Visit(node->key());
  Visit(node->value());
}


void AstNumberingVisitor::VisitArrayLiteral(ArrayLiteral* node) {
  node->set_base_id(ReserveIdRange(node->num_ids()));
  for (int i = 0; i < node->values()->length(); i++) {
    Visit(node->values()->at(i));
  }
}


void AstNumberingVisitor::VisitCall(Call* node) {
  node->set_base_id(ReserveIdRange(Call::num_ids()));
  Visit(node->expression());
  VisitArguments(node->arguments());
}


void AstNumberingVisitor::VisitCallNew(CallNew* node) {
  node->set_base_id(ReserveIdRange(CallNew::num_ids()));
  Visit(node->expression());
  VisitArguments(node->arguments());
}


void AstNumberingVisitor::VisitStatements(ZoneList<Statement*>* statements) {
  if (statements == NULL) return;
  for (int i = 0; i < statements->length(); i++) {
    Visit(statements->at(i));
  }
}


void AstNumberingVisitor::VisitDeclarations(
    ZoneList<Declaration*>* declarations) {
  for (int i = 0; i < declarations->length(); i++) {
    Visit(declarations->at(i));
  }
}


void AstNumberingVisitor::VisitArguments(ZoneList<Expression*>* arguments) {
  for (int i = 0; i < arguments->length(); i++) {
    Visit(arguments->at(i));
  }
}


void AstNumberingVisitor::VisitFunctionLiteral(FunctionLiteral* node) {
  node->set_base_id(ReserveIdRange(FunctionLiteral::num_ids()));
  // We don't recurse into the declarations or body of the function literal:
  // you have to separately Renumber() each FunctionLiteral that you compile.
}


void AstNumberingVisitor::Renumber(FunctionLiteral* node) {
  if (node->scope()->HasIllegalRedeclaration()) {
    node->scope()->VisitIllegalRedeclaration(this);
    return;
  }

  Scope* scope = node->scope();
  VisitDeclarations(scope->declarations());
  if (scope->is_function_scope() && scope->function() != NULL) {
    // Visit the name of the named function expression.
    Visit(scope->function());
  }
  VisitStatements(node->body());
}


bool AstNumbering::Renumber(FunctionLiteral* function, Zone* zone) {
  AstNumberingVisitor visitor(zone);
  visitor.Renumber(function);
  return !visitor.HasStackOverflow();
}
}
}  // namespace v8::internal
