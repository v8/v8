// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-context-specialization.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/contexts-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

Reduction JSContextSpecialization::Reduce(Node* node) {
  DisallowHeapAllocation no_heap_allocation;
  DisallowHandleAllocation no_handle_allocation;
  DisallowHandleDereference no_handle_dereference;
  DisallowCodeDependencyChange no_dependency_change;

  switch (node->opcode()) {
    case IrOpcode::kParameter:
      return ReduceParameter(node);
    case IrOpcode::kJSLoadContext:
      return ReduceJSLoadContext(node);
    case IrOpcode::kJSStoreContext:
      return ReduceJSStoreContext(node);
    default:
      break;
  }
  return NoChange();
}

Reduction JSContextSpecialization::ReduceParameter(Node* node) {
  DCHECK_EQ(IrOpcode::kParameter, node->opcode());
  int const index = ParameterIndexOf(node->op());
  if (index == Linkage::kJSCallClosureParamIndex) {
    // Constant-fold the function parameter {node}.
    Handle<JSFunction> function;
    if (closure().ToHandle(&function)) {
      Node* value = jsgraph()->HeapConstant(function);
      return Replace(value);
    }
  }
  return NoChange();
}

Reduction JSContextSpecialization::SimplifyJSLoadContext(Node* node,
                                                         Node* new_context,
                                                         size_t new_depth) {
  DCHECK_EQ(IrOpcode::kJSLoadContext, node->opcode());
  const ContextAccess& access = ContextAccessOf(node->op());
  DCHECK_LE(new_depth, access.depth());

  if (new_depth == access.depth() &&
      new_context == NodeProperties::GetContextInput(node)) {
    return NoChange();
  }

  const Operator* op = jsgraph_->javascript()->LoadContext(
      new_depth, access.index(), access.immutable());
  NodeProperties::ReplaceContextInput(node, new_context);
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

Reduction JSContextSpecialization::SimplifyJSStoreContext(Node* node,
                                                          Node* new_context,
                                                          size_t new_depth) {
  DCHECK_EQ(IrOpcode::kJSStoreContext, node->opcode());
  const ContextAccess& access = ContextAccessOf(node->op());
  DCHECK_LE(new_depth, access.depth());

  if (new_depth == access.depth() &&
      new_context == NodeProperties::GetContextInput(node)) {
    return NoChange();
  }

  const Operator* op =
      jsgraph_->javascript()->StoreContext(new_depth, access.index());
  NodeProperties::ReplaceContextInput(node, new_context);
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

namespace {

bool IsContextParameter(Node* node) {
  DCHECK_EQ(IrOpcode::kParameter, node->opcode());
  Node* const start = NodeProperties::GetValueInput(node, 0);
  DCHECK_EQ(IrOpcode::kStart, start->opcode());
  int const index = ParameterIndexOf(node->op());
  // The context is always the last parameter to a JavaScript function, and
  // {Parameter} indices start at -1, so value outputs of {Start} look like
  // this: closure, receiver, param0, ..., paramN, context.
  return index == start->op()->ValueOutputCount() - 2;
}

// Given a context {node} and the {distance} from that context to the target
// context (which we want to read from or store to), try to return a
// specialization context.  If successful, update {distance} to whatever
// distance remains from the specialization context.
base::Optional<ContextHeapReference> GetSpecializationContext(
    const JSHeapBroker* broker, Node* node, size_t* distance,
    Maybe<OuterContext> maybe_outer) {
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      HeapReference object =
          broker->HeapReferenceForObject(HeapConstantOf(node->op()));
      if (object.IsContext()) return object.AsContext();
      break;
    }
    case IrOpcode::kParameter: {
      OuterContext outer;
      if (maybe_outer.To(&outer) && IsContextParameter(node) &&
          *distance >= outer.distance) {
        *distance -= outer.distance;
        return broker->HeapReferenceForObject(outer.context).AsContext();
      }
      break;
    }
    default:
      break;
  }
  return base::Optional<ContextHeapReference>();
}

}  // anonymous namespace

Reduction JSContextSpecialization::ReduceJSLoadContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSLoadContext, node->opcode());

  const ContextAccess& access = ContextAccessOf(node->op());
  size_t depth = access.depth();

  // First walk up the context chain in the graph as far as possible.
  Node* context = NodeProperties::GetOuterContext(node, &depth);

  base::Optional<ContextHeapReference> maybe_concrete =
      GetSpecializationContext(js_heap_broker(), context, &depth, outer());
  if (!maybe_concrete.has_value()) {
    // We do not have a concrete context object, so we can only partially reduce
    // the load by folding-in the outer context node.
    return SimplifyJSLoadContext(node, context, depth);
  }

  // Now walk up the concrete context chain for the remaining depth.
  ContextHeapReference concrete = maybe_concrete.value();
  for (; depth > 0; --depth) {
    concrete = concrete.previous(js_heap_broker()).value();
  }

  if (!access.immutable()) {
    // We found the requested context object but since the context slot is
    // mutable we can only partially reduce the load.
    return SimplifyJSLoadContext(node, jsgraph()->Constant(concrete.object()),
                                 depth);
  }

  // This will hold the final value, if we can figure it out.
  base::Optional<ObjectReference> maybe_value;

  maybe_value =
      concrete.get(js_heap_broker(), static_cast<int>(access.index()));
  if (maybe_value.has_value() && !maybe_value->IsSmi()) {
    // Even though the context slot is immutable, the context might have escaped
    // before the function to which it belongs has initialized the slot.
    // We must be conservative and check if the value in the slot is currently
    // the hole or undefined. Only if it is neither of these, can we be sure
    // that it won't change anymore.
    HeapReferenceType type =
        maybe_value->AsHeapReference().type(js_heap_broker());
    if (type.oddball_type() == HeapReferenceType::kAny ||
        type.oddball_type() == HeapReferenceType::kUndefined ||
        type.oddball_type() == HeapReferenceType::kHole) {
      maybe_value.reset();
    }
  }

  if (!maybe_value.has_value()) {
    return SimplifyJSLoadContext(node, jsgraph()->Constant(concrete.object()),
                                 depth);
  }

  // Success. The context load can be replaced with the constant.
  // TODO(titzer): record the specialization for sharing code across
  // multiple contexts that have the same value in the corresponding context
  // slot.
  Node* constant = jsgraph_->Constant(maybe_value->object());
  ReplaceWithValue(node, constant);
  return Replace(constant);
}


Reduction JSContextSpecialization::ReduceJSStoreContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSStoreContext, node->opcode());

  const ContextAccess& access = ContextAccessOf(node->op());
  size_t depth = access.depth();

  // First walk up the context chain in the graph until we reduce the depth to 0
  // or hit a node that does not have a CreateXYZContext operator.
  Node* context = NodeProperties::GetOuterContext(node, &depth);

  base::Optional<ContextHeapReference> maybe_concrete =
      GetSpecializationContext(js_heap_broker(), context, &depth, outer());
  if (!maybe_concrete.has_value()) {
    // We do not have a concrete context object, so we can only partially reduce
    // the load by folding-in the outer context node.
    return SimplifyJSStoreContext(node, context, depth);
  }

  // Now walk up the concrete context chain for the remaining depth.
  ContextHeapReference concrete = maybe_concrete.value();
  for (; depth > 0; --depth) {
    concrete = concrete.previous(js_heap_broker()).value();
  }

  return SimplifyJSStoreContext(node, jsgraph()->Constant(concrete.object()),
                                depth);
}


Isolate* JSContextSpecialization::isolate() const {
  return jsgraph()->isolate();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
