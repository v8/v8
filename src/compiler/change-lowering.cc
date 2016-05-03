// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/change-lowering.h"

#include "src/compiler/js-graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

ChangeLowering::~ChangeLowering() {}


Reduction ChangeLowering::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kLoadField:
      return ReduceLoadField(node);
    case IrOpcode::kStoreField:
      return ReduceStoreField(node);
    case IrOpcode::kLoadElement:
      return ReduceLoadElement(node);
    case IrOpcode::kStoreElement:
      return ReduceStoreElement(node);
    case IrOpcode::kAllocate:
      return ReduceAllocate(node);
    default:
      return NoChange();
  }
  UNREACHABLE();
  return NoChange();
}

Reduction ChangeLowering::ReduceLoadField(Node* node) {
  const FieldAccess& access = FieldAccessOf(node->op());
  Node* offset = jsgraph()->IntPtrConstant(access.offset - access.tag());
  node->InsertInput(graph()->zone(), 1, offset);
  NodeProperties::ChangeOp(node, machine()->Load(access.machine_type));
  return Changed(node);
}

Reduction ChangeLowering::ReduceStoreField(Node* node) {
  const FieldAccess& access = FieldAccessOf(node->op());
  Node* offset = jsgraph()->IntPtrConstant(access.offset - access.tag());
  node->InsertInput(graph()->zone(), 1, offset);
  NodeProperties::ChangeOp(node, machine()->Store(StoreRepresentation(
                                     access.machine_type.representation(),
                                     access.write_barrier_kind)));
  return Changed(node);
}


Node* ChangeLowering::ComputeIndex(const ElementAccess& access,
                                   Node* const key) {
  Node* index = key;
  const int element_size_shift =
      ElementSizeLog2Of(access.machine_type.representation());
  if (element_size_shift) {
    index = graph()->NewNode(machine()->Word32Shl(), index,
                             jsgraph()->Int32Constant(element_size_shift));
  }
  const int fixed_offset = access.header_size - access.tag();
  if (fixed_offset) {
    index = graph()->NewNode(machine()->Int32Add(), index,
                             jsgraph()->Int32Constant(fixed_offset));
  }
  if (machine()->Is64()) {
    // TODO(turbofan): This is probably only correct for typed arrays, and only
    // if the typed arrays are at most 2GiB in size, which happens to match
    // exactly our current situation.
    index = graph()->NewNode(machine()->ChangeUint32ToUint64(), index);
  }
  return index;
}

Reduction ChangeLowering::ReduceLoadElement(Node* node) {
  const ElementAccess& access = ElementAccessOf(node->op());
  node->ReplaceInput(1, ComputeIndex(access, node->InputAt(1)));
  NodeProperties::ChangeOp(node, machine()->Load(access.machine_type));
  return Changed(node);
}

Reduction ChangeLowering::ReduceStoreElement(Node* node) {
  const ElementAccess& access = ElementAccessOf(node->op());
  node->ReplaceInput(1, ComputeIndex(access, node->InputAt(1)));
  NodeProperties::ChangeOp(node, machine()->Store(StoreRepresentation(
                                     access.machine_type.representation(),
                                     access.write_barrier_kind)));
  return Changed(node);
}

Reduction ChangeLowering::ReduceAllocate(Node* node) {
  PretenureFlag pretenure = OpParameter<PretenureFlag>(node->op());
  Node* target = pretenure == NOT_TENURED
                     ? jsgraph()->AllocateInNewSpaceStubConstant()
                     : jsgraph()->AllocateInOldSpaceStubConstant();
  node->InsertInput(graph()->zone(), 0, target);
  if (!allocate_operator_.is_set()) {
    CallDescriptor* descriptor =
        Linkage::GetAllocateCallDescriptor(graph()->zone());
    allocate_operator_.set(common()->Call(descriptor));
  }
  NodeProperties::ChangeOp(node, allocate_operator_.get());
  return Changed(node);
}

Isolate* ChangeLowering::isolate() const { return jsgraph()->isolate(); }


Graph* ChangeLowering::graph() const { return jsgraph()->graph(); }


CommonOperatorBuilder* ChangeLowering::common() const {
  return jsgraph()->common();
}


MachineOperatorBuilder* ChangeLowering::machine() const {
  return jsgraph()->machine();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
