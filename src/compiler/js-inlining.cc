// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/access-builder.h"
#include "src/compiler/ast-graph-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/generic-node-inl.h"
#include "src/compiler/graph-inl.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/js-inlining.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-aux-data-inl.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties-inl.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/typer.h"
#include "src/parser.h"
#include "src/rewriter.h"
#include "src/scopes.h"


namespace v8 {
namespace internal {
namespace compiler {

class InlinerVisitor : public NullNodeVisitor {
 public:
  explicit InlinerVisitor(JSInliner* inliner) : inliner_(inliner) {}

  GenericGraphVisit::Control Post(Node* node) {
    switch (node->opcode()) {
      case IrOpcode::kJSCallFunction:
        inliner_->TryInlineCall(node);
        break;
      default:
        break;
    }
    return GenericGraphVisit::CONTINUE;
  }

 private:
  JSInliner* inliner_;
};


void JSInliner::Inline() {
  InlinerVisitor visitor(this);
  jsgraph_->graph()->VisitNodeInputsFromEnd(&visitor);
}


// TODO(sigurds) Find a home for this function and reuse it everywhere (esp. in
// test cases, where similar code is currently duplicated).
static void Parse(Handle<JSFunction> function, CompilationInfoWithZone* info) {
  CHECK(Parser::Parse(info));
  StrictMode strict_mode = info->function()->strict_mode();
  info->SetStrictMode(strict_mode);
  info->SetOptimizing(BailoutId::None(), Handle<Code>(function->code()));
  CHECK(Rewriter::Rewrite(info));
  CHECK(Scope::Analyze(info));
  CHECK_NE(NULL, info->scope());
  Handle<ScopeInfo> scope_info = ScopeInfo::Create(info->scope(), info->zone());
  info->shared_info()->set_scope_info(*scope_info);
}


// A facade on a JSFunction's graph to facilitate inlining. It assumes the
// that the function graph has only one return statement, and provides
// {UnifyReturn} to convert a function graph to that end.
class Inlinee {
 public:
  Inlinee(Node* start, Node* end) : start_(start), end_(end) {}

  // Returns the last regular control node, that is
  // the last control node before the end node.
  Node* end_block() { return NodeProperties::GetControlInput(unique_return()); }

  // Return the effect output of the graph,
  // that is the effect input of the return statement of the inlinee.
  Node* effect_output() {
    return NodeProperties::GetEffectInput(unique_return());
  }
  // Return the value output of the graph,
  // that is the value input of the return statement of the inlinee.
  Node* value_output() {
    return NodeProperties::GetValueInput(unique_return(), 0);
  }
  // Return the unique return statement of the graph.
  Node* unique_return() {
    Node* unique_return = NodeProperties::GetControlInput(end_);
    DCHECK_EQ(IrOpcode::kReturn, unique_return->opcode());
    return unique_return;
  }
  // Inline this graph at {call}, use {jsgraph} and its zone to create
  // any new nodes.
  void InlineAtCall(JSGraph* jsgraph, Node* call);

  // Ensure that only a single return reaches the end node.
  static void UnifyReturn(JSGraph* jsgraph);

