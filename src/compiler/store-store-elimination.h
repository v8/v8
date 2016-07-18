// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_STORE_STORE_ELIMINATION_H_
#define V8_COMPILER_STORE_STORE_ELIMINATION_H_

#include "src/compiler/common-operator.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
class JSGraph;

class StoreStoreElimination final {
 public:
  StoreStoreElimination(JSGraph* js_graph, Zone* temp_zone);
  ~StoreStoreElimination();
  void Run();

 private:
  static bool IsEligibleNode(Node* node);
  void ReduceEligibleNode(Node* node);
  JSGraph* jsgraph() const { return jsgraph_; }
  Zone* temp_zone() const { return temp_zone_; }

  JSGraph* const jsgraph_;
  Zone* const temp_zone_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_STORE_STORE_ELIMINATION_H_
