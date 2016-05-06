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
  PretenureFlag const pretenure = OpParameter<PretenureFlag>(node->op());

  Node* size = node->InputAt(0);
  Node* effect = node->InputAt(1);
  Node* control = node->InputAt(2);

  if (machine()->Is64()) {
    size = graph()->NewNode(machine()->ChangeInt32ToInt64(), size);
  }

  Node* top_address = jsgraph()->ExternalConstant(
      pretenure == NOT_TENURED
          ? ExternalReference::new_space_allocation_top_address(isolate())
          : ExternalReference::old_space_allocation_top_address(isolate()));
  Node* limit_address = jsgraph()->ExternalConstant(
      pretenure == NOT_TENURED
          ? ExternalReference::new_space_allocation_limit_address(isolate())
          : ExternalReference::old_space_allocation_limit_address(isolate()));

  Node* top = effect =
      graph()->NewNode(machine()->Load(MachineType::Pointer()), top_address,
                       jsgraph()->IntPtrConstant(0), effect, control);
  Node* limit = effect =
      graph()->NewNode(machine()->Load(MachineType::Pointer()), limit_address,
                       jsgraph()->IntPtrConstant(0), effect, control);

  Node* new_top = graph()->NewNode(machine()->IntAdd(), top, size);

  Node* check = graph()->NewNode(machine()->UintLessThan(), new_top, limit);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = effect;
  Node* vtrue;
  {
    etrue = graph()->NewNode(
        machine()->Store(StoreRepresentation(
            MachineType::PointerRepresentation(), kNoWriteBarrier)),
        top_address, jsgraph()->IntPtrConstant(0), new_top, etrue, if_true);
    vtrue = graph()->NewNode(
        machine()->BitcastWordToTagged(),
        graph()->NewNode(machine()->IntAdd(), top,
                         jsgraph()->IntPtrConstant(kHeapObjectTag)));
  }

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = effect;
  Node* vfalse;
  {
    Node* target = pretenure == NOT_TENURED
                       ? jsgraph()->AllocateInNewSpaceStubConstant()
                       : jsgraph()->AllocateInOldSpaceStubConstant();
    if (!allocate_operator_.is_set()) {
      CallDescriptor* descriptor =
          Linkage::GetAllocateCallDescriptor(graph()->zone());
      allocate_operator_.set(common()->Call(descriptor));
    }
    vfalse = efalse = graph()->NewNode(allocate_operator_.get(), target, size,
                                       efalse, if_false);
  }

  control = graph()->NewNode(common()->Merge(2), if_true, if_false);
  effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
  Node* value = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), vtrue, vfalse, control);

  ReplaceWithValue(node, value, effect);
  return Replace(value);
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
