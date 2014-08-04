// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simplified-lowering.h"

#include "src/compiler/graph-inl.h"
#include "src/compiler/node-properties-inl.h"
#include "src/objects.h"

namespace v8 {
namespace internal {
namespace compiler {

Node* SimplifiedLowering::IsTagged(Node* node) {
  // TODO(titzer): factor this out to a TaggingScheme abstraction.
  STATIC_ASSERT(kSmiTagMask == 1);  // Only works if tag is the low bit.
  return graph()->NewNode(machine()->WordAnd(), node,
                          jsgraph()->Int32Constant(kSmiTagMask));
}


Node* SimplifiedLowering::Untag(Node* node) {
  // TODO(titzer): factor this out to a TaggingScheme abstraction.
  Node* shift_amount = jsgraph()->Int32Constant(kSmiTagSize + kSmiShiftSize);
  return graph()->NewNode(machine()->WordSar(), node, shift_amount);
}


Node* SimplifiedLowering::SmiTag(Node* node) {
  // TODO(titzer): factor this out to a TaggingScheme abstraction.
  Node* shift_amount = jsgraph()->Int32Constant(kSmiTagSize + kSmiShiftSize);
  return graph()->NewNode(machine()->WordShl(), node, shift_amount);
}


Node* SimplifiedLowering::OffsetMinusTagConstant(int32_t offset) {
  return jsgraph()->Int32Constant(offset - kHeapObjectTag);
}


static void UpdateControlSuccessors(Node* before, Node* node) {
  ASSERT(IrOpcode::IsControlOpcode(before->opcode()));
  UseIter iter = before->uses().begin();
  while (iter != before->uses().end()) {
    if (IrOpcode::IsControlOpcode((*iter)->opcode()) &&
        NodeProperties::IsControlEdge(iter.edge())) {
      iter = iter.UpdateToAndIncrement(node);
      continue;
    }
    ++iter;
  }
}


void SimplifiedLowering::DoChangeTaggedToUI32(Node* node, Node* effect,
                                              Node* control, bool is_signed) {
  // if (IsTagged(val))
  // ConvertFloat64To(Int32|Uint32)(Load[kMachineFloat64](input, #value_offset))
  // else Untag(val)
  Node* val = node->InputAt(0);
  Node* branch = graph()->NewNode(common()->Branch(), IsTagged(val), control);

  // true branch.
  Node* tbranch = graph()->NewNode(common()->IfTrue(), branch);
  Node* loaded = graph()->NewNode(
      machine()->Load(kMachineFloat64), val,
      OffsetMinusTagConstant(HeapNumber::kValueOffset), effect);
  Operator* op = is_signed ? machine()->ChangeFloat64ToInt32()
                           : machine()->ChangeFloat64ToUint32();
  Node* converted = graph()->NewNode(op, loaded);

  // false branch.
  Node* fbranch = graph()->NewNode(common()->IfFalse(), branch);
  Node* untagged = Untag(val);

  // merge.
  Node* merge = graph()->NewNode(common()->Merge(2), tbranch, fbranch);
  Node* phi = graph()->NewNode(common()->Phi(2), converted, untagged, merge);
  UpdateControlSuccessors(control, merge);
  branch->ReplaceInput(1, control);
  node->ReplaceUses(phi);
}


void SimplifiedLowering::DoChangeTaggedToFloat64(Node* node, Node* effect,
                                                 Node* control) {
  // if (IsTagged(input)) Load[kMachineFloat64](input, #value_offset)
  // else ConvertFloat64(Untag(input))
  Node* val = node->InputAt(0);
  Node* branch = graph()->NewNode(common()->Branch(), IsTagged(val), control);

  // true branch.
  Node* tbranch = graph()->NewNode(common()->IfTrue(), branch);
  Node* loaded = graph()->NewNode(
      machine()->Load(kMachineFloat64), val,
      OffsetMinusTagConstant(HeapNumber::kValueOffset), effect);

  // false branch.
  Node* fbranch = graph()->NewNode(common()->IfFalse(), branch);
  Node* untagged = Untag(val);
  Node* converted =
      graph()->NewNode(machine()->ChangeInt32ToFloat64(), untagged);

  // merge.
  Node* merge = graph()->NewNode(common()->Merge(2), tbranch, fbranch);
  Node* phi = graph()->NewNode(common()->Phi(2), loaded, converted, merge);
  UpdateControlSuccessors(control, merge);
  branch->ReplaceInput(1, control);
  node->ReplaceUses(phi);
}


void SimplifiedLowering::DoChangeUI32ToTagged(Node* node, Node* effect,
                                              Node* control, bool is_signed) {
  Node* val = node->InputAt(0);
  Node* is_smi = NULL;
  if (is_signed) {
    if (SmiValuesAre32Bits()) {
      // All int32s fit in this case.
      ASSERT(kPointerSize == 8);
      return node->ReplaceUses(SmiTag(val));
    } else {
      // TODO(turbofan): use an Int32AddWithOverflow to tag and check here.
      Node* lt = graph()->NewNode(machine()->Int32LessThanOrEqual(), val,
                                  jsgraph()->Int32Constant(Smi::kMaxValue));
      Node* gt =
          graph()->NewNode(machine()->Int32LessThanOrEqual(),
                           jsgraph()->Int32Constant(Smi::kMinValue), val);
      is_smi = graph()->NewNode(machine()->Word32And(), lt, gt);
    }
  } else {
    // Check if Uint32 value is in the smi range.
    is_smi = graph()->NewNode(machine()->Uint32LessThanOrEqual(), val,
                              jsgraph()->Int32Constant(Smi::kMaxValue));
  }

  // TODO(turbofan): fold smi test branch eagerly.
  // if (IsSmi(input)) SmiTag(input);
  // else InlineAllocAndInitHeapNumber(ConvertToFloat64(input)))
  Node* branch = graph()->NewNode(common()->Branch(), is_smi, control);

  // true branch.
  Node* tbranch = graph()->NewNode(common()->IfTrue(), branch);
  Node* smi_tagged = SmiTag(val);

  // false branch.
  Node* fbranch = graph()->NewNode(common()->IfFalse(), branch);
  Node* heap_num = jsgraph()->Constant(0.0);  // TODO(titzer): alloc and init

  // merge.
  Node* merge = graph()->NewNode(common()->Merge(2), tbranch, fbranch);
  Node* phi = graph()->NewNode(common()->Phi(2), smi_tagged, heap_num, merge);
  UpdateControlSuccessors(control, merge);
  branch->ReplaceInput(1, control);
  node->ReplaceUses(phi);
}


void SimplifiedLowering::DoChangeFloat64ToTagged(Node* node, Node* effect,
                                                 Node* control) {
  return;  // TODO(titzer): need to call runtime to allocate in one branch
}


void SimplifiedLowering::DoChangeBoolToBit(Node* node, Node* effect,
                                           Node* control) {
  Node* val = node->InputAt(0);
  Operator* op =
      kPointerSize == 8 ? machine()->Word64Equal() : machine()->Word32Equal();
  Node* cmp = graph()->NewNode(op, val, jsgraph()->TrueConstant());
  node->ReplaceUses(cmp);
}


void SimplifiedLowering::DoChangeBitToBool(Node* node, Node* effect,
                                           Node* control) {
  Node* val = node->InputAt(0);
  Node* branch = graph()->NewNode(common()->Branch(), val, control);

  // true branch.
  Node* tbranch = graph()->NewNode(common()->IfTrue(), branch);
  // false branch.
  Node* fbranch = graph()->NewNode(common()->IfFalse(), branch);
  // merge.
  Node* merge = graph()->NewNode(common()->Merge(2), tbranch, fbranch);
  Node* phi = graph()->NewNode(common()->Phi(2), jsgraph()->TrueConstant(),
                               jsgraph()->FalseConstant(), merge);
  UpdateControlSuccessors(control, merge);
  branch->ReplaceInput(1, control);
  node->ReplaceUses(phi);
}


static WriteBarrierKind ComputeWriteBarrierKind(
    MachineRepresentation representation, Type* type) {
  // TODO(turbofan): skip write barriers for Smis, etc.
  if (representation == kMachineTagged) {
    return kFullWriteBarrier;
  }
  return kNoWriteBarrier;
}


void SimplifiedLowering::DoLoadField(Node* node, Node* effect, Node* control) {
  const FieldAccess& access = FieldAccessOf(node->op());
  node->set_op(machine_.Load(access.representation));
  Node* offset =
      graph()->NewNode(common()->Int32Constant(access.offset - kHeapObjectTag));
  node->InsertInput(zone(), 1, offset);
}


void SimplifiedLowering::DoStoreField(Node* node, Node* effect, Node* control) {
  const FieldAccess& access = FieldAccessOf(node->op());
  WriteBarrierKind kind =
      ComputeWriteBarrierKind(access.representation, access.type);
  node->set_op(machine_.Store(access.representation, kind));
  Node* offset =
      graph()->NewNode(common()->Int32Constant(access.offset - kHeapObjectTag));
  node->InsertInput(zone(), 1, offset);
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


void SimplifiedLowering::DoLoadElement(Node* node, Node* effect,
                                       Node* control) {
  const ElementAccess& access = ElementAccessOf(node->op());
  node->set_op(machine_.Load(access.representation));
  node->ReplaceInput(1, ComputeIndex(access, node->InputAt(1)));
}


void SimplifiedLowering::DoStoreElement(Node* node, Node* effect,
                                        Node* control) {
  const ElementAccess& access = ElementAccessOf(node->op());
  WriteBarrierKind kind =
      ComputeWriteBarrierKind(access.representation, access.type);
  node->set_op(machine_.Store(access.representation, kind));
  node->ReplaceInput(1, ComputeIndex(access, node->InputAt(1)));
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
      DoChangeTaggedToUI32(node, start, start, true);
      break;
    case IrOpcode::kChangeTaggedToUint32:
      DoChangeTaggedToUI32(node, start, start, false);
      break;
    case IrOpcode::kChangeTaggedToFloat64:
      DoChangeTaggedToFloat64(node, start, start);
      break;
    case IrOpcode::kChangeInt32ToTagged:
      DoChangeUI32ToTagged(node, start, start, true);
      break;
    case IrOpcode::kChangeUint32ToTagged:
      DoChangeUI32ToTagged(node, start, start, false);
      break;
    case IrOpcode::kChangeFloat64ToTagged:
      DoChangeFloat64ToTagged(node, start, start);
      break;
    case IrOpcode::kChangeBoolToBit:
      DoChangeBoolToBit(node, start, start);
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
