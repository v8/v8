// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_GENERATOR_H_
#define V8_INTERPRETER_BYTECODE_GENERATOR_H_

#include "src/ast.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {
namespace interpreter {

class BytecodeGenerator : public AstVisitor {
 public:
  BytecodeGenerator(Isolate* isolate, Zone* zone);
  virtual ~BytecodeGenerator();

  Handle<BytecodeArray> MakeBytecode(CompilationInfo* info);

#define DECLARE_VISIT(type) void Visit##type(type* node) override;
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

  // Visiting function for declarations list is overridden.
  void VisitDeclarations(ZoneList<Declaration*>* declarations) override;

 private:
  class ControlScope;
  class ControlScopeForIteration;

  DEFINE_AST_VISITOR_SUBCLASS_MEMBERS();

  void VisitArithmeticExpression(BinaryOperation* binop);
  void VisitPropertyLoad(Register obj, Property* expr);
  void VisitVariableLoad(Variable* variable, FeedbackVectorSlot slot);
  void VisitVariableAssignment(Variable* variable, FeedbackVectorSlot slot);

  // Dispatched from VisitUnaryOperation.
  void VisitVoid(UnaryOperation* expr);
  void VisitTypeOf(UnaryOperation* expr);
  void VisitNot(UnaryOperation* expr);

  inline BytecodeArrayBuilder* builder() { return &builder_; }
  inline Scope* scope() const { return scope_; }
  inline void set_scope(Scope* scope) { scope_ = scope; }
  inline ControlScope* control_scope() const { return control_scope_; }
  inline void set_control_scope(ControlScope* scope) { control_scope_ = scope; }
  inline CompilationInfo* info() const { return info_; }
  inline void set_info(CompilationInfo* info) { info_ = info; }
  ZoneVector<Handle<Object>>* globals() { return &globals_; }

  LanguageMode language_mode() const;
  Strength language_mode_strength() const;
  int feedback_index(FeedbackVectorSlot slot) const;
  Register current_context() const;

  BytecodeArrayBuilder builder_;
  CompilationInfo* info_;
  Scope* scope_;
  ZoneVector<Handle<Object>> globals_;
  ControlScope* control_scope_;

  // TODO(rmcilroy): Encapsulate this in an environment object.
  Register current_context_;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_BYTECODE_GENERATOR_H_
