// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-inlining-heuristic.h"

#include "src/compiler/node-matchers.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

Reduction JSInliningHeuristic::Reduce(Node* node) {
  if (node->opcode() != IrOpcode::kJSCallFunction) return NoChange();

  Node* callee = node->InputAt(0);
  HeapObjectMatcher match(callee);
  if (!match.HasValue() || !match.Value()->IsJSFunction()) return NoChange();
  Handle<JSFunction> function = Handle<JSFunction>::cast(match.Value());

  // Functions marked with %SetForceInlineFlag are immediately inlined.
  if (function->shared()->force_inline()) {
    return inliner_.ReduceJSCallFunction(node, function);
  }

  // All other functions are only handled with general inlining.
  if (mode_ == kRestrictedInlining) return NoChange();
  return inliner_.ReduceJSCallFunction(node, function);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
