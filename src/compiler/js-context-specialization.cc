// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/generic-node-inl.h"
#include "src/compiler/js-context-specialization.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-aux-data-inl.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

// TODO(titzer): factor this out to a common routine with js-typed-lowering.
static void ReplaceEffectfulWithValue(Node* node, Node* value) {
  Node* effect = NodeProperties::GetEffectInput(node);

  // Requires distinguishing between value and effect edges.
  UseIter iter = node->uses().begin();
  while (iter != node->uses().end()) {
    if (NodeProperties::IsEffectEdge(iter.edge())) {
      iter = iter.UpdateToAndIncrement(effect);
    } else {
      iter = iter.UpdateToAndIncrement(value);
    }
  }
}


void JSContextSpecializer::SpecializeToContext() {
  ValueMatcher<Handle<Context> > match(context_);

  // Iterate over all uses of the context and try to replace {LoadContext}
  // nodes with their values from the constant context.
  UseIter iter = match.node()->uses().begin();
  while (iter != match.node()->uses().end()) {
    Node* use = *iter;
    if (use->opcode() == IrOpcode::kJSLoadContext) {
      Reduction r = ReduceJSLoadContext(use);
      if (r.Changed() && r.replacement() != use) {
        ReplaceEffectfulWithValue(use, r.replacement());
      }
    }
    ++iter;
  }
}


Reduction JSContextSpecializer::ReduceJSLoadContext(Node* node) {
  ASSERT_EQ(IrOpcode::kJSLoadContext, node->opcode());

  ContextAccess access =
      static_cast<Operator1<ContextAccess>*>(node->op())->parameter();

  // Find the right parent context.
  Context* context = *info_->context();
  for (int i = access.depth(); i > 0; --i) {
    context = context->previous();
  }

  // If the access itself is mutable, only fold-in the parent.
  if (!access.immutable()) {
    // The access does not have to look up a parent, nothing to fold.
    if (access.depth() == 0) {
      return Reducer::NoChange();
    }
    Operator* op = jsgraph_->javascript()->LoadContext(0, access.index(),
                                                       access.immutable());
    node->set_op(op);
    Handle<Object> context_handle = Handle<Object>(context, info_->isolate());
    node->ReplaceInput(0, jsgraph_->Constant(context_handle));
    return Reducer::Changed(node);
  }
  Handle<Object> value =
      Handle<Object>(context->get(access.index()), info_->isolate());

  // Even though the context slot is immutable, the context might have escaped
  // before the function to which it belongs has initialized the slot.
  // We must be conservative and check if the value in the slot is currently the
  // hole or undefined. If it is neither of these, then it must be initialized.
  if (value->IsUndefined() || value->IsTheHole()) return Reducer::NoChange();

  // Success. The context load can be replaced with the constant.
  // TODO(titzer): record the specialization for sharing code across multiple
  // contexts that have the same value in the corresponding context slot.
  return Reducer::Replace(jsgraph_->Constant(value));
}
}
}
}  // namespace v8::internal::compiler
