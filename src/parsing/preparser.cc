// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#include "src/allocation.h"
#include "src/base/logging.h"
#include "src/conversions-inl.h"
#include "src/conversions.h"
#include "src/globals.h"
#include "src/list.h"
#include "src/parsing/duplicate-finder.h"
#include "src/parsing/parser-base.h"
#include "src/parsing/preparse-data-format.h"
#include "src/parsing/preparse-data.h"
#include "src/parsing/preparser.h"
#include "src/unicode.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// The CHECK_OK macro is a convenient macro to enforce error
// handling for functions that may fail (by returning !*ok).
//
// CAUTION: This macro appends extra statements after a call,
// thus it must never be used where only a single statement
// is correct (e.g. an if statement branch w/o braces)!

#define CHECK_OK_VALUE(x) ok); \
  if (!*ok) return x;          \
  ((void)0
#define DUMMY )  // to make indentation work
#undef DUMMY

#define CHECK_OK CHECK_OK_VALUE(Statement::Default())
#define CHECK_OK_VOID CHECK_OK_VALUE(this->Void())

PreParserIdentifier PreParser::GetSymbol() const {
  switch (scanner()->current_token()) {
    case Token::ENUM:
      return PreParserIdentifier::Enum();
    case Token::AWAIT:
      return PreParserIdentifier::Await();
    case Token::FUTURE_STRICT_RESERVED_WORD:
      return PreParserIdentifier::FutureStrictReserved();
    case Token::LET:
      return PreParserIdentifier::Let();
    case Token::STATIC:
      return PreParserIdentifier::Static();
    case Token::YIELD:
      return PreParserIdentifier::Yield();
    case Token::ASYNC:
      return PreParserIdentifier::Async();
    default:
      if (scanner()->UnescapedLiteralMatches("eval", 4))
        return PreParserIdentifier::Eval();
      if (scanner()->UnescapedLiteralMatches("arguments", 9))
        return PreParserIdentifier::Arguments();
      if (scanner()->UnescapedLiteralMatches("undefined", 9))
        return PreParserIdentifier::Undefined();
      if (scanner()->LiteralMatches("prototype", 9))
        return PreParserIdentifier::Prototype();
      if (scanner()->LiteralMatches("constructor", 11))
        return PreParserIdentifier::Constructor();
      return PreParserIdentifier::Default();
  }
}

PreParser::PreParseResult PreParser::PreParseLazyFunction(
    LanguageMode language_mode, FunctionKind kind, bool has_simple_parameters,
    bool parsing_module, ParserRecorder* log, bool may_abort, int* use_counts) {
  parsing_module_ = parsing_module;
  log_ = log;
  use_counts_ = use_counts;
  // Lazy functions always have trivial outer scopes (no with/catch scopes).
  DCHECK_NULL(scope_state_);
  DeclarationScope* top_scope = NewScriptScope();
  FunctionState top_state(&function_state_, &scope_state_, top_scope,
                          kNormalFunction);
  scope()->SetLanguageMode(language_mode);
  DeclarationScope* function_scope = NewFunctionScope(kind);
  if (!has_simple_parameters) function_scope->SetHasNonSimpleParameters();
  FunctionState function_state(&function_state_, &scope_state_, function_scope,
                               kind);
  DCHECK_EQ(Token::LBRACE, scanner()->current_token());
  bool ok = true;
  int start_position = peek_position();
  LazyParsingResult result = ParseLazyFunctionLiteralBody(may_abort, &ok);
  use_counts_ = nullptr;
  if (result == kLazyParsingAborted) {
    return kPreParseAbort;
  } else if (stack_overflow()) {
    return kPreParseStackOverflow;
  } else if (!ok) {
    ReportUnexpectedToken(scanner()->current_token());
  } else {
    DCHECK_EQ(Token::RBRACE, scanner()->peek());
    if (is_strict(scope()->language_mode())) {
      int end_pos = scanner()->location().end_pos;
      CheckStrictOctalLiteral(start_position, end_pos, &ok);
      CheckDecimalLiteralWithLeadingZero(start_position, end_pos);
    }
  }
  return kPreParseSuccess;
}


// Preparsing checks a JavaScript program and emits preparse-data that helps
// a later parsing to be faster.
// See preparser-data.h for the data.

// The PreParser checks that the syntax follows the grammar for JavaScript,
// and collects some information about the program along the way.
// The grammar check is only performed in order to understand the program
// sufficiently to deduce some information about it, that can be used
// to speed up later parsing. Finding errors is not the goal of pre-parsing,
// rather it is to speed up properly written and correct programs.
// That means that contextual checks (like a label being declared where
// it is used) are generally omitted.

PreParser::Statement PreParser::ParseAsyncFunctionDeclaration(
    ZoneList<const AstRawString*>* names, bool default_export, bool* ok) {
  // AsyncFunctionDeclaration ::
  //   async [no LineTerminator here] function BindingIdentifier[Await]
  //       ( FormalParameters[Await] ) { AsyncFunctionBody }
  DCHECK_EQ(scanner()->current_token(), Token::ASYNC);
  int pos = position();
  Expect(Token::FUNCTION, CHECK_OK);
  ParseFunctionFlags flags = ParseFunctionFlags::kIsAsync;
  return ParseHoistableDeclaration(pos, flags, names, default_export, ok);
}

PreParser::Statement PreParser::ParseClassDeclaration(
    ZoneList<const AstRawString*>* names, bool default_export, bool* ok) {
  int pos = position();
  bool is_strict_reserved = false;
  Identifier name =
      ParseIdentifierOrStrictReservedWord(&is_strict_reserved, CHECK_OK);
  ExpressionClassifier no_classifier(this);
  ParseClassLiteral(name, scanner()->location(), is_strict_reserved, pos,
                    CHECK_OK);
  return Statement::Default();
}

PreParser::Statement PreParser::ParseFunctionDeclaration(bool* ok) {
  Consume(Token::FUNCTION);
  int pos = position();
  ParseFunctionFlags flags = ParseFunctionFlags::kIsNormal;
  if (Check(Token::MUL)) {
    flags |= ParseFunctionFlags::kIsGenerator;
    if (allow_harmony_restrictive_declarations()) {
      ReportMessageAt(scanner()->location(),
                      MessageTemplate::kGeneratorInLegacyContext);
      *ok = false;
      return Statement::Default();
    }
  }
  // PreParser is not able to parse "export default" yet (since PreParser is
  // at the moment only used for functions, and it cannot occur
  // there). TODO(marja): update this when it is.
  return ParseHoistableDeclaration(pos, flags, nullptr, false, ok);
}

PreParser::Statement PreParser::ParseSwitchStatement(
    ZoneList<const AstRawString*>* labels, bool* ok) {
  // SwitchStatement ::
  //   'switch' '(' Expression ')' '{' CaseClause* '}'

  Expect(Token::SWITCH, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);

  {
    BlockState cases_block_state(&scope_state_);
    Expect(Token::LBRACE, CHECK_OK);
    Token::Value token = peek();
    while (token != Token::RBRACE) {
      if (token == Token::CASE) {
        Expect(Token::CASE, CHECK_OK);
        ParseExpression(true, CHECK_OK);
      } else {
        Expect(Token::DEFAULT, CHECK_OK);
      }
      Expect(Token::COLON, CHECK_OK);
      token = peek();
      Statement statement = Statement::Jump();
      while (token != Token::CASE &&
             token != Token::DEFAULT &&
             token != Token::RBRACE) {
        statement = ParseStatementListItem(CHECK_OK);
        token = peek();
      }
    }
  }
  Expect(Token::RBRACE, ok);
  return Statement::Default();
}

PreParser::Statement PreParser::ParseForStatement(
    ZoneList<const AstRawString*>* labels, bool* ok) {
  // ForStatement ::
  //   'for' '(' Expression? ';' Expression? ';' Expression? ')' Statement

  // Create an in-between scope for let-bound iteration variables.
  bool has_lexical = false;

  BlockState block_state(&scope_state_);
  Expect(Token::FOR, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  if (peek() != Token::SEMICOLON) {
    ForEachStatement::VisitMode mode;
    if (peek() == Token::VAR || peek() == Token::CONST ||
        (peek() == Token::LET && IsNextLetKeyword())) {
      DeclarationParsingResult parsing_result;

      ParseVariableDeclarations(kForStatement, &parsing_result, nullptr,
                                CHECK_OK);
      if (parsing_result.descriptor.mode == CONST ||
          parsing_result.descriptor.mode == LET) {
        has_lexical = true;
      }
      if (CheckInOrOf(&mode)) {
        if (!*ok) return Statement::Default();
        if (parsing_result.declarations.length() != 1) {
          ReportMessageAt(parsing_result.bindings_loc,
                          MessageTemplate::kForInOfLoopMultiBindings,
                          ForEachStatement::VisitModeString(mode));
          *ok = false;
          return Statement::Default();
        }
        bool is_binding_pattern =
            parsing_result.declarations[0].pattern.IsObjectLiteral() ||
            parsing_result.declarations[0].pattern.IsArrayLiteral();
        if (parsing_result.first_initializer_loc.IsValid() &&
            (is_strict(language_mode()) || mode == ForEachStatement::ITERATE ||
             has_lexical || is_binding_pattern || allow_harmony_for_in())) {
          // Only increment the use count if we would have let this through
          // without the flag.
          if (use_counts_ != nullptr && allow_harmony_for_in()) {
            ++use_counts_[v8::Isolate::kForInInitializer];
          }
          ReportMessageAt(parsing_result.first_initializer_loc,
                          MessageTemplate::kForInOfLoopInitializer,
                          ForEachStatement::VisitModeString(mode));
          *ok = false;
          return Statement::Default();
        }

        if (mode == ForEachStatement::ITERATE) {
          ExpressionClassifier classifier(this);
          ParseAssignmentExpression(true, CHECK_OK);
          RewriteNonPattern(CHECK_OK);
        } else {
          ParseExpression(true, CHECK_OK);
        }

        Expect(Token::RPAREN, CHECK_OK);
        {
          ReturnExprScope no_tail_calls(function_state_,
                                        ReturnExprContext::kInsideForInOfBody);
          ParseScopedStatement(nullptr, true, CHECK_OK);
        }
        return Statement::Default();
      }
    } else {
      int lhs_beg_pos = peek_position();
      ExpressionClassifier classifier(this);
      Expression lhs = ParseExpressionCoverGrammar(false, CHECK_OK);
      int lhs_end_pos = scanner()->location().end_pos;
      bool is_for_each = CheckInOrOf(&mode);
      bool is_destructuring = is_for_each &&
                              (lhs->IsArrayLiteral() || lhs->IsObjectLiteral());

      if (is_destructuring) {
        ValidateAssignmentPattern(CHECK_OK);
      } else {
        ValidateExpression(CHECK_OK);
      }

      if (is_for_each) {
        if (!is_destructuring) {
          lhs = CheckAndRewriteReferenceExpression(
              lhs, lhs_beg_pos, lhs_end_pos, MessageTemplate::kInvalidLhsInFor,
              kSyntaxError, CHECK_OK);
        }

        if (mode == ForEachStatement::ITERATE) {
          ExpressionClassifier classifier(this);
          ParseAssignmentExpression(true, CHECK_OK);
          RewriteNonPattern(CHECK_OK);
        } else {
          ParseExpression(true, CHECK_OK);
        }

        Expect(Token::RPAREN, CHECK_OK);
        {
          BlockState block_state(&scope_state_);
          ParseScopedStatement(nullptr, true, CHECK_OK);
        }
        return Statement::Default();
      }
    }
  }

  // Parsed initializer at this point.
  Expect(Token::SEMICOLON, CHECK_OK);

  // If there are let bindings, then condition and the next statement of the
  // for loop must be parsed in a new scope.
  Scope* inner_scope = scope();
  // TODO(verwaest): Allocate this through a ScopeState as well.
  if (has_lexical) inner_scope = NewScopeWithParent(inner_scope, BLOCK_SCOPE);

  {
    BlockState block_state(&scope_state_, inner_scope);

    if (peek() != Token::SEMICOLON) {
      ParseExpression(true, CHECK_OK);
    }
    Expect(Token::SEMICOLON, CHECK_OK);

    if (peek() != Token::RPAREN) {
      ParseExpression(true, CHECK_OK);
    }
    Expect(Token::RPAREN, CHECK_OK);

    ParseScopedStatement(nullptr, true, ok);
  }
  return Statement::Default();
}


PreParser::Statement PreParser::ParseTryStatement(bool* ok) {
  // TryStatement ::
  //   'try' Block Catch
  //   'try' Block Finally
  //   'try' Block Catch Finally
  //
  // Catch ::
  //   'catch' '(' Identifier ')' Block
  //
  // Finally ::
  //   'finally' Block

  Expect(Token::TRY, CHECK_OK);

  {
    ReturnExprScope no_tail_calls(function_state_,
                                  ReturnExprContext::kInsideTryBlock);
    ParseBlock(nullptr, CHECK_OK);
  }

  Token::Value tok = peek();
  if (tok != Token::CATCH && tok != Token::FINALLY) {
    ReportMessageAt(scanner()->location(), MessageTemplate::kNoCatchOrFinally);
    *ok = false;
    return Statement::Default();
  }
  TailCallExpressionList tail_call_expressions_in_catch_block(zone());
  bool catch_block_exists = false;
  if (tok == Token::CATCH) {
    Consume(Token::CATCH);
    Expect(Token::LPAREN, CHECK_OK);
    Scope* catch_scope = NewScope(CATCH_SCOPE);
    ExpressionClassifier pattern_classifier(this);
    ParsePrimaryExpression(CHECK_OK);
    ValidateBindingPattern(CHECK_OK);
    Expect(Token::RPAREN, CHECK_OK);
    {
      CollectExpressionsInTailPositionToListScope
          collect_tail_call_expressions_scope(
              function_state_, &tail_call_expressions_in_catch_block);
      BlockState block_state(&scope_state_, catch_scope);
      {
        BlockState block_state(&scope_state_);
        ParseBlock(nullptr, CHECK_OK);
      }
    }
    catch_block_exists = true;
    tok = peek();
  }
  if (tok == Token::FINALLY) {
    Consume(Token::FINALLY);
    ParseBlock(nullptr, CHECK_OK);
    if (FLAG_harmony_explicit_tailcalls && catch_block_exists &&
        tail_call_expressions_in_catch_block.has_explicit_tail_calls()) {
      // TODO(ishell): update chapter number.
      // ES8 XX.YY.ZZ
      ReportMessageAt(tail_call_expressions_in_catch_block.location(),
                      MessageTemplate::kUnexpectedTailCallInCatchBlock);
      *ok = false;
      return Statement::Default();
    }
  }
  return Statement::Default();
}


// Redefinition of CHECK_OK for parsing expressions.
#undef CHECK_OK
#define CHECK_OK CHECK_OK_VALUE(Expression::Default())

PreParser::Expression PreParser::ParseFunctionLiteral(
    Identifier function_name, Scanner::Location function_name_location,
    FunctionNameValidity function_name_validity, FunctionKind kind,
    int function_token_pos, FunctionLiteral::FunctionType function_type,
    LanguageMode language_mode, bool* ok) {
  // Function ::
  //   '(' FormalParameterList? ')' '{' FunctionBody '}'

  // Parse function body.
  PreParserStatementList body;
  bool outer_is_script_scope = scope()->is_script_scope();
  DeclarationScope* function_scope = NewFunctionScope(kind);
  function_scope->SetLanguageMode(language_mode);
  FunctionState function_state(&function_state_, &scope_state_, function_scope,
                               kind);
  DuplicateFinder duplicate_finder(scanner()->unicode_cache());
  ExpressionClassifier formals_classifier(this, &duplicate_finder);

  Expect(Token::LPAREN, CHECK_OK);
  int start_position = scanner()->location().beg_pos;
  function_scope->set_start_position(start_position);
  PreParserFormalParameters formals(function_scope);
  ParseFormalParameterList(&formals, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);
  int formals_end_position = scanner()->location().end_pos;

  CheckArityRestrictions(formals.arity, kind, formals.has_rest, start_position,
                         formals_end_position, CHECK_OK);

  // See Parser::ParseFunctionLiteral for more information about lazy parsing
  // and lazy compilation.
  bool is_lazily_parsed = (outer_is_script_scope && allow_lazy() &&
                           !function_state_->this_function_is_parenthesized());

  Expect(Token::LBRACE, CHECK_OK);
  if (is_lazily_parsed) {
    ParseLazyFunctionLiteralBody(false, CHECK_OK);
  } else {
    ParseStatementList(body, Token::RBRACE, CHECK_OK);
  }
  Expect(Token::RBRACE, CHECK_OK);

  // Parsing the body may change the language mode in our scope.
  language_mode = function_scope->language_mode();

  // Validate name and parameter names. We can do this only after parsing the
  // function, since the function can declare itself strict.
  CheckFunctionName(language_mode, function_name, function_name_validity,
                    function_name_location, CHECK_OK);
  const bool allow_duplicate_parameters =
      is_sloppy(language_mode) && formals.is_simple && !IsConciseMethod(kind);
  ValidateFormalParameters(language_mode, allow_duplicate_parameters, CHECK_OK);

  if (is_strict(language_mode)) {
    int end_position = scanner()->location().end_pos;
    CheckStrictOctalLiteral(start_position, end_position, CHECK_OK);
    CheckDecimalLiteralWithLeadingZero(start_position, end_position);
  }

  return Expression::Default();
}

PreParser::Expression PreParser::ParseAsyncFunctionExpression(bool* ok) {
  // AsyncFunctionDeclaration ::
  //   async [no LineTerminator here] function ( FormalParameters[Await] )
  //       { AsyncFunctionBody }
  //
  //   async [no LineTerminator here] function BindingIdentifier[Await]
  //       ( FormalParameters[Await] ) { AsyncFunctionBody }
  int pos = position();
  Expect(Token::FUNCTION, CHECK_OK);
  bool is_strict_reserved = false;
  Identifier name;
  FunctionLiteral::FunctionType type = FunctionLiteral::kAnonymousExpression;

  if (peek_any_identifier()) {
    type = FunctionLiteral::kNamedExpression;
    name = ParseIdentifierOrStrictReservedWord(FunctionKind::kAsyncFunction,
                                               &is_strict_reserved, CHECK_OK);
  }

  ParseFunctionLiteral(name, scanner()->location(),
                       is_strict_reserved ? kFunctionNameIsStrictReserved
                                          : kFunctionNameValidityUnknown,
                       FunctionKind::kAsyncFunction, pos, type, language_mode(),
                       CHECK_OK);
  return Expression::Default();
}

PreParser::LazyParsingResult PreParser::ParseLazyFunctionLiteralBody(
    bool may_abort, bool* ok) {
  int body_start = position();
  PreParserStatementList body;
  LazyParsingResult result = ParseStatementList(
      body, Token::RBRACE, may_abort, CHECK_OK_VALUE(kLazyParsingComplete));
  if (result == kLazyParsingAborted) return result;

  // Position right after terminal '}'.
  DCHECK_EQ(Token::RBRACE, scanner()->peek());
  int body_end = scanner()->peek_location().end_pos;
  DeclarationScope* scope = this->scope()->AsDeclarationScope();
  DCHECK(scope->is_function_scope());
  log_->LogFunction(body_start, body_end,
                    function_state_->materialized_literal_count(),
                    function_state_->expected_property_count(), language_mode(),
                    scope->uses_super_property(), scope->calls_eval());
  return kLazyParsingComplete;
}

PreParserExpression PreParser::ParseClassLiteral(
    PreParserIdentifier name, Scanner::Location class_name_location,
    bool name_is_strict_reserved, int pos, bool* ok) {
  // All parts of a ClassDeclaration and ClassExpression are strict code.
  if (name_is_strict_reserved) {
    ReportMessageAt(class_name_location,
                    MessageTemplate::kUnexpectedStrictReserved);
    *ok = false;
    return EmptyExpression();
  }
  if (IsEvalOrArguments(name)) {
    ReportMessageAt(class_name_location, MessageTemplate::kStrictEvalArguments);
    *ok = false;
    return EmptyExpression();
  }

  LanguageMode class_language_mode = language_mode();
  BlockState block_state(&scope_state_);
  scope()->SetLanguageMode(
      static_cast<LanguageMode>(class_language_mode | STRICT));
  // TODO(marja): Make PreParser use scope names too.
  // this->scope()->SetScopeName(name);

  bool has_extends = Check(Token::EXTENDS);
  if (has_extends) {
    ExpressionClassifier extends_classifier(this);
    ParseLeftHandSideExpression(CHECK_OK);
    CheckNoTailCallExpressions(CHECK_OK);
    ValidateExpression(CHECK_OK);
    impl()->AccumulateFormalParameterContainmentErrors();
  }

  ClassLiteralChecker checker(this);
  bool has_seen_constructor = false;

  Expect(Token::LBRACE, CHECK_OK);
  while (peek() != Token::RBRACE) {
    if (Check(Token::SEMICOLON)) continue;
    bool is_computed_name = false;  // Classes do not care about computed
                                    // property names here.
    ExpressionClassifier property_classifier(this);
    ParseClassPropertyDefinition(&checker, has_extends, &is_computed_name,
                                 &has_seen_constructor, CHECK_OK);
    ValidateExpression(CHECK_OK);
    impl()->AccumulateFormalParameterContainmentErrors();
  }

  Expect(Token::RBRACE, CHECK_OK);

  return Expression::Default();
}

void PreParser::ParseAsyncArrowSingleExpressionBody(PreParserStatementList body,
                                                    bool accept_IN, int pos,
                                                    bool* ok) {
  scope()->ForceContextAllocation();

  PreParserExpression return_value =
      ParseAssignmentExpression(accept_IN, CHECK_OK_VOID);

  body->Add(PreParserStatement::ExpressionStatement(return_value), zone());
}

#undef CHECK_OK
#undef CHECK_OK_CUSTOM


}  // namespace internal
}  // namespace v8
