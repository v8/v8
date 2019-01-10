// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/ast.h"
#include "src/message-template.h"
#include "src/objects-inl.h"
#include "src/parsing/expression-scope-reparenter.h"
#include "src/parsing/parser.h"

namespace v8 {

namespace internal {

// An AST visitor which performs declaration and assignment related tasks,
// particularly for destructuring patterns:
//
//   1. Declares variables from variable proxies (particularly for destructuring
//      declarations),
//   2. Marks destructuring-assigned variable proxies as assigned, and
//   3. Rewrites scopes for parameters containing a sloppy eval.
//
// Historically this also rewrote destructuring assignments/declarations as a
// block of multiple assignments, hence the named, however this is now done
// during bytecode generation.
//
// TODO(leszeks): Rename or remove this class
class PatternRewriter final : public AstVisitor<PatternRewriter> {
 public:
  // Limit the allowed number of local variables in a function. The hard limit
  // is that offsets computed by FullCodeGenerator::StackOperand and similar
  // functions are ints, and they should not overflow. In addition, accessing
  // local variables creates user-controlled constants in the generated code,
  // and we don't want too much user-controlled memory inside the code (this was
  // the reason why this limit was introduced in the first place; see
  // https://codereview.chromium.org/7003030/ ).
  static const int kMaxNumFunctionLocals = 4194303;  // 2^22-1

  typedef Parser::DeclarationDescriptor DeclarationDescriptor;

  static void InitializeVariables(
      Parser* parser, Block* block,
      const DeclarationDescriptor* declaration_descriptor,
      const Parser::DeclarationParsingResult::Declaration* declaration,
      ZonePtrList<const AstRawString>* names);

 private:
  PatternRewriter(Parser* parser, const DeclarationDescriptor* descriptor,
                  ZonePtrList<const AstRawString>* names, bool has_initializer,
                  int initializer_position = kNoSourcePosition,
                  bool declares_parameter_containing_sloppy_eval = false)
      : parser_(parser),
        descriptor_(descriptor),
        names_(names),
        initializer_position_(initializer_position),
        has_initializer_(has_initializer),
        declares_parameter_containing_sloppy_eval_(
            declares_parameter_containing_sloppy_eval) {}

#define DECLARE_VISIT(type) void Visit##type(v8::internal::type* node);
  // Visiting functions for AST nodes make this an AstVisitor.
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

  void RecurseIntoSubpattern(AstNode* pattern) { Visit(pattern); }

  Expression* Visit(Assignment* assign) {
    if (parser_->has_error()) return parser_->FailureExpression();
    DCHECK_EQ(Token::ASSIGN, assign->op());

    Expression* pattern = assign->target();
    if (pattern->IsObjectLiteral()) {
      VisitObjectLiteral(pattern->AsObjectLiteral());
    } else {
      DCHECK(pattern->IsArrayLiteral());
      VisitArrayLiteral(pattern->AsArrayLiteral());
    }
    return assign;
  }

  void RewriteParameterScopes(Expression* expr);

  AstNodeFactory* factory() const { return parser_->factory(); }
  AstValueFactory* ast_value_factory() const {
    return parser_->ast_value_factory();
  }

  std::vector<void*>* pointer_buffer() { return parser_->pointer_buffer(); }

  Zone* zone() const { return parser_->zone(); }
  Scope* scope() const { return parser_->scope(); }

  Parser* const parser_;
  const DeclarationDescriptor* descriptor_;
  ZonePtrList<const AstRawString>* names_;
  const int initializer_position_;
  const bool has_initializer_;
  const bool declares_parameter_containing_sloppy_eval_;

