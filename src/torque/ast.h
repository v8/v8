// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_AST_H_
#define V8_TORQUE_AST_H_

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "src/base/optional.h"

namespace v8 {
namespace internal {
namespace torque {

enum class SourceId : int {};

struct SourcePosition {
  SourceId source;
  int line;
  int column;
};

#define AST_EXPRESSION_NODE_KIND_LIST(V) \
  V(CallExpression)                      \
  V(LogicalOrExpression)                 \
  V(LogicalAndExpression)                \
  V(ConditionalExpression)               \
  V(IdentifierExpression)                \
  V(StringLiteralExpression)             \
  V(NumberLiteralExpression)             \
  V(FieldAccessExpression)               \
  V(ElementAccessExpression)             \
  V(AssignmentExpression)                \
  V(IncrementDecrementExpression)        \
  V(CastExpression)                      \
  V(ConvertExpression)

#define AST_STATEMENT_NODE_KIND_LIST(V) \
  V(BlockStatement)                     \
  V(ExpressionStatement)                \
  V(IfStatement)                        \
  V(WhileStatement)                     \
  V(ForLoopStatement)                   \
  V(ForOfLoopStatement)                 \
  V(BreakStatement)                     \
  V(ContinueStatement)                  \
  V(ReturnStatement)                    \
  V(DebugStatement)                     \
  V(AssertStatement)                    \
  V(TailCallStatement)                  \
  V(VarDeclarationStatement)            \
  V(GotoStatement)                      \
  V(TryCatchStatement)

#define AST_DECLARATION_NODE_KIND_LIST(V) \
  V(TypeDeclaration)                      \
  V(MacroDeclaration)                     \
  V(ExternalMacroDeclaration)             \
  V(BuiltinDeclaration)                   \
  V(ExternalBuiltinDeclaration)           \
  V(ExternalRuntimeDeclaration)           \
  V(ConstDeclaration)                     \
  V(DefaultModuleDeclaration)             \
  V(ExplicitModuleDeclaration)

#define AST_NODE_KIND_LIST(V)       \
  AST_EXPRESSION_NODE_KIND_LIST(V)  \
  AST_STATEMENT_NODE_KIND_LIST(V)   \
  AST_DECLARATION_NODE_KIND_LIST(V) \
  V(CatchBlock)                     \
  V(LabelBlock)

struct AstNode {
 public:
  enum class Kind {
#define ENUM_ITEM(name) k##name,
    AST_NODE_KIND_LIST(ENUM_ITEM)
#undef ENUM_ITEM
  };

  AstNode(Kind k, SourcePosition p) : kind(k), pos(p) {}
  virtual ~AstNode() {}

