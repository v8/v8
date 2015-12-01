// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_
#define V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_

#include "src/compiler.h"
#include "src/compiler/js-graph.h"
#include "src/interpreter/bytecode-array-iterator.h"
#include "src/interpreter/bytecodes.h"

namespace v8 {
namespace internal {
namespace compiler {

// The BytecodeGraphBuilder produces a high-level IR graph based on
// interpreter bytecodes.
class BytecodeGraphBuilder {
 public:
  BytecodeGraphBuilder(Zone* local_zone, CompilationInfo* info,
                       JSGraph* jsgraph);

  // Creates a graph by visiting bytecodes.
  bool CreateGraph(bool stack_check = true);

  Graph* graph() const { return jsgraph_->graph(); }

 private:
  class Environment;

  void CreateGraphBody(bool stack_check);
  void VisitBytecodes();

  Node* LoadAccumulator(Node* value);

  // Get or create the node that represents the outer function closure.
  Node* GetFunctionClosure();

  // Get or create the node that represents the outer function context.
  Node* GetFunctionContext();

  // Get or create the node that represents the incoming new target value.
  Node* GetNewTarget();

  // Builder for accessing a (potentially immutable) object field.
  Node* BuildLoadObjectField(Node* object, int offset);
  Node* BuildLoadImmutableObjectField(Node* object, int offset);

  // Builder for accessing type feedback vector.
  Node* BuildLoadFeedbackVector();

  // Builder for loading the a native context field.
  Node* BuildLoadNativeContextField(int index);

  // Helper function for creating a pair containing type feedback vector and
  // a feedback slot.
  VectorSlotPair CreateVectorSlotPair(int slot_id);

  // Replaces the frame state inputs with empty frame states.
  void AddEmptyFrameStateInputs(Node* node);

  void set_environment(Environment* env) { environment_ = env; }
  const Environment* environment() const { return environment_; }
  Environment* environment() { return environment_; }

  // Node creation helpers
  Node* NewNode(const Operator* op, bool incomplete = false) {
    return MakeNode(op, 0, static_cast<Node**>(NULL), incomplete);
  }

  Node* NewNode(const Operator* op, Node* n1) {
    Node* buffer[] = {n1};
    return MakeNode(op, arraysize(buffer), buffer, false);
  }

  Node* NewNode(const Operator* op, Node* n1, Node* n2) {
    Node* buffer[] = {n1, n2};
    return MakeNode(op, arraysize(buffer), buffer, false);
  }

  Node* NewNode(const Operator* op, Node* n1, Node* n2, Node* n3) {
    Node* buffer[] = {n1, n2, n3};
    return MakeNode(op, arraysize(buffer), buffer, false);
  }

  Node* NewNode(const Operator* op, Node* n1, Node* n2, Node* n3, Node* n4) {
    Node* buffer[] = {n1, n2, n3, n4};
    return MakeNode(op, arraysize(buffer), buffer, false);
  }

  Node* MakeNode(const Operator* op, int value_input_count, Node** value_inputs,
                 bool incomplete);

  Node* MergeControl(Node* control, Node* other);

  Node** EnsureInputBufferSize(int size);

  void UpdateControlDependencyToLeaveFunction(Node* exit);

  Node* ProcessCallArguments(const Operator* call_op, Node* callee,
                             interpreter::Register receiver, size_t arity);
  Node* ProcessCallNewArguments(const Operator* call_new_op,
                                interpreter::Register callee,
                                interpreter::Register first_arg, size_t arity);
  Node* ProcessCallRuntimeArguments(const Operator* call_runtime_op,
                                    interpreter::Register first_arg,
                                    size_t arity);

  void BuildLoadGlobal(const interpreter::BytecodeArrayIterator& iterator,
                       TypeofMode typeof_mode);
  void BuildStoreGlobal(const interpreter::BytecodeArrayIterator& iterator);
  void BuildNamedLoad(const interpreter::BytecodeArrayIterator& iterator);
  void BuildKeyedLoad(const interpreter::BytecodeArrayIterator& iterator);
  void BuildNamedStore(const interpreter::BytecodeArrayIterator& iterator);
  void BuildKeyedStore(const interpreter::BytecodeArrayIterator& iterator);
  void BuildCall(const interpreter::BytecodeArrayIterator& iterator);
  void BuildBinaryOp(const Operator* op,
                     const interpreter::BytecodeArrayIterator& iterator);
  void BuildCompareOp(const Operator* op,
                      const interpreter::BytecodeArrayIterator& iterator);
  void BuildDelete(const interpreter::BytecodeArrayIterator& iterator);
  void BuildCastOperator(const Operator* js_op,
                         const interpreter::BytecodeArrayIterator& iterator);

