// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/ast-numbering.h"

#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/compiler.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

class AstNumberingVisitor final : public AstVisitor<AstNumberingVisitor> {
 public:
  AstNumberingVisitor(uintptr_t stack_limit, Zone* zone) : zone_(zone) {
    InitializeAstVisitor(stack_limit);
  }

  bool Renumber(FunctionLiteral* node);

 private:
// AST node visitor interface.
#define DEFINE_VISIT(type) void Visit##type(type* node);
  AST_NODE_LIST(DEFINE_VISIT)
#undef DEFINE_VISIT

  void VisitSuspend(Suspend* node);

  void VisitStatementsAndDeclarations(Block* node);
  void VisitStatements(ZoneList<Statement*>* statements);
  void VisitDeclarations(Declaration::List* declarations);
  void VisitArguments(ZoneList<Expression*>* arguments);
  void VisitLiteralProperty(LiteralProperty* property);

  Zone* zone() const { return zone_; }

  Zone* zone_;
  FunctionKind function_kind_;

  DEFINE_AST_VISITOR_SUBCLASS_MEMBERS();
  DISALLOW_COPY_AND_ASSIGN(AstNumberingVisitor);
};

void AstNumberingVisitor::VisitVariableDeclaration(VariableDeclaration* node) {
  VisitVariableProxy(node->proxy());
}

void AstNumberingVisitor::VisitEmptyStatement(EmptyStatement* node) {
}

void AstNumberingVisitor::VisitSloppyBlockFunctionStatement(
    SloppyBlockFunctionStatement* node) {
  Visit(node->statement());
}

void AstNumberingVisitor::VisitContinueStatement(ContinueStatement* node) {
}

void AstNumberingVisitor::VisitBreakStatement(BreakStatement* node) {
}

void AstNumberingVisitor::VisitDebuggerStatement(DebuggerStatement* node) {
}

void AstNumberingVisitor::VisitNativeFunctionLiteral(
    NativeFunctionLiteral* node) {
}

void AstNumberingVisitor::VisitDoExpression(DoExpression* node) {
  Visit(node->block());
  Visit(node->result());
}

void AstNumberingVisitor::VisitLiteral(Literal* node) {
}

void AstNumberingVisitor::VisitRegExpLiteral(RegExpLiteral* node) {
}

void AstNumberingVisitor::VisitVariableProxy(VariableProxy* node) {
}

void AstNumberingVisitor::VisitThisFunction(ThisFunction* node) {
}

void AstNumberingVisitor::VisitSuperPropertyReference(
    SuperPropertyReference* node) {
  Visit(node->this_var());
  Visit(node->home_object());
}

void AstNumberingVisitor::VisitSuperCallReference(SuperCallReference* node) {
  Visit(node->this_var());
  Visit(node->new_target_var());
  Visit(node->this_function_var());
}

void AstNumberingVisitor::VisitExpressionStatement(ExpressionStatement* node) {
  Visit(node->expression());
}

void AstNumberingVisitor::VisitReturnStatement(ReturnStatement* node) {
  Visit(node->expression());
}

void AstNumberingVisitor::VisitSuspend(Suspend* node) {
  Visit(node->expression());
}

void AstNumberingVisitor::VisitYield(Yield* node) { VisitSuspend(node); }

void AstNumberingVisitor::VisitYieldStar(YieldStar* node) {
  Visit(node->expression());
}

void AstNumberingVisitor::VisitAwait(Await* node) { VisitSuspend(node); }

void AstNumberingVisitor::VisitThrow(Throw* node) {
  Visit(node->exception());
}

void AstNumberingVisitor::VisitUnaryOperation(UnaryOperation* node) {
  Visit(node->expression());
}

void AstNumberingVisitor::VisitCountOperation(CountOperation* node) {
  Visit(node->expression());
}

void AstNumberingVisitor::VisitBlock(Block* node) {
  VisitStatementsAndDeclarations(node);
}

void AstNumberingVisitor::VisitStatementsAndDeclarations(Block* node) {
  Scope* scope = node->scope();
  DCHECK(scope == nullptr || !scope->HasBeenRemoved());
  if (scope) VisitDeclarations(scope->declarations());
  VisitStatements(node->statements());
}

