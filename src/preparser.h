// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_PREPARSER_H
#define V8_PREPARSER_H

#include "unicode.h"

namespace v8 {
namespace internal {
namespace preparser {

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

enum StatementType {
  kUnknownStatement
};

enum ExpressionType {
  kUnknownExpression,
  kIdentifierExpression,  // Used to detect labels.
  kThisExpression,
  kThisPropertyExpression
};

enum IdentifierType {
  kUnknownIdentifier
};

enum SourceElementTypes {
  kUnknownSourceElements
};


typedef int SourceElements;
typedef int Expression;
typedef int Statement;
typedef int Identifier;
typedef int Arguments;


template <typename Scanner, typename PreParserLog>
class PreParser {
 public:
  PreParser() : scope_(NULL), allow_lazy_(true) { }
  ~PreParser() { }

  // Pre-parse the program from the character stream; returns true on
  // success (even if parsing failed, the pre-parse data successfully
  // captured the syntax error), and false if a stack-overflow happened
  // during parsing.
  bool PreParseProgram(Scanner* scanner,
                       PreParserLog* log,
                       bool allow_lazy) {
    allow_lazy_ = allow_lazy;
    scanner_ = scanner;
    log_ = log;
    Scope top_scope(&scope_, kTopLevelScope);
    bool ok = true;
    ParseSourceElements(Token::EOS, &ok);
    bool stack_overflow = scanner_->stack_overflow();
    if (!ok && !stack_overflow) {
      ReportUnexpectedToken(scanner_->current_token());
    }
    return !stack_overflow;
  }

 private:
  enum ScopeType {
    kTopLevelScope,
    kFunctionScope
  };

  class Scope {
   public:
    Scope(Scope** variable, ScopeType type)
        : variable_(variable),
          prev_(*variable),
          type_(type),
          materialized_literal_count_(0),
          expected_properties_(0),
          with_nesting_count_(0) {
      *variable = this;
    }
    ~Scope() { *variable_ = prev_; }
    void NextMaterializedLiteralIndex() { materialized_literal_count_++; }
    void AddProperty() { expected_properties_++; }
    ScopeType type() { return type_; }
    int expected_properties() { return expected_properties_; }
    int materialized_literal_count() { return materialized_literal_count_; }
    bool IsInsideWith() { return with_nesting_count_ != 0; }
    void EnterWith() { with_nesting_count_++; }
    void LeaveWith() { with_nesting_count_--; }

   private:
    Scope** const variable_;
    Scope* const prev_;
    const ScopeType type_;
    int materialized_literal_count_;
    int expected_properties_;
    int with_nesting_count_;
  };

  // Types that allow us to recognize simple this-property assignments.
  // A simple this-property assignment is a statement on the form
  // "this.propertyName = {primitive constant or function parameter name);"
  // where propertyName isn't "__proto__".
  // The result is only relevant if the function body contains only
  // simple this-property assignments.

  // Report syntax error
  void ReportUnexpectedToken(Token::Value token);
  void ReportMessageAt(int start_pos,
                       int end_pos,
                       const char* type,
                       const char* name_opt) {
    log_->LogMessage(start_pos, end_pos, type, name_opt);
  }

  // All ParseXXX functions take as the last argument an *ok parameter
  // which is set to false if parsing failed; it is unchanged otherwise.
  // By making the 'exception handling' explicit, we are forced to check
  // for failure at the call sites.
  SourceElements ParseSourceElements(int end_token, bool* ok);
  Statement ParseStatement(bool* ok);
  Statement ParseFunctionDeclaration(bool* ok);
  Statement ParseNativeDeclaration(bool* ok);
  Statement ParseBlock(bool* ok);
  Statement ParseVariableStatement(bool* ok);
  Statement ParseVariableDeclarations(bool accept_IN, int* num_decl, bool* ok);
  Statement ParseExpressionOrLabelledStatement(bool* ok);
  Statement ParseIfStatement(bool* ok);
  Statement ParseContinueStatement(bool* ok);
  Statement ParseBreakStatement(bool* ok);
  Statement ParseReturnStatement(bool* ok);
  Statement ParseWithStatement(bool* ok);
  Statement ParseSwitchStatement(bool* ok);
  Statement ParseDoWhileStatement(bool* ok);
  Statement ParseWhileStatement(bool* ok);
  Statement ParseForStatement(bool* ok);
  Statement ParseThrowStatement(bool* ok);
  Statement ParseTryStatement(bool* ok);
  Statement ParseDebuggerStatement(bool* ok);