  const Kind kind;
  SourcePosition pos;
};

struct AstNodeClassCheck {
  template <class T>
  static bool IsInstanceOf(AstNode* node);
};

// Boilerplate for most derived classes.
#define DEFINE_AST_NODE_LEAF_BOILERPLATE(T)  \
  static const Kind kKind = Kind::k##T;      \
  static T* cast(AstNode* node) {            \
    if (node->kind != kKind) return nullptr; \
    return static_cast<T*>(node);            \
  }

// Boilerplate for classes with subclasses.
#define DEFINE_AST_NODE_INNER_BOILERPLATE(T)          \
  template <class F>                                  \
  static T* cast(F* node) {                           \
    DCHECK(AstNodeClassCheck::IsInstanceOf<T>(node)); \
    return static_cast<T*>(node);                     \
  }

struct Expression : AstNode {
  Expression(Kind k, SourcePosition p) : AstNode(k, p) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(Expression)
};

struct LocationExpression : Expression {
  LocationExpression(Kind k, SourcePosition p) : Expression(k, p) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(LocationExpression)
};

struct Declaration : AstNode {
  Declaration(Kind k, SourcePosition p) : AstNode(k, p) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(Declaration)
};

struct Statement : AstNode {
  Statement(Kind k, SourcePosition p) : AstNode(k, p) {}
  DEFINE_AST_NODE_INNER_BOILERPLATE(Statement)
};

class Module;

struct ModuleDeclaration : Declaration {
  ModuleDeclaration(AstNode::Kind kind, SourcePosition p,
                    std::vector<Declaration*> d)
      : Declaration(kind, p), module(nullptr), declarations(std::move(d)) {}
  virtual bool IsDefault() const = 0;
  //  virtual std::string GetName() const = 0;
  void SetModule(Module* m) { module = m; }
  Module* GetModule() const { return module; }
  Module* module;
  std::vector<Declaration*> declarations;
};

struct DefaultModuleDeclaration : ModuleDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(DefaultModuleDeclaration)
  DefaultModuleDeclaration(SourcePosition p, std::vector<Declaration*> d)
      : ModuleDeclaration(kKind, p, d) {}
  bool IsDefault() const override { return true; }
};

struct ExplicitModuleDeclaration : ModuleDeclaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExplicitModuleDeclaration)
  ExplicitModuleDeclaration(SourcePosition p, std::string n,
                            std::vector<Declaration*> d)
      : ModuleDeclaration(kKind, p, d), name(std::move(n)) {}
  bool IsDefault() const override { return false; }
  std::string name;
};

class SourceFileMap {
 public:
  SourceFileMap() {}
  const std::string& GetSource(SourceId id) const {
    return sources_[static_cast<int>(id)];
  }

  std::string PositionAsString(SourcePosition pos) {
    return GetSource(pos.source) + ":" + std::to_string(pos.line) + ":" +
           std::to_string(pos.column);
  }

 private:
  friend class Ast;
  SourceId AddSource(std::string path) {
    sources_.push_back(std::move(path));
    return static_cast<SourceId>(sources_.size() - 1);
  }
  std::vector<std::string> sources_;
};

class Ast {
 public:
  Ast()
      : default_module_{SourcePosition(), {}},
        source_file_map_(new SourceFileMap()) {}

  std::vector<Declaration*>& declarations() {
    return default_module_.declarations;
  }
  const std::vector<Declaration*>& declarations() const {
    return default_module_.declarations;
  }
  void AddNode(std::unique_ptr<AstNode> node) {
    nodes_.emplace_back(std::move(node));
  }
  SourceId AddSource(std::string path) {
    return source_file_map_->AddSource(path);
  }
  DefaultModuleDeclaration* default_module() { return &default_module_; }
  SourceFileMap* source_file_map() { return &*source_file_map_; }

 private:
  DefaultModuleDeclaration default_module_;
  std::unique_ptr<SourceFileMap> source_file_map_;
  std::vector<std::unique_ptr<AstNode>> nodes_;
};

struct CallExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(CallExpression)
  CallExpression(SourcePosition p, std::string c, bool o,
                 std::vector<Expression*> a, std::vector<std::string> l)
      : Expression(kKind, p),
        callee(std::move(c)),
        is_operator(o),
        arguments(std::move(a)),
        labels(l) {}
  std::string callee;
  bool is_operator;
  std::vector<Expression*> arguments;
  std::vector<std::string> labels;
};

struct LogicalOrExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(LogicalOrExpression)
  LogicalOrExpression(SourcePosition p, Expression* l, Expression* r)
      : Expression(kKind, p), left(l), right(r) {}
  Expression* left;
  Expression* right;
};

struct LogicalAndExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(LogicalAndExpression)
  LogicalAndExpression(SourcePosition p, Expression* l, Expression* r)
      : Expression(kKind, p), left(l), right(r) {}
  Expression* left;
  Expression* right;
};

struct ConditionalExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ConditionalExpression)
  ConditionalExpression(SourcePosition p, Expression* c, Expression* t,
                        Expression* f)
      : Expression(kKind, p), condition(c), if_true(t), if_false(f) {}
  Expression* condition;
  Expression* if_true;
  Expression* if_false;
};

