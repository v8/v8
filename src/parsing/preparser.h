// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PARSING_PREPARSER_H
#define V8_PARSING_PREPARSER_H

#include "src/ast/scopes.h"
#include "src/bailout-reason.h"
#include "src/base/hashmap.h"
#include "src/messages.h"
#include "src/parsing/expression-classifier.h"
#include "src/parsing/func-name-inferrer.h"
#include "src/parsing/parser-base.h"
#include "src/parsing/scanner.h"
#include "src/parsing/token.h"

namespace v8 {
namespace internal {


class PreParserIdentifier {
 public:
  PreParserIdentifier() : type_(kUnknownIdentifier) {}
  static PreParserIdentifier Default() {
    return PreParserIdentifier(kUnknownIdentifier);
  }
  static PreParserIdentifier Eval() {
    return PreParserIdentifier(kEvalIdentifier);
  }
  static PreParserIdentifier Arguments() {
    return PreParserIdentifier(kArgumentsIdentifier);
  }
  static PreParserIdentifier Undefined() {
    return PreParserIdentifier(kUndefinedIdentifier);
  }
  static PreParserIdentifier FutureReserved() {
    return PreParserIdentifier(kFutureReservedIdentifier);
  }
  static PreParserIdentifier FutureStrictReserved() {
    return PreParserIdentifier(kFutureStrictReservedIdentifier);
  }
  static PreParserIdentifier Let() {
    return PreParserIdentifier(kLetIdentifier);
  }
  static PreParserIdentifier Static() {
    return PreParserIdentifier(kStaticIdentifier);
  }
  static PreParserIdentifier Yield() {
    return PreParserIdentifier(kYieldIdentifier);
  }
  static PreParserIdentifier Prototype() {
    return PreParserIdentifier(kPrototypeIdentifier);
  }
  static PreParserIdentifier Constructor() {
    return PreParserIdentifier(kConstructorIdentifier);
  }
  static PreParserIdentifier Enum() {
    return PreParserIdentifier(kEnumIdentifier);
  }
  static PreParserIdentifier Await() {
    return PreParserIdentifier(kAwaitIdentifier);
  }
  static PreParserIdentifier Async() {
    return PreParserIdentifier(kAsyncIdentifier);
  }
  bool IsEval() const { return type_ == kEvalIdentifier; }
  bool IsArguments() const { return type_ == kArgumentsIdentifier; }
  bool IsEvalOrArguments() const { return IsEval() || IsArguments(); }
  bool IsUndefined() const { return type_ == kUndefinedIdentifier; }
  bool IsLet() const { return type_ == kLetIdentifier; }
  bool IsStatic() const { return type_ == kStaticIdentifier; }
  bool IsYield() const { return type_ == kYieldIdentifier; }
  bool IsPrototype() const { return type_ == kPrototypeIdentifier; }
  bool IsConstructor() const { return type_ == kConstructorIdentifier; }
  bool IsEnum() const { return type_ == kEnumIdentifier; }
  bool IsAwait() const { return type_ == kAwaitIdentifier; }
  bool IsAsync() const { return type_ == kAsyncIdentifier; }
  bool IsFutureStrictReserved() const {
    return type_ == kFutureStrictReservedIdentifier ||
           type_ == kLetIdentifier || type_ == kStaticIdentifier ||
           type_ == kYieldIdentifier;
  }

  // Allow identifier->name()[->length()] to work. The preparser
  // does not need the actual positions/lengths of the identifiers.
  const PreParserIdentifier* operator->() const { return this; }
  const PreParserIdentifier raw_name() const { return *this; }

  int position() const { return 0; }
  int length() const { return 0; }

 private:
  enum Type {
    kUnknownIdentifier,
    kFutureReservedIdentifier,
    kFutureStrictReservedIdentifier,
    kLetIdentifier,
    kStaticIdentifier,
    kYieldIdentifier,
    kEvalIdentifier,
    kArgumentsIdentifier,
    kUndefinedIdentifier,
    kPrototypeIdentifier,
    kConstructorIdentifier,
    kEnumIdentifier,
    kAwaitIdentifier,
    kAsyncIdentifier
  };

  explicit PreParserIdentifier(Type type) : type_(type) {}
  Type type_;

  friend class PreParserExpression;
};


class PreParserExpression {
 public:
  static PreParserExpression Default() {
    return PreParserExpression(TypeField::encode(kExpression));
  }

  static PreParserExpression Spread(PreParserExpression expression) {
    return PreParserExpression(TypeField::encode(kSpreadExpression));
  }

  static PreParserExpression FromIdentifier(PreParserIdentifier id) {
    return PreParserExpression(TypeField::encode(kIdentifierExpression) |
                               IdentifierTypeField::encode(id.type_));
  }

  static PreParserExpression BinaryOperation(PreParserExpression left,
                                             Token::Value op,
                                             PreParserExpression right) {
    return PreParserExpression(TypeField::encode(kBinaryOperationExpression));
  }

  static PreParserExpression Assignment() {
    return PreParserExpression(TypeField::encode(kExpression) |
                               ExpressionTypeField::encode(kAssignment));
  }

  static PreParserExpression ObjectLiteral() {
    return PreParserExpression(TypeField::encode(kObjectLiteralExpression));
  }

  static PreParserExpression ArrayLiteral() {
    return PreParserExpression(TypeField::encode(kArrayLiteralExpression));
  }

  static PreParserExpression StringLiteral() {
    return PreParserExpression(TypeField::encode(kStringLiteralExpression));
  }

  static PreParserExpression UseStrictStringLiteral() {
    return PreParserExpression(TypeField::encode(kStringLiteralExpression) |
                               IsUseStrictField::encode(true));
  }

  static PreParserExpression UseTypesStringLiteral() {
    return PreParserExpression(TypeField::encode(kStringLiteralExpression) |
                               IsUseTypesField::encode(true));
  }

  static PreParserExpression This() {
    return PreParserExpression(TypeField::encode(kExpression) |
                               ExpressionTypeField::encode(kThisExpression));
  }

  static PreParserExpression ThisProperty() {
    return PreParserExpression(
        TypeField::encode(kExpression) |
        ExpressionTypeField::encode(kThisPropertyExpression));
  }

  static PreParserExpression Property() {
    return PreParserExpression(
        TypeField::encode(kExpression) |
        ExpressionTypeField::encode(kPropertyExpression));
  }

  static PreParserExpression Call() {
    return PreParserExpression(TypeField::encode(kExpression) |
                               ExpressionTypeField::encode(kCallExpression));
  }

  static PreParserExpression CallEval() {
    return PreParserExpression(
        TypeField::encode(kExpression) |
        ExpressionTypeField::encode(kCallEvalExpression));
  }

  static PreParserExpression SuperCallReference() {
    return PreParserExpression(
        TypeField::encode(kExpression) |
        ExpressionTypeField::encode(kSuperCallReference));
  }