  DEFINE_AST_VISITOR_MEMBERS_WITHOUT_STACKOVERFLOW()
};

void Parser::InitializeVariables(
    Block* block, const DeclarationDescriptor* declaration_descriptor,
    const DeclarationParsingResult::Declaration* declaration,
    ZonePtrList<const AstRawString>* names) {
  if (has_error()) return;
  PatternRewriter::InitializeVariables(this, block, declaration_descriptor,
                                       declaration, names);

  if (declaration->initializer) {
    int pos = declaration->value_beg_position;
    if (pos == kNoSourcePosition) {
      pos = declaration->initializer_position;
    }
    Assignment* assignment = factory()->NewAssignment(
        Token::INIT, declaration->pattern, declaration->initializer, pos);
    block->statements()->Add(factory()->NewExpressionStatement(assignment, pos),
                             zone());
  }
}

void PatternRewriter::InitializeVariables(
    Parser* parser, Block* block,
    const DeclarationDescriptor* declaration_descriptor,
    const Parser::DeclarationParsingResult::Declaration* declaration,
    ZonePtrList<const AstRawString>* names) {
  DCHECK(block->ignore_completion_value());

  PatternRewriter rewriter(parser, declaration_descriptor, names,
                           declaration->initializer != nullptr,
                           declaration->initializer_position,
                           declaration_descriptor->kind == PARAMETER_VARIABLE &&
                               parser->scope()->is_block_scope());

  rewriter.RecurseIntoSubpattern(declaration->pattern);
}

void PatternRewriter::VisitVariableProxy(VariableProxy* proxy) {
  DCHECK_NOT_NULL(descriptor_);

  Scope* target_scope = scope();
  if (declares_parameter_containing_sloppy_eval_) {
    // When an extra declaration scope needs to be inserted to account for
    // a sloppy eval in a default parameter or function body, the parameter
    // needs to be declared in the function's scope, not in the varblock
    // scope which will be used for the initializer expression.
    DCHECK_EQ(descriptor_->mode, VariableMode::kLet);
    target_scope = target_scope->outer_scope();
  }
  Scope* var_init_scope = scope();

#ifdef DEBUG
  // Calculate the scope we expect the variable to be declared in, for DCHECKs.
  Scope* expected_declaration_scope =
      declares_parameter_containing_sloppy_eval_
          ? scope()->outer_scope()
          : (IsLexicalVariableMode(descriptor_->mode)
                 ? scope()
                 : scope()->GetDeclarationScope());
#endif

  // Declare variable.
  // Note that we *always* must treat the initial value via a separate init
  // assignment for variables and constants because the value must be assigned
  // when the variable is encountered in the source. But the variable/constant
  // is declared (and set to 'undefined') upon entering the function within
  // which the variable or constant is declared. Only function variables have
  // an initial value in the declaration (because they are initialized upon
  // entering the function).

  // A declaration of the form:
  //
  //    var v = x;
  //
  // is syntactic sugar for:
  //
  //    var v; v = x;
  //
  // In particular, we need to re-lookup 'v' if it may be a different 'v' than
  // the 'v' in the declaration (e.g., if we are inside a 'with' statement or
  // 'catch' block).
  //
  // For 'let' and 'const' declared variables the initialization always assigns
  // to the declared variable. But for var initializations that are declared in
  // a different scope we need to do a new lookup, so clone the variable for the
  // declaration and don't consider the original variable resolved.
  if (has_initializer_ && descriptor_->mode == VariableMode::kVar &&
      !var_init_scope->is_declaration_scope()) {
    DCHECK_EQ(target_scope->GetDeclarationScope(), expected_declaration_scope);
    // The cloned variable is not added to the unresolved list of the target
    // scope, as it is about to be resolved by the declaration. The original
    // variable will be left unresolved for now.
    var_init_scope->AddUnresolved(proxy);
    proxy = factory()->NewVariableProxy(proxy->raw_name(), NORMAL_VARIABLE,
                                        proxy->position());
  }

  parser_->DeclareVariable(
      proxy, descriptor_->kind, descriptor_->mode,
      Variable::DefaultInitializationFlag(descriptor_->mode), target_scope,
      descriptor_->declaration_pos);

  if (parser_->has_error()) return;
  Variable* var = proxy->var();
  DCHECK_NOT_NULL(var);
  DCHECK(proxy->is_resolved());
  DCHECK_EQ(var->scope(), expected_declaration_scope);
  DCHECK_NE(initializer_position_, kNoSourcePosition);
  var->set_initializer_position(initializer_position_);

  if (var->scope()->num_var() > kMaxNumFunctionLocals) {
    parser_->ReportMessage(MessageTemplate::kTooManyVariables);
    return;
  }
  if (names_) {
    names_->Add(proxy->raw_name(), zone());
  }

  // If there's no initializer, we're done.
  if (!has_initializer_) return;

  parser_->MarkLoopVariableAsAssigned(proxy->var());
}

// When an extra declaration scope needs to be inserted to account for
// a sloppy eval in a default parameter or function body, the expressions
// needs to be in that new inner scope which was added after initial
// parsing.
void PatternRewriter::RewriteParameterScopes(Expression* expr) {
  if (declares_parameter_containing_sloppy_eval_) {
    ReparentExpressionScope(parser_->stack_limit(), expr, scope());
  }
}

void PatternRewriter::VisitObjectLiteral(ObjectLiteral* pattern) {
  for (ObjectLiteralProperty* property : *pattern->properties()) {
    Expression* key = property->key();
    if (!key->IsLiteral()) {
      // Computed property names contain expressions which might require
      // scope rewriting.
      RewriteParameterScopes(key);
    }
    RecurseIntoSubpattern(property->value());
  }
}

void PatternRewriter::VisitArrayLiteral(ArrayLiteral* node) {
  for (Expression* value : *node->values()) {
    if (value->IsTheHoleLiteral()) continue;
    RecurseIntoSubpattern(value);
  }
}

void PatternRewriter::VisitAssignment(Assignment* node) {
  DCHECK_EQ(Token::ASSIGN, node->op());

  // Initializer may have been parsed in the wrong scope.
  RewriteParameterScopes(node->value());

  RecurseIntoSubpattern(node->target());
}

void PatternRewriter::VisitSpread(Spread* node) {
  RecurseIntoSubpattern(node->expression());
}

// =============== UNREACHABLE =============================

#define NOT_A_PATTERN(Node) \
  void PatternRewriter::Visit##Node(v8::internal::Node*) { UNREACHABLE(); }

NOT_A_PATTERN(BinaryOperation)
NOT_A_PATTERN(NaryOperation)
NOT_A_PATTERN(Block)
NOT_A_PATTERN(BreakStatement)
NOT_A_PATTERN(Call)
NOT_A_PATTERN(CallNew)
NOT_A_PATTERN(CallRuntime)
NOT_A_PATTERN(ClassLiteral)
NOT_A_PATTERN(CompareOperation)
NOT_A_PATTERN(CompoundAssignment)
NOT_A_PATTERN(Conditional)
NOT_A_PATTERN(ContinueStatement)
NOT_A_PATTERN(CountOperation)
NOT_A_PATTERN(DebuggerStatement)
NOT_A_PATTERN(DoExpression)
NOT_A_PATTERN(DoWhileStatement)
NOT_A_PATTERN(EmptyStatement)
NOT_A_PATTERN(EmptyParentheses)
NOT_A_PATTERN(ExpressionStatement)
NOT_A_PATTERN(ForInStatement)
NOT_A_PATTERN(ForOfStatement)
NOT_A_PATTERN(ForStatement)
NOT_A_PATTERN(FunctionDeclaration)
NOT_A_PATTERN(FunctionLiteral)
NOT_A_PATTERN(GetIterator)
NOT_A_PATTERN(GetTemplateObject)
NOT_A_PATTERN(IfStatement)
NOT_A_PATTERN(ImportCallExpression)
NOT_A_PATTERN(Literal)
NOT_A_PATTERN(NativeFunctionLiteral)
NOT_A_PATTERN(Property)
NOT_A_PATTERN(RegExpLiteral)
NOT_A_PATTERN(ResolvedProperty)
NOT_A_PATTERN(ReturnStatement)
NOT_A_PATTERN(SloppyBlockFunctionStatement)
NOT_A_PATTERN(StoreInArrayLiteral)
NOT_A_PATTERN(SuperPropertyReference)
NOT_A_PATTERN(SuperCallReference)
NOT_A_PATTERN(SwitchStatement)
NOT_A_PATTERN(TemplateLiteral)
NOT_A_PATTERN(ThisFunction)
NOT_A_PATTERN(Throw)
NOT_A_PATTERN(TryCatchStatement)
NOT_A_PATTERN(TryFinallyStatement)
NOT_A_PATTERN(UnaryOperation)
NOT_A_PATTERN(VariableDeclaration)
NOT_A_PATTERN(WhileStatement)
NOT_A_PATTERN(WithStatement)
NOT_A_PATTERN(Yield)
NOT_A_PATTERN(YieldStar)
NOT_A_PATTERN(Await)
NOT_A_PATTERN(InitializeClassMembersStatement)

#undef NOT_A_PATTERN
}  // namespace internal
}  // namespace v8
