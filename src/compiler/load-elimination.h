// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LOAD_ELIMINATION_H_
#define V8_COMPILER_LOAD_ELIMINATION_H_

namespace v8 {
namespace internal {

// Forward declarations.
class Zone;

namespace compiler {

// Forward declarations.
class Graph;

// Eliminates redundant loads via scalar replacement of aggregates.
class LoadElimination final {
 public:
  LoadElimination(Graph* graph, Zone* zone) : graph_(graph), zone_(zone) {}

  void Run();

 private:
  Graph* graph() const { return graph_; }
  Zone* zone() const { return zone_; }

  Graph* const graph_;
  Zone* const zone_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_LOAD_ELIMINATION_H_