  static PreParserExpression NoTemplateTag() {
    return PreParserExpression(
        TypeField::encode(kExpression) |
        ExpressionTypeField::encode(kNoTemplateTagExpression));
  }

  static PreParserExpression Empty() {
    return PreParserExpression(TypeField::encode(kEmptyExpression));
  }

  bool IsEmpty() const { return TypeField::decode(code_) == kEmptyExpression; }

  bool IsIdentifier() const {
    return TypeField::decode(code_) == kIdentifierExpression;
  }

  PreParserIdentifier AsIdentifier() const {
    DCHECK(IsIdentifier());
    return PreParserIdentifier(IdentifierTypeField::decode(code_));
  }

  bool IsAssignment() const {
    return TypeField::decode(code_) == kExpression &&
           ExpressionTypeField::decode(code_) == kAssignment;
  }

  bool IsObjectLiteral() const {
    return TypeField::decode(code_) == kObjectLiteralExpression;
  }

  bool IsArrayLiteral() const {
    return TypeField::decode(code_) == kArrayLiteralExpression;
  }

  bool IsStringLiteral() const {
    return TypeField::decode(code_) == kStringLiteralExpression;
  }

  bool IsUseStrictLiteral() const {
    return TypeField::decode(code_) == kStringLiteralExpression &&
           IsUseStrictField::decode(code_);
  }

  bool IsUseTypesLiteral() const {
    return TypeField::decode(code_) == kStringLiteralExpression &&
           IsUseTypesField::decode(code_);
  }

  bool IsThis() const {
    return TypeField::decode(code_) == kExpression &&
           ExpressionTypeField::decode(code_) == kThisExpression;
  }

  bool IsThisProperty() const {
    return TypeField::decode(code_) == kExpression &&
           ExpressionTypeField::decode(code_) == kThisPropertyExpression;
  }

  bool IsProperty() const {
    return TypeField::decode(code_) == kExpression &&
           (ExpressionTypeField::decode(code_) == kPropertyExpression ||
            ExpressionTypeField::decode(code_) == kThisPropertyExpression);
  }

  bool IsCall() const {
    return TypeField::decode(code_) == kExpression &&
           (ExpressionTypeField::decode(code_) == kCallExpression ||
            ExpressionTypeField::decode(code_) == kCallEvalExpression);
  }

  bool IsDirectEvalCall() const {
    return TypeField::decode(code_) == kExpression &&
           ExpressionTypeField::decode(code_) == kCallEvalExpression;
  }

  bool IsSuperCallReference() const {
    return TypeField::decode(code_) == kExpression &&
           ExpressionTypeField::decode(code_) == kSuperCallReference;
  }

  bool IsValidReferenceExpression() const {
    return IsIdentifier() || IsProperty();
  }

  // At the moment PreParser doesn't track these expression types.
  bool IsFunctionLiteral() const { return false; }
  bool IsCallNew() const { return false; }

  bool IsNoTemplateTag() const {
    return TypeField::decode(code_) == kExpression &&
           ExpressionTypeField::decode(code_) == kNoTemplateTagExpression;
  }

  bool IsSpreadExpression() const {
    return TypeField::decode(code_) == kSpreadExpression;
  }

  PreParserExpression AsFunctionLiteral() { return *this; }

  bool IsBinaryOperation() const {
    return TypeField::decode(code_) == kBinaryOperationExpression;
  }

  // Dummy implementation for making expression->somefunc() work in both Parser
  // and PreParser.
  PreParserExpression* operator->() { return this; }

  // More dummy implementations of things PreParser doesn't need to track:
  void set_index(int index) {}  // For YieldExpressions
  void set_should_eager_compile() {}

  int position() const { return kNoSourcePosition; }
  void set_function_token_position(int position) {}

 private:
  enum Type {
    kExpression,
    kEmptyExpression,
    kIdentifierExpression,
    kStringLiteralExpression,
    kBinaryOperationExpression,
    kSpreadExpression,
    kObjectLiteralExpression,
    kArrayLiteralExpression
  };

  enum ExpressionType {
    kThisExpression,
    kThisPropertyExpression,
    kPropertyExpression,
    kCallExpression,
    kCallEvalExpression,
    kSuperCallReference,
    kNoTemplateTagExpression,
    kAssignment
  };

  explicit PreParserExpression(uint32_t expression_code)
      : code_(expression_code) {}

  // The first three bits are for the Type.
  typedef BitField<Type, 0, 3> TypeField;

  // The high order bit applies only to nodes which would inherit from the
  // Expression ASTNode --- This is by necessity, due to the fact that
  // Expression nodes may be represented as multiple Types, not exclusively
  // through kExpression.
  // TODO(caitp, adamk): clean up PreParserExpression bitfields.
  typedef BitField<bool, 31, 1> ParenthesizedField;

  // The rest of the bits are interpreted depending on the value
  // of the Type field, so they can share the storage.
  typedef BitField<ExpressionType, TypeField::kNext, 3> ExpressionTypeField;
  typedef BitField<bool, TypeField::kNext, 1> IsUseStrictField;
  typedef BitField<bool, IsUseStrictField::kNext, 1> IsUseTypesField;
  typedef BitField<PreParserIdentifier::Type, TypeField::kNext, 10>
      IdentifierTypeField;
  typedef BitField<bool, TypeField::kNext, 1> HasCoverInitializedNameField;

  uint32_t code_;
};


// The pre-parser doesn't need to build lists of expressions, identifiers, or
// the like.
template <typename T>
class PreParserList {
 public:
  // These functions make list->Add(some_expression) work (and do nothing).
  explicit PreParserList(int length = 0) : length_(length) {}
  PreParserList* operator->() { return this; }
  void Add(T, void*) { ++length_; }
  int length() const { return length_; }
 private:
  int length_;
};


typedef PreParserList<PreParserIdentifier> PreParserIdentifierList;
typedef PreParserList<PreParserExpression> PreParserExpressionList;


class PreParserStatement {
 public:
  static PreParserStatement Default() {
    return PreParserStatement(kUnknownStatement);
  }

  static PreParserStatement Jump() {
    return PreParserStatement(kJumpStatement);
  }

  static PreParserStatement FunctionDeclaration() {
    return PreParserStatement(kFunctionDeclaration);
  }

  // Creates expression statement from expression.
  // Preserves being an unparenthesized string literal, possibly
  // "use strict".
  static PreParserStatement ExpressionStatement(
      PreParserExpression expression) {
    if (expression.IsUseStrictLiteral()) {
      return PreParserStatement(kUseStrictExpressionStatement);
    }
    if (expression.IsUseTypesLiteral()) {
      return PreParserStatement(kUseTypesExpressionStatement);
    }
    if (expression.IsStringLiteral()) {
      return PreParserStatement(kStringLiteralExpressionStatement);
    }
    return Default();
  }

  bool IsStringLiteral() {
    return code_ == kStringLiteralExpressionStatement
        || IsUseStrictLiteral() || IsUseTypesLiteral();
  }

