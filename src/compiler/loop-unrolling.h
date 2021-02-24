// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LOOP_UNROLLING_H_
#define V8_COMPILER_LOOP_UNROLLING_H_

// Loop unrolling is an optimization that copies the body of a loop and creates
// a fresh loop, whose iteration corresponds to 2 or more iterations of the
// initial loop. For a high-level description of the algorithm see
// docs.google.com/document/d/1AsUCqslMUB6fLdnGq0ZoPk2kn50jIJAWAL77lKXXP5g/

#include "src/compiler/common-operator.h"
#include "src/compiler/loop-analysis.h"

namespace v8 {
namespace internal {
namespace compiler {

void UnrollLoop(Node* loop_node, ZoneUnorderedSet<Node*>* loop, uint32_t depth,
                Graph* graph, CommonOperatorBuilder* common, Zone* tmp_zone,
                SourcePositionTable* source_positions,
                NodeOriginTable* node_origins);

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_LOOP_UNROLLING_H_
