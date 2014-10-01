// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_JS_OPERATOR_H_
#define V8_COMPILER_JS_OPERATOR_H_

#include "src/runtime/runtime.h"
#include "src/unique.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class Operator;
struct JSOperatorBuilderImpl;


// Defines the arity and the call flags for a JavaScript function call. This is
// used as a parameter by JSCallFunction operators.
class CallFunctionParameters FINAL {
 public:
  CallFunctionParameters(size_t arity, CallFunctionFlags flags)
      : arity_(arity), flags_(flags) {}

  size_t arity() const { return arity_; }
  CallFunctionFlags flags() const { return flags_; }

 private:
  const size_t arity_;
  const CallFunctionFlags flags_;
};

const CallFunctionParameters& CallFunctionParametersOf(const Operator* op);


// Defines the arity and the ID for a runtime function call. This is used as a
// parameter by JSCallRuntime operators.
class CallRuntimeParameters FINAL {
 public:
  CallRuntimeParameters(Runtime::FunctionId id, size_t arity)
      : id_(id), arity_(arity) {}

  Runtime::FunctionId id() const { return id_; }
  size_t arity() const { return arity_; }

 private:
  const Runtime::FunctionId id_;
  const size_t arity_;
};

const CallRuntimeParameters& CallRuntimeParametersOf(const Operator* op);


// Defines the location of a context slot relative to a specific scope. This is
// used as a parameter by JSLoadContext and JSStoreContext operators and allows
// accessing a context-allocated variable without keeping track of the scope.
class ContextAccess FINAL {
 public:
  ContextAccess(size_t depth, size_t index, bool immutable);

  size_t depth() const { return depth_; }
  size_t index() const { return index_; }
  bool immutable() const { return immutable_; }

 private:
  // For space reasons, we keep this tightly packed, otherwise we could just use
  // a simple int/int/bool POD.
  const bool immutable_;
  const uint16_t depth_;
  const uint32_t index_;
};

bool operator==(const ContextAccess& lhs, const ContextAccess& rhs);
bool operator!=(const ContextAccess& lhs, const ContextAccess& rhs);

const ContextAccess& ContextAccessOf(const Operator* op);


// Defines the property being loaded from an object by a named load. This is
// used as a parameter by JSLoadNamed operators.
class LoadNamedParameters FINAL {
 public:
  LoadNamedParameters(const Unique<Name>& name, ContextualMode contextual_mode)
      : name_(name), contextual_mode_(contextual_mode) {}

  const Unique<Name>& name() const { return name_; }
  ContextualMode contextual_mode() const { return contextual_mode_; }

 private:
  const Unique<Name> name_;
  const ContextualMode contextual_mode_;
};

const LoadNamedParameters& LoadNamedParametersOf(const Operator* op);


// Defines the property being stored to an object by a named store. This is
// used as a parameter by JSStoreNamed operators.
class StoreNamedParameters FINAL {
 public:
  StoreNamedParameters(StrictMode strict_mode, const Unique<Name>& name)
      : strict_mode_(strict_mode), name_(name) {}

  StrictMode strict_mode() const { return strict_mode_; }
  const Unique<Name>& name() const { return name_; }

 private:
  const StrictMode strict_mode_;
  const Unique<Name> name_;
};

const StoreNamedParameters& StoreNamedParametersOf(const Operator* op);


// Interface for building JavaScript-level operators, e.g. directly from the
// AST. Most operators have no parameters, thus can be globally shared for all
// graphs.
class JSOperatorBuilder FINAL {
 public:
  explicit JSOperatorBuilder(Zone* zone);

  const Operator* Equal();
  const Operator* NotEqual();
  const Operator* StrictEqual();
  const Operator* StrictNotEqual();
  const Operator* LessThan();
  const Operator* GreaterThan();
  const Operator* LessThanOrEqual();
  const Operator* GreaterThanOrEqual();
  const Operator* BitwiseOr();
  const Operator* BitwiseXor();
  const Operator* BitwiseAnd();
  const Operator* ShiftLeft();
  const Operator* ShiftRight();
  const Operator* ShiftRightLogical();
  const Operator* Add();
  const Operator* Subtract();
  const Operator* Multiply();
  const Operator* Divide();
  const Operator* Modulus();

  const Operator* UnaryNot();
  const Operator* ToBoolean();
  const Operator* ToNumber();
  const Operator* ToString();
  const Operator* ToName();
  const Operator* ToObject();
  const Operator* Yield();

  const Operator* Create();

  const Operator* CallFunction(size_t arity, CallFunctionFlags flags);
  const Operator* CallRuntime(Runtime::FunctionId id, size_t arity);

  const Operator* CallConstruct(int arguments);

  const Operator* LoadProperty();
  const Operator* LoadNamed(const Unique<Name>& name,
                            ContextualMode contextual_mode = NOT_CONTEXTUAL);

  const Operator* StoreProperty(StrictMode strict_mode);
  const Operator* StoreNamed(StrictMode strict_mode, const Unique<Name>& name);

  const Operator* DeleteProperty(StrictMode strict_mode);

  const Operator* HasProperty();

  const Operator* LoadContext(size_t depth, size_t index, bool immutable);
  const Operator* StoreContext(size_t depth, size_t index);

  const Operator* TypeOf();
  const Operator* InstanceOf();
  const Operator* Debugger();

  // TODO(titzer): nail down the static parts of each of these context flavors.
  const Operator* CreateFunctionContext();
  const Operator* CreateCatchContext(const Unique<String>& name);
  const Operator* CreateWithContext();
  const Operator* CreateBlockContext();
  const Operator* CreateModuleContext();
  const Operator* CreateGlobalContext();

 private:
  Zone* zone() const { return zone_; }

  const JSOperatorBuilderImpl& impl_;
  Zone* const zone_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_JS_OPERATOR_H_
