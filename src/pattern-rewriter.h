// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PATTERN_MATCHER_H_
#define V8_PATTERN_MATCHER_H_

#include "src/ast.h"
#include "src/parser.h"

namespace v8 {

namespace internal {

class Parser::PatternRewriter : private AstVisitor {
 public:
  struct DeclarationDescriptor {
    Parser* parser;
    Scope* declaration_scope;
    Scope* scope;
    int initializer_position;
    VariableMode mode;
    ZoneList<const AstRawString*>* names;
    bool is_const;
    Block* block;
    bool needs_init;
    int pos;
    Token::Value init_op;
  };

  explicit PatternRewriter(const DeclarationDescriptor* decl,
                           Expression* pattern)
      : decl_(decl),
        pattern_(pattern),
        current_value_(nullptr),
        ok_(nullptr),
        nvars_(nullptr) {}

  PatternRewriter()
      : decl_(nullptr),
        pattern_(nullptr),
        current_value_(nullptr),
        ok_(nullptr),
        nvars_(nullptr) {}

  bool IsSingleVariableBinding() const;
  const AstRawString* SingleName() const;

  void DeclareAndInitializeVariables(Expression* value, int* nvars, bool* ok);

 private:
#define DECLARE_VISIT(type) void Visit##type(v8::internal::type* node) override;
  // Visiting functions for AST nodes make this an AstVisitor.
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT
  virtual void Visit(AstNode* node) override;

  void RecurseIntoSubpattern(AstNode* pattern, Expression* value) {
    Expression* old_value = current_value_;
    current_value_ = value;
    pattern->Accept(this);
    current_value_ = old_value;
  }

  AstNodeFactory* factory() const { return decl_->parser->factory(); }
  AstValueFactory* ast_value_factory() const {
    return decl_->parser->ast_value_factory();
  }
  bool inside_with() const { return decl_->parser->inside_with(); }
  Zone* zone() const { return decl_->parser->zone(); }

  const DeclarationDescriptor* decl_;
  Expression* pattern_;
  Expression* current_value_;
  bool* ok_;
  int* nvars_;
};
}
}  // namespace v8::internal


#endif  // V8_PATTERN_MATCHER_H_