  Expression ParseExpression(bool accept_IN, bool* ok);
  Expression ParseAssignmentExpression(bool accept_IN, bool* ok);
  Expression ParseConditionalExpression(bool accept_IN, bool* ok);
  Expression ParseBinaryExpression(int prec, bool accept_IN, bool* ok);
  Expression ParseUnaryExpression(bool* ok);
  Expression ParsePostfixExpression(bool* ok);
  Expression ParseLeftHandSideExpression(bool* ok);
  Expression ParseNewExpression(bool* ok);
  Expression ParseMemberExpression(bool* ok);
  Expression ParseNewPrefix(int* new_count, bool* ok);
  Expression ParseMemberWithNewPrefixesExpression(int* new_count, bool* ok);
  Expression ParsePrimaryExpression(bool* ok);
  Expression ParseArrayLiteral(bool* ok);
  Expression ParseObjectLiteral(bool* ok);
  Expression ParseRegExpLiteral(bool seen_equal, bool* ok);
  Expression ParseV8Intrinsic(bool* ok);

  Arguments ParseArguments(bool* ok);
  Expression ParseFunctionLiteral(bool* ok);

  Identifier ParseIdentifier(bool* ok);
  Identifier ParseIdentifierName(bool* ok);
  Identifier ParseIdentifierOrGetOrSet(bool* is_get, bool* is_set, bool* ok);

  Identifier GetIdentifierSymbol();
  unsigned int HexDigitValue(char digit);
  Expression GetStringSymbol();


  Token::Value peek() { return scanner_->peek(); }
  Token::Value Next() {
    Token::Value next = scanner_->Next();
    return next;
  }

  void Consume(Token::Value token) {
    Next();
  }

  void Expect(Token::Value token, bool* ok) {
    if (Next() != token) {
      *ok = false;
    }
  }

  bool Check(Token::Value token) {
    Token::Value next = peek();
    if (next == token) {
      Consume(next);
      return true;
    }
    return false;
  }
  void ExpectSemicolon(bool* ok);

  static int Precedence(Token::Value tok, bool accept_IN);

