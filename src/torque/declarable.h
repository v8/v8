// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_DECLARABLE_H_
#define V8_TORQUE_DECLARABLE_H_

#include <cassert>
#include <string>

#include "src/base/logging.h"
#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

class Scope;
class ScopeChain;

class Declarable {
 public:
  virtual ~Declarable() {}
  enum Kind {
    kTypeImpl,
    kVariable,
    kParameter,
    kMacro,
    kMacroList,
    kBuiltin,
    kRuntimeFunction,
    kLabel,
    kConstant
  };
  explicit Declarable(Kind kind) : kind_(kind) {}
  Kind kind() const { return kind_; }
  bool IsTypeImpl() const { return kind() == kTypeImpl; }
  bool IsMacro() const { return kind() == kMacro; }
  bool IsBuiltin() const { return kind() == kBuiltin; }
  bool IsRuntimeFunction() const { return kind() == kRuntimeFunction; }
  bool IsParameter() const { return kind() == kParameter; }
  bool IsLabel() const { return kind() == kLabel; }
  bool IsVariable() const { return kind() == kVariable; }
  bool IsMacroList() const { return kind() == kMacroList; }
  bool IsConstant() const { return kind() == kConstant; }
  bool IsValue() const {
    return IsVariable() || IsConstant() || IsParameter() || IsLabel();
  }
  virtual const char* type_name() const { return "<<unknown>>"; }

 private:
  Kind kind_;
};

#define DECLARE_DECLARABLE_BOILERPLATE(x, y)           \
  static x* cast(Declarable* declarable) {             \
    assert(declarable->Is##x());                       \
    return static_cast<x*>(declarable);                \
  }                                                    \
  static const x* cast(const Declarable* declarable) { \
    assert(declarable->Is##x());                       \
    return static_cast<const x*>(declarable);          \
  }                                                    \
  const char* type_name() const override { return #y; }

class TypeImpl : public Declarable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(TypeImpl, type_impl);
  TypeImpl(TypeImpl* parent, const std::string& name,
           const std::string& generated_type)
      : Declarable(Declarable::kTypeImpl),
        parent_(parent),
        name_(name),
        generated_type_(generated_type) {}
  TypeImpl* parent() const { return parent_; }
  const std::string& name() const { return name_; }
  const std::string& generated_type() const { return generated_type_; }

 private:
  TypeImpl* parent_;
  std::string name_;
  std::string generated_type_;
};

class Value : public Declarable {
 public:
  const std::string& name() const { return name_; }
  virtual bool IsConst() const { return true; }
  virtual std::string GetValueForDeclaration() const = 0;
  virtual std::string GetValueForRead() const {
    return GetValueForDeclaration();
  }
  virtual std::string GetValueForWrite() const { UNREACHABLE(); }
  DECLARE_DECLARABLE_BOILERPLATE(Value, value);
  Type type() const { return type_; }

 protected:
  Value(Kind kind, Type type, const std::string& name)
      : Declarable(kind), type_(type), name_(name) {}

 private:
  Type type_;
  std::string name_;
};

class Parameter : public Value {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Parameter, parameter);
  std::string GetValueForDeclaration() const override { return var_name_; }

 private:
  friend class Declarations;
  Parameter(const std::string& name, Type type, const std::string& var_name)
      : Value(Declarable::kParameter, type, name), var_name_(var_name) {}

  std::string var_name_;
};

class Variable : public Value {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Variable, variable);
  bool IsConst() const override { return false; }
  std::string GetValueForDeclaration() const override { return value_; }
  std::string GetValueForRead() const override { return value_ + "->value()"; }
  std::string GetValueForWrite() const override {
    return std::string("*") + value_;
  }
  void Define() { defined_ = true; }
  bool IsDefined() const { return defined_; }

 private:
  friend class Declarations;
  Variable(const std::string& name, const std::string& value, Type type)
      : Value(Declarable::kVariable, type, name),
        value_(value),
        defined_(false) {}

  std::string value_;
  bool defined_;
};

class Label : public Value {
 public:
  void AddVariable(Variable* var) { parameters_.push_back(var); }
  std::string GetSourceName() const { return source_name_; }
  std::string GetValueForDeclaration() const override { return name(); }
  Variable* GetParameter(size_t i) const { return parameters_[i]; }
  size_t GetParameterCount() const { return parameters_.size(); }
  const std::vector<Variable*>& GetParameters() const { return parameters_; }

