// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_OPTIMIZER_H_
#define V8_MAGLEV_MAGLEV_GRAPH_OPTIMIZER_H_

#include "src/base/logging.h"
#include "src/common/scoped-modification.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-reducer.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevGraphOptimizer {
 public:
  explicit MaglevGraphOptimizer(Graph* graph);

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block);
  void PostProcessBasicBlock(BasicBlock* block);
  void PostPhiProcessing() {}

#define DECLARE_PROCESS(NodeT)                                        \
  ProcessResult Visit##NodeT();                                       \
  ProcessResult Process(NodeT* node, const ProcessingState& state) {  \
    ScopedModification<NodeBase*> current_node(&current_node_, node); \
    PreProcessNode(node);                                             \
    ProcessResult result = Visit##NodeT();                            \
    PostProcessNode(node);                                            \
    return result;                                                    \
  }
  NODE_BASE_LIST(DECLARE_PROCESS)
#undef DECLARE_PROCESS

  KnownNodeAspects& known_node_aspects() { return empty_known_node_aspects_; }

  DeoptFrame* GetDeoptFrameForEagerDeopt() {
    return &current_node()->eager_deopt_info()->top_frame();
  }

 private:
  MaglevReducer<MaglevGraphOptimizer> reducer_;

  // TODO(victorgomes): To improve this!
  KnownNodeAspects empty_known_node_aspects_;

  NodeBase* current_node_;
  int current_node_index_;  // Initial index position in basic block.

  NodeBase* current_node() const {
    CHECK_NOT_NULL(current_node_);
    return current_node_;
  }

  void PreProcessNode(Node*);
  void PostProcessNode(Node*);

  // Phis are treated differently since they are not stored directly in the
  // basic block.
  void PreProcessNode(Phi*);
  void PostProcessNode(Phi*);

  // Control nodes are singletons in the basic block.
  void PreProcessNode(ControlNode*);
  void PostProcessNode(ControlNode*);

  ValueNode* GetInputAt(int index) const;
  ProcessResult ReplaceWith(ValueNode* node);
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_OPTIMIZER_H_