  // Growth increment for the temporary buffer used to construct input lists to
  // new nodes.
  static const int kInputBufferSizeIncrement = 64;

  // Field accessors
  CommonOperatorBuilder* common() const { return jsgraph_->common(); }
  Zone* graph_zone() const { return graph()->zone(); }
  CompilationInfo* info() const { return info_; }
  JSGraph* jsgraph() const { return jsgraph_; }
  JSOperatorBuilder* javascript() const { return jsgraph_->javascript(); }
  Zone* local_zone() const { return local_zone_; }
  const Handle<BytecodeArray>& bytecode_array() const {
    return bytecode_array_;
  }

  LanguageMode language_mode() const {
    // TODO(mythria): Don't rely on parse information to get language mode.
    return info()->language_mode();
  }

#define DECLARE_VISIT_BYTECODE(name, ...) \
  void Visit##name(const interpreter::BytecodeArrayIterator& iterator);
  BYTECODE_LIST(DECLARE_VISIT_BYTECODE)
#undef DECLARE_VISIT_BYTECODE

  Zone* local_zone_;
  CompilationInfo* info_;
  JSGraph* jsgraph_;
  Handle<BytecodeArray> bytecode_array_;
  Environment* environment_;

  // Temporary storage for building node input lists.
  int input_buffer_size_;
  Node** input_buffer_;

  // Nodes representing values in the activation record.
  SetOncePointer<Node> function_context_;
  SetOncePointer<Node> function_closure_;
  SetOncePointer<Node> new_target_;

  // Optimization to cache loaded feedback vector.
  SetOncePointer<Node> feedback_vector_;

  // Control nodes that exit the function body.
  ZoneVector<Node*> exit_controls_;

  DISALLOW_COPY_AND_ASSIGN(BytecodeGraphBuilder);
};


class BytecodeGraphBuilder::Environment : public ZoneObject {
 public:
  Environment(BytecodeGraphBuilder* builder, int register_count,
              int parameter_count, Node* control_dependency, Node* context);

  int parameter_count() const { return parameter_count_; }
  int register_count() const { return register_count_; }

  void BindRegister(interpreter::Register the_register, Node* node);
  Node* LookupRegister(interpreter::Register the_register) const;

  void BindAccumulator(Node* node);
  Node* LookupAccumulator() const;

  bool IsMarkedAsUnreachable() const;
  void MarkAsUnreachable();

  // Effect dependency tracked by this environment.
  Node* GetEffectDependency() { return effect_dependency_; }
  void UpdateEffectDependency(Node* dependency) {
    effect_dependency_ = dependency;
  }

  // Control dependency tracked by this environment.
  Node* GetControlDependency() const { return control_dependency_; }
  void UpdateControlDependency(Node* dependency) {
    control_dependency_ = dependency;
  }

  Node* Context() const { return context_; }
  void SetContext(Node* new_context) { context_ = new_context; }

 private:
  int RegisterToValuesIndex(interpreter::Register the_register) const;

  Zone* zone() const { return builder_->local_zone(); }
  Graph* graph() const { return builder_->graph(); }
  CommonOperatorBuilder* common() const { return builder_->common(); }
  BytecodeGraphBuilder* builder() const { return builder_; }
  const NodeVector* values() const { return &values_; }
  NodeVector* values() { return &values_; }
  Node* accumulator() { return accumulator_; }
  int register_base() const { return register_base_; }

  BytecodeGraphBuilder* builder_;
  int register_count_;
  int parameter_count_;
  Node* accumulator_;
  Node* context_;
  Node* control_dependency_;
  Node* effect_dependency_;
  NodeVector values_;
  int register_base_;
};


}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BYTECODE_GRAPH_BUILDER_H_
