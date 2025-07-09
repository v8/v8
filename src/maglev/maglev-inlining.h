// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_INLINING_H_
#define V8_MAGLEV_MAGLEV_INLINING_H_

#include "src/compiler/heap-refs.h"
#include "src/compiler/js-heap-broker.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-ir.h"

namespace v8::internal::maglev {

class MaglevInliner {
 public:
  explicit MaglevInliner(Graph* graph) : graph_(graph) {}

  void Run(bool is_tracing_maglev_graphs_enabled);

 private:
  int max_inlined_bytecode_size_cumulative() const;

  Graph* graph_;

  compiler::JSHeapBroker* broker() const { return graph_->broker(); }
  Zone* zone() const { return graph_->zone(); }

  MaglevCallSiteInfo* ChooseNextCallSite();
  MaybeReduceResult BuildInlineFunction(MaglevCallSiteInfo* call_site);

  // Truncates the graph at the given basic block `block`.  All blocks
  // following `block` (exclusive) are removed from the graph and returned.
  // `block` itself is removed from the graph and not returned.
  std::vector<BasicBlock*> TruncateGraphAt(BasicBlock* block);

  void RegisterNode(MaglevGraphBuilder& builder, Node* node) {
    if (builder.has_graph_labeller()) {
      builder.graph_labeller()->RegisterNode(node);
    }
  }

  ValueNode* EnsureTagged(MaglevGraphBuilder& builder, ValueNode* node);
  static void UpdatePredecessorsOf(BasicBlock* block, BasicBlock* prev_pred,
                                   BasicBlock* new_pred);
  void RemovePredecessorFollowing(ControlNode* control, BasicBlock* call_block);

  void RemoveUnreachableBlocks() {
    graph_->set_may_have_unreachable_blocks();
    graph_->RemoveUnreachableBlocks();
  }
};

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_INLINING_H_
