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

namespace v8 { namespace internal {


VariableProxySentinel VariableProxySentinel::this_proxy_(true);
VariableProxySentinel VariableProxySentinel::identifier_proxy_(false);
ValidLeftHandSideSentinel ValidLeftHandSideSentinel::instance_;
Property Property::this_property_(VariableProxySentinel::this_proxy(), NULL, 0);
Call Call::sentinel_(NULL, NULL, false, 0);


// ----------------------------------------------------------------------------
// All the Accept member functions for each syntax tree node type.

#define DECL_ACCEPT(type)                \
  void type::Accept(Visitor* v) {        \
    if (v->CheckStackOverflow()) return; \
    v->Visit##type(this);                \
  }
NODE_LIST(DECL_ACCEPT)
#undef DECL_ACCEPT


// ----------------------------------------------------------------------------
// Implementation of other node functionality.

VariableProxy::VariableProxy(Handle<String> name,
                             bool is_this,
                             bool inside_with)
  : name_(name),
    var_(NULL),
    is_this_(is_this),
    inside_with_(inside_with) {
  // names must be canonicalized for fast equality checks
  ASSERT(name->IsSymbol());
  // at least one access, otherwise no need for a VariableProxy
  var_uses_.RecordAccess(1);
}


VariableProxy::VariableProxy(bool is_this)
  : is_this_(is_this) {
}


void VariableProxy::BindTo(Variable* var) {
  ASSERT(var_ == NULL);  // must be bound only once
  ASSERT(var != NULL);  // must bind
  ASSERT((is_this() && var->is_this()) || name_.is_identical_to(var->name()));
  // Ideally CONST-ness should match. However, this is very hard to achieve
  // because we don't know the exact semantics of conflicting (const and
  // non-const) multiple variable declarations, const vars introduced via
  // eval() etc.  Const-ness and variable declarations are a complete mess
  // in JS. Sigh...
  var_ = var;
  var->var_uses()->RecordUses(&var_uses_);
  var->obj_uses()->RecordUses(&obj_uses_);
}


#ifdef DEBUG

const char* LoopStatement::OperatorString() const {
  switch (type()) {
    case DO_LOOP: return "DO";
    case FOR_LOOP: return "FOR";
    case WHILE_LOOP: return "WHILE";
  }
  return NULL;
}

#endif  // DEBUG


Token::Value Assignment::binary_op() const {
  switch (op_) {
    case Token::ASSIGN_BIT_OR: return Token::BIT_OR;
    case Token::ASSIGN_BIT_XOR: return Token::BIT_XOR;
    case Token::ASSIGN_BIT_AND: return Token::BIT_AND;
    case Token::ASSIGN_SHL: return Token::SHL;
    case Token::ASSIGN_SAR: return Token::SAR;
    case Token::ASSIGN_SHR: return Token::SHR;
    case Token::ASSIGN_ADD: return Token::ADD;
    case Token::ASSIGN_SUB: return Token::SUB;
    case Token::ASSIGN_MUL: return Token::MUL;
    case Token::ASSIGN_DIV: return Token::DIV;
    case Token::ASSIGN_MOD: return Token::MOD;
    default: UNREACHABLE();
  }
  return Token::ILLEGAL;
}


bool FunctionLiteral::AllowsLazyCompilation() {
  return scope()->AllowsLazyCompilation();
}


ObjectLiteral::Property::Property(Literal* key, Expression* value) {
  key_ = key;
  value_ = value;
  Object* k = *key->handle();
  if (k->IsSymbol() && Heap::Proto_symbol()->Equals(String::cast(k))) {
    kind_ = PROTOTYPE;
  } else {
    kind_ = value_->AsLiteral() == NULL ? COMPUTED : CONSTANT;
  }
}


ObjectLiteral::Property::Property(bool is_getter, FunctionLiteral* value) {
  key_ = new Literal(value->name());
  value_ = value;
  kind_ = is_getter ? GETTER : SETTER;
}


void TargetCollector::AddTarget(JumpTarget* target) {
  // Add the label to the collector, but discard duplicates.
  int length = targets_->length();
  for (int i = 0; i < length; i++) {
    if (targets_->at(i) == target) return;
  }
  targets_->Add(target);
}


// ----------------------------------------------------------------------------
// Implementation of Visitor


void Visitor::VisitStatements(ZoneList<Statement*>* statements) {
  for (int i = 0; i < statements->length(); i++) {
    Visit(statements->at(i));
  }
}


void Visitor::VisitExpressions(ZoneList<Expression*>* expressions) {
  for (int i = 0; i < expressions->length(); i++) {
    // The variable statement visiting code may pass NULL expressions
    // to this code. Maybe this should be handled by introducing an
    // undefined expression or literal?  Revisit this code if this
    // changes
    Expression* expression = expressions->at(i);
    if (expression != NULL) Visit(expression);
  }
}


} }  // namespace v8::internal