void AstNumberingVisitor::VisitFunctionDeclaration(FunctionDeclaration* node) {
  VisitVariableProxy(node->proxy());
  VisitFunctionLiteral(node->fun());
}

void AstNumberingVisitor::VisitCallRuntime(CallRuntime* node) {
  VisitArguments(node->arguments());
}

void AstNumberingVisitor::VisitWithStatement(WithStatement* node) {
  Visit(node->expression());
  Visit(node->statement());
}

void AstNumberingVisitor::VisitDoWhileStatement(DoWhileStatement* node) {
  Visit(node->body());
  Visit(node->cond());
}

void AstNumberingVisitor::VisitWhileStatement(WhileStatement* node) {
  Visit(node->cond());
  Visit(node->body());
}

void AstNumberingVisitor::VisitTryCatchStatement(TryCatchStatement* node) {
  DCHECK(node->scope() == nullptr || !node->scope()->HasBeenRemoved());
  Visit(node->try_block());
  Visit(node->catch_block());
}

void AstNumberingVisitor::VisitTryFinallyStatement(TryFinallyStatement* node) {
  Visit(node->try_block());
  Visit(node->finally_block());
}

void AstNumberingVisitor::VisitProperty(Property* node) {
  Visit(node->key());
  Visit(node->obj());
}

void AstNumberingVisitor::VisitResolvedProperty(ResolvedProperty* node) {
  Visit(node->object());
  Visit(node->property());
}

void AstNumberingVisitor::VisitAssignment(Assignment* node) {
  Visit(node->target());
  Visit(node->value());
}

void AstNumberingVisitor::VisitCompoundAssignment(CompoundAssignment* node) {
  VisitBinaryOperation(node->binary_operation());
  // We don't need to recurse down the assignment version since we already did
  // the binop.
  DCHECK_EQ(node->target(), node->binary_operation()->left());
  DCHECK_EQ(node->value(), node->binary_operation()->right());
}

void AstNumberingVisitor::VisitBinaryOperation(BinaryOperation* node) {
  Visit(node->left());
  Visit(node->right());
}

void AstNumberingVisitor::VisitNaryOperation(NaryOperation* node) {
  Visit(node->first());
  for (size_t i = 0; i < node->subsequent_length(); ++i) {
    Visit(node->subsequent(i));
  }
}

void AstNumberingVisitor::VisitCompareOperation(CompareOperation* node) {
  Visit(node->left());
  Visit(node->right());
}

void AstNumberingVisitor::VisitSpread(Spread* node) {
  Visit(node->expression());
}

void AstNumberingVisitor::VisitEmptyParentheses(EmptyParentheses* node) {
  UNREACHABLE();
}

void AstNumberingVisitor::VisitGetIterator(GetIterator* node) {
  Visit(node->iterable());
}

void AstNumberingVisitor::VisitGetTemplateObject(GetTemplateObject* node) {}

void AstNumberingVisitor::VisitImportCallExpression(
    ImportCallExpression* node) {
  Visit(node->argument());
}

void AstNumberingVisitor::VisitForInStatement(ForInStatement* node) {
  Visit(node->enumerable());  // Not part of loop.
  Visit(node->each());
  Visit(node->body());
}

void AstNumberingVisitor::VisitForOfStatement(ForOfStatement* node) {
  Visit(node->assign_iterator());  // Not part of loop.
  Visit(node->assign_next());
  Visit(node->next_result());
  Visit(node->result_done());
  Visit(node->assign_each());
  Visit(node->body());
}

void AstNumberingVisitor::VisitConditional(Conditional* node) {
  Visit(node->condition());
  Visit(node->then_expression());
  Visit(node->else_expression());
}

void AstNumberingVisitor::VisitIfStatement(IfStatement* node) {
  Visit(node->condition());
  if (!node->condition()->ToBooleanIsFalse()) {
    Visit(node->then_statement());
  }
  if (node->HasElseStatement() && !node->condition()->ToBooleanIsTrue()) {
    Visit(node->else_statement());
  }
}

