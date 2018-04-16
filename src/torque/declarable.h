// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_DECLARABLE_H_
#define V8_TORQUE_DECLARABLE_H_

#include <string>

#include "src/torque/types.h"
#include "src/torque/utils.h"

namespace v8 {
namespace internal {
namespace torque {

class Scope;

class Declarable {
 public:
  virtual ~Declarable() {}
  enum Kind {
    kVariable = 0,
    kParameter,
    kMacro,
    kMacroList,
    kBuiltin,
    kRuntime,
    kLabel,
    kConstant
  };
  explicit Declarable(Kind kind) : kind_(kind) {}
  Kind kind() const { return kind_; }
  bool IsMacro() const { return kind() == kMacro; }
  bool IsBuiltin() const { return kind() == kBuiltin; }
  bool IsRuntime() const { return kind() == kRuntime; }
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

class Value : public Declarable {
 public:
  Value(Kind kind, Type type, const std::string& name)
      : Declarable(kind), type_(type), name_(name) {}
  const std::string& name() const { return name_; }
  virtual bool IsConst() const { return true; }
  virtual std::string GetValueForDeclaration() const = 0;
  virtual std::string GetValueForRead() const {
    return GetValueForDeclaration();
  }
  virtual std::string GetValueForWrite() const { UNREACHABLE(); }
  DECLARE_DECLARABLE_BOILERPLATE(Value, value);
  Type type() const { return type_; }

 private:
  Type type_;
  std::string name_;
};

class Parameter : public Value {
 public:
  Parameter(const std::string& name, Type type, const std::string& var_name)
      : Value(Declarable::kParameter, type, name), var_name_(var_name) {}
  DECLARE_DECLARABLE_BOILERPLATE(Parameter, parameter);
  std::string GetValueForDeclaration() const override { return var_name_; }

 private:
  std::string var_name_;
};

class Variable : public Value {
 public:
  Variable(const std::string& name, const std::string& value, Type type)
      : Value(Declarable::kVariable, type, name),
        value_(value),
        defined_(false) {}
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
  std::string value_;
  bool defined_;
};

class Label : public Value {
 public:
  explicit Label(const std::string& name) : Label(name, {}) {}
  Label(const std::string& name, const std::vector<Variable*>& parameters)
      : Value(Declarable::kLabel, Type(),
              "label_" + name + "_" + std::to_string(next_id_++)),
        source_name_(name),
        parameters_(parameters),
        used_(false) {}
  std::string GetSourceName() const { return source_name_; }
  std::string GetValueForDeclaration() const override { return name(); }
  Variable* GetParameter(size_t i) const { return parameters_[i]; }
  size_t GetParameterCount() const { return parameters_.size(); }
  const std::vector<Variable*>& GetParameters() const { return parameters_; }

  DECLARE_DECLARABLE_BOILERPLATE(Label, label);
  void MarkUsed() { used_ = true; }
  bool IsUsed() const { return used_; }

 private:
  std::string source_name_;
  std::vector<Variable*> parameters_;
  static size_t next_id_;
  bool used_;
};

class Constant : public Value {
 public:
  explicit Constant(const std::string& name, Type type,
                    const std::string& value)
      : Value(Declarable::kConstant, type, name), value_(value) {}
  DECLARE_DECLARABLE_BOILERPLATE(Constant, constant);
  std::string GetValueForDeclaration() const override { return value_; }

 private:
  std::string value_;
};

class Callable : public Declarable {
 public:
  Callable(Declarable::Kind kind, const std::string& name, Scope* scope,
           const Signature& signature)
      : Declarable(kind),
        name_(name),
        scope_(scope),
        signature_(signature),
        returns_(0) {}
  static Callable* cast(Declarable* declarable) {
    assert(declarable->IsMacro() || declarable->IsBuiltin() ||
           declarable->IsRuntime());
    return static_cast<Callable*>(declarable);
  }
  static const Callable* cast(const Declarable* declarable) {
    assert(declarable->IsMacro() || declarable->IsBuiltin() ||
           declarable->IsRuntime());
    return static_cast<const Callable*>(declarable);
  }
  Scope* scope() const { return scope_; }
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

 private:
  std::string name_;
  Scope* scope_;
  Signature signature_;
  size_t returns_;
};

class Macro : public Callable {
 public:
  Macro(const std::string& name, Scope* scope, const Signature& signature)
      : Macro(Declarable::kMacro, name, scope, signature) {}
  DECLARE_DECLARABLE_BOILERPLATE(Macro, macro);

  void AddLabel(const LabelDeclaration& label) { labels_.push_back(label); }

  const LabelDeclarationVector& GetLabels() { return labels_; }

 protected:
  Macro(Declarable::Kind type, const std::string& name, Scope* scope,
        const Signature& signature)
      : Callable(type, name, scope, signature) {}

 private:
  LabelDeclarationVector labels_;
};

class MacroList : public Declarable {
 public:
  MacroList() : Declarable(Declarable::kMacroList) {}
  DECLARE_DECLARABLE_BOILERPLATE(MacroList, macro_list);
  const std::vector<Macro*>& list() { return list_; }
  void AddMacro(Macro* macro) { list_.push_back(macro); }

 private:
  std::vector<Macro*> list_;
};

class Builtin : public Callable {
 public:
  Builtin(const std::string& name, bool java_script, Scope* scope,
          const Signature& signature)
      : Callable(Declarable::kBuiltin, name, scope, signature),
        java_script_(java_script) {}
  DECLARE_DECLARABLE_BOILERPLATE(Builtin, builtin);
  bool IsJavaScript() const { return java_script_; }

 private:
  bool java_script_;
};

class Runtime : public Callable {
 public:
  Runtime(const std::string& name, Scope* scope, const Signature& signature)
      : Callable(Declarable::kRuntime, name, scope, signature) {}
  DECLARE_DECLARABLE_BOILERPLATE(Runtime, runtime);
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

inline std::ostream& operator<<(std::ostream& os, const Runtime& b) {
  os << "runtime " << b.signature().return_type << " " << b.name()
     << b.signature().parameter_types;
  return os;
}

#undef DECLARE_DECLARABLE_BOILERPLATE

}  // namespace torque
}  // namespace internal
}  // namespace v8

#endif  // V8_TORQUE_DECLARABLE_H_