  Scanner* scanner_;
  PreParserLog* log_;
  Scope* scope_;
  bool allow_lazy_;
};


#define CHECK_OK  ok);  \
  if (!*ok) return -1;  \
  ((void)0
#define DUMMY )  // to make indentation work
#undef DUMMY


template <typename Scanner, typename Log>
void PreParser<Scanner, Log>::ReportUnexpectedToken(Token::Value token) {
  // We don't report stack overflows here, to avoid increasing the
  // stack depth even further.  Instead we report it after parsing is
  // over, in ParseProgram.
  if (token == Token::ILLEGAL && scanner_->stack_overflow()) {
    return;
  }
  typename Scanner::Location source_location = scanner_->location();

  // Four of the tokens are treated specially
  switch (token) {
  case Token::EOS:
    return ReportMessageAt(source_location.beg_pos, source_location.end_pos,
                           "unexpected_eos", NULL);
  case Token::NUMBER:
    return ReportMessageAt(source_location.beg_pos, source_location.end_pos,
                           "unexpected_token_number", NULL);
  case Token::STRING:
    return ReportMessageAt(source_location.beg_pos, source_location.end_pos,
                           "unexpected_token_string", NULL);
  case Token::IDENTIFIER:
    return ReportMessageAt(source_location.beg_pos, source_location.end_pos,
                           "unexpected_token_identifier", NULL);
  default:
    const char* name = Token::String(token);
    ReportMessageAt(source_location.beg_pos, source_location.end_pos,
                    "unexpected_token", name);
  }
}


template <typename Scanner, typename Log>
SourceElements PreParser<Scanner, Log>::ParseSourceElements(int end_token,
                                                            bool* ok) {
  // SourceElements ::
  //   (Statement)* <end_token>

  while (peek() != end_token) {
    ParseStatement(CHECK_OK);
  }
  return kUnknownSourceElements;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseStatement(bool* ok) {
  // Statement ::
  //   Block
  //   VariableStatement
  //   EmptyStatement
  //   ExpressionStatement
  //   IfStatement
  //   IterationStatement
  //   ContinueStatement
  //   BreakStatement
  //   ReturnStatement
  //   WithStatement
  //   LabelledStatement
  //   SwitchStatement
  //   ThrowStatement
  //   TryStatement
  //   DebuggerStatement

  // Note: Since labels can only be used by 'break' and 'continue'
  // statements, which themselves are only valid within blocks,
  // iterations or 'switch' statements (i.e., BreakableStatements),
  // labels can be simply ignored in all other cases; except for
  // trivial labeled break statements 'label: break label' which is
  // parsed into an empty statement.

  // Keep the source position of the statement
  switch (peek()) {
    case Token::LBRACE:
      return ParseBlock(ok);

    case Token::CONST:
    case Token::VAR:
      return ParseVariableStatement(ok);

    case Token::SEMICOLON:
      Next();
      return kUnknownStatement;

    case Token::IF:
      return  ParseIfStatement(ok);

    case Token::DO:
      return ParseDoWhileStatement(ok);

    case Token::WHILE:
      return ParseWhileStatement(ok);

    case Token::FOR:
      return ParseForStatement(ok);

    case Token::CONTINUE:
      return ParseContinueStatement(ok);

    case Token::BREAK:
      return ParseBreakStatement(ok);

    case Token::RETURN:
      return ParseReturnStatement(ok);

    case Token::WITH:
      return ParseWithStatement(ok);

    case Token::SWITCH:
      return ParseSwitchStatement(ok);

    case Token::THROW:
      return ParseThrowStatement(ok);

    case Token::TRY:
      return ParseTryStatement(ok);

    case Token::FUNCTION:
      return ParseFunctionDeclaration(ok);

    case Token::NATIVE:
      return ParseNativeDeclaration(ok);

    case Token::DEBUGGER:
      return ParseDebuggerStatement(ok);

    default:
      return ParseExpressionOrLabelledStatement(ok);
  }
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseFunctionDeclaration(bool* ok) {
  // FunctionDeclaration ::
  //   'function' Identifier '(' FormalParameterListopt ')' '{' FunctionBody '}'
  Expect(Token::FUNCTION, CHECK_OK);
  ParseIdentifier(CHECK_OK);
  ParseFunctionLiteral(CHECK_OK);
  return kUnknownStatement;
}


// Language extension which is only enabled for source files loaded
// through the API's extension mechanism.  A native function
// declaration is resolved by looking up the function through a
// callback provided by the extension.
template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseNativeDeclaration(bool* ok) {
  Expect(Token::NATIVE, CHECK_OK);
  Expect(Token::FUNCTION, CHECK_OK);
  ParseIdentifier(CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  bool done = (peek() == Token::RPAREN);
  while (!done) {
    ParseIdentifier(CHECK_OK);
    done = (peek() == Token::RPAREN);
    if (!done) {
      Expect(Token::COMMA, CHECK_OK);
    }
  }
  Expect(Token::RPAREN, CHECK_OK);
  Expect(Token::SEMICOLON, CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseBlock(bool* ok) {
  // Block ::
  //   '{' Statement* '}'

  // Note that a Block does not introduce a new execution scope!
  // (ECMA-262, 3rd, 12.2)
  //
  Expect(Token::LBRACE, CHECK_OK);
  while (peek() != Token::RBRACE) {
    ParseStatement(CHECK_OK);
  }
  Expect(Token::RBRACE, CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseVariableStatement(bool* ok) {
  // VariableStatement ::
  //   VariableDeclarations ';'

  Statement result = ParseVariableDeclarations(true, NULL, CHECK_OK);
  ExpectSemicolon(CHECK_OK);
  return result;
}


// If the variable declaration declares exactly one non-const
// variable, then *var is set to that variable. In all other cases,
// *var is untouched; in particular, it is the caller's responsibility
// to initialize it properly. This mechanism is also used for the parsing
// of 'for-in' loops.
template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseVariableDeclarations(bool accept_IN,
                                                  int* num_decl,
                                                  bool* ok) {
  // VariableDeclarations ::
  //   ('var' | 'const') (Identifier ('=' AssignmentExpression)?)+[',']

  if (peek() == Token::VAR) {
    Consume(Token::VAR);
  } else if (peek() == Token::CONST) {
    Consume(Token::CONST);
  } else {
    *ok = false;
    return 0;
  }

  // The scope of a variable/const declared anywhere inside a function
  // is the entire function (ECMA-262, 3rd, 10.1.3, and 12.2). .
  int nvars = 0;  // the number of variables declared
  do {
    // Parse variable name.
    if (nvars > 0) Consume(Token::COMMA);
    ParseIdentifier(CHECK_OK);
    nvars++;
    if (peek() == Token::ASSIGN) {
      Expect(Token::ASSIGN, CHECK_OK);
      ParseAssignmentExpression(accept_IN, CHECK_OK);
    }
  } while (peek() == Token::COMMA);

  if (num_decl != NULL) *num_decl = nvars;
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseExpressionOrLabelledStatement(
    bool* ok) {
  // ExpressionStatement | LabelledStatement ::
  //   Expression ';'
  //   Identifier ':' Statement

  Expression expr = ParseExpression(true, CHECK_OK);
  if (peek() == Token::COLON && expr == kIdentifierExpression) {
    Consume(Token::COLON);
    return ParseStatement(ok);
  }
  // Parsed expression statement.
  ExpectSemicolon(CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseIfStatement(bool* ok) {
  // IfStatement ::
  //   'if' '(' Expression ')' Statement ('else' Statement)?

  Expect(Token::IF, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);
  ParseStatement(CHECK_OK);
  if (peek() == Token::ELSE) {
    Next();
    ParseStatement(CHECK_OK);
  }
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseContinueStatement(bool* ok) {
  // ContinueStatement ::
  //   'continue' [no line terminator] Identifier? ';'

  Expect(Token::CONTINUE, CHECK_OK);
  Token::Value tok = peek();
  if (!scanner_->has_line_terminator_before_next() &&
      tok != Token::SEMICOLON &&
      tok != Token::RBRACE &&
      tok != Token::EOS) {
    ParseIdentifier(CHECK_OK);
  }
  ExpectSemicolon(CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseBreakStatement(bool* ok) {
  // BreakStatement ::
  //   'break' [no line terminator] Identifier? ';'

  Expect(Token::BREAK, CHECK_OK);
  Token::Value tok = peek();
  if (!scanner_->has_line_terminator_before_next() &&
      tok != Token::SEMICOLON &&
      tok != Token::RBRACE &&
      tok != Token::EOS) {
    ParseIdentifier(CHECK_OK);
  }
  ExpectSemicolon(CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseReturnStatement(bool* ok) {
  // ReturnStatement ::
  //   'return' [no line terminator] Expression? ';'

  // Consume the return token. It is necessary to do the before
  // reporting any errors on it, because of the way errors are
  // reported (underlining).
  Expect(Token::RETURN, CHECK_OK);

  // An ECMAScript program is considered syntactically incorrect if it
  // contains a return statement that is not within the body of a
  // function. See ECMA-262, section 12.9, page 67.
  // This is not handled during preparsing.

  Token::Value tok = peek();
  if (!scanner_->has_line_terminator_before_next() &&
      tok != Token::SEMICOLON &&
      tok != Token::RBRACE &&
      tok != Token::EOS) {
    ParseExpression(true, CHECK_OK);
  }
  ExpectSemicolon(CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseWithStatement(bool* ok) {
  // WithStatement ::
  //   'with' '(' Expression ')' Statement
  Expect(Token::WITH, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);

  scope_->EnterWith();
  ParseStatement(CHECK_OK);
  scope_->LeaveWith();
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseSwitchStatement(bool* ok) {
  // SwitchStatement ::
  //   'switch' '(' Expression ')' '{' CaseClause* '}'

  Expect(Token::SWITCH, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);

  Expect(Token::LBRACE, CHECK_OK);
  Token::Value token = peek();
  while (token != Token::RBRACE) {
    if (token == Token::CASE) {
      Expect(Token::CASE, CHECK_OK);
      ParseExpression(true, CHECK_OK);
      Expect(Token::COLON, CHECK_OK);
    } else if (token == Token::DEFAULT) {
      Expect(Token::DEFAULT, CHECK_OK);
      Expect(Token::COLON, CHECK_OK);
    } else {
      ParseStatement(CHECK_OK);
    }
    token = peek();
  }
  Expect(Token::RBRACE, CHECK_OK);

  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseDoWhileStatement(bool* ok) {
  // DoStatement ::
  //   'do' Statement 'while' '(' Expression ')' ';'

  Expect(Token::DO, CHECK_OK);
  ParseStatement(CHECK_OK);
  Expect(Token::WHILE, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseWhileStatement(bool* ok) {
  // WhileStatement ::
  //   'while' '(' Expression ')' Statement

  Expect(Token::WHILE, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  ParseExpression(true, CHECK_OK);
  Expect(Token::RPAREN, CHECK_OK);
  ParseStatement(CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseForStatement(bool* ok) {
  // ForStatement ::
  //   'for' '(' Expression? ';' Expression? ';' Expression? ')' Statement

  Expect(Token::FOR, CHECK_OK);
  Expect(Token::LPAREN, CHECK_OK);
  if (peek() != Token::SEMICOLON) {
    if (peek() == Token::VAR || peek() == Token::CONST) {
      int decl_count;
      ParseVariableDeclarations(false, &decl_count, CHECK_OK);
      if (peek() == Token::IN && decl_count == 1) {
        Expect(Token::IN, CHECK_OK);
        ParseExpression(true, CHECK_OK);
        Expect(Token::RPAREN, CHECK_OK);

        ParseStatement(CHECK_OK);
        return kUnknownStatement;
      }
    } else {
      ParseExpression(false, CHECK_OK);
      if (peek() == Token::IN) {
        Expect(Token::IN, CHECK_OK);
        ParseExpression(true, CHECK_OK);
        Expect(Token::RPAREN, CHECK_OK);

        ParseStatement(CHECK_OK);
        return kUnknownStatement;
      }
    }
  }

  // Parsed initializer at this point.
  Expect(Token::SEMICOLON, CHECK_OK);

  if (peek() != Token::SEMICOLON) {
    ParseExpression(true, CHECK_OK);
  }
  Expect(Token::SEMICOLON, CHECK_OK);

  if (peek() != Token::RPAREN) {
    ParseExpression(true, CHECK_OK);
  }
  Expect(Token::RPAREN, CHECK_OK);

  ParseStatement(CHECK_OK);
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseThrowStatement(bool* ok) {
  // ThrowStatement ::
  //   'throw' [no line terminator] Expression ';'

  Expect(Token::THROW, CHECK_OK);
  if (scanner_->has_line_terminator_before_next()) {
    typename Scanner::Location pos = scanner_->location();
    ReportMessageAt(pos.beg_pos, pos.end_pos,
                    "newline_after_throw", NULL);
    *ok = false;
    return NULL;
  }
  ParseExpression(true, CHECK_OK);
  ExpectSemicolon(CHECK_OK);

  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseTryStatement(bool* ok) {
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

  // In preparsing, allow any number of catch/finally blocks, including zero
  // of both.

  Expect(Token::TRY, CHECK_OK);

  ParseBlock(CHECK_OK);

  bool catch_or_finally_seen = false;
  if (peek() == Token::CATCH) {
    Expect(Token::CATCH, CHECK_OK);
    Expect(Token::LPAREN, CHECK_OK);
    ParseIdentifier(CHECK_OK);
    Expect(Token::RPAREN, CHECK_OK);
    ParseBlock(CHECK_OK);
    catch_or_finally_seen = true;
  }
  if (peek() == Token::FINALLY) {
    Expect(Token::FINALLY, CHECK_OK);
    ParseBlock(CHECK_OK);
    catch_or_finally_seen = true;
  }
  if (!catch_or_finally_seen) {
    *ok = false;
  }
  return kUnknownStatement;
}


template <typename Scanner, typename Log>
Statement PreParser<Scanner, Log>::ParseDebuggerStatement(bool* ok) {
  // In ECMA-262 'debugger' is defined as a reserved keyword. In some browser
  // contexts this is used as a statement which invokes the debugger as if a
  // break point is present.
  // DebuggerStatement ::
  //   'debugger' ';'

  Expect(Token::DEBUGGER, CHECK_OK);
  ExpectSemicolon(CHECK_OK);
  return kUnknownStatement;
}


// Precedence = 1
template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseExpression(bool accept_IN, bool* ok) {
  // Expression ::
  //   AssignmentExpression
  //   Expression ',' AssignmentExpression

  Expression result = ParseAssignmentExpression(accept_IN, CHECK_OK);
  while (peek() == Token::COMMA) {
    Expect(Token::COMMA, CHECK_OK);
    ParseAssignmentExpression(accept_IN, CHECK_OK);
    result = kUnknownExpression;
  }
  return result;
}


// Precedence = 2
template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseAssignmentExpression(bool accept_IN,
                                                              bool* ok) {
  // AssignmentExpression ::
  //   ConditionalExpression
  //   LeftHandSideExpression AssignmentOperator AssignmentExpression

  Expression expression = ParseConditionalExpression(accept_IN, CHECK_OK);

  if (!Token::IsAssignmentOp(peek())) {
    // Parsed conditional expression only (no assignment).
    return expression;
  }

  Token::Value op = Next();  // Get assignment operator.
  ParseAssignmentExpression(accept_IN, CHECK_OK);

  if ((op == Token::ASSIGN) && (expression == kThisPropertyExpression)) {
    scope_->AddProperty();
  }

  return kUnknownExpression;
}


// Precedence = 3
template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseConditionalExpression(bool accept_IN,
                                                               bool* ok) {
  // ConditionalExpression ::
  //   LogicalOrExpression
  //   LogicalOrExpression '?' AssignmentExpression ':' AssignmentExpression

  // We start using the binary expression parser for prec >= 4 only!
  Expression expression = ParseBinaryExpression(4, accept_IN, CHECK_OK);
  if (peek() != Token::CONDITIONAL) return expression;
  Consume(Token::CONDITIONAL);
  // In parsing the first assignment expression in conditional
  // expressions we always accept the 'in' keyword; see ECMA-262,
  // section 11.12, page 58.
  ParseAssignmentExpression(true, CHECK_OK);
  Expect(Token::COLON, CHECK_OK);
  ParseAssignmentExpression(accept_IN, CHECK_OK);
  return kUnknownExpression;
}


template <typename Scanner, typename Log>
int PreParser<Scanner, Log>::Precedence(Token::Value tok, bool accept_IN) {
  if (tok == Token::IN && !accept_IN)
    return 0;  // 0 precedence will terminate binary expression parsing

  return Token::Precedence(tok);
}


// Precedence >= 4
template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseBinaryExpression(int prec,
                                                          bool accept_IN,
                                                          bool* ok) {
  Expression result = ParseUnaryExpression(CHECK_OK);
  for (int prec1 = Precedence(peek(), accept_IN); prec1 >= prec; prec1--) {
    // prec1 >= 4
    while (Precedence(peek(), accept_IN) == prec1) {
      Next();
      ParseBinaryExpression(prec1 + 1, accept_IN, CHECK_OK);
      result = kUnknownExpression;
    }
  }
  return result;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseUnaryExpression(bool* ok) {
  // UnaryExpression ::
  //   PostfixExpression
  //   'delete' UnaryExpression
  //   'void' UnaryExpression
  //   'typeof' UnaryExpression
  //   '++' UnaryExpression
  //   '--' UnaryExpression
  //   '+' UnaryExpression
  //   '-' UnaryExpression
  //   '~' UnaryExpression
  //   '!' UnaryExpression

  Token::Value op = peek();
  if (Token::IsUnaryOp(op) || Token::IsCountOp(op)) {
    op = Next();
    ParseUnaryExpression(ok);
    return kUnknownExpression;
  } else {
    return ParsePostfixExpression(ok);
  }
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParsePostfixExpression(bool* ok) {
  // PostfixExpression ::
  //   LeftHandSideExpression ('++' | '--')?

  Expression expression = ParseLeftHandSideExpression(CHECK_OK);
  if (!scanner_->has_line_terminator_before_next() &&
      Token::IsCountOp(peek())) {
    Next();
    return kUnknownExpression;
  }
  return expression;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseLeftHandSideExpression(bool* ok) {
  // LeftHandSideExpression ::
  //   (NewExpression | MemberExpression) ...

  Expression result;
  if (peek() == Token::NEW) {
    result = ParseNewExpression(CHECK_OK);
  } else {
    result = ParseMemberExpression(CHECK_OK);
  }

  while (true) {
    switch (peek()) {
      case Token::LBRACK: {
        Consume(Token::LBRACK);
        ParseExpression(true, CHECK_OK);
        Expect(Token::RBRACK, CHECK_OK);
        if (result == kThisExpression) {
          result = kThisPropertyExpression;
        } else {
          result = kUnknownExpression;
        }
        break;
      }

      case Token::LPAREN: {
        ParseArguments(CHECK_OK);
        result = kUnknownExpression;
        break;
      }

      case Token::PERIOD: {
        Consume(Token::PERIOD);
        ParseIdentifierName(CHECK_OK);
        if (result == kThisExpression) {
          result = kThisPropertyExpression;
        } else {
          result = kUnknownExpression;
        }
        break;
      }

      default:
        return result;
    }
  }
}



template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseNewPrefix(int* new_count, bool* ok) {
  // NewExpression ::
  //   ('new')+ MemberExpression

  // The grammar for new expressions is pretty warped. The keyword
  // 'new' can either be a part of the new expression (where it isn't
  // followed by an argument list) or a part of the member expression,
  // where it must be followed by an argument list. To accommodate
  // this, we parse the 'new' keywords greedily and keep track of how
  // many we have parsed. This information is then passed on to the
  // member expression parser, which is only allowed to match argument
  // lists as long as it has 'new' prefixes left
  Expect(Token::NEW, CHECK_OK);
  *new_count++;

  if (peek() == Token::NEW) {
    ParseNewPrefix(new_count, CHECK_OK);
  } else {
    ParseMemberWithNewPrefixesExpression(new_count, CHECK_OK);
  }

  if (*new_count > 0) {
    *new_count--;
  }
  return kUnknownExpression;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseNewExpression(bool* ok) {
  int new_count = 0;
  return ParseNewPrefix(&new_count, ok);
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseMemberExpression(bool* ok) {
  return ParseMemberWithNewPrefixesExpression(NULL, ok);
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseMemberWithNewPrefixesExpression(
    int* new_count, bool* ok) {
  // MemberExpression ::
  //   (PrimaryExpression | FunctionLiteral)
  //     ('[' Expression ']' | '.' Identifier | Arguments)*

  // Parse the initial primary or function expression.
  Expression result = NULL;
  if (peek() == Token::FUNCTION) {
    Consume(Token::FUNCTION);
    if (peek() == Token::IDENTIFIER) {
      ParseIdentifier(CHECK_OK);
    }
    result = ParseFunctionLiteral(CHECK_OK);
  } else {
    result = ParsePrimaryExpression(CHECK_OK);
  }

  while (true) {
    switch (peek()) {
      case Token::LBRACK: {
        Consume(Token::LBRACK);
        ParseExpression(true, CHECK_OK);
        Expect(Token::RBRACK, CHECK_OK);
        if (result == kThisExpression) {
          result = kThisPropertyExpression;
        } else {
          result = kUnknownExpression;
        }
        break;
      }
      case Token::PERIOD: {
        Consume(Token::PERIOD);
        ParseIdentifierName(CHECK_OK);
        if (result == kThisExpression) {
          result = kThisPropertyExpression;
        } else {
          result = kUnknownExpression;
        }
        break;
      }
      case Token::LPAREN: {
        if ((new_count == NULL) || *new_count == 0) return result;
        // Consume one of the new prefixes (already parsed).
        ParseArguments(CHECK_OK);
        *new_count--;
        result = kUnknownExpression;
        break;
      }
      default:
        return result;
    }
  }
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParsePrimaryExpression(bool* ok) {
  // PrimaryExpression ::
  //   'this'
  //   'null'
  //   'true'
  //   'false'
  //   Identifier
  //   Number
  //   String
  //   ArrayLiteral
  //   ObjectLiteral
  //   RegExpLiteral
  //   '(' Expression ')'

  Expression result = kUnknownExpression;
  switch (peek()) {
    case Token::THIS: {
      Next();
      result = kThisExpression;
      break;
    }

    case Token::IDENTIFIER: {
      ParseIdentifier(CHECK_OK);
      result = kIdentifierExpression;
      break;
    }

    case Token::NULL_LITERAL:
    case Token::TRUE_LITERAL:
    case Token::FALSE_LITERAL:
    case Token::NUMBER: {
      Next();
      break;
    }
    case Token::STRING: {
      Next();
      result = GetStringSymbol();
      break;
    }

    case Token::ASSIGN_DIV:
      result = ParseRegExpLiteral(true, CHECK_OK);
      break;

    case Token::DIV:
      result = ParseRegExpLiteral(false, CHECK_OK);
      break;

    case Token::LBRACK:
      result = ParseArrayLiteral(CHECK_OK);
      break;

    case Token::LBRACE:
      result = ParseObjectLiteral(CHECK_OK);
      break;

    case Token::LPAREN:
      Consume(Token::LPAREN);
      result = ParseExpression(true, CHECK_OK);
      Expect(Token::RPAREN, CHECK_OK);
      if (result == kIdentifierExpression) result = kUnknownExpression;
      break;

    case Token::MOD:
      result = ParseV8Intrinsic(CHECK_OK);
      break;

    default: {
      Next();
      *ok = false;
      return kUnknownExpression;
    }
  }

  return result;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseArrayLiteral(bool* ok) {
  // ArrayLiteral ::
  //   '[' Expression? (',' Expression?)* ']'
  Expect(Token::LBRACK, CHECK_OK);
  while (peek() != Token::RBRACK) {
    if (peek() != Token::COMMA) {
      ParseAssignmentExpression(true, CHECK_OK);
    }
    if (peek() != Token::RBRACK) {
      Expect(Token::COMMA, CHECK_OK);
    }
  }
  Expect(Token::RBRACK, CHECK_OK);

  scope_->NextMaterializedLiteralIndex();
  return kUnknownExpression;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseObjectLiteral(bool* ok) {
  // ObjectLiteral ::
  //   '{' (
  //       ((IdentifierName | String | Number) ':' AssignmentExpression)
  //     | (('get' | 'set') (IdentifierName | String | Number) FunctionLiteral)
  //    )*[','] '}'

  Expect(Token::LBRACE, CHECK_OK);
  while (peek() != Token::RBRACE) {
    Token::Value next = peek();
    switch (next) {
      case Token::IDENTIFIER: {
        bool is_getter = false;
        bool is_setter = false;
        ParseIdentifierOrGetOrSet(&is_getter, &is_setter, CHECK_OK);
        if ((is_getter || is_setter) && peek() != Token::COLON) {
            Token::Value name = Next();
            if (name != Token::IDENTIFIER &&
                name != Token::NUMBER &&
                name != Token::STRING &&
                !Token::IsKeyword(name)) {
              *ok = false;
              return kUnknownExpression;
            }
            ParseFunctionLiteral(CHECK_OK);
            if (peek() != Token::RBRACE) {
              Expect(Token::COMMA, CHECK_OK);
            }
            continue;  // restart the while
        }
        break;
      }
      case Token::STRING:
        Consume(next);
        GetStringSymbol();
        break;
      case Token::NUMBER:
        Consume(next);
        break;
      default:
        if (Token::IsKeyword(next)) {
          Consume(next);
        } else {
          // Unexpected token.
          *ok = false;
          return kUnknownExpression;
        }
    }

    Expect(Token::COLON, CHECK_OK);
    ParseAssignmentExpression(true, CHECK_OK);

    // TODO(1240767): Consider allowing trailing comma.
    if (peek() != Token::RBRACE) Expect(Token::COMMA, CHECK_OK);
  }
  Expect(Token::RBRACE, CHECK_OK);

  scope_->NextMaterializedLiteralIndex();
  return kUnknownExpression;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseRegExpLiteral(bool seen_equal,
                                                       bool* ok) {
  if (!scanner_->ScanRegExpPattern(seen_equal)) {
    Next();
    typename Scanner::Location location = scanner_->location();
    ReportMessageAt(location.beg_pos, location.end_pos,
                    "unterminated_regexp", NULL);
    *ok = false;
    return kUnknownExpression;
  }

  scope_->NextMaterializedLiteralIndex();

  if (!scanner_->ScanRegExpFlags()) {
    Next();
    typename Scanner::Location location = scanner_->location();
    ReportMessageAt(location.beg_pos, location.end_pos,
                    "invalid_regexp_flags", NULL);
    *ok = false;
    return kUnknownExpression;
  }
  Next();
  return kUnknownExpression;
}


template <typename Scanner, typename Log>
Arguments PreParser<Scanner, Log>::ParseArguments(bool* ok) {
  // Arguments ::
  //   '(' (AssignmentExpression)*[','] ')'

  Expect(Token::LPAREN, CHECK_OK);
  bool done = (peek() == Token::RPAREN);
  int argc = 0;
  while (!done) {
    ParseAssignmentExpression(true, CHECK_OK);
    argc++;
    done = (peek() == Token::RPAREN);
    if (!done) Expect(Token::COMMA, CHECK_OK);
  }
  Expect(Token::RPAREN, CHECK_OK);
  return argc;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseFunctionLiteral(bool* ok) {
  // Function ::
  //   '(' FormalParameterList? ')' '{' FunctionBody '}'

  // Parse function body.
  ScopeType outer_scope_type = scope_->type();
  bool inside_with = scope_->IsInsideWith();
  Scope function_scope(&scope_, kFunctionScope);

  //  FormalParameterList ::
  //    '(' (Identifier)*[','] ')'
  Expect(Token::LPAREN, CHECK_OK);
  bool done = (peek() == Token::RPAREN);
  while (!done) {
    ParseIdentifier(CHECK_OK);
    done = (peek() == Token::RPAREN);
    if (!done) {
      Expect(Token::COMMA, CHECK_OK);
    }
  }
  Expect(Token::RPAREN, CHECK_OK);

  Expect(Token::LBRACE, CHECK_OK);
  int function_block_pos = scanner_->location().beg_pos;

  // Determine if the function will be lazily compiled.
  // Currently only happens to top-level functions.
  // Optimistically assume that all top-level functions are lazily compiled.
  bool is_lazily_compiled =
      (outer_scope_type == kTopLevelScope && !inside_with && allow_lazy_);

  if (is_lazily_compiled) {
    log_->PauseRecording();
    ParseSourceElements(Token::RBRACE, ok);
    log_->ResumeRecording();
    if (!*ok) return kUnknownExpression;

    Expect(Token::RBRACE, CHECK_OK);

    int end_pos = scanner_->location().end_pos;
    log_->LogFunction(function_block_pos, end_pos,
                      function_scope.materialized_literal_count(),
                      function_scope.expected_properties());
  } else {
    ParseSourceElements(Token::RBRACE, CHECK_OK);
    Expect(Token::RBRACE, CHECK_OK);
  }
  return kUnknownExpression;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::ParseV8Intrinsic(bool* ok) {
  // CallRuntime ::
  //   '%' Identifier Arguments

  Expect(Token::MOD, CHECK_OK);
  ParseIdentifier(CHECK_OK);
  ParseArguments(CHECK_OK);

  return kUnknownExpression;
}


template <typename Scanner, typename Log>
void PreParser<Scanner, Log>::ExpectSemicolon(bool* ok) {
  // Check for automatic semicolon insertion according to
  // the rules given in ECMA-262, section 7.9, page 21.
  Token::Value tok = peek();
  if (tok == Token::SEMICOLON) {
    Next();
    return;
  }
  if (scanner_->has_line_terminator_before_next() ||
      tok == Token::RBRACE ||
      tok == Token::EOS) {
    return;
  }
  Expect(Token::SEMICOLON, ok);
}


template <typename Scanner, typename Log>
Identifier PreParser<Scanner, Log>::GetIdentifierSymbol() {
  const char* literal_chars = scanner_->literal_string();
  int literal_length = scanner_->literal_length();
  int identifier_pos = scanner_->location().beg_pos;

  log_->LogSymbol(identifier_pos, literal_chars, literal_length);

  return kUnknownExpression;
}


template <typename Scanner, typename Log>
Expression PreParser<Scanner, Log>::GetStringSymbol() {
  const char* literal_chars = scanner_->literal_string();
  int literal_length = scanner_->literal_length();

  int literal_position = scanner_->location().beg_pos;
  log_->LogSymbol(literal_position, literal_chars, literal_length);

  return kUnknownExpression;
}


template <typename Scanner, typename Log>
Identifier PreParser<Scanner, Log>::ParseIdentifier(bool* ok) {
  Expect(Token::IDENTIFIER, ok);
  return GetIdentifierSymbol();
}


template <typename Scanner, typename Log>
Identifier PreParser<Scanner, Log>::ParseIdentifierName(bool* ok) {
  Token::Value next = Next();
  if (Token::IsKeyword(next)) {
    int pos = scanner_->location().beg_pos;
    const char* keyword = Token::String(next);
    log_->LogSymbol(pos, keyword, strlen(keyword));
    return kUnknownExpression;
  }
  if (next == Token::IDENTIFIER) {
    return GetIdentifierSymbol();
  }
  *ok = false;
  return kUnknownIdentifier;
}


// This function reads an identifier and determines whether or not it
// is 'get' or 'set'.  The reason for not using ParseIdentifier and
// checking on the output is that this involves heap allocation which
// we can't do during preparsing.
template <typename Scanner, typename Log>
Identifier PreParser<Scanner, Log>::ParseIdentifierOrGetOrSet(bool* is_get,
                                                   bool* is_set,
                                                   bool* ok) {
  Expect(Token::IDENTIFIER, CHECK_OK);
  if (scanner_->literal_length() == 3) {
    const char* token = scanner_->literal_string();
    *is_get = strncmp(token, "get", 3) == 0;
    *is_set = !*is_get && strncmp(token, "set", 3) == 0;
  }
  return GetIdentifierSymbol();
}

#undef CHECK_OK
} } }  // v8::internal::preparser

#endif  // V8_PREPARSER_H
