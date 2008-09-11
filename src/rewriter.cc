// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#include "v8.h"

#include "ast.h"
#include "scopes.h"
#include "rewriter.h"

namespace v8 { namespace internal {


class Processor: public Visitor {
 public:
  explicit Processor(VariableProxy* result)
      : result_(result),
        result_assigned_(false),
        is_set_(false),
        in_try_(false) {
  }

  void Process(ZoneList<Statement*>* statements);
  bool result_assigned() const  { return result_assigned_; }

 private:
  VariableProxy* result_;

  // We are not tracking result usage via the result_'s use
  // counts (we leave the accurate computation to the
  // usage analyzer). Instead we simple remember if
  // there was ever an assignment to result_.
  bool result_assigned_;

  // To avoid storing to .result all the time, we eliminate some of
  // the stores by keeping track of whether or not we're sure .result
  // will be overwritten anyway. This is a bit more tricky than what I
  // was hoping for
  bool is_set_;
  bool in_try_;

  Expression* SetResult(Expression* value) {
    result_assigned_ = true;
    return new Assignment(Token::ASSIGN, result_, value, kNoPosition);
  }

  // Node visitors.
#define DEF_VISIT(type) \
  virtual void Visit##type(type* node);
  NODE_LIST(DEF_VISIT)
#undef DEF_VISIT
};


void Processor::Process(ZoneList<Statement*>* statements) {
  for (int i = statements->length() - 1; i >= 0; --i) {
    Visit(statements->at(i));
  }
}


void Processor::VisitBlock(Block* node) {
  // An initializer block is the rewritten form of a variable declaration
  // with initialization expressions. The initializer block contains the
  // list of assignments corresponding to the initialization expressions.
  // While unclear from the spec (ECMA-262, 3rd., 12.2), the value of
  // a variable declaration with initialization expression is 'undefined'
  // with some JS VMs: For instance, using smjs, print(eval('var x = 7'))
  // returns 'undefined'. To obtain the same behavior with v8, we need
  // to prevent rewriting in that case.
  if (!node->is_initializer_block()) Process(node->statements());
}


void Processor::VisitExpressionStatement(ExpressionStatement* node) {
  // Rewrite : <x>; -> .result = <x>;
  if (!is_set_) {
    node->set_expression(SetResult(node->expression()));
    if (!in_try_) is_set_ = true;
  }
}


void Processor::VisitIfStatement(IfStatement* node) {
  // Rewrite both then and else parts (reversed).
  bool save = is_set_;
  Visit(node->else_statement());
  bool set_after_then = is_set_;
  is_set_ = save;
  Visit(node->then_statement());
  is_set_ = is_set_ && set_after_then;
}




void Processor::VisitLoopStatement(LoopStatement* node) {
  // Rewrite loop body statement.
  bool set_after_loop = is_set_;
  Visit(node->body());
  is_set_ = is_set_ && set_after_loop;
}


void Processor::VisitForInStatement(ForInStatement* node) {
  // Rewrite for-in body statement.
  bool set_after_for = is_set_;
  Visit(node->body());
  is_set_ = is_set_ && set_after_for;
}


void Processor::VisitTryCatch(TryCatch* node) {
  // Rewrite both try and catch blocks (reversed order).
  bool set_after_catch = is_set_;
  Visit(node->catch_block());
  is_set_ = is_set_ && set_after_catch;
  bool save = in_try_;
  in_try_ = true;
  Visit(node->try_block());
  in_try_ = save;
}


void Processor::VisitTryFinally(TryFinally* node) {
  // Rewrite both try and finally block (reversed order).
  Visit(node->finally_block());
  bool save = in_try_;
  in_try_ = true;
  Visit(node->try_block());
  in_try_ = save;
}


void Processor::VisitSwitchStatement(SwitchStatement* node) {
  // Rewrite statements in all case clauses in reversed order.
  ZoneList<CaseClause*>* clauses = node->cases();
  bool set_after_switch = is_set_;
  for (int i = clauses->length() - 1; i >= 0; --i) {
    CaseClause* clause = clauses->at(i);
    Process(clause->statements());
  }
  is_set_ = is_set_ && set_after_switch;
}


void Processor::VisitContinueStatement(ContinueStatement* node) {
  is_set_ = false;
}


void Processor::VisitBreakStatement(BreakStatement* node) {
  is_set_ = false;
}


// Do nothing:
void Processor::VisitDeclaration(Declaration* node) {}
void Processor::VisitEmptyStatement(EmptyStatement* node) {}
void Processor::VisitReturnStatement(ReturnStatement* node) {}
void Processor::VisitWithEnterStatement(WithEnterStatement* node) {}
void Processor::VisitWithExitStatement(WithExitStatement* node) {}
void Processor::VisitDebuggerStatement(DebuggerStatement* node) {}


// Expressions are never visited yet.
void Processor::VisitFunctionLiteral(FunctionLiteral* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitFunctionBoilerplateLiteral(
    FunctionBoilerplateLiteral* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitConditional(Conditional* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitSlot(Slot* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitVariableProxy(VariableProxy* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitLiteral(Literal* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitRegExpLiteral(RegExpLiteral* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitArrayLiteral(ArrayLiteral* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitObjectLiteral(ObjectLiteral* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitAssignment(Assignment* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitThrow(Throw* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitProperty(Property* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitCall(Call* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitCallNew(CallNew* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitCallRuntime(CallRuntime* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitUnaryOperation(UnaryOperation* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitCountOperation(CountOperation* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitBinaryOperation(BinaryOperation* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitCompareOperation(CompareOperation* node) {
  USE(node);
  UNREACHABLE();
}


void Processor::VisitThisFunction(ThisFunction* node) {
  USE(node);
  UNREACHABLE();
}


bool Rewriter::Process(FunctionLiteral* function) {
  Scope* scope = function->scope();
  if (scope->is_function_scope()) return true;

  ZoneList<Statement*>* body = function->body();
  if (body->is_empty()) return true;

  VariableProxy* result = scope->NewTemporary(Factory::result_symbol());
  Processor processor(result);
  processor.Process(body);
  if (processor.HasStackOverflow()) return false;

  if (processor.result_assigned()) body->Add(new ReturnStatement(result));
  return true;
}


} }  // namespace v8::internal
