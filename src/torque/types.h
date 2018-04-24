// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_TYPES_H_
#define V8_TORQUE_TYPES_H_

#include <string>
#include <vector>

#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

static const char* const NEVER_TYPE_STRING = "never";
static const char* const BRANCH_TYPE_STRING = "branch";
static const char* const BIT_TYPE_STRING = "bit";
static const char* const VOID_TYPE_STRING = "void";
static const char* const ARGUMENTS_TYPE_STRING = "Arguments";
static const char* const TAGGED_TYPE_STRING = "tagged";
static const char* const CONTEXT_TYPE_STRING = "Context";
static const char* const EXCEPTION_TYPE_STRING = "Exception";
static const char* const OBJECT_TYPE_STRING = "Object";
static const char* const STRING_TYPE_STRING = "String";
static const char* const INTPTR_TYPE_STRING = "intptr";
static const char* const CONST_INT31_TYPE_STRING = "const_int31";
static const char* const CONST_INT32_TYPE_STRING = "const_int32";
static const char* const CONST_FLOAT64_TYPE_STRING = "const_float64";

class Label;

struct Type;
class TypeImpl {
 public:
  TypeImpl(TypeImpl* parent, const std::string& name,
           const std::string& generated_type)
      : parent_(parent), name_(name), generated_type_(generated_type) {}
  TypeImpl* parent() const { return parent_; }
  const std::string& name() const { return name_; }
  const std::string& generated_type() const { return generated_type_; }

 private:
  TypeImpl* parent_;
  std::string name_;
  std::string generated_type_;
};

typedef struct Type {
 public:
  Type() : impl_(nullptr) {}
  Type(TypeImpl* type_impl) : impl_(type_impl) {}
  bool operator==(const Type& other) const { return impl_ == other.impl_; }
  bool operator!=(const Type& other) const { return impl_ != other.impl_; }
  bool Is(const Type& other) const { return impl_ == other.impl_; }
  bool Is(const std::string& name) const { return name == impl_->name(); }

  bool IsSubclass(Type from) {
    TypeImpl* to_class = type_impl();
    TypeImpl* from_class = from.type_impl();
    while (from_class != nullptr) {
      if (to_class == from_class) return true;
      from_class = from_class->parent();
    }
    return false;
  }

  bool IsException() const { return name() == EXCEPTION_TYPE_STRING; }
  bool IsVoid() const { return name() == VOID_TYPE_STRING; }
  bool IsNever() const { return name() == NEVER_TYPE_STRING; }
  bool IsBit() const { return name() == BIT_TYPE_STRING; }
  bool IsVoidOrNever() const { return IsVoid() || IsNever(); }

  const std::string& name() const { return impl_->name(); }

  const std::string& GetGeneratedTypeName() const {
    return type_impl()->generated_type();
  }

  std::string GetGeneratedTNodeTypeName() const {
    std::string result = type_impl()->generated_type();
    result = result.substr(6, result.length() - 7);
    return result;
  }

 protected:
  TypeImpl* type_impl() const { return impl_; }

 private:
  TypeImpl* impl_;
} Type;

inline std::ostream& operator<<(std::ostream& os, Type t) {
  os << t.name().c_str();
  return os;
}

using TypeVector = std::vector<Type>;

class VisitResult {
 public:
  VisitResult() {}
  VisitResult(Type type, const std::string& variable)
      : type_(type), variable_(variable) {}
  Type type() const { return type_; }
  const std::string& variable() const { return variable_; }

 private:
  Type type_;
  std::string variable_;
};

class VisitResultVector : public std::vector<VisitResult> {
 public:
  VisitResultVector() : std::vector<VisitResult>() {}
  VisitResultVector(std::initializer_list<VisitResult> init)
      : std::vector<VisitResult>(init) {}
  TypeVector GetTypeVector() const {
    TypeVector result;
    for (auto& visit_result : *this) {
      result.push_back(visit_result.type());
    }
    return result;
  }
};

std::ostream& operator<<(std::ostream& os, const TypeVector& types);

struct NameAndType {
  std::string name;
  Type type;
};

typedef std::vector<NameAndType> NameAndTypeVector;

struct LabelDefinition {
  std::string name;
  NameAndTypeVector parameters;
};

typedef std::vector<LabelDefinition> LabelDefinitionVector;

struct LabelDeclaration {
  std::string name;
  TypeVector types;
};

typedef std::vector<LabelDeclaration> LabelDeclarationVector;

struct ParameterTypes {
  TypeVector types;
  bool var_args;
};

std::ostream& operator<<(std::ostream& os, const ParameterTypes& parameters);

struct Signature {
  const TypeVector& types() const { return parameter_types.types; }
  NameVector parameter_names;
  ParameterTypes parameter_types;
  Type return_type;
  LabelDeclarationVector labels;
};

struct Arguments {
  VisitResultVector parameters;
  std::vector<Label*> labels;
};

std::ostream& operator<<(std::ostream& os, const Signature& sig);

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_TYPES_H_