struct IdentifierExpression : LocationExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IdentifierExpression)
  IdentifierExpression(SourcePosition p, std::string n)
      : LocationExpression(kKind, p), name(std::move(n)) {}
  std::string name;
};

struct StringLiteralExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(StringLiteralExpression)
  StringLiteralExpression(SourcePosition p, std::string l)
      : Expression(kKind, p), literal(std::move(l)) {}
  std::string literal;
};

struct NumberLiteralExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(NumberLiteralExpression)
  NumberLiteralExpression(SourcePosition p, std::string n)
      : Expression(kKind, p), number(std::move(n)) {}
  std::string number;
};

struct CastExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(CastExpression)
  CastExpression(SourcePosition p, std::string t, std::string o, Expression* v)
      : Expression(kKind, p), type(std::move(t)), otherwise(o), value(v) {}
  std::string type;
  std::string otherwise;
  Expression* value;
};

struct ConvertExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ConvertExpression)
  ConvertExpression(SourcePosition p, std::string t, Expression* v)
      : Expression(kKind, p), type(std::move(t)), value(v) {}
  std::string type;
  Expression* value;
};

struct ElementAccessExpression : LocationExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ElementAccessExpression)
  ElementAccessExpression(SourcePosition p, Expression* a, Expression* i)
      : LocationExpression(kKind, p), array(a), index(i) {}
  Expression* array;
  Expression* index;
};

struct FieldAccessExpression : LocationExpression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(FieldAccessExpression)
  FieldAccessExpression(SourcePosition p, Expression* o, std::string f)
      : LocationExpression(kKind, p), object(o), field(std::move(f)) {}
  Expression* object;
  std::string field;
};

struct AssignmentExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(AssignmentExpression)
  AssignmentExpression(SourcePosition p, LocationExpression* l,
                       base::Optional<std::string> o, Expression* v)
      : Expression(kKind, p), location(l), op(std::move(o)), value(v) {}
  LocationExpression* location;
  base::Optional<std::string> op;
  Expression* value;
};

enum class IncrementDecrementOperator { kIncrement, kDecrement };

struct IncrementDecrementExpression : Expression {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IncrementDecrementExpression)
  IncrementDecrementExpression(SourcePosition p, LocationExpression* l,
                               IncrementDecrementOperator o, bool pf)
      : Expression(kKind, p), location(l), op(o), postfix(pf) {}
  LocationExpression* location;
  IncrementDecrementOperator op;
  bool postfix;
};

struct ExpressionStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExpressionStatement)
  ExpressionStatement(SourcePosition p, Expression* e)
      : Statement(kKind, p), expression(e) {}
  Expression* expression;
};

struct IfStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(IfStatement)
  IfStatement(SourcePosition p, Expression* c, bool cexpr, Statement* t,
              base::Optional<Statement*> f)
      : Statement(kKind, p),
        condition(c),
        is_constexpr(cexpr),
        if_true(t),
        if_false(f) {}
  Expression* condition;
  bool is_constexpr;
  Statement* if_true;
  base::Optional<Statement*> if_false;
};

struct WhileStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(WhileStatement)
  WhileStatement(SourcePosition p, Expression* c, Statement* b)
      : Statement(kKind, p), condition(c), body(b) {}
  Expression* condition;
  Statement* body;
};

struct ReturnStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ReturnStatement)
  ReturnStatement(SourcePosition p, base::Optional<Expression*> v)
      : Statement(kKind, p), value(v) {}
  base::Optional<Expression*> value;
};

struct DebugStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(DebugStatement)
  DebugStatement(SourcePosition p, const std::string& r, bool n)
      : Statement(kKind, p), reason(r), never_continues(n) {}
  std::string reason;
  bool never_continues;
};

