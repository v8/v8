// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-call-reducer.h"

#include "src/compiler/js-graph.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects-inl.h"
#include "src/type-feedback-vector-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

VectorSlotPair CallCountFeedback(VectorSlotPair p) {
  // Extract call count from {p}.
  if (!p.IsValid()) return VectorSlotPair();
  CallICNexus n(p.vector(), p.slot());
  int const call_count = n.ExtractCallCount();
  if (call_count <= 0) return VectorSlotPair();

  // Create megamorphic CallIC feedback with the given {call_count}.
  StaticFeedbackVectorSpec spec;
  FeedbackVectorSlot slot = spec.AddCallICSlot();
  Handle<TypeFeedbackMetadata> metadata =
      TypeFeedbackMetadata::New(n.GetIsolate(), &spec);
  Handle<TypeFeedbackVector> vector =
      TypeFeedbackVector::New(n.GetIsolate(), metadata);
  CallICNexus nexus(vector, slot);
  nexus.ConfigureMegamorphic(call_count);
  return VectorSlotPair(vector, slot);
}

}  // namespace


Reduction JSCallReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kJSCallFunction:
      return ReduceJSCallFunction(node);
    default:
      break;
  }
  return NoChange();
}


// ES6 section 19.2.3.1 Function.prototype.apply ( thisArg, argArray )
Reduction JSCallReducer::ReduceFunctionPrototypeApply(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCallFunction, node->opcode());
  Node* target = NodeProperties::GetValueInput(node, 0);
  CallFunctionParameters const& p = CallFunctionParametersOf(node->op());
  Handle<JSFunction> apply =
      Handle<JSFunction>::cast(HeapObjectMatcher(target).Value());
  size_t arity = p.arity();
  DCHECK_LE(2u, arity);
  ConvertReceiverMode convert_mode = ConvertReceiverMode::kAny;
  if (arity == 2) {
    // Neither thisArg nor argArray was provided.
    convert_mode = ConvertReceiverMode::kNullOrUndefined;
    node->ReplaceInput(0, node->InputAt(1));
    node->ReplaceInput(1, jsgraph()->UndefinedConstant());
  } else if (arity == 3) {
    // The argArray was not provided, just remove the {target}.
    node->RemoveInput(0);
    --arity;
  } else if (arity == 4) {
    // Check if argArray is an arguments object, and {node} is the only value
    // user of argArray (except for value uses in frame states).
    Node* arg_array = NodeProperties::GetValueInput(node, 3);
    if (arg_array->opcode() != IrOpcode::kJSCreateArguments) return NoChange();
    for (Edge edge : arg_array->use_edges()) {
      if (edge.from()->opcode() == IrOpcode::kStateValues) continue;
      if (!NodeProperties::IsValueEdge(edge)) continue;
      if (edge.from() == node) continue;
      return NoChange();
    }
    // Get to the actual frame state from which to extract the arguments;
    // we can only optimize this in case the {node} was already inlined into
    // some other function (and same for the {arg_array}).
    CreateArgumentsParameters const& p =
        CreateArgumentsParametersOf(arg_array->op());
    Node* frame_state = NodeProperties::GetFrameStateInput(arg_array, 0);
    Node* outer_state = frame_state->InputAt(kFrameStateOuterStateInput);
    if (outer_state->opcode() != IrOpcode::kFrameState) return NoChange();
    FrameStateInfo outer_info = OpParameter<FrameStateInfo>(outer_state);
    if (outer_info.type() == FrameStateType::kArgumentsAdaptor) {
      // Need to take the parameters from the arguments adaptor.
      frame_state = outer_state;
    }
    FrameStateInfo state_info = OpParameter<FrameStateInfo>(frame_state);
    if (p.type() == CreateArgumentsParameters::kMappedArguments) {
      // Mapped arguments (sloppy mode) cannot be handled if they are aliased.
      Handle<SharedFunctionInfo> shared;
      if (!state_info.shared_info().ToHandle(&shared)) return NoChange();
      if (shared->internal_formal_parameter_count() != 0) return NoChange();
    }
    // Remove the argArray input from the {node}.
    node->RemoveInput(static_cast<int>(--arity));
    // Add the actual parameters to the {node}, skipping the receiver.
    Node* const parameters = frame_state->InputAt(kFrameStateParametersInput);
    for (int i = p.start_index() + 1; i < state_info.parameter_count(); ++i) {
      node->InsertInput(graph()->zone(), static_cast<int>(arity),
                        parameters->InputAt(i));
      ++arity;
    }
    // Drop the {target} from the {node}.
    node->RemoveInput(0);
    --arity;
  } else {
    return NoChange();
  }
  // Change {node} to the new {JSCallFunction} operator.
  NodeProperties::ChangeOp(
      node, javascript()->CallFunction(arity, p.language_mode(),
                                       CallCountFeedback(p.feedback()),
                                       convert_mode, p.tail_call_mode()));
  // Change context of {node} to the Function.prototype.apply context,
  // to ensure any exception is thrown in the correct context.
  NodeProperties::ReplaceContextInput(
      node, jsgraph()->HeapConstant(handle(apply->context(), isolate())));
  // Try to further reduce the JSCallFunction {node}.
  Reduction const reduction = ReduceJSCallFunction(node);
  return reduction.Changed() ? reduction : Changed(node);
}


