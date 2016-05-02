// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_CHANGE_LOWERING_H_
#define V8_COMPILER_CHANGE_LOWERING_H_

#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
struct ElementAccess;
class JSGraph;
class Linkage;
class MachineOperatorBuilder;
class Operator;

class ChangeLowering final : public Reducer {
 public:
  explicit ChangeLowering(JSGraph* jsgraph) : jsgraph_(jsgraph) {}
  ~ChangeLowering() final;

  Reduction Reduce(Node* node) final;

 private:
  Reduction ReduceLoadField(Node* node);
  Reduction ReduceStoreField(Node* node);
  Reduction ReduceLoadElement(Node* node);
  Reduction ReduceStoreElement(Node* node);
  Reduction ReduceAllocate(Node* node);

  Node* ComputeIndex(const ElementAccess& access, Node* const key);
  Graph* graph() const;
  Isolate* isolate() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  CommonOperatorBuilder* common() const;
  MachineOperatorBuilder* machine() const;

  JSGraph* const jsgraph_;
  SetOncePointer<const Operator> allocate_operator_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_CHANGE_LOWERING_H_
