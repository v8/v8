// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/change-lowering.h"

#include "src/address-map.h"
#include "src/code-factory.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

ChangeLowering::~ChangeLowering() {}


Reduction ChangeLowering::Reduce(Node* node) {
  Node* control = graph()->start();
  switch (node->opcode()) {
    case IrOpcode::kChangeBitToBool:
      return ReduceChangeBitToBool(node->InputAt(0), control);
    case IrOpcode::kChangeBoolToBit:
      return ReduceChangeBoolToBit(node->InputAt(0));
    case IrOpcode::kChangeInt31ToTagged:
      return ReduceChangeInt31ToTagged(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedSignedToInt32:
      return ReduceChangeTaggedSignedToInt32(node->InputAt(0));
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
    case IrOpcode::kObjectIsSmi:
      return ReduceObjectIsSmi(node);
    default:
      return NoChange();
  }
  UNREACHABLE();
  return NoChange();
}

Node* ChangeLowering::SmiShiftBitsConstant() {
  return jsgraph()->IntPtrConstant(kSmiShiftSize + kSmiTagSize);
}

Node* ChangeLowering::ChangeInt32ToSmi(Node* value) {
  if (machine()->Is64()) {
    value = graph()->NewNode(machine()->ChangeInt32ToInt64(), value);
  }
  return graph()->NewNode(machine()->WordShl(), value, SmiShiftBitsConstant());
}

Node* ChangeLowering::ChangeSmiToWord32(Node* value) {
  value = graph()->NewNode(machine()->WordSar(), value, SmiShiftBitsConstant());
  if (machine()->Is64()) {
    value = graph()->NewNode(machine()->TruncateInt64ToInt32(), value);
  }
  return value;
}


Node* ChangeLowering::ChangeUint32ToFloat64(Node* value) {
  return graph()->NewNode(machine()->ChangeUint32ToFloat64(), value);
}

Reduction ChangeLowering::ReduceChangeBitToBool(Node* value, Node* control) {
  return Replace(
      graph()->NewNode(common()->Select(MachineRepresentation::kTagged), value,
                       jsgraph()->TrueConstant(), jsgraph()->FalseConstant()));
}

Reduction ChangeLowering::ReduceChangeBoolToBit(Node* value) {
  return Replace(graph()->NewNode(machine()->WordEqual(), value,
                                  jsgraph()->TrueConstant()));
}

Reduction ChangeLowering::ReduceChangeInt31ToTagged(Node* value,
                                                    Node* control) {
  return Replace(ChangeInt32ToSmi(value));
}

Reduction ChangeLowering::ReduceChangeTaggedSignedToInt32(Node* value) {
  return Replace(ChangeSmiToWord32(value));
}

namespace {

WriteBarrierKind ComputeWriteBarrierKind(BaseTaggedness base_is_tagged,
                                         MachineRepresentation representation,
                                         Node* value) {
  // TODO(bmeurer): Optimize write barriers based on input.
  if (base_is_tagged == kTaggedBase &&
      representation == MachineRepresentation::kTagged) {
    if (value->opcode() == IrOpcode::kHeapConstant) {
      return kPointerWriteBarrier;
    } else if (value->opcode() == IrOpcode::kNumberConstant) {
      double const number_value = OpParameter<double>(value);
      if (IsSmiDouble(number_value)) return kNoWriteBarrier;
      return kPointerWriteBarrier;
    }
    return kFullWriteBarrier;
  }
  return kNoWriteBarrier;
}

WriteBarrierKind ComputeWriteBarrierKind(BaseTaggedness base_is_tagged,
                                         MachineRepresentation representation,
                                         int field_offset, Node* value) {
  if (base_is_tagged == kTaggedBase && field_offset == HeapObject::kMapOffset) {
    // Write barriers for storing maps are cheaper.
    return kMapWriteBarrier;
  }
  return ComputeWriteBarrierKind(base_is_tagged, representation, value);
}

}  // namespace

Reduction ChangeLowering::ReduceLoadField(Node* node) {
  const FieldAccess& access = FieldAccessOf(node->op());
  Node* offset = jsgraph()->IntPtrConstant(access.offset - access.tag());
  node->InsertInput(graph()->zone(), 1, offset);
  NodeProperties::ChangeOp(node, machine()->Load(access.machine_type));
  return Changed(node);
}

Reduction ChangeLowering::ReduceStoreField(Node* node) {
  const FieldAccess& access = FieldAccessOf(node->op());
  WriteBarrierKind kind = ComputeWriteBarrierKind(
      access.base_is_tagged, access.machine_type.representation(),
      access.offset, node->InputAt(1));
  Node* offset = jsgraph()->IntPtrConstant(access.offset - access.tag());
  node->InsertInput(graph()->zone(), 1, offset);
  NodeProperties::ChangeOp(node,
                           machine()->Store(StoreRepresentation(
                               access.machine_type.representation(), kind)));
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
  NodeProperties::ChangeOp(
      node, machine()->Store(StoreRepresentation(
                access.machine_type.representation(),
                ComputeWriteBarrierKind(access.base_is_tagged,
                                        access.machine_type.representation(),
                                        node->InputAt(2)))));
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

Reduction ChangeLowering::ReduceObjectIsSmi(Node* node) {
  node->ReplaceInput(0,
                     graph()->NewNode(machine()->WordAnd(), node->InputAt(0),
                                      jsgraph()->IntPtrConstant(kSmiTagMask)));
  node->AppendInput(graph()->zone(), jsgraph()->IntPtrConstant(kSmiTag));
  NodeProperties::ChangeOp(node, machine()->WordEqual());
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
