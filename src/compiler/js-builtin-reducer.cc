// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph-inl.h"
#include "src/compiler/js-builtin-reducer.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties-inl.h"
#include "src/types.h"

namespace v8 {
namespace internal {
namespace compiler {


// Helper method that assumes replacement nodes are pure values that don't
// produce an effect. Replaces {node} with {reduction} and relaxes effects.
static Reduction ReplaceWithPureReduction(Node* node, Reduction reduction) {
  if (reduction.Changed()) {
    NodeProperties::ReplaceWithValue(node, reduction.replacement());
    return reduction;
  }
  return Reducer::NoChange();
}


// Helper class to access JSCallFunction nodes that are potential candidates
// for reduction when they have a BuiltinFunctionId associated with them.
class JSCallReduction {
 public:
  explicit JSCallReduction(Node* node) : node_(node) {}

  // Determines whether the node is a JSCallFunction operation that targets a
  // constant callee being a well-known builtin with a BuiltinFunctionId.
  bool HasBuiltinFunctionId() {
    if (node_->opcode() != IrOpcode::kJSCallFunction) return false;
    HeapObjectMatcher<Object> m(NodeProperties::GetValueInput(node_, 0));
    if (!m.HasValue() || !m.Value().handle()->IsJSFunction()) return false;
    Handle<JSFunction> function = Handle<JSFunction>::cast(m.Value().handle());
    return function->shared()->HasBuiltinFunctionId();
  }

  // Retrieves the BuiltinFunctionId as described above.
  BuiltinFunctionId GetBuiltinFunctionId() {
    DCHECK_EQ(IrOpcode::kJSCallFunction, node_->opcode());
    HeapObjectMatcher<Object> m(NodeProperties::GetValueInput(node_, 0));
    Handle<JSFunction> function = Handle<JSFunction>::cast(m.Value().handle());
    return function->shared()->builtin_function_id();
  }

  // Determines whether the call takes one input of the given type.
  bool InputsMatch(Type* t1) {
    return GetJSCallArity() == 1 &&
           NodeProperties::GetBounds(GetJSCallInput(0)).upper->Is(t1);
  }

  // Determines whether the call takes two inputs of the given types.
  bool InputsMatch(Type* t1, Type* t2) {
    return GetJSCallArity() == 2 &&
           NodeProperties::GetBounds(GetJSCallInput(0)).upper->Is(t1) &&
           NodeProperties::GetBounds(GetJSCallInput(1)).upper->Is(t2);
  }

  Node* left() { return GetJSCallInput(0); }
  Node* right() { return GetJSCallInput(1); }

 protected:
  int GetJSCallArity() {
    DCHECK_EQ(IrOpcode::kJSCallFunction, node_->opcode());
    // Skip first (i.e. callee) and second (i.e. receiver) operand.
    return OperatorProperties::GetValueInputCount(node_->op()) - 2;
  }

  Node* GetJSCallInput(int index) {
    DCHECK_EQ(IrOpcode::kJSCallFunction, node_->opcode());
    DCHECK_LT(index, GetJSCallArity());
    // Skip first (i.e. callee) and second (i.e. receiver) operand.
    return NodeProperties::GetValueInput(node_, index + 2);
  }

 private:
  Node* node_;
};


// ES6 draft 08-24-14, section 20.2.2.19.
Reduction JSBuiltinReducer::ReduceMathImul(Node* node) {
  JSCallReduction r(node);
  if (r.InputsMatch(Type::Integral32(), Type::Integral32())) {
    // Math.imul(a:int32, b:int32) -> Int32Mul(a, b)
    Node* value = graph()->NewNode(machine()->Int32Mul(), r.left(), r.right());
    return Replace(value);
  }
  return NoChange();
}


Reduction JSBuiltinReducer::Reduce(Node* node) {
  JSCallReduction r(node);

  // Dispatch according to the BuiltinFunctionId if present.
  if (!r.HasBuiltinFunctionId()) return NoChange();
  switch (r.GetBuiltinFunctionId()) {
    case kMathImul:
      return ReplaceWithPureReduction(node, ReduceMathImul(node));
    default:
      break;
  }
  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
