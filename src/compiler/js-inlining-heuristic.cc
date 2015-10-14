// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-inlining-heuristic.h"

#include "src/compiler/dead-code-elimination.h"  // TODO(mstarzinger): Remove!
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

  // Handling of special inlining modes right away:
  //  - For restricted inlining: stop all handling at this point.
  //  - For stressing inlining: immediately handle all functions.
  switch (mode_) {
    case kRestrictedInlining:
      return NoChange();
    case kStressInlining:
      return inliner_.ReduceJSCallFunction(node, function);
    case kGeneralInlining:
      break;
  }

  // ---------------------------------------------------------------------------
  // Everything below this line is part of the inlining heuristic.
  // ---------------------------------------------------------------------------

  // Built-in functions are handled by the JSBuiltinReducer.
  if (function->shared()->HasBuiltinFunctionId()) return NoChange();

  // Quick check on source code length to avoid parsing large candidate.
  if (function->shared()->SourceSize() > FLAG_max_inlined_source_size) {
    return NoChange();
  }

  // Quick check on the size of the AST to avoid parsing large candidate.
  if (function->shared()->ast_node_count() > FLAG_max_inlined_nodes) {
    return NoChange();
  }

  // Gather feedback on how often this call site has been hit before.
  CallFunctionParameters p = CallFunctionParametersOf(node->op());
  CallICNexus nexus(p.feedback().vector(), p.feedback().slot());
  int calls = nexus.ExtractCallCount();

  // ---------------------------------------------------------------------------
  // Everything above this line is part of the inlining heuristic.
  // ---------------------------------------------------------------------------

  // In the general case we remember the candidate for later.
  candidates_.push_back({function, node, calls});
  return NoChange();
}


void JSInliningHeuristic::ProcessCandidates() {
  if (candidates_.empty()) return;  // Nothing to do without candidates.
  std::sort(candidates_.begin(), candidates_.end(), Compare);
  if (FLAG_trace_turbo_inlining) PrintCandidates();

  int cumulative_count = 0;
  for (const Candidate& candidate : candidates_) {
    if (cumulative_count > FLAG_max_inlined_nodes_cumulative) break;
    inliner_.ReduceJSCallFunction(candidate.node, candidate.function);
    cumulative_count += candidate.function->shared()->ast_node_count();
  }

  // TODO(mstarzinger): Temporary workaround to eliminate dead control from the
  // graph being introduced by the inliner. Make this part of the pipeline.
  GraphReducer graph_reducer(local_zone_, jsgraph_->graph(), jsgraph_->Dead());
  DeadCodeElimination dead_code_elimination(&graph_reducer, jsgraph_->graph(),
                                            jsgraph_->common());
  graph_reducer.AddReducer(&dead_code_elimination);
  graph_reducer.ReduceGraph();
}


// static
bool JSInliningHeuristic::Compare(const Candidate& left,
                                  const Candidate& right) {
  return left.calls > right.calls;
}


void JSInliningHeuristic::PrintCandidates() {
  PrintF("Candidates for inlining (size=%zu):\n", candidates_.size());
  for (const Candidate& candidate : candidates_) {
    PrintF("  id:%d, calls:%d, size[source]:%d, size[ast]:%d / %s\n",
           candidate.node->id(), candidate.calls,
           candidate.function->shared()->SourceSize(),
           candidate.function->shared()->ast_node_count(),
           candidate.function->shared()->DebugName()->ToCString().get());
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