struct AssertStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(AssertStatement)
  AssertStatement(SourcePosition p, Expression* e, const std::string& s)
      : Statement(kKind, p), expression(e), source(s) {}
  Expression* expression;
  std::string source;
};

struct TailCallStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TailCallStatement)
  TailCallStatement(SourcePosition p, CallExpression* c)
      : Statement(kKind, p), call(c) {}
  CallExpression* call;
};

struct VarDeclarationStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(VarDeclarationStatement)
  VarDeclarationStatement(SourcePosition p, std::string n, std::string t,
                          base::Optional<Expression*> i)
      : Statement(kKind, p),
        name(std::move(n)),
        type(std::move(t)),
        initializer(std::move(i)) {}
  std::string name;
  std::string type;
  base::Optional<Expression*> initializer;
};

struct BreakStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(BreakStatement)
  explicit BreakStatement(SourcePosition p) : Statement(kKind, p) {}
};

struct ContinueStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ContinueStatement)
  explicit ContinueStatement(SourcePosition p) : Statement(kKind, p) {}
};

struct GotoStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(GotoStatement)
  GotoStatement(SourcePosition p, std::string l,
                const std::vector<Expression*>& a)
      : Statement(kKind, p), label(std::move(l)), arguments(std::move(a)) {}
  std::string label;
  std::vector<Expression*> arguments;
};

struct ForLoopStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ForLoopStatement)
  ForLoopStatement(SourcePosition p, base::Optional<VarDeclarationStatement*> d,
                   Expression* t, Expression* a, Statement* b)
      : Statement(kKind, p),
        var_declaration(d),
        test(std::move(t)),
        action(std::move(a)),
        body(std::move(b)) {}
  base::Optional<VarDeclarationStatement*> var_declaration;
  Expression* test;
  Expression* action;
  Statement* body;
};

struct ForOfLoopStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ForOfLoopStatement)
  ForOfLoopStatement(SourcePosition p, VarDeclarationStatement* d,
                     Expression* i, base::Optional<Expression*> bg,
                     base::Optional<Expression*> e, Statement* bd)
      : Statement(kKind, p),
        var_declaration(d),
        iterable(std::move(i)),
        begin(std::move(bg)),
        end(std::move(e)),
        body(std::move(bd)) {}
  VarDeclarationStatement* var_declaration;
  Expression* iterable;
  base::Optional<Expression*> begin;
  base::Optional<Expression*> end;
  Statement* body;
};

struct CatchBlock : AstNode {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(CatchBlock)
  CatchBlock(SourcePosition p, const std::string& c, Statement* b)
      : AstNode(kKind, p), caught(std::move(c)), body(std::move(b)) {}
  std::string caught;
  Statement* body;
};

struct ParameterList {
  std::vector<std::string> names;
  std::vector<std::string> types;
  bool has_varargs;
  std::string arguments_variable;
};

struct LabelBlock : AstNode {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(LabelBlock)
  LabelBlock(SourcePosition p, const std::string& l,
             const ParameterList& p_list, Statement* b)
      : AstNode(kKind, p),
        label(std::move(l)),
        parameters(p_list),
        body(std::move(b)) {}
  std::string label;
  ParameterList parameters;
  Statement* body;
};

struct TryCatchStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TryCatchStatement)
  TryCatchStatement(SourcePosition p, Statement* t, std::vector<CatchBlock*> c)
      : Statement(kKind, p), try_block(std::move(t)), catch_blocks(c) {}
  Statement* try_block;
  std::vector<CatchBlock*> catch_blocks;
  std::vector<LabelBlock*> label_blocks;
};

struct BlockStatement : Statement {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(BlockStatement)
  BlockStatement(SourcePosition p, bool d, std::vector<Statement*> s)
      : Statement(kKind, p), deferred(d), statements(std::move(s)) {}
  bool deferred;
  std::vector<Statement*> statements;
};

