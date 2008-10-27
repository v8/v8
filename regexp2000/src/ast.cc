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
#include "string-stream.h"

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


void LabelCollector::AddLabel(Label* label) {
  // Add the label to the collector, but discard duplicates.
  int length = labels_->length();
  for (int i = 0; i < length; i++) {
    if (labels_->at(i) == label) return;
  }
  labels_->Add(label);
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


// ----------------------------------------------------------------------------
// Regular expressions

#define MAKE_ACCEPT(Name)                                            \
  void* RegExp##Name::Accept(RegExpVisitor* visitor, void* data) {   \
    return visitor->Visit##Name(this, data);                         \
  }
FOR_EACH_REG_EXP_NODE_TYPE(MAKE_ACCEPT)
#undef MAKE_ACCEPT


RegExpEmpty RegExpEmpty::kInstance;


// Convert regular expression trees to a simple sexp representation.
// This representation should be different from the input grammar
// in as many cases as possible, to make it more difficult for incorrect
// parses to look as correct ones which is likely if the input and
// output formats are alike.
class RegExpUnparser: public RegExpVisitor {
 public:
  RegExpUnparser();
  void VisitCharacterRange(CharacterRange that);
  SmartPointer<char> ToString() { return stream_.ToCString(); }
#define MAKE_CASE(Name) virtual void* Visit##Name(RegExp##Name*, void* data);
  FOR_EACH_REG_EXP_NODE_TYPE(MAKE_CASE)
#undef MAKE_CASE
 private:
  StringStream* stream() { return &stream_; }
  HeapStringAllocator alloc_;
  StringStream stream_;
};


RegExpUnparser::RegExpUnparser() : stream_(&alloc_) {
}


void* RegExpUnparser::VisitDisjunction(RegExpDisjunction* that, void* data) {
  stream()->Add("(|");
  for (int i = 0; i <  that->nodes()->length(); i++) {
    stream()->Add(" ");
    that->nodes()->at(i)->Accept(this, data);
  }
  stream()->Add(")");
  return NULL;
}


void* RegExpUnparser::VisitAlternative(RegExpAlternative* that, void* data) {
  stream()->Add("(:");
  for (int i = 0; i <  that->nodes()->length(); i++) {
    stream()->Add(" ");
    that->nodes()->at(i)->Accept(this, data);
  }
  stream()->Add(")");
  return NULL;
}


void RegExpUnparser::VisitCharacterRange(CharacterRange that) {
  if (that.is_character_class()) {
    stream()->Add("&%c", that.from());
  } else if (that.IsSingleton()) {
    stream()->Add("%c", that.from());
  } else {
    stream()->Add("%c-%c", that.from(), that.to());
  }
}



void* RegExpUnparser::VisitCharacterClass(RegExpCharacterClass* that,
                                          void* data) {
  if (that->is_negated())
    stream()->Add("^");
  stream()->Add("[");
  for (int i = 0; i < that->ranges()->length(); i++) {
    if (i > 0) stream()->Add(" ");
    VisitCharacterRange(that->ranges()->at(i));
  }
  stream()->Add("]");
  return NULL;
}


void* RegExpUnparser::VisitAssertion(RegExpAssertion* that, void* data) {
  switch (that->type()) {
    case RegExpAssertion::START_OF_INPUT:
      stream()->Add("@^i");
      break;
    case RegExpAssertion::END_OF_INPUT:
      stream()->Add("@$i");
      break;
    case RegExpAssertion::START_OF_LINE:
      stream()->Add("@^l");
      break;
    case RegExpAssertion::END_OF_LINE:
      stream()->Add("@$l");
       break;
    case RegExpAssertion::BOUNDARY:
      stream()->Add("@b");
      break;
    case RegExpAssertion::NON_BOUNDARY:
      stream()->Add("@B");
      break;
  }
  return NULL;
}


void* RegExpUnparser::VisitAtom(RegExpAtom* that, void* data) {
  stream()->Add("'%w'", that->data());
  return NULL;
}


void* RegExpUnparser::VisitQuantifier(RegExpQuantifier* that, void* data) {
  stream()->Add("(# %i ", that->min());
  if (that->max() == RegExpQuantifier::kInfinity) {
    stream()->Add("- ");
  } else {
    stream()->Add("%i ", that->max());
  }
  stream()->Add(that->is_greedy() ? "g " : "n ");
  that->body()->Accept(this, data);
  stream()->Add(")");
  return NULL;
}


void* RegExpUnparser::VisitCapture(RegExpCapture* that, void* data) {
  stream()->Add("(^ ");
  that->body()->Accept(this, data);
  stream()->Add(")");
  return NULL;
}


void* RegExpUnparser::VisitLookahead(RegExpLookahead* that, void* data) {
  stream()->Add("(-> ");
  stream()->Add(that->is_positive() ? "+ " : "- ");
  that->body()->Accept(this, data);
  stream()->Add(")");
  return NULL;
}


void* RegExpUnparser::VisitEmpty(RegExpEmpty* that, void* data) {
  stream()->Put('%');
  return NULL;
}


SmartPointer<char> RegExpTree::ToString() {
  RegExpUnparser unparser;
  Accept(&unparser, NULL);
  return unparser.ToString();
}


} }  // namespace v8::internal