 private:
  Node* start_;
  Node* end_;
};


void Inlinee::UnifyReturn(JSGraph* jsgraph) {
  Graph* graph = jsgraph->graph();

  Node* final_merge = NodeProperties::GetControlInput(graph->end(), 0);
  if (final_merge->opcode() == IrOpcode::kReturn) {
    // nothing to do
    return;
  }
  DCHECK_EQ(IrOpcode::kMerge, final_merge->opcode());

  int predecessors =
      OperatorProperties::GetControlInputCount(final_merge->op());
  Operator* op_phi = jsgraph->common()->Phi(kMachAnyTagged, predecessors);
  Operator* op_ephi = jsgraph->common()->EffectPhi(predecessors);

  NodeVector values(jsgraph->zone());
  NodeVector effects(jsgraph->zone());
  // Iterate over all control flow predecessors,
  // which must be return statements.
  InputIter iter = final_merge->inputs().begin();
  while (iter != final_merge->inputs().end()) {
    Node* input = *iter;
    switch (input->opcode()) {
      case IrOpcode::kReturn:
        values.push_back(NodeProperties::GetValueInput(input, 0));
        effects.push_back(NodeProperties::GetEffectInput(input));
        iter.UpdateToAndIncrement(NodeProperties::GetControlInput(input));
        input->RemoveAllInputs();
        break;
      default:
        UNREACHABLE();
        ++iter;
        break;
    }
  }
  values.push_back(final_merge);
  effects.push_back(final_merge);
  Node* phi =
      graph->NewNode(op_phi, static_cast<int>(values.size()), &values.front());
  Node* ephi = graph->NewNode(op_ephi, static_cast<int>(effects.size()),
                              &effects.front());
  Node* new_return =
      graph->NewNode(jsgraph->common()->Return(), phi, ephi, final_merge);
  graph->end()->ReplaceInput(0, new_return);
}


class CopyVisitor : public NullNodeVisitor {
 public:
  CopyVisitor(Graph* source_graph, Graph* target_graph, Zone* temp_zone)
      : copies_(source_graph->NodeCount(), NULL, temp_zone),
        sentinels_(source_graph->NodeCount(), NULL, temp_zone),
        source_graph_(source_graph),
        target_graph_(target_graph),
        temp_zone_(temp_zone),
        sentinel_op_(IrOpcode::kDead, Operator::kNoProperties, 0, 0,
                     "sentinel") {}

  GenericGraphVisit::Control Post(Node* original) {
    NodeVector inputs(temp_zone_);
    for (InputIter it = original->inputs().begin();
         it != original->inputs().end(); ++it) {
      inputs.push_back(GetCopy(*it));
    }

    // Reuse the operator in the copy. This assumes that op lives in a zone
    // that lives longer than graph()'s zone.
    Node* copy = target_graph_->NewNode(
        original->op(), static_cast<int>(inputs.size()), &inputs.front());
    copies_[original->id()] = copy;
    return GenericGraphVisit::CONTINUE;
  }

  Node* GetCopy(Node* original) {
    Node* copy = copies_[original->id()];
    if (copy == NULL) {
      copy = GetSentinel(original);
    }
    return copy;
  }

  void CopyGraph() {
    source_graph_->VisitNodeInputsFromEnd(this);
    ReplaceSentinels();
  }

  const NodeVector& copies() { return copies_; }

 private:
  void ReplaceSentinels() {
    for (int id = 0; id < source_graph_->NodeCount(); ++id) {
      Node* sentinel = sentinels_[id];
      if (sentinel == NULL) continue;
      Node* copy = copies_[id];
      DCHECK_NE(NULL, copy);
      sentinel->ReplaceUses(copy);
    }
  }

  Node* GetSentinel(Node* original) {
    Node* sentinel = sentinels_[original->id()];
    if (sentinel == NULL) {
      sentinel = target_graph_->NewNode(&sentinel_op_);
    }
    return sentinel;
  }

