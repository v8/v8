// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CCTEST_COMPILER_GRAPH_TESTER_H_
#define V8_CCTEST_COMPILER_GRAPH_TESTER_H_

#include "src/v8.h"
#include "test/cctest/cctest.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"

namespace v8 {
namespace internal {
namespace compiler {

class GraphTester : public HandleAndZoneScope, public Graph {
 public:
  GraphTester() : Graph(main_zone()) {}
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_COMPILER_GRAPH_TESTER_H_