void AstNumberingVisitor::VisitSwitchStatement(SwitchStatement* node) {
  Visit(node->tag());
  for (CaseClause* clause : *node->cases()) {
    if (!clause->is_default()) Visit(clause->label());
    VisitStatements(clause->statements());
  }
}

void AstNumberingVisitor::VisitForStatement(ForStatement* node) {
  if (node->init() != nullptr) Visit(node->init());  // Not part of loop.
  if (node->cond() != nullptr) Visit(node->cond());
  if (node->next() != nullptr) Visit(node->next());
  Visit(node->body());
}

void AstNumberingVisitor::VisitClassLiteral(ClassLiteral* node) {
  if (node->extends()) Visit(node->extends());
  if (node->constructor()) Visit(node->constructor());
  if (node->static_fields_initializer() != nullptr) {
    Visit(node->static_fields_initializer());
  }
  if (node->instance_fields_initializer_function() != nullptr) {
    Visit(node->instance_fields_initializer_function());
  }
  for (int i = 0; i < node->properties()->length(); i++) {
    VisitLiteralProperty(node->properties()->at(i));
  }
}

void AstNumberingVisitor::VisitInitializeClassFieldsStatement(
    InitializeClassFieldsStatement* node) {
  for (int i = 0; i < node->fields()->length(); i++) {
    VisitLiteralProperty(node->fields()->at(i));
  }
}

void AstNumberingVisitor::VisitObjectLiteral(ObjectLiteral* node) {
  for (int i = 0; i < node->properties()->length(); i++) {
    VisitLiteralProperty(node->properties()->at(i));
  }
}

void AstNumberingVisitor::VisitLiteralProperty(LiteralProperty* node) {
  Visit(node->key());
  Visit(node->value());
}

void AstNumberingVisitor::VisitArrayLiteral(ArrayLiteral* node) {
  for (int i = 0; i < node->values()->length(); i++) {
    Visit(node->values()->at(i));
  }
}

void AstNumberingVisitor::VisitCall(Call* node) {
  Visit(node->expression());
  VisitArguments(node->arguments());
}

void AstNumberingVisitor::VisitCallNew(CallNew* node) {
  Visit(node->expression());
  VisitArguments(node->arguments());
}

void AstNumberingVisitor::VisitStatements(ZoneList<Statement*>* statements) {
  if (statements == nullptr) return;
  for (int i = 0; i < statements->length(); i++) {
    Visit(statements->at(i));
    if (statements->at(i)->IsJump()) break;
  }
}

void AstNumberingVisitor::VisitDeclarations(Declaration::List* decls) {
  for (Declaration* decl : *decls) Visit(decl);
}

void AstNumberingVisitor::VisitArguments(ZoneList<Expression*>* arguments) {
  for (int i = 0; i < arguments->length(); i++) {
    Visit(arguments->at(i));
  }
}

void AstNumberingVisitor::VisitFunctionLiteral(FunctionLiteral* node) {
  if (node->ShouldEagerCompile()) {
    // If the function literal is being eagerly compiled, recurse into the
    // declarations and body of the function literal.
    if (!AstNumbering::Renumber(stack_limit_, zone_, node)) {
      SetStackOverflow();
      return;
    }
  }
}

void AstNumberingVisitor::VisitRewritableExpression(
    RewritableExpression* node) {
  Visit(node->expression());
}

bool AstNumberingVisitor::Renumber(FunctionLiteral* node) {
  DeclarationScope* scope = node->scope();
  DCHECK(!scope->HasBeenRemoved());
  function_kind_ = node->kind();

  VisitDeclarations(scope->declarations());
  VisitStatements(node->body());

  return !HasStackOverflow();
}

bool AstNumbering::Renumber(uintptr_t stack_limit, Zone* zone,
                            FunctionLiteral* function) {
  DisallowHeapAllocation no_allocation;
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;

  AstNumberingVisitor visitor(stack_limit, zone);
  return visitor.Renumber(function);
}
}  // namespace internal
}  // namespace v8