  bool IsUseStrictLiteral() {
    return code_ == kUseStrictExpressionStatement;
  }

  bool IsUseTypesLiteral() { return code_ == kUseTypesExpressionStatement; }

  bool IsFunctionDeclaration() {
    return code_ == kFunctionDeclaration;
  }

  bool IsJumpStatement() {
    return code_ == kJumpStatement;
  }

 private:
  enum Type {
    kUnknownStatement,
    kJumpStatement,
    kStringLiteralExpressionStatement,
    kUseStrictExpressionStatement,
    kFunctionDeclaration,
    kUseTypesExpressionStatement
  };

  explicit PreParserStatement(Type code) : code_(code) {}
  Type code_;
};


typedef PreParserList<PreParserStatement> PreParserStatementList;


namespace typesystem {


enum PreParserTypeInfoEnum {
  kValidNone = 0,
  kValidType = 1 << 0,
  kValidBindingIdentifier = 1 << 1,
  kValidBindingPattern = 1 << 2,
  kStringLiteralType = 1 << 3,
  kValidBindingIdentifierOrPattern =
      kValidBindingIdentifier | kValidBindingPattern
};

typedef base::Flags<PreParserTypeInfoEnum> PreParserTypeInfo;


class PreParserTypeBase {
 public:
  PreParserTypeInfo type_info() const { return type_info_; }

  bool IsValidType() const { return is_valid(kValidType); }
  bool IsValidBindingIdentifier() const {
    return is_valid(kValidBindingIdentifier);
  }
  bool IsValidBindingIdentifierOrPattern() const {
    return is_valid(kValidBindingIdentifierOrPattern);
  }
  bool IsStringLiteralType() const { return is_valid(kStringLiteralType); }

 protected:
  explicit PreParserTypeBase(PreParserTypeInfo info) : type_info_(info) {}

  bool is_valid(PreParserTypeInfo info) const { return type_info_ & info; }

 private:
  PreParserTypeInfo type_info_;
};


class PreParserType;


class PreParserTypeParameter {
 public:
  static PreParserTypeParameter Default() { return PreParserTypeParameter(); }

 private:
  PreParserTypeParameter() {}
};


class PreParserFormalParameter : public PreParserTypeBase {
 public:
  static PreParserFormalParameter Named() {
    return PreParserFormalParameter(kValidNone);
  }
  V8_INLINE static PreParserFormalParameter Unnamed(const PreParserType& type);

 private:
  explicit PreParserFormalParameter(PreParserTypeInfo info)
      : PreParserTypeBase(info) {}
};


typedef PreParserList<PreParserTypeParameter> PreParserTypeParameters;


class PreParserFormalParameters
    : public PreParserList<PreParserFormalParameter> {
 public:
  explicit PreParserFormalParameters(bool valid = false, int arity = 0)
      : PreParserList<PreParserFormalParameter>(arity), valid_type_(valid) {}

  PreParserFormalParameters* operator->() { return this; }

  void Add(const PreParserFormalParameter& param, void* dummy) {
    PreParserList<PreParserFormalParameter>::Add(param, dummy);
    valid_type_ = length() == 1 && param.IsValidType();
  }

  bool IsValidType() const { return valid_type_; }

 private:
  bool valid_type_;
};


class PreParserType : public PreParserTypeBase {
 public:
  static PreParserType Default(bool valid_type = true,
                               bool valid_binding_identifier = false,
                               bool valid_binding_pattern = false) {
    return PreParserType(PreParserTypeInfo(
        (valid_type ? kValidType : kValidNone) |
        (valid_binding_identifier ? kValidBindingIdentifier : kValidNone) |
        (valid_binding_pattern ? kValidBindingPattern : kValidNone)));
  }
  static PreParserType Reference(bool simple) {
    return PreParserType(PreParserTypeInfo(
        simple ? (kValidType | kValidBindingIdentifier) : kValidType));
  }
  static PreParserType Parenthesized(bool valid_type, int arity) {
    return PreParserType(
        PreParserTypeInfo(valid_type ? kValidType : kValidNone), arity);
  }
  static PreParserType StringLiteral() {
    return PreParserType(PreParserTypeInfo(kValidType | kStringLiteralType));
  }

  // Dummy implementation for making type->somefunc() work in both Parser
  // and PreParser.
  PreParserType* operator->() { return this; }

  PreParserType Uncover(bool* ok) {
    *ok = IsValidType();
    return *this;
  }

  PreParserFormalParameters AsValidParameterList(Zone* zone, bool* ok) const {
    if (arity_ >= 0) return PreParserFormalParameters(arity_);
    *ok = false;
    return PreParserFormalParameters();
  }

 private:
  explicit PreParserType(PreParserTypeInfo info, int arity = -1)
      : PreParserTypeBase(info), arity_(arity) {}

  int arity_;
};


typedef PreParserList<PreParserType> PreParserTypeList;


class PreParserTypeMember : public PreParserTypeBase {
 public:
  static PreParserTypeMember Default(bool valid_type, bool valid_binder) {
    return PreParserTypeMember(PreParserTypeInfo(
        (valid_type ? kValidType : kValidNone) |
        (valid_binder ? kValidBindingIdentifierOrPattern : kValidNone)));
  }
  static PreParserTypeMember IndexSignature() {
    return PreParserTypeMember(kValidType);
  }

  // Dummy implementation for making type_member->somefunc() work in both
  // Parser and PreParser.
  PreParserTypeMember* operator->() { return this; }