// ES6 section 19.2.3.3 Function.prototype.call (thisArg, ...args)
Reduction JSCallReducer::ReduceFunctionPrototypeCall(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCallFunction, node->opcode());
  CallFunctionParameters const& p = CallFunctionParametersOf(node->op());
  Handle<JSFunction> call = Handle<JSFunction>::cast(
      HeapObjectMatcher(NodeProperties::GetValueInput(node, 0)).Value());
  // Change context of {node} to the Function.prototype.call context,
  // to ensure any exception is thrown in the correct context.
  NodeProperties::ReplaceContextInput(
      node, jsgraph()->HeapConstant(handle(call->context(), isolate())));
  // Remove the target from {node} and use the receiver as target instead, and
  // the thisArg becomes the new target.  If thisArg was not provided, insert
  // undefined instead.
  size_t arity = p.arity();
  DCHECK_LE(2u, arity);
  ConvertReceiverMode convert_mode;
  if (arity == 2) {
    // The thisArg was not provided, use undefined as receiver.
    convert_mode = ConvertReceiverMode::kNullOrUndefined;
    node->ReplaceInput(0, node->InputAt(1));
    node->ReplaceInput(1, jsgraph()->UndefinedConstant());
  } else {
    // Just remove the target, which is the first value input.
    convert_mode = ConvertReceiverMode::kAny;
    node->RemoveInput(0);
    --arity;
  }
  NodeProperties::ChangeOp(
      node, javascript()->CallFunction(arity, p.language_mode(),
                                       CallCountFeedback(p.feedback()),
                                       convert_mode, p.tail_call_mode()));
  // Try to further reduce the JSCallFunction {node}.
  Reduction const reduction = ReduceJSCallFunction(node);
  return reduction.Changed() ? reduction : Changed(node);
}


Reduction JSCallReducer::ReduceJSCallFunction(Node* node) {
  DCHECK_EQ(IrOpcode::kJSCallFunction, node->opcode());
  CallFunctionParameters const& p = CallFunctionParametersOf(node->op());
  Node* target = NodeProperties::GetValueInput(node, 0);
  Node* frame_state = NodeProperties::GetFrameStateInput(node, 1);
  Node* control = NodeProperties::GetControlInput(node);
  Node* effect = NodeProperties::GetEffectInput(node);

  // Try to specialize JSCallFunction {node}s with constant {target}s.
  HeapObjectMatcher m(target);
  if (m.HasValue()) {
    if (m.Value()->IsJSFunction()) {
      Handle<SharedFunctionInfo> shared(
          Handle<JSFunction>::cast(m.Value())->shared(), isolate());

      // Raise a TypeError if the {target} is a "classConstructor".
      if (IsClassConstructor(shared->kind())) {
        NodeProperties::RemoveFrameStateInput(node, 0);
        NodeProperties::RemoveValueInputs(node);
        NodeProperties::ChangeOp(
            node, javascript()->CallRuntime(
                      Runtime::kThrowConstructorNonCallableError, 0));
        return Changed(node);
      }

      // Check for known builtin functions.
      if (shared->HasBuiltinFunctionId()) {
        switch (shared->builtin_function_id()) {
          case kFunctionApply:
            return ReduceFunctionPrototypeApply(node);
          case kFunctionCall:
            return ReduceFunctionPrototypeCall(node);
          default:
            break;
        }
      }
    }
    // Don't mess with other {node}s that have a constant {target}.
    // TODO(bmeurer): Also support optimizing bound functions and proxies here.
    return NoChange();
  }

  // Not much we can do if deoptimization support is disabled.
  if (!(flags() & kDeoptimizationEnabled)) return NoChange();

  // Extract feedback from the {node} using the CallICNexus.
  if (!p.feedback().IsValid()) return NoChange();
  CallICNexus nexus(p.feedback().vector(), p.feedback().slot());
  Handle<Object> feedback(nexus.GetFeedback(), isolate());
  if (feedback->IsWeakCell()) {
    Handle<WeakCell> cell = Handle<WeakCell>::cast(feedback);
    if (cell->value()->IsJSFunction()) {
      // Check that the {target} is still the {target_function}.
      Node* target_function = jsgraph()->HeapConstant(
          handle(JSFunction::cast(cell->value()), isolate()));
      Node* check = graph()->NewNode(simplified()->ReferenceEqual(Type::Any()),
                                     target, target_function);
      Node* branch =
          graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);
      Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
      Node* deoptimize = graph()->NewNode(common()->Deoptimize(), frame_state,
                                          effect, if_false);
      // TODO(bmeurer): This should be on the AdvancedReducer somehow.
      NodeProperties::MergeControlToEnd(graph(), common(), deoptimize);
      control = graph()->NewNode(common()->IfTrue(), branch);

      // Specialize the JSCallFunction node to the {target_function}.
      NodeProperties::ReplaceValueInput(node, target_function, 0);
      NodeProperties::ReplaceControlInput(node, control);

      // Try to further reduce the JSCallFunction {node}.
      Reduction const reduction = ReduceJSCallFunction(node);
      return reduction.Changed() ? reduction : Changed(node);
    }
  }
  return NoChange();
}


Graph* JSCallReducer::graph() const { return jsgraph()->graph(); }


Isolate* JSCallReducer::isolate() const { return jsgraph()->isolate(); }


CommonOperatorBuilder* JSCallReducer::common() const {
  return jsgraph()->common();
}


JSOperatorBuilder* JSCallReducer::javascript() const {
  return jsgraph()->javascript();
}


SimplifiedOperatorBuilder* JSCallReducer::simplified() const {
  return jsgraph()->simplified();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