struct TypeDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(TypeDeclaration)
  TypeDeclaration(SourcePosition p, std::string n,
                  base::Optional<std::string> e, base::Optional<std::string> g)
      : Declaration(kKind, p),
        name(std::move(n)),
        extends(std::move(e)),
        generates(std::move(g)) {}
  std::string name;
  base::Optional<std::string> extends;
  base::Optional<std::string> generates;
  base::Optional<std::string> constexpr_generates;
};

struct LabelAndTypes {
  std::string name;
  std::vector<std::string> types;
};

typedef std::vector<LabelAndTypes> LabelAndTypesVector;

struct MacroDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(MacroDeclaration)
  MacroDeclaration(SourcePosition p, std::string n, ParameterList pl,
                   std::string r, const LabelAndTypesVector& l, Statement* b)
      : Declaration(kKind, p),
        name(std::move(n)),
        parameters(std::move(pl)),
        return_type(std::move(r)),
        labels(std::move(l)),
        body(std::move(b)) {}
  std::string name;
  ParameterList parameters;
  std::string return_type;
  LabelAndTypesVector labels;
  Statement* body;
};

struct ExternalMacroDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExternalMacroDeclaration)
  ExternalMacroDeclaration(SourcePosition p, std::string n, bool i,
                           base::Optional<std::string> o, ParameterList pl,
                           std::string r, const LabelAndTypesVector& l)
      : Declaration(kKind, p),
        name(std::move(n)),
        implicit(i),
        op(std::move(o)),
        parameters(std::move(pl)),
        return_type(std::move(r)),
        labels(std::move(l)) {}
  std::string name;
  bool implicit;
  base::Optional<std::string> op;
  ParameterList parameters;
  std::string return_type;
  LabelAndTypesVector labels;
};

struct BuiltinDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(BuiltinDeclaration)
  BuiltinDeclaration(SourcePosition p, bool j, std::string n, ParameterList pl,
                     std::string r, Statement* b)
      : Declaration(kKind, p),
        javascript_linkage(j),
        name(std::move(n)),
        parameters(std::move(pl)),
        return_type(std::move(r)),
        body(std::move(b)) {}
  bool javascript_linkage;
  std::string name;
  ParameterList parameters;
  std::string return_type;
  Statement* body;
};

struct ExternalBuiltinDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExternalBuiltinDeclaration)
  ExternalBuiltinDeclaration(SourcePosition p, bool j, std::string n,
                             ParameterList pl, std::string r)
      : Declaration(kKind, p),
        javascript_linkage(j),
        name(std::move(n)),
        parameters(std::move(pl)),
        return_type(std::move(r)) {}
  bool javascript_linkage;
  std::string name;
  ParameterList parameters;
  std::string return_type;
};

struct ExternalRuntimeDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ExternalRuntimeDeclaration)
  ExternalRuntimeDeclaration(SourcePosition p, std::string n, ParameterList pl,
                             std::string r)
      : Declaration(kKind, p),
        name(std::move(n)),
        parameters(std::move(pl)),
        return_type(std::move(r)) {}
  std::string name;
  ParameterList parameters;
  std::string return_type;
};

struct ConstDeclaration : Declaration {
  DEFINE_AST_NODE_LEAF_BOILERPLATE(ConstDeclaration)
  ConstDeclaration(SourcePosition p, std::string n, std::string t,
                   std::string l)
      : Declaration(kKind, p),
        name(std::move(n)),
        type(std::move(t)),
        literal(std::move(l)) {}
  std::string name;
  std::string type;
  std::string literal;
};

#define ENUM_ITEM(name)                     \
  case AstNode::Kind::k##name:              \
    return std::is_base_of<T, name>::value; \
    break;

template <class T>
bool AstNodeClassCheck::IsInstanceOf(AstNode* node) {
  switch (node->kind) {
    AST_NODE_KIND_LIST(ENUM_ITEM)
    default:
      UNIMPLEMENTED();
  }
  return true;
}

#undef ENUM_ITEM

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_AST_H_
