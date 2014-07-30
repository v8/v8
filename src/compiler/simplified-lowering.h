// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SIMPLIFIED_LOWERING_H_
#define V8_COMPILER_SIMPLIFIED_LOWERING_H_

#include "src/compiler/graph-reducer.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/lowering-builder.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

class SimplifiedLowering : public LoweringBuilder {
 public:
  explicit SimplifiedLowering(JSGraph* jsgraph,
                              SourcePositionTable* source_positions)
      : LoweringBuilder(jsgraph->graph(), source_positions),
        jsgraph_(jsgraph),
        machine_(jsgraph->zone()) {}
  virtual ~SimplifiedLowering() {}

  virtual void Lower(Node* node);

 private:
  JSGraph* jsgraph_;
  MachineOperatorBuilder machine_;

  Node* DoChangeTaggedToInt32(Node* node, Node* effect, Node* control);
  Node* DoChangeTaggedToUint32(Node* node, Node* effect, Node* control);
  Node* DoChangeTaggedToFloat64(Node* node, Node* effect, Node* control);
  Node* DoChangeInt32ToTagged(Node* node, Node* effect, Node* control);
  Node* DoChangeUint32ToTagged(Node* node, Node* effect, Node* control);
  Node* DoChangeFloat64ToTagged(Node* node, Node* effect, Node* control);
  Node* DoChangeBoolToBit(Node* node, Node* effect, Node* control);
  Node* DoChangeBitToBool(Node* node, Node* effect, Node* control);
  Node* DoLoadField(Node* node, Node* effect, Node* control);
  Node* DoStoreField(Node* node, Node* effect, Node* control);
  Node* DoLoadElement(Node* node, Node* effect, Node* control);
  Node* DoStoreElement(Node* node, Node* effect, Node* control);

  Node* ComputeIndex(const ElementAccess& access, Node* index);

  Zone* zone() { return jsgraph_->zone(); }
  JSGraph* jsgraph() { return jsgraph_; }
  Graph* graph() { return jsgraph()->graph(); }
  CommonOperatorBuilder* common() { return jsgraph()->common(); }
  MachineOperatorBuilder* machine() { return &machine_; }
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SIMPLIFIED_LOWERING_H_
