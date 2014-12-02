// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_GENERIC_ALGORITHM_INL_H_
#define V8_COMPILER_GENERIC_ALGORITHM_INL_H_

#include <vector>

#include "src/compiler/generic-algorithm.h"
#include "src/compiler/graph.h"
#include "src/compiler/node.h"

namespace v8 {
namespace internal {
namespace compiler {

template <class N>
class NodeInputIterationTraits {
 public:
  typedef N Node;
  typedef typename N::InputEdges::iterator Iterator;

  static Iterator begin(Node* node) { return node->input_edges().begin(); }
  static Iterator end(Node* node) { return node->input_edges().end(); }
  static int max_id(Graph* graph) { return graph->NodeCount(); }
  static Node* to(Iterator iterator) { return (*iterator).to(); }
  static Node* from(Iterator iterator) { return (*iterator).from(); }
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_GENERIC_ALGORITHM_INL_H_
