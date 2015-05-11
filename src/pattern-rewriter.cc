// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast.h"
#include "src/parser.h"
#include "src/pattern-rewriter.h"

namespace v8 {

namespace internal {


bool Parser::PatternRewriter::IsSingleVariableBinding() const {
  return pattern_->IsVariableProxy();
}


const AstRawString* Parser::PatternRewriter::SingleName() const {
  DCHECK(IsSingleVariableBinding());
  return pattern_->AsVariableProxy()->raw_name();
}


void Parser::PatternRewriter::DeclareAndInitializeVariables(Expression* value,
                                                            int* nvars,
                                                            bool* ok) {
  ok_ = ok;
  nvars_ = nvars;
  RecurseIntoSubpattern(pattern_, value);
  ok_ = nullptr;
  nvars_ = nullptr;
}


void Parser::PatternRewriter::VisitVariableProxy(VariableProxy* pattern) {
  Expression* value = current_value_;
  decl_->scope->RemoveUnresolved(pattern->AsVariableProxy());

  // Declare variable.
  // Note that we *always* must treat the initial value via a separate init
  // assignment for variables and constants because the value must be assigned
  // when the variable is encountered in the source. But the variable/constant
  // is declared (and set to 'undefined') upon entering the function within
  // which the variable or constant is declared. Only function variables have
  // an initial value in the declaration (because they are initialized upon
  // entering the function).
  //
  // If we have a legacy const declaration, in an inner scope, the proxy
  // is always bound to the declared variable (independent of possibly
  // surrounding 'with' statements).
  // For let/const declarations in harmony mode, we can also immediately
  // pre-resolve the proxy because it resides in the same scope as the
  // declaration.
  Parser* parser = decl_->parser;
  const AstRawString* name = pattern->raw_name();
  VariableProxy* proxy = parser->NewUnresolved(name, decl_->mode);
  Declaration* declaration = factory()->NewVariableDeclaration(
      proxy, decl_->mode, decl_->scope, decl_->pos);
  Variable* var = parser->Declare(declaration, decl_->mode != VAR, ok_);
  if (!*ok_) return;
  DCHECK_NOT_NULL(var);
  DCHECK(!proxy->is_resolved() || proxy->var() == var);
  var->set_initializer_position(decl_->initializer_position);
  (*nvars_)++;
  if (decl_->declaration_scope->num_var_or_const() > kMaxNumFunctionLocals) {
    parser->ReportMessage("too_many_variables");
    *ok_ = false;
    return;
  }
  if (decl_->names) {
    decl_->names->Add(name, zone());
  }

  // Initialize variables if needed. A
  // declaration of the form:
  //
  //    var v = x;
  //
  // is syntactic sugar for:
  //
  //    var v; v = x;
  //
  // In particular, we need to re-lookup 'v' (in scope_, not
  // declaration_scope) as it may be a different 'v' than the 'v' in the
  // declaration (e.g., if we are inside a 'with' statement or 'catch'
  // block).
  //
  // However, note that const declarations are different! A const
  // declaration of the form:
  //
  //   const c = x;
  //
  // is *not* syntactic sugar for:
  //
  //   const c; c = x;
  //
  // The "variable" c initialized to x is the same as the declared
  // one - there is no re-lookup (see the last parameter of the
  // Declare() call above).
  Scope* initialization_scope =
      decl_->is_const ? decl_->declaration_scope : decl_->scope;


  // Global variable declarations must be compiled in a specific
  // way. When the script containing the global variable declaration
  // is entered, the global variable must be declared, so that if it
  // doesn't exist (on the global object itself, see ES5 errata) it
  // gets created with an initial undefined value. This is handled
  // by the declarations part of the function representing the
  // top-level global code; see Runtime::DeclareGlobalVariable. If
  // it already exists (in the object or in a prototype), it is
  // *not* touched until the variable declaration statement is
  // executed.
  //
  // Executing the variable declaration statement will always
  // guarantee to give the global object an own property.
  // This way, global variable declarations can shadow
  // properties in the prototype chain, but only after the variable
  // declaration statement has been executed. This is important in
  // browsers where the global object (window) has lots of
  // properties defined in prototype objects.
  if (initialization_scope->is_script_scope() &&
      !IsLexicalVariableMode(decl_->mode)) {
    // Compute the arguments for the runtime
    // call.test-parsing/InitializedDeclarationsInStrictForOfError
    ZoneList<Expression*>* arguments =
        new (zone()) ZoneList<Expression*>(3, zone());
    // We have at least 1 parameter.
    arguments->Add(factory()->NewStringLiteral(name, decl_->pos), zone());
    CallRuntime* initialize;

    if (decl_->is_const) {
      arguments->Add(value, zone());
      value = NULL;  // zap the value to avoid the unnecessary assignment

      // Construct the call to Runtime_InitializeConstGlobal
      // and add it to the initialization statement block.
      // Note that the function does different things depending on
      // the number of arguments (1 or 2).
      initialize = factory()->NewCallRuntime(
          ast_value_factory()->initialize_const_global_string(),
          Runtime::FunctionForId(Runtime::kInitializeConstGlobal), arguments,
          decl_->pos);
    } else {
      // Add language mode.
      // We may want to pass singleton to avoid Literal allocations.
      LanguageMode language_mode = initialization_scope->language_mode();
      arguments->Add(factory()->NewNumberLiteral(language_mode, decl_->pos),
                     zone());

      // Be careful not to assign a value to the global variable if
      // we're in a with. The initialization value should not
      // necessarily be stored in the global object in that case,
      // which is why we need to generate a separate assignment node.
      if (value != NULL && !inside_with()) {
        arguments->Add(value, zone());
        value = NULL;  // zap the value to avoid the unnecessary assignment
        // Construct the call to Runtime_InitializeVarGlobal
        // and add it to the initialization statement block.
        initialize = factory()->NewCallRuntime(
            ast_value_factory()->initialize_var_global_string(),
            Runtime::FunctionForId(Runtime::kInitializeVarGlobal), arguments,
            decl_->pos);
      } else {
        initialize = NULL;
      }
    }

    if (initialize != NULL) {
      decl_->block->AddStatement(
          factory()->NewExpressionStatement(initialize, RelocInfo::kNoPosition),
          zone());
    }
  } else if (decl_->needs_init) {
    // Constant initializations always assign to the declared constant which
    // is always at the function scope level. This is only relevant for
    // dynamically looked-up variables and constants (the
    // start context for constant lookups is always the function context,
    // while it is the top context for var declared variables). Sigh...
    // For 'let' and 'const' declared variables in harmony mode the
    // initialization also always assigns to the declared variable.
    DCHECK_NOT_NULL(proxy);
    DCHECK_NOT_NULL(proxy->var());
    DCHECK_NOT_NULL(value);
    Assignment* assignment =
        factory()->NewAssignment(decl_->init_op, proxy, value, decl_->pos);
    decl_->block->AddStatement(
        factory()->NewExpressionStatement(assignment, RelocInfo::kNoPosition),
        zone());
    value = NULL;
  }

  // Add an assignment node to the initialization statement block if we still
  // have a pending initialization value.
  if (value != NULL) {
    DCHECK(decl_->mode == VAR);
    // 'var' initializations are simply assignments (with all the consequences
    // if they are inside a 'with' statement - they may change a 'with' object
    // property).
    VariableProxy* proxy = initialization_scope->NewUnresolved(factory(), name);
    Assignment* assignment =
        factory()->NewAssignment(decl_->init_op, proxy, value, decl_->pos);
    decl_->block->AddStatement(
        factory()->NewExpressionStatement(assignment, RelocInfo::kNoPosition),
        zone());
  }
}


void Parser::PatternRewriter::VisitObjectLiteral(ObjectLiteral* pattern) {
  auto temp = decl_->declaration_scope->NewTemporary(
      ast_value_factory()->empty_string());
  auto assignment =
      factory()->NewAssignment(Token::ASSIGN, factory()->NewVariableProxy(temp),
                               current_value_, RelocInfo::kNoPosition);
  decl_->block->AddStatement(
      factory()->NewExpressionStatement(assignment, RelocInfo::kNoPosition),
      zone());
  for (ObjectLiteralProperty* property : *pattern->properties()) {
    // TODO(dslomov): computed property names.
    RecurseIntoSubpattern(
        property->value(),
        factory()->NewProperty(factory()->NewVariableProxy(temp),
                               property->key(), RelocInfo::kNoPosition));
  }
}


void Parser::PatternRewriter::VisitArrayLiteral(ArrayLiteral* node) {
  // TODO(dslomov): implement.
}


void Parser::PatternRewriter::VisitAssignment(Assignment* node) {
  // TODO(dslomov): implement.
}


void Parser::PatternRewriter::VisitSpread(Spread* node) {
  // TODO(dslomov): implement.
}


// =============== UNREACHABLE =============================

void Parser::PatternRewriter::Visit(AstNode* node) { UNREACHABLE(); }

#define NOT_A_PATTERN(Node)                                        \
  void Parser::PatternRewriter::Visit##Node(v8::internal::Node*) { \
    UNREACHABLE();                                                 \
  }

NOT_A_PATTERN(BinaryOperation)
NOT_A_PATTERN(Block)
NOT_A_PATTERN(BreakStatement)
NOT_A_PATTERN(Call)
NOT_A_PATTERN(CallNew)
NOT_A_PATTERN(CallRuntime)
NOT_A_PATTERN(CaseClause)
NOT_A_PATTERN(ClassLiteral)
NOT_A_PATTERN(CompareOperation)
NOT_A_PATTERN(Conditional)
NOT_A_PATTERN(ContinueStatement)
NOT_A_PATTERN(CountOperation)
NOT_A_PATTERN(DebuggerStatement)
NOT_A_PATTERN(DoWhileStatement)
NOT_A_PATTERN(EmptyStatement)
NOT_A_PATTERN(ExportDeclaration)
NOT_A_PATTERN(ExpressionStatement)
NOT_A_PATTERN(ForInStatement)
NOT_A_PATTERN(ForOfStatement)
NOT_A_PATTERN(ForStatement)
NOT_A_PATTERN(FunctionDeclaration)
NOT_A_PATTERN(FunctionLiteral)
NOT_A_PATTERN(IfStatement)
NOT_A_PATTERN(ImportDeclaration)
NOT_A_PATTERN(Literal)
NOT_A_PATTERN(NativeFunctionLiteral)
NOT_A_PATTERN(Property)
NOT_A_PATTERN(RegExpLiteral)
NOT_A_PATTERN(ReturnStatement)
NOT_A_PATTERN(SuperReference)
NOT_A_PATTERN(SwitchStatement)
NOT_A_PATTERN(ThisFunction)
NOT_A_PATTERN(Throw)
NOT_A_PATTERN(TryCatchStatement)
NOT_A_PATTERN(TryFinallyStatement)
NOT_A_PATTERN(UnaryOperation)
NOT_A_PATTERN(VariableDeclaration)
NOT_A_PATTERN(WhileStatement)
NOT_A_PATTERN(WithStatement)
NOT_A_PATTERN(Yield)

#undef NOT_A_PATTERN
}
}  // namespace v8::internal