  NodeVector copies_;
  NodeVector sentinels_;
  Graph* source_graph_;
  Graph* target_graph_;
  Zone* temp_zone_;
  SimpleOperator sentinel_op_;
};


void Inlinee::InlineAtCall(JSGraph* jsgraph, Node* call) {
  // The scheduler is smart enough to place our code; we just ensure {control}
  // becomes the control input of the start of the inlinee.
  Node* control = NodeProperties::GetControlInput(call);

  // The inlinee uses the context from the JSFunction object. This will
  // also be the effect dependency for the inlinee as it produces an effect.
  SimplifiedOperatorBuilder simplified(jsgraph->zone());
  Node* context = jsgraph->graph()->NewNode(
      simplified.LoadField(AccessBuilder::ForJSFunctionContext()),
      NodeProperties::GetValueInput(call, 0),
      NodeProperties::GetEffectInput(call));

  // {inlinee_inputs} counts JSFunction, Receiver, arguments, context,
  // but not effect, control.
  int inlinee_inputs = start_->op()->OutputCount();
  // Context is last argument.
  int inlinee_context_index = inlinee_inputs - 1;
  // {inliner_inputs} counts JSFunction, Receiver, arguments, but not
  // context, effect, control.
  int inliner_inputs = OperatorProperties::GetValueInputCount(call->op());
  // Iterate over all uses of the start node.
  UseIter iter = start_->uses().begin();
  while (iter != start_->uses().end()) {
    Node* use = *iter;
    switch (use->opcode()) {
      case IrOpcode::kParameter: {
        int index = 1 + OpParameter<int>(use->op());
        if (index < inliner_inputs && index < inlinee_context_index) {
          // There is an input from the call, and the index is a value
          // projection but not the context, so rewire the input.
          NodeProperties::ReplaceWithValue(*iter, call->InputAt(index));
        } else if (index == inlinee_context_index) {
          // This is the context projection, rewire it to the context from the
          // JSFunction object.
          NodeProperties::ReplaceWithValue(*iter, context);
        } else if (index < inlinee_context_index) {
          // Call has fewer arguments than required, fill with undefined.
          NodeProperties::ReplaceWithValue(*iter, jsgraph->UndefinedConstant());
        } else {
          // We got too many arguments, discard for now.
          // TODO(sigurds): Fix to treat arguments array correctly.
        }
        ++iter;
        break;
      }
      default:
        if (NodeProperties::IsEffectEdge(iter.edge())) {
          iter.UpdateToAndIncrement(context);
        } else if (NodeProperties::IsControlEdge(iter.edge())) {
          iter.UpdateToAndIncrement(control);
        } else {
          UNREACHABLE();
        }
        break;
    }
  }

  // Iterate over all uses of the call node.
  iter = call->uses().begin();
  while (iter != call->uses().end()) {
    if (NodeProperties::IsEffectEdge(iter.edge())) {
      iter.UpdateToAndIncrement(effect_output());
    } else if (NodeProperties::IsControlEdge(iter.edge())) {
      UNREACHABLE();
    } else {
      DCHECK(NodeProperties::IsValueEdge(iter.edge()));
      iter.UpdateToAndIncrement(value_output());
    }
  }
  call->RemoveAllInputs();
  DCHECK_EQ(0, call->UseCount());
  // TODO(sigurds) Remove this once we copy.
  unique_return()->RemoveAllInputs();
}


void JSInliner::TryInlineCall(Node* call) {
  DCHECK_EQ(IrOpcode::kJSCallFunction, call->opcode());

  HeapObjectMatcher<JSFunction> match(call->InputAt(0));
  if (!match.HasValue()) {
    return;
  }

  Handle<JSFunction> function = match.Value().handle();

  if (function->shared()->native()) {
    if (FLAG_trace_turbo_inlining) {
      SmartArrayPointer<char> name =
          function->shared()->DebugName()->ToCString();
      PrintF("Not Inlining %s into %s because inlinee is native\n", name.get(),
             info_->shared_info()->DebugName()->ToCString().get());
    }
    return;
  }

  CompilationInfoWithZone info(function);
  Parse(function, &info);

  if (info.scope()->arguments() != NULL) {
    // For now do not inline functions that use their arguments array.
    SmartArrayPointer<char> name = function->shared()->DebugName()->ToCString();
    if (FLAG_trace_turbo_inlining) {
      PrintF(
          "Not Inlining %s into %s because inlinee uses arguments "
          "array\n",
          name.get(), info_->shared_info()->DebugName()->ToCString().get());
    }
    return;
  }

  if (FLAG_trace_turbo_inlining) {
    SmartArrayPointer<char> name = function->shared()->DebugName()->ToCString();
    PrintF("Inlining %s into %s\n", name.get(),
           info_->shared_info()->DebugName()->ToCString().get());
  }

  Graph graph(info.zone());
  Typer typer(info.zone());
  JSGraph jsgraph(&graph, jsgraph_->common(), jsgraph_->javascript(), &typer,
                  jsgraph_->machine());

  AstGraphBuilder graph_builder(&info, &jsgraph);
  graph_builder.CreateGraph();
  Inlinee::UnifyReturn(&jsgraph);

  CopyVisitor visitor(&graph, jsgraph_->graph(), info.zone());
  visitor.CopyGraph();

  Inlinee inlinee(visitor.GetCopy(graph.start()), visitor.GetCopy(graph.end()));
  inlinee.InlineAtCall(jsgraph_, call);
}
}
}
}  // namespace v8::internal::compiler
