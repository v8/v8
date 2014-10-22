// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/common-operator.h"
#include "src/compiler/control-reducer.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties-inl.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

enum VisitState { kUnvisited, kOnStack, kRevisit, kVisited };

#define TRACE(x) \
  if (FLAG_trace_turbo) PrintF x

class ControlReducerImpl {
 public:
  ControlReducerImpl(Zone* zone, JSGraph* jsgraph,
                     CommonOperatorBuilder* common)
      : zone_(zone),
        jsgraph_(jsgraph),
        common_(common),
        state_(jsgraph->graph()->NodeCount(), kUnvisited, zone_),
        stack_(zone_),
        revisit_(zone_),
        dead_(NULL) {}

  Zone* zone_;
  JSGraph* jsgraph_;
  CommonOperatorBuilder* common_;
  ZoneVector<VisitState> state_;
  ZoneDeque<Node*> stack_;
  ZoneDeque<Node*> revisit_;
  Node* dead_;

  void Trim() {
    //  Mark all nodes reachable from end.
    NodeVector nodes(zone_);
    state_.assign(jsgraph_->graph()->NodeCount(), kUnvisited);
    Push(jsgraph_->graph()->end());
    while (!stack_.empty()) {
      Node* node = stack_[stack_.size() - 1];
      stack_.pop_back();
      state_[node->id()] = kVisited;
      nodes.push_back(node);
      for (InputIter i = node->inputs().begin(); i != node->inputs().end();
           ++i) {
        Recurse(*i);  // pushes node onto the stack if necessary.
      }
    }
    // Process cached nodes in the JSGraph too.
    jsgraph_->GetCachedNodes(&nodes);
    // Remove dead->live edges.
    for (size_t j = 0; j < nodes.size(); j++) {
      Node* node = nodes[j];
      for (UseIter i = node->uses().begin(); i != node->uses().end();) {
        size_t id = static_cast<size_t>((*i)->id());
        if (state_[id] != kVisited) {
          TRACE(("DeadLink: #%d:%s(%d) -> #%d:%s\n", (*i)->id(),
                 (*i)->op()->mnemonic(), i.index(), node->id(),
                 node->op()->mnemonic()));
          i.UpdateToAndIncrement(NULL);
        } else {
          ++i;
        }
      }
    }
#if DEBUG
    // Verify that no inputs to live nodes are NULL.
    for (size_t j = 0; j < nodes.size(); j++) {
      Node* node = nodes[j];
      for (InputIter i = node->inputs().begin(); i != node->inputs().end();
           ++i) {
        CHECK_NE(NULL, *i);
      }
      for (UseIter i = node->uses().begin(); i != node->uses().end(); ++i) {
        size_t id = static_cast<size_t>((*i)->id());
        CHECK_EQ(kVisited, state_[id]);
      }
    }
#endif
  }

  // Push a node onto the stack if its state is {kUnvisited} or {kRevisit}.
  bool Recurse(Node* node) {
    size_t id = static_cast<size_t>(node->id());
    if (id < state_.size()) {
      if (state_[id] != kRevisit && state_[id] != kUnvisited) return false;
    } else {
      state_.resize((3 * id) / 2, kUnvisited);
    }
    Push(node);
    return true;
  }

  void Push(Node* node) {
    state_[node->id()] = kOnStack;
    stack_.push_back(node);
  }
};

void ControlReducer::ReduceGraph(Zone* zone, JSGraph* jsgraph,
                                 CommonOperatorBuilder* common) {
  ControlReducerImpl impl(zone, jsgraph, NULL);
  // Only trim the graph for now. Control reduction can reduce non-terminating
  // loops to graphs that are unschedulable at the moment.
  impl.Trim();
}


void ControlReducer::TrimGraph(Zone* zone, JSGraph* jsgraph) {
  ControlReducerImpl impl(zone, jsgraph, NULL);
  impl.Trim();
}
}
}
}  // namespace v8::internal::compiler