  DECLARE_DECLARABLE_BOILERPLATE(Label, label);
  void MarkUsed() { used_ = true; }
  bool IsUsed() const { return used_; }

 private:
  friend class Declarations;
  explicit Label(const std::string& name)
      : Value(Declarable::kLabel, Type(),
              "label_" + name + "_" + std::to_string(next_id_++)),
        source_name_(name),
        used_(false) {}

  std::string source_name_;
  std::vector<Variable*> parameters_;
  static size_t next_id_;
  bool used_;
};

class Constant : public Value {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Constant, constant);
  std::string GetValueForDeclaration() const override { return value_; }

 private:
  friend class Declarations;
  explicit Constant(const std::string& name, Type type,
                    const std::string& value)
      : Value(Declarable::kConstant, type, name), value_(value) {}

  std::string value_;
};

class Callable : public Declarable {
 public:
  static Callable* cast(Declarable* declarable) {
    assert(declarable->IsMacro() || declarable->IsBuiltin() ||
           declarable->IsRuntimeFunction());
    return static_cast<Callable*>(declarable);
  }
  static const Callable* cast(const Declarable* declarable) {
    assert(declarable->IsMacro() || declarable->IsBuiltin() ||
           declarable->IsRuntimeFunction());
    return static_cast<const Callable*>(declarable);
  }
  const std::string& name() const { return name_; }
  const Signature& signature() const { return signature_; }
  const NameVector& parameter_names() const {
    return signature_.parameter_names;
  }
  bool HasReturnValue() const {
    return !signature_.return_type.IsVoidOrNever();
  }
  void IncrementReturns() { ++returns_; }
  bool HasReturns() const { return returns_; }

 protected:
  Callable(Declarable::Kind kind, const std::string& name,
           const Signature& signature)
      : Declarable(kind), name_(name), signature_(signature), returns_(0) {}

 private:
  std::string name_;
  Signature signature_;
  size_t returns_;
};

class Macro : public Callable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(Macro, macro);

 protected:
  Macro(Declarable::Kind type, const std::string& name,
        const Signature& signature)
      : Callable(type, name, signature) {}

 private:
  friend class Declarations;
  Macro(const std::string& name, const Signature& signature)
      : Macro(Declarable::kMacro, name, signature) {}
};

class MacroList : public Declarable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(MacroList, macro_list);
  const std::vector<Macro*>& list() { return list_; }
  Macro* AddMacro(Macro* macro) {
    list_.emplace_back(macro);
    return macro;
  }

 private:
  friend class Declarations;
  MacroList() : Declarable(Declarable::kMacroList) {}

  std::vector<Macro*> list_;
};

class Builtin : public Callable {
 public:
  enum Kind { kStub = 0, kFixedArgsJavaScript, kVarArgsJavaScript };
  DECLARE_DECLARABLE_BOILERPLATE(Builtin, builtin);
  Kind kind() const { return kind_; }
  bool IsStub() const { return kind_ == kStub; }
  bool IsVarArgsJavaScript() const { return kind_ == kVarArgsJavaScript; }
  bool IsFixedArgsJavaScript() const { return kind_ == kFixedArgsJavaScript; }

 private:
  friend class Declarations;
  Builtin(const std::string& name, Builtin::Kind kind,
          const Signature& signature)
      : Callable(Declarable::kBuiltin, name, signature), kind_(kind) {}

  Kind kind_;
};

class RuntimeFunction : public Callable {
 public:
  DECLARE_DECLARABLE_BOILERPLATE(RuntimeFunction, runtime);

 private:
  friend class Declarations;
  RuntimeFunction(const std::string& name, const Signature& signature)
      : Callable(Declarable::kRuntimeFunction, name, signature) {}
};

inline std::ostream& operator<<(std::ostream& os, const Callable& m) {
  os << "macro " << m.signature().return_type << " " << m.name()
     << m.signature().parameter_types;
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const Variable& v) {
  os << "variable " << v.name() << ": " << v.type();
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const Builtin& b) {
  os << "builtin " << b.signature().return_type << " " << b.name()
     << b.signature().parameter_types;
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const RuntimeFunction& b) {
  os << "runtime function " << b.signature().return_type << " " << b.name()
     << b.signature().parameter_types;
  return os;
}

#undef DECLARE_DECLARABLE_BOILERPLATE

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_DECLARABLE_H_
