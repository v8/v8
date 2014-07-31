// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simplified-lowering.h"

#include "src/compiler/graph-inl.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace compiler {

Node* SimplifiedLowering::DoChangeTaggedToInt32(Node* node, Node* effect,
                                                Node* control) {
  return node;
}


Node* SimplifiedLowering::DoChangeTaggedToUint32(Node* node, Node* effect,
                                                 Node* control) {
  return node;
}


Node* SimplifiedLowering::DoChangeTaggedToFloat64(Node* node, Node* effect,
                                                  Node* control) {
  return node;
}


Node* SimplifiedLowering::DoChangeInt32ToTagged(Node* node, Node* effect,
                                                Node* control) {
  return node;
}


Node* SimplifiedLowering::DoChangeUint32ToTagged(Node* node, Node* effect,
                                                 Node* control) {
  return node;
}


Node* SimplifiedLowering::DoChangeFloat64ToTagged(Node* node, Node* effect,
                                                  Node* control) {
  return node;
}


Node* SimplifiedLowering::DoChangeBoolToBit(Node* node, Node* effect,
                                            Node* control) {
  Node* val = node->InputAt(0);
  Operator* op = machine()->WordEqual();
  return graph()->NewNode(op, val, jsgraph()->TrueConstant());
}


Node* SimplifiedLowering::DoChangeBitToBool(Node* node, Node* effect,
                                            Node* control) {
  return node;
}


static WriteBarrierKind ComputeWriteBarrierKind(
    MachineRepresentation representation, Type* type) {
  // TODO(turbofan): skip write barriers for Smis, etc.
  if (representation == kMachineTagged) {
    return kFullWriteBarrier;
  }
  return kNoWriteBarrier;
}


Node* SimplifiedLowering::DoLoadField(Node* node, Node* effect, Node* control) {
  const FieldAccess& access = FieldAccessOf(node->op());
  node->set_op(machine_.Load(access.representation));
  Node* offset =
      graph()->NewNode(common()->Int32Constant(access.offset - kHeapObjectTag));
  node->InsertInput(zone(), 1, offset);
  return node;
}


Node* SimplifiedLowering::DoStoreField(Node* node, Node* effect,
                                       Node* control) {
  const FieldAccess& access = FieldAccessOf(node->op());
  WriteBarrierKind kind =
      ComputeWriteBarrierKind(access.representation, access.type);
  node->set_op(machine_.Store(access.representation, kind));
  Node* offset =
      graph()->NewNode(common()->Int32Constant(access.offset - kHeapObjectTag));
  node->InsertInput(zone(), 1, offset);
  return node;
}


Node* SimplifiedLowering::ComputeIndex(const ElementAccess& access,
                                       Node* index) {
  int element_size = 0;
  switch (access.representation) {
    case kMachineTagged:
      element_size = kPointerSize;
      break;
    case kMachineWord8:
      element_size = 1;
      break;
    case kMachineWord16:
      element_size = 2;
      break;
    case kMachineWord32:
      element_size = 4;
      break;
    case kMachineWord64:
    case kMachineFloat64:
      element_size = 8;
      break;
    case kMachineLast:
      UNREACHABLE();
      break;
  }
  if (element_size != 1) {
    index = graph()->NewNode(
        machine()->Int32Mul(),
        graph()->NewNode(common()->Int32Constant(element_size)), index);
  }
  int fixed_offset = access.header_size - kHeapObjectTag;
  if (fixed_offset == 0) return index;
  return graph()->NewNode(
      machine()->Int32Add(),
      graph()->NewNode(common()->Int32Constant(fixed_offset)), index);
}


Node* SimplifiedLowering::DoLoadElement(Node* node, Node* effect,
                                        Node* control) {
  const ElementAccess& access = ElementAccessOf(node->op());
  node->set_op(machine_.Load(access.representation));
  node->ReplaceInput(1, ComputeIndex(access, node->InputAt(1)));
  return node;
}


Node* SimplifiedLowering::DoStoreElement(Node* node, Node* effect,
                                         Node* control) {
  const ElementAccess& access = ElementAccessOf(node->op());
  WriteBarrierKind kind =
      ComputeWriteBarrierKind(access.representation, access.type);
  node->set_op(machine_.Store(access.representation, kind));
  node->ReplaceInput(1, ComputeIndex(access, node->InputAt(1)));
  return node;
}


void SimplifiedLowering::Lower(Node* node) {
  Node* start = graph()->start();
  switch (node->opcode()) {
    case IrOpcode::kBooleanNot:
    case IrOpcode::kNumberEqual:
    case IrOpcode::kNumberLessThan:
    case IrOpcode::kNumberLessThanOrEqual:
    case IrOpcode::kNumberAdd:
    case IrOpcode::kNumberSubtract:
    case IrOpcode::kNumberMultiply:
    case IrOpcode::kNumberDivide:
    case IrOpcode::kNumberModulus:
    case IrOpcode::kNumberToInt32:
    case IrOpcode::kNumberToUint32:
    case IrOpcode::kReferenceEqual:
    case IrOpcode::kStringEqual:
    case IrOpcode::kStringLessThan:
    case IrOpcode::kStringLessThanOrEqual:
    case IrOpcode::kStringAdd:
      break;
    case IrOpcode::kChangeTaggedToInt32:
      DoChangeTaggedToInt32(node, start, start);
      break;
    case IrOpcode::kChangeTaggedToUint32:
      DoChangeTaggedToUint32(node, start, start);
      break;
    case IrOpcode::kChangeTaggedToFloat64:
      DoChangeTaggedToFloat64(node, start, start);
      break;
    case IrOpcode::kChangeInt32ToTagged:
      DoChangeInt32ToTagged(node, start, start);
      break;
    case IrOpcode::kChangeUint32ToTagged:
      DoChangeUint32ToTagged(node, start, start);
      break;
    case IrOpcode::kChangeFloat64ToTagged:
      DoChangeFloat64ToTagged(node, start, start);
      break;
    case IrOpcode::kChangeBoolToBit:
      node->ReplaceUses(DoChangeBoolToBit(node, start, start));
      break;
    case IrOpcode::kChangeBitToBool:
      DoChangeBitToBool(node, start, start);
      break;
    case IrOpcode::kLoadField:
      DoLoadField(node, start, start);
      break;
    case IrOpcode::kStoreField:
      DoStoreField(node, start, start);
      break;
    case IrOpcode::kLoadElement:
      DoLoadElement(node, start, start);
      break;
    case IrOpcode::kStoreElement:
      DoStoreElement(node, start, start);
      break;
    default:
      break;
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
