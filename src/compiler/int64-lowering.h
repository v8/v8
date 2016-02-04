// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_INT64_REDUCER_H_
#define V8_COMPILER_INT64_REDUCER_H_

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-marker.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class Int64Lowering {
 public:
  Int64Lowering(Graph* graph, MachineOperatorBuilder* machine,
                CommonOperatorBuilder* common, Zone* zone);

  void ReduceGraph();
  Graph* graph() const { return graph_; }
  MachineOperatorBuilder* machine() const { return machine_; }
  CommonOperatorBuilder* common() const { return common_; }

 private:
  enum class State : uint8_t { kUnvisited, kOnStack, kInputsPushed, kVisited };

  struct Replacement {
    Node* low;
    Node* high;
  };

  void ReduceTop();
  void ReduceNode(Node* node);

  Graph* const graph_;
  MachineOperatorBuilder* machine_;
  CommonOperatorBuilder* common_;
  NodeMarker<State> state_;
  ZoneStack<Node*> stack_;
  Replacement* replacements_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_INT64_REDUCER_H_