 private:
  explicit PreParserTypeMember(PreParserTypeInfo info)
      : PreParserTypeBase(info) {}
};


typedef PreParserList<PreParserTypeMember> PreParserTypeMembers;


V8_INLINE PreParserFormalParameter
PreParserFormalParameter::Unnamed(const PreParserType& type) {
  return PreParserFormalParameter(type.type_info());
}


}  // namespace typesystem


class PreParserFactory {
 public:
  explicit PreParserFactory(void* unused_value_factory) {}
  PreParserExpression NewStringLiteral(PreParserIdentifier identifier,
                                       int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewNumberLiteral(double number,
                                       int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewRegExpLiteral(PreParserIdentifier js_pattern,
                                       int js_flags, int literal_index,
                                       int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewArrayLiteral(PreParserExpressionList values,
                                      int literal_index,
                                      int pos) {
    return PreParserExpression::ArrayLiteral();
  }
  PreParserExpression NewArrayLiteral(PreParserExpressionList values,
                                      int first_spread_index, int literal_index,
                                      int pos) {
    return PreParserExpression::ArrayLiteral();
  }
  PreParserExpression NewObjectLiteralProperty(PreParserExpression key,
                                               PreParserExpression value,
                                               ObjectLiteralProperty::Kind kind,
                                               bool is_static,
                                               bool is_computed_name) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewObjectLiteralProperty(PreParserExpression key,
                                               PreParserExpression value,
                                               bool is_static,
                                               bool is_computed_name) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewObjectLiteral(PreParserExpressionList properties,
                                       int literal_index,
                                       int boilerplate_properties,
                                       int pos) {
    return PreParserExpression::ObjectLiteral();
  }
  PreParserExpression NewVariableProxy(void* variable) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewProperty(PreParserExpression obj,
                                  PreParserExpression key,
                                  int pos) {
    if (obj.IsThis()) {
      return PreParserExpression::ThisProperty();
    }
    return PreParserExpression::Property();
  }
  PreParserExpression NewUnaryOperation(Token::Value op,
                                        PreParserExpression expression,
                                        int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewBinaryOperation(Token::Value op,
                                         PreParserExpression left,
                                         PreParserExpression right, int pos) {
    return PreParserExpression::BinaryOperation(left, op, right);
  }
  PreParserExpression NewCompareOperation(Token::Value op,
                                          PreParserExpression left,
                                          PreParserExpression right, int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewRewritableExpression(PreParserExpression expression) {
    return expression;
  }
  PreParserExpression NewAssignment(Token::Value op,
                                    PreParserExpression left,
                                    PreParserExpression right,
                                    int pos) {
    return PreParserExpression::Assignment();
  }
  PreParserExpression NewYield(PreParserExpression generator_object,
                               PreParserExpression expression, int pos,
                               Yield::OnException on_exception) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewConditional(PreParserExpression condition,
                                     PreParserExpression then_expression,
                                     PreParserExpression else_expression,
                                     int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewCountOperation(Token::Value op,
                                        bool is_prefix,
                                        PreParserExpression expression,
                                        int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewCall(PreParserExpression expression,
                              PreParserExpressionList arguments,
                              int pos) {
    if (expression.IsIdentifier() && expression.AsIdentifier().IsEval()) {
      return PreParserExpression::CallEval();
    }
    return PreParserExpression::Call();
  }
  PreParserExpression NewCallNew(PreParserExpression expression,
                                 PreParserExpressionList arguments,
                                 int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewCallRuntime(const AstRawString* name,
                                     const Runtime::Function* function,
                                     PreParserExpressionList arguments,
                                     int pos) {
    return PreParserExpression::Default();
  }
  PreParserStatement NewEmptyStatement(int pos) {
    return PreParserStatement::Default();
  }
  PreParserStatement NewReturnStatement(PreParserExpression expression,
                                        int pos) {
    return PreParserStatement::Default();
  }
  PreParserExpression NewFunctionLiteral(
      PreParserIdentifier name, Scope* scope, PreParserStatementList body,
      int materialized_literal_count, int expected_property_count,
      int parameter_count,
      FunctionLiteral::ParameterFlag has_duplicate_parameters,
      FunctionLiteral::FunctionType function_type,
      FunctionLiteral::EagerCompileHint eager_compile_hint, FunctionKind kind,
      int position) {
    return PreParserExpression::Default();
  }

  PreParserExpression NewSpread(PreParserExpression expression, int pos,
                                int expr_pos) {
    return PreParserExpression::Spread(expression);
  }

  PreParserExpression NewEmptyParentheses(int pos) {
    return PreParserExpression::Default();
  }

  typesystem::PreParserType NewPredefinedType(
      typesystem::PredefinedType::Kind kind, int pos) {
    return typesystem::PreParserType::Default(
        true, kind != typesystem::PredefinedType::kVoidType);
  }

  typesystem::PreParserType NewThisType(int pos) {
    return typesystem::PreParserType::Default();
  }

  typesystem::PreParserType NewUnionType(const typesystem::PreParserType& left,
                                         const typesystem::PreParserType& right,
                                         int pos) {
    return typesystem::PreParserType::Default();
  }

  typesystem::PreParserType NewIntersectionType(
      const typesystem::PreParserType& left,
      const typesystem::PreParserType& right, int pos) {
    return typesystem::PreParserType::Default();
  }

  typesystem::PreParserType NewArrayType(const typesystem::PreParserType& base,
                                         int pos) {
    return typesystem::PreParserType::Default();
  }

  typesystem::PreParserType NewTupleType(
      const typesystem::PreParserTypeList& elements, bool valid_type,
      bool valid_binder, bool spread, int pos) {
    return typesystem::PreParserType::Default(valid_type, valid_binder);
  }

  typesystem::PreParserType NewObjectType(
      const typesystem::PreParserTypeMembers& members, bool valid_type,
      bool valid_binder, int pos) {
    return typesystem::PreParserType::Default(valid_type, valid_binder);
  }

  typesystem::PreParserType NewFunctionType(
      const typesystem::PreParserTypeParameters& type_parameters,
      const typesystem::PreParserFormalParameters& parameters,
      const typesystem::PreParserType& result_type, int pos,
      bool constructor = false) {
    return typesystem::PreParserType::Default();
  }

  typesystem::PreParserType NewStringLiteralType(
      const PreParserIdentifier& string, int pos) {
    return typesystem::PreParserType::StringLiteral();
  }

  typesystem::PreParserType NewTypeReference(
      const PreParserIdentifier& name,
      const typesystem::PreParserTypeList& type_arguments, int pos) {
    return typesystem::PreParserType::Reference(type_arguments.length() == 0);
  }

  typesystem::PreParserType NewQueryType(
      const PreParserIdentifier& name,
      const PreParserIdentifierList& property_names, int pos) {
    return typesystem::PreParserType::Default();
  }

  typesystem::PreParserFormalParameter NewFormalParameter(
      const typesystem::PreParserType& binder, bool optional, bool spread,
      const typesystem::PreParserType& type, int pos) {
    return typesystem::PreParserFormalParameter::Named();
  }

  typesystem::PreParserFormalParameter NewFormalParameter(
      const typesystem::PreParserType& type, int pos) {
    return typesystem::PreParserFormalParameter::Unnamed(type);
  }

  typesystem::PreParserType NewTypeOrParameters(
      const typesystem::PreParserFormalParameters& parameters, int pos) {
    return typesystem::PreParserType::Parenthesized(parameters.IsValidType(),
                                                    parameters.length());
  }

  typesystem::PreParserTypeParameter NewTypeParameter(
      const PreParserIdentifier& name, const typesystem::PreParserType& extends,
      int pos) {
    return typesystem::PreParserTypeParameter::Default();
  }

  typesystem::PreParserTypeMember NewTypeMember(
      const PreParserExpression& property, bool optional,
      const typesystem::PreParserTypeParameters& type_parameters,
      const typesystem::PreParserFormalParameters& parameters,
      const typesystem::PreParserType& result_type, bool valid_type,
      bool valid_binder, int pos, bool constructor = false) {
    return typesystem::PreParserTypeMember::Default(valid_type, valid_binder);
  }

  typesystem::PreParserTypeMember NewTypeMember(
      const PreParserExpression& property,
      typesystem::TypeMember::IndexType index_type,
      const typesystem::PreParserType& result_type, int pos) {
    return typesystem::PreParserTypeMember::IndexSignature();
  }

  // Return the object itself as AstVisitor and implement the needed
  // dummy method right in this class.
  PreParserFactory* visitor() { return this; }
  int* ast_properties() {
    static int dummy = 42;
    return &dummy;
  }
};


struct PreParserFormalParameters : FormalParametersBase {
  explicit PreParserFormalParameters(Scope* scope)
      : FormalParametersBase(scope) {}
  int arity = 0;

  int Arity() const { return arity; }
  PreParserIdentifier at(int i) { return PreParserIdentifier(); }  // Dummy
};


class PreParser;

class PreParserTraits {
 public:
  struct Type {
    // TODO(marja): To be removed. The Traits object should contain all the data
    // it needs.
    typedef PreParser* Parser;

    // PreParser doesn't need to store generator variables.
    typedef void GeneratorVariable;

    typedef int AstProperties;

    typedef v8::internal::ExpressionClassifier<PreParserTraits>
        ExpressionClassifier;

    // Return types for traversing functions.
    typedef PreParserIdentifier Identifier;
    typedef PreParserIdentifierList IdentifierList;
    typedef PreParserExpression Expression;
    typedef PreParserExpression YieldExpression;
    typedef PreParserExpression FunctionLiteral;
    typedef PreParserExpression ClassLiteral;
    typedef PreParserExpression ObjectLiteralProperty;
    typedef PreParserExpression Literal;
    typedef PreParserExpressionList ExpressionList;
    typedef PreParserExpressionList PropertyList;
    typedef PreParserIdentifier FormalParameter;
    typedef PreParserFormalParameters FormalParameters;
    typedef PreParserStatement Statement;
    typedef PreParserStatementList StatementList;

    struct TypeSystem {
      typedef typesystem::PreParserType Type;
      typedef typesystem::PreParserTypeList TypeList;
      typedef typesystem::PreParserTypeParameter TypeParameter;
      typedef typesystem::PreParserTypeParameters TypeParameters;
      typedef typesystem::PreParserFormalParameter FormalParameter;
      typedef typesystem::PreParserFormalParameters FormalParameters;
      typedef typesystem::PreParserTypeMember TypeMember;
      typedef typesystem::PreParserTypeMembers TypeMembers;
    };

    // For constructing objects returned by the traversing functions.
    typedef PreParserFactory Factory;
  };

  explicit PreParserTraits(PreParser* pre_parser) : pre_parser_(pre_parser) {}

  // Helper functions for recursive descent.
  static bool IsEval(PreParserIdentifier identifier) {
    return identifier.IsEval();
  }

  static bool IsArguments(PreParserIdentifier identifier) {
    return identifier.IsArguments();
  }

  static bool IsAwait(PreParserIdentifier identifier) {
    return identifier.IsAwait();
  }

  static bool IsAsync(PreParserIdentifier identifier) {
    return identifier.IsAsync();
  }

  static bool IsEvalOrArguments(PreParserIdentifier identifier) {
    return identifier.IsEvalOrArguments();
  }

  static bool IsUndefined(PreParserIdentifier identifier) {
    return identifier.IsUndefined();
  }

  static bool IsPrototype(PreParserIdentifier identifier) {
    return identifier.IsPrototype();
  }

  static bool IsConstructor(PreParserIdentifier identifier) {
    return identifier.IsConstructor();
  }

  // Returns true if the expression is of type "this.foo".
  static bool IsThisProperty(PreParserExpression expression) {
    return expression.IsThisProperty();
  }

  static bool IsIdentifier(PreParserExpression expression) {
    return expression.IsIdentifier();
  }

  static PreParserIdentifier AsIdentifier(PreParserExpression expression) {
    return expression.AsIdentifier();
  }

  static bool IsEvalIdentifier(PreParserExpression expression) {
    return IsIdentifier(expression) && IsEval(AsIdentifier(expression));
  }

  static bool IsDirectEvalCall(PreParserExpression expression) {
    return expression.IsDirectEvalCall();
  }

  static bool IsFutureStrictReserved(PreParserIdentifier identifier) {
    return identifier.IsFutureStrictReserved();
  }

  static bool IsBoilerplateProperty(PreParserExpression property) {
    // PreParser doesn't count boilerplate properties.
    return false;
  }

  static bool IsArrayIndex(PreParserIdentifier string, uint32_t* index) {
    return false;
  }

  static PreParserExpression GetPropertyValue(PreParserExpression property) {
    return PreParserExpression::Default();
  }

  // Functions for encapsulating the differences between parsing and preparsing;
  // operations interleaved with the recursive descent.
  static void PushLiteralName(FuncNameInferrer* fni, PreParserIdentifier id) {
    // PreParser should not use FuncNameInferrer.
    UNREACHABLE();
  }

  static void PushPropertyName(FuncNameInferrer* fni,
                               PreParserExpression expression) {
    // PreParser should not use FuncNameInferrer.
    UNREACHABLE();
  }

  static void InferFunctionName(FuncNameInferrer* fni,
                                PreParserExpression expression) {
    // PreParser should not use FuncNameInferrer.
    UNREACHABLE();
  }

  static void CheckAssigningFunctionLiteralToProperty(
      PreParserExpression left, PreParserExpression right) {}

  static PreParserExpression MarkExpressionAsAssigned(
      PreParserExpression expression) {
    // TODO(marja): To be able to produce the same errors, the preparser needs
    // to start tracking which expressions are variables and which are assigned.
    return expression;
  }

  bool ShortcutNumericLiteralBinaryExpression(PreParserExpression* x,
                                              PreParserExpression y,
                                              Token::Value op,
                                              int pos,
                                              PreParserFactory* factory) {
    return false;
  }

  PreParserExpression BuildUnaryExpression(PreParserExpression expression,
                                           Token::Value op, int pos,
                                           PreParserFactory* factory) {
    return PreParserExpression::Default();
  }

  PreParserExpression BuildIteratorResult(PreParserExpression value,
                                          bool done) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewThrowReferenceError(MessageTemplate::Template message,
                                             int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewThrowSyntaxError(MessageTemplate::Template message,
                                          Handle<Object> arg, int pos) {
    return PreParserExpression::Default();
  }
  PreParserExpression NewThrowTypeError(MessageTemplate::Template message,
                                        Handle<Object> arg, int pos) {
    return PreParserExpression::Default();
  }

  // Reporting errors.
  void ReportMessageAt(Scanner::Location location,
                       MessageTemplate::Template message,
                       const char* arg = NULL,
                       ParseErrorType error_type = kSyntaxError);
  void ReportMessageAt(int start_pos, int end_pos,
                       MessageTemplate::Template message,
                       const char* arg = NULL,
                       ParseErrorType error_type = kSyntaxError);

  // "null" return type creators.
  static PreParserIdentifier EmptyIdentifier() {
    return PreParserIdentifier::Default();
  }
  static PreParserIdentifier EmptyIdentifierString() {
    return PreParserIdentifier::Default();
  }
  static PreParserExpression EmptyExpression() {
    return PreParserExpression::Empty();
  }
  static PreParserExpression EmptyLiteral() {
    return PreParserExpression::Default();
  }
  static PreParserExpression EmptyObjectLiteralProperty() {
    return PreParserExpression::Default();
  }
  static PreParserExpression EmptyFunctionLiteral() {
    return PreParserExpression::Default();
  }
  static PreParserExpressionList NullExpressionList() {
    return PreParserExpressionList();
  }
  static typesystem::PreParserType EmptyType() {
    return typesystem::PreParserType::Default(false);
  }
  static typesystem::PreParserTypeList NullTypeList() {
    return typesystem::PreParserTypeList();
  }
  static typesystem::PreParserTypeParameters NullTypeParameters() {
    return typesystem::PreParserTypeParameters();
  }
  static bool IsNullTypeParameters(
      const typesystem::PreParserTypeParameters& typ_pars) {
    return typ_pars.length() == 0;
  }
  static typesystem::PreParserTypeList EmptyTypeList() {
    return typesystem::PreParserTypeList();
  }
  static typesystem::PreParserTypeParameters EmptyTypeParameters() {
    return typesystem::PreParserTypeParameters();
  }
  static typesystem::PreParserFormalParameters EmptyFormalParameters() {
    return typesystem::PreParserFormalParameters();
  }
  static typesystem::PreParserFormalParameters NullFormalParameters() {
    return typesystem::PreParserFormalParameters();
  }
  static PreParserIdentifierList NullIdentifierList() {
    return PreParserIdentifierList();
  }
  static PreParserIdentifierList EmptyIdentifierList() {
    return PreParserIdentifierList();
  }
  static typesystem::PreParserType HoleTypeElement() {
    return typesystem::PreParserType::Default(false);
  }
  static typesystem::PreParserTypeMembers EmptyTypeMembers() {
    return typesystem::PreParserTypeMembers();
  }
  static typesystem::PreParserTypeMember EmptyTypeMember() {
    return typesystem::PreParserTypeMember::Default(false, false);
  }
  static bool IsEmptyExpression(const PreParserExpression& expression) {
    return expression.IsEmpty();
  }

  // Odd-ball literal creators.
  static PreParserExpression GetLiteralTheHole(int position,
                                               PreParserFactory* factory) {
    return PreParserExpression::Default();
  }

  // Producing data during the recursive descent.
  PreParserIdentifier GetSymbol(Scanner* scanner);
  PreParserIdentifier GetNumberAsSymbol(Scanner* scanner);

  static PreParserIdentifier GetNextSymbol(Scanner* scanner) {
    return PreParserIdentifier::Default();
  }

  static PreParserExpression ThisExpression(Scope* scope,
                                            PreParserFactory* factory,
                                            int pos) {
    return PreParserExpression::This();
  }

  static PreParserExpression NewSuperPropertyReference(
      Scope* scope, PreParserFactory* factory, int pos) {
    return PreParserExpression::Default();
  }

  static PreParserExpression NewSuperCallReference(Scope* scope,
                                                   PreParserFactory* factory,
                                                   int pos) {
    return PreParserExpression::SuperCallReference();
  }

  static PreParserExpression NewTargetExpression(Scope* scope,
                                                 PreParserFactory* factory,
                                                 int pos) {
    return PreParserExpression::Default();
  }

  static PreParserExpression FunctionSentExpression(Scope* scope,
                                                    PreParserFactory* factory,
                                                    int pos) {
    return PreParserExpression::Default();
  }

  static PreParserExpression ExpressionFromLiteral(
      Token::Value token, int pos, Scanner* scanner,
      PreParserFactory* factory) {
    return PreParserExpression::Default();
  }

  static PreParserExpression ExpressionFromIdentifier(
      PreParserIdentifier name, int start_position, int end_position,
      Scope* scope, PreParserFactory* factory) {
    return PreParserExpression::FromIdentifier(name);
  }

  PreParserExpression ExpressionFromString(int pos,
                                           Scanner* scanner,
                                           PreParserFactory* factory = NULL);

  PreParserExpression GetIterator(PreParserExpression iterable,
                                  PreParserFactory* factory, int pos) {
    return PreParserExpression::Default();
  }

  static PreParserExpressionList NewExpressionList(int size, Zone* zone) {
    return PreParserExpressionList();
  }

  static PreParserStatementList NewStatementList(int size, Zone* zone) {
    return PreParserStatementList();
  }

  static PreParserExpressionList NewPropertyList(int size, Zone* zone) {
    return PreParserExpressionList();
  }

  static void AddParameterInitializationBlock(
      const PreParserFormalParameters& parameters, PreParserStatementList list,
      bool is_async, bool* ok) {}

  V8_INLINE void SkipLazyFunctionBody(int* materialized_literal_count,
                                      int* expected_property_count, bool* ok) {
    UNREACHABLE();
  }

  V8_INLINE PreParserStatementList ParseEagerFunctionBody(
      PreParserIdentifier function_name, int pos,
      const PreParserFormalParameters& parameters, FunctionKind kind,
      FunctionLiteral::FunctionType function_type, bool* ok);

  V8_INLINE void ParseArrowFunctionFormalParameterList(
      PreParserFormalParameters* parameters,
      PreParserExpression expression, const Scanner::Location& params_loc,
      Scanner::Location* duplicate_loc, bool* ok);

  void ParseAsyncArrowSingleExpressionBody(
      PreParserStatementList body, bool accept_IN,
      Type::ExpressionClassifier* classifier, int pos, bool* ok);

  V8_INLINE PreParserExpression ParseAsyncFunctionExpression(bool* ok);

  void ReindexLiterals(const PreParserFormalParameters& paramaters) {}

  struct TemplateLiteralState {};

  TemplateLiteralState OpenTemplateLiteral(int pos) {
    return TemplateLiteralState();
  }
  void AddTemplateSpan(TemplateLiteralState*, bool) {}
  void AddTemplateExpression(TemplateLiteralState*, PreParserExpression) {}
  PreParserExpression CloseTemplateLiteral(TemplateLiteralState*, int,
                                           PreParserExpression tag) {
    if (IsTaggedTemplate(tag)) {
      // Emulate generation of array literals for tag callsite
      // 1st is array of cooked strings, second is array of raw strings
      MaterializeTemplateCallsiteLiterals();
    }
    return EmptyExpression();
  }
  inline void MaterializeTemplateCallsiteLiterals();
  PreParserExpression NoTemplateTag() {
    return PreParserExpression::NoTemplateTag();
  }
  static bool IsTaggedTemplate(const PreParserExpression tag) {
    return !tag.IsNoTemplateTag();
  }

  void AddFormalParameter(PreParserFormalParameters* parameters,
                          PreParserExpression pattern,
                          PreParserExpression initializer,
                          int initializer_end_position, bool is_rest) {
    ++parameters->arity;
  }
  void DeclareFormalParameter(Scope* scope, PreParserIdentifier parameter,
                              Type::ExpressionClassifier* classifier) {
    if (!classifier->is_simple_parameter_list()) {
      scope->SetHasNonSimpleParameters();
    }
  }

  void CheckConflictingVarDeclarations(Scope* scope, bool* ok) {}

  // Temporary glue; these functions will move to ParserBase.
  PreParserExpression ParseV8Intrinsic(bool* ok);
  V8_INLINE PreParserExpression ParseDoExpression(bool* ok);
  PreParserExpression ParseFunctionLiteral(
      PreParserIdentifier name, Scanner::Location function_name_location,
      FunctionNameValidity function_name_validity, FunctionKind kind,
      int function_token_position, FunctionLiteral::FunctionType type,
      LanguageMode language_mode, bool is_typed,
      typesystem::TypeFlags type_flags, bool* ok);

  PreParserExpression ParseClassLiteral(Type::ExpressionClassifier* classifier,
                                        PreParserIdentifier name,
                                        Scanner::Location class_name_location,
                                        bool name_is_strict_reserved, int pos,
                                        bool ambient, bool* ok);

  V8_INLINE void MarkCollectedTailCallExpressions() {}
  V8_INLINE void MarkTailPosition(PreParserExpression) {}

  PreParserExpressionList PrepareSpreadArguments(PreParserExpressionList list) {
    return list;
  }

  inline void MaterializeUnspreadArgumentsLiterals(int count);

  inline PreParserExpression SpreadCall(PreParserExpression function,
                                        PreParserExpressionList args, int pos);

  inline PreParserExpression SpreadCallNew(PreParserExpression function,
                                           PreParserExpressionList args,
                                           int pos);

  inline PreParserExpression ExpressionListToExpression(
      PreParserExpressionList args) {
    return PreParserExpression::Default();
  }

  inline void RewriteDestructuringAssignments() {}

  inline PreParserExpression RewriteExponentiation(PreParserExpression left,
                                                   PreParserExpression right,
                                                   int pos) {
    return left;
  }
  inline PreParserExpression RewriteAssignExponentiation(
      PreParserExpression left, PreParserExpression right, int pos) {
    return left;
  }

  inline void QueueDestructuringAssignmentForRewriting(PreParserExpression) {}
  inline void QueueNonPatternForRewriting(PreParserExpression, bool* ok) {}

  void SetFunctionNameFromPropertyName(PreParserExpression,
                                       PreParserIdentifier) {}
  void SetFunctionNameFromIdentifierRef(PreParserExpression,
                                        PreParserExpression) {}

  inline void RewriteNonPattern(Type::ExpressionClassifier* classifier,
                                bool* ok);

  inline PreParserExpression RewriteAwaitExpression(PreParserExpression value,
                                                    int pos);

  V8_INLINE ZoneList<typename Type::ExpressionClassifier::Error>*
      GetReportedErrorList() const;
  V8_INLINE Zone* zone() const;
  V8_INLINE ZoneList<PreParserExpression>* GetNonPatternList() const;

  inline PreParserExpression RewriteYieldStar(
      PreParserExpression generator, PreParserExpression expr, int pos);

 private:
  PreParser* pre_parser_;
};


// Preparsing checks a JavaScript program and emits preparse-data that helps
// a later parsing to be faster.
// See preparse-data-format.h for the data format.

// The PreParser checks that the syntax follows the grammar for JavaScript,
// and collects some information about the program along the way.
// The grammar check is only performed in order to understand the program
// sufficiently to deduce some information about it, that can be used
// to speed up later parsing. Finding errors is not the goal of pre-parsing,
// rather it is to speed up properly written and correct programs.
// That means that contextual checks (like a label being declared where
// it is used) are generally omitted.
class PreParser : public ParserBase<PreParserTraits> {
 public:
  typedef PreParserIdentifier Identifier;
  typedef PreParserExpression Expression;
  typedef PreParserStatement Statement;

  enum PreParseResult {
    kPreParseStackOverflow,
    kPreParseSuccess
  };

  PreParser(Zone* zone, Scanner* scanner, AstValueFactory* ast_value_factory,
            ParserRecorder* log, uintptr_t stack_limit)
      : ParserBase<PreParserTraits>(zone, scanner, stack_limit, NULL,
                                    ast_value_factory, log, this),
        use_counts_(nullptr) {}

  // Pre-parse the program from the character stream; returns true on
  // success (even if parsing failed, the pre-parse data successfully
  // captured the syntax error), and false if a stack-overflow happened
  // during parsing.
  PreParseResult PreParseProgram(int* materialized_literals = 0,
                                 bool is_module = false) {
    Scope* scope = NewScope(scope_, SCRIPT_SCOPE);

    // ModuleDeclarationInstantiation for Source Text Module Records creates a
    // new Module Environment Record whose outer lexical environment record is
    // the global scope.
    if (is_module) {
      scope = NewScope(scope, MODULE_SCOPE);
    }

    PreParserFactory factory(NULL);
    FunctionState top_scope(&function_state_, &scope_, scope, kNormalFunction,
                            &factory);
    bool ok = true;
    int start_position = scanner()->peek_location().beg_pos;
    parsing_module_ = is_module;
    ParseStatementList(Token::EOS, &ok);
    if (stack_overflow()) return kPreParseStackOverflow;
    if (!ok) {
      ReportUnexpectedToken(scanner()->current_token());
    } else if (is_strict(scope_->language_mode())) {
      CheckStrictOctalLiteral(start_position, scanner()->location().end_pos,
                              &ok);
      CheckDecimalLiteralWithLeadingZero(use_counts_, start_position,
                                         scanner()->location().end_pos);
    }
    if (materialized_literals) {
      *materialized_literals = function_state_->materialized_literal_count();
    }
    return kPreParseSuccess;
  }

  // Parses a single function literal, from the opening parentheses before
  // parameters to the closing brace after the body.
  // Returns a FunctionEntry describing the body of the function in enough
  // detail that it can be lazily compiled.
  // The scanner is expected to have matched the "function" or "function*"
  // keyword and parameters, and have consumed the initial '{'.
  // At return, unless an error occurred, the scanner is positioned before the
  // the final '}'.
  PreParseResult PreParseLazyFunction(LanguageMode language_mode, bool is_typed,
                                      FunctionKind kind,
                                      bool has_simple_parameters,
                                      bool parsing_module, ParserRecorder* log,
                                      Scanner::BookmarkScope* bookmark,
                                      int* use_counts);

 private:
  friend class PreParserTraits;

  static const int kLazyParseTrialLimit = 200;

  // These types form an algebra over syntactic categories that is just
  // rich enough to let us recognize and propagate the constructs that
  // are either being counted in the preparser data, or is important
  // to throw the correct syntax error exceptions.

  // All ParseXXX functions take as the last argument an *ok parameter
  // which is set to false if parsing failed; it is unchanged otherwise.
  // By making the 'exception handling' explicit, we are forced to check
  // for failure at the call sites.
  Statement ParseStatementListItem(bool* ok);
  void ParseStatementList(int end_token, bool* ok,
                          Scanner::BookmarkScope* bookmark = nullptr);
  Statement ParseStatement(AllowLabelledFunctionStatement allow_function,
                           bool* ok);
  Statement ParseSubStatement(AllowLabelledFunctionStatement allow_function,
                              bool* ok);
  Statement ParseScopedStatement(bool legacy, bool* ok);
  Statement ParseHoistableDeclaration(bool ambient, bool* ok);
  Statement ParseHoistableDeclaration(int pos, ParseFunctionFlags flags,
                                      bool ambient, bool* ok);
  Statement ParseFunctionDeclaration(bool ambient, bool* ok);
  Statement ParseAsyncFunctionDeclaration(bool ambient, bool* ok);
  Expression ParseAsyncFunctionExpression(bool* ok);
  Statement ParseClassDeclaration(bool ambient, bool* ok);
  Statement ParseBlock(bool* ok);
  Statement ParseVariableStatement(VariableDeclarationContext var_context,
                                   bool ambient, bool* ok);
  Statement ParseVariableDeclarations(VariableDeclarationContext var_context,
                                      int* num_decl, bool* is_lexical,
                                      bool* is_binding_pattern,
                                      Scanner::Location* first_initializer_loc,
                                      Scanner::Location* bindings_loc,
                                      bool ambient, bool* ok);
  Statement ParseExpressionOrLabelledStatement(
      AllowLabelledFunctionStatement allow_function, bool* ok);
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
  Expression ParseConditionalExpression(bool accept_IN, bool* ok);
  Expression ParseObjectLiteral(bool* ok);
  Expression ParseV8Intrinsic(bool* ok);
  Expression ParseDoExpression(bool* ok);

  V8_INLINE void SkipLazyFunctionBody(int* materialized_literal_count,
                                      int* expected_property_count, bool* ok);
  V8_INLINE PreParserStatementList ParseEagerFunctionBody(
      PreParserIdentifier function_name, int pos,
      const PreParserFormalParameters& parameters, FunctionKind kind,
      FunctionLiteral::FunctionType function_type, bool* ok);

  Expression ParseFunctionLiteral(Identifier name,
                                  Scanner::Location function_name_location,
                                  FunctionNameValidity function_name_validity,
                                  FunctionKind kind, int function_token_pos,
                                  FunctionLiteral::FunctionType function_type,
                                  LanguageMode language_mode, bool is_typed,
                                  typesystem::TypeFlags type_flags, bool* ok);
  void ParseLazyFunctionLiteralBody(bool* ok,
                                    Scanner::BookmarkScope* bookmark = nullptr);

  PreParserExpression ParseClassLiteral(ExpressionClassifier* classifier,
                                        PreParserIdentifier name,
                                        Scanner::Location class_name_location,
                                        bool name_is_strict_reserved, int pos,
                                        bool ambient, bool* ok);

  int* use_counts_;
};


void PreParserTraits::MaterializeTemplateCallsiteLiterals() {
  pre_parser_->function_state_->NextMaterializedLiteralIndex();
  pre_parser_->function_state_->NextMaterializedLiteralIndex();
}


void PreParserTraits::MaterializeUnspreadArgumentsLiterals(int count) {
  for (int i = 0; i < count; ++i) {
    pre_parser_->function_state_->NextMaterializedLiteralIndex();
  }
}


PreParserExpression PreParserTraits::SpreadCall(PreParserExpression function,
                                                PreParserExpressionList args,
                                                int pos) {
  return pre_parser_->factory()->NewCall(function, args, pos);
}

PreParserExpression PreParserTraits::SpreadCallNew(PreParserExpression function,
                                                   PreParserExpressionList args,
                                                   int pos) {
  return pre_parser_->factory()->NewCallNew(function, args, pos);
}


void PreParserTraits::ParseArrowFunctionFormalParameterList(
    PreParserFormalParameters* parameters,
    PreParserExpression params, const Scanner::Location& params_loc,
    Scanner::Location* duplicate_loc, bool* ok) {
  // TODO(wingo): Detect duplicated identifiers in paramlists.  Detect parameter
  // lists that are too long.
}

PreParserExpression PreParserTraits::ParseAsyncFunctionExpression(bool* ok) {
  return pre_parser_->ParseAsyncFunctionExpression(ok);
}

PreParserExpression PreParserTraits::ParseDoExpression(bool* ok) {
  return pre_parser_->ParseDoExpression(ok);
}


void PreParserTraits::RewriteNonPattern(Type::ExpressionClassifier* classifier,
                                        bool* ok) {
  pre_parser_->ValidateExpression(classifier, ok);
}

PreParserExpression PreParserTraits::RewriteAwaitExpression(
    PreParserExpression value, int pos) {
  return value;
}

ZoneList<PreParserExpression>* PreParserTraits::GetNonPatternList() const {
  return pre_parser_->function_state_->non_patterns_to_rewrite();
}


ZoneList<typename PreParserTraits::Type::ExpressionClassifier::Error>*
PreParserTraits::GetReportedErrorList() const {
  return pre_parser_->function_state_->GetReportedErrorList();
}


Zone* PreParserTraits::zone() const {
  return pre_parser_->function_state_->scope()->zone();
}


PreParserExpression PreParserTraits::RewriteYieldStar(
    PreParserExpression generator, PreParserExpression expression, int pos) {
  return PreParserExpression::Default();
}

PreParserStatementList PreParser::ParseEagerFunctionBody(
    PreParserIdentifier function_name, int pos,
    const PreParserFormalParameters& parameters, FunctionKind kind,
    FunctionLiteral::FunctionType function_type, bool* ok) {
  ParsingModeScope parsing_mode(this, PARSE_EAGERLY);

  Scope* inner_scope = scope_;
  if (!parameters.is_simple) inner_scope = NewScope(scope_, BLOCK_SCOPE);

  {
    BlockState block_state(&scope_, inner_scope);
    ParseStatementList(Token::RBRACE, ok);
    if (!*ok) return PreParserStatementList();
  }

  Expect(Token::RBRACE, ok);
  return PreParserStatementList();
}


PreParserStatementList PreParserTraits::ParseEagerFunctionBody(
    PreParserIdentifier function_name, int pos,
    const PreParserFormalParameters& parameters, FunctionKind kind,
    FunctionLiteral::FunctionType function_type, bool* ok) {
  return pre_parser_->ParseEagerFunctionBody(function_name, pos, parameters,
                                             kind, function_type, ok);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_PARSING_PREPARSER_H
