// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-heap-copy-reducer.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/js-heap-broker.h"

namespace v8 {
namespace internal {
namespace compiler {

JSHeapCopyReducer::JSHeapCopyReducer(JSHeapBroker* broker) : broker_(broker) {}

JSHeapBroker* JSHeapCopyReducer::broker() { return broker_; }

Reduction JSHeapCopyReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      // Call the constructor to make sure the broker copies the data.
      ObjectRef(broker(), HeapConstantOf(node->op()));
      break;
    }
    default:
      break;
  }
  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
