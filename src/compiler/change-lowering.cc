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
      return ChangeBitToBool(node->InputAt(0), control);
    case IrOpcode::kChangeBoolToBit:
      return ChangeBoolToBit(node->InputAt(0));
    case IrOpcode::kChangeInt31ToTagged:
      return ChangeInt31ToTagged(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedSignedToInt32:
      return ChangeTaggedSignedToInt32(node->InputAt(0));
    case IrOpcode::kLoadField:
      return LoadField(node);
    case IrOpcode::kStoreField:
      return StoreField(node);
    case IrOpcode::kLoadElement:
      return LoadElement(node);
    case IrOpcode::kStoreElement:
      return StoreElement(node);
    case IrOpcode::kAllocate:
      return Allocate(node);
    case IrOpcode::kObjectIsSmi:
      return ObjectIsSmi(node);
    case IrOpcode::kChangeInt32ToTagged:
    case IrOpcode::kChangeUint32ToTagged:
    case IrOpcode::kChangeFloat64ToTagged:
      FATAL("Changes should be already lowered during effect linearization.");
      break;
    default:
      return NoChange();
  }
  UNREACHABLE();
  return NoChange();
}


Node* ChangeLowering::HeapNumberValueIndexConstant() {
  return jsgraph()->IntPtrConstant(HeapNumber::kValueOffset - kHeapObjectTag);
}

Node* ChangeLowering::SmiShiftBitsConstant() {
  return jsgraph()->IntPtrConstant(kSmiShiftSize + kSmiTagSize);
}

Node* ChangeLowering::ChangeInt32ToFloat64(Node* value) {
  return graph()->NewNode(machine()->ChangeInt32ToFloat64(), value);
}

Node* ChangeLowering::ChangeInt32ToSmi(Node* value) {
  if (machine()->Is64()) {
    value = graph()->NewNode(machine()->ChangeInt32ToInt64(), value);
  }
  return graph()->NewNode(machine()->WordShl(), value, SmiShiftBitsConstant());
}


Node* ChangeLowering::ChangeSmiToFloat64(Node* value) {
  return ChangeInt32ToFloat64(ChangeSmiToWord32(value));
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


Node* ChangeLowering::ChangeUint32ToSmi(Node* value) {
  if (machine()->Is64()) {
    value = graph()->NewNode(machine()->ChangeUint32ToUint64(), value);
  }
  return graph()->NewNode(machine()->WordShl(), value, SmiShiftBitsConstant());
}


Node* ChangeLowering::LoadHeapNumberValue(Node* value, Node* control) {
  return graph()->NewNode(machine()->Load(MachineType::Float64()), value,
                          HeapNumberValueIndexConstant(), graph()->start(),
                          control);
}


Node* ChangeLowering::TestNotSmi(Node* value) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagMask == 1);
  return graph()->NewNode(machine()->WordAnd(), value,
                          jsgraph()->IntPtrConstant(kSmiTagMask));
}


Reduction ChangeLowering::ChangeBitToBool(Node* value, Node* control) {
  return Replace(
      graph()->NewNode(common()->Select(MachineRepresentation::kTagged), value,
                       jsgraph()->TrueConstant(), jsgraph()->FalseConstant()));
}


Reduction ChangeLowering::ChangeBoolToBit(Node* value) {
  return Replace(graph()->NewNode(machine()->WordEqual(), value,
                                  jsgraph()->TrueConstant()));
}

Reduction ChangeLowering::ChangeInt31ToTagged(Node* value, Node* control) {
  return Replace(ChangeInt32ToSmi(value));
}

Reduction ChangeLowering::ChangeTaggedSignedToInt32(Node* value) {
  return Replace(ChangeSmiToWord32(value));
}

namespace {

WriteBarrierKind ComputeWriteBarrierKind(BaseTaggedness base_is_tagged,
                                         MachineRepresentation representation,
                                         Type* field_type, Type* input_type) {
  if (field_type->Is(Type::TaggedSigned()) ||
      input_type->Is(Type::TaggedSigned())) {
    // Write barriers are only for writes of heap objects.
    return kNoWriteBarrier;
  }
  if (input_type->Is(Type::BooleanOrNullOrUndefined())) {
    // Write barriers are not necessary when storing true, false, null or
    // undefined, because these special oddballs are always in the root set.
    return kNoWriteBarrier;
  }
  if (base_is_tagged == kTaggedBase &&
      representation == MachineRepresentation::kTagged) {
    if (input_type->IsConstant() &&
        input_type->AsConstant()->Value()->IsHeapObject()) {
      Handle<HeapObject> input =
          Handle<HeapObject>::cast(input_type->AsConstant()->Value());
      if (input->IsMap()) {
        // Write barriers for storing maps are cheaper.
        return kMapWriteBarrier;
      }
      Isolate* const isolate = input->GetIsolate();
      RootIndexMap root_index_map(isolate);
      int root_index = root_index_map.Lookup(*input);
      if (root_index != RootIndexMap::kInvalidRootIndex &&
          isolate->heap()->RootIsImmortalImmovable(root_index)) {
        // Write barriers are unnecessary for immortal immovable roots.
        return kNoWriteBarrier;
      }
    }
    if (field_type->Is(Type::TaggedPointer()) ||
        input_type->Is(Type::TaggedPointer())) {
      // Write barriers for heap objects don't need a Smi check.
      return kPointerWriteBarrier;
    }
    // Write barriers are only for writes into heap objects (i.e. tagged base).
    return kFullWriteBarrier;
  }
  return kNoWriteBarrier;
}


WriteBarrierKind ComputeWriteBarrierKind(BaseTaggedness base_is_tagged,
                                         MachineRepresentation representation,
                                         int field_offset, Type* field_type,
                                         Type* input_type) {
  if (base_is_tagged == kTaggedBase && field_offset == HeapObject::kMapOffset) {
    // Write barriers for storing maps are cheaper.
    return kMapWriteBarrier;
  }
  return ComputeWriteBarrierKind(base_is_tagged, representation, field_type,
                                 input_type);
}

}  // namespace


Reduction ChangeLowering::LoadField(Node* node) {
  const FieldAccess& access = FieldAccessOf(node->op());
  Node* offset = jsgraph()->IntPtrConstant(access.offset - access.tag());
  node->InsertInput(graph()->zone(), 1, offset);
  NodeProperties::ChangeOp(node, machine()->Load(access.machine_type));
  return Changed(node);
}


Reduction ChangeLowering::StoreField(Node* node) {
  const FieldAccess& access = FieldAccessOf(node->op());
  Type* type = NodeProperties::GetType(node->InputAt(1));
  WriteBarrierKind kind = ComputeWriteBarrierKind(
      access.base_is_tagged, access.machine_type.representation(),
      access.offset, access.type, type);
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


Reduction ChangeLowering::LoadElement(Node* node) {
  const ElementAccess& access = ElementAccessOf(node->op());
  node->ReplaceInput(1, ComputeIndex(access, node->InputAt(1)));
  NodeProperties::ChangeOp(node, machine()->Load(access.machine_type));
  return Changed(node);
}


Reduction ChangeLowering::StoreElement(Node* node) {
  const ElementAccess& access = ElementAccessOf(node->op());
  Type* type = NodeProperties::GetType(node->InputAt(2));
  node->ReplaceInput(1, ComputeIndex(access, node->InputAt(1)));
  NodeProperties::ChangeOp(
      node, machine()->Store(StoreRepresentation(
                access.machine_type.representation(),
                ComputeWriteBarrierKind(access.base_is_tagged,
                                        access.machine_type.representation(),
                                        access.type, type))));
  return Changed(node);
}


Reduction ChangeLowering::Allocate(Node* node) {
  PretenureFlag pretenure = OpParameter<PretenureFlag>(node->op());
  Callable callable = CodeFactory::Allocate(isolate(), pretenure);
  Node* target = jsgraph()->HeapConstant(callable.code());
  CallDescriptor* descriptor = Linkage::GetStubCallDescriptor(
      isolate(), jsgraph()->zone(), callable.descriptor(), 0,
      CallDescriptor::kNoFlags, Operator::kNoThrow);
  const Operator* op = common()->Call(descriptor);
  node->InsertInput(graph()->zone(), 0, target);
  node->InsertInput(graph()->zone(), 2, jsgraph()->NoContextConstant());
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

Node* ChangeLowering::IsSmi(Node* value) {
  return graph()->NewNode(
      machine()->WordEqual(),
      graph()->NewNode(machine()->WordAnd(), value,
                       jsgraph()->IntPtrConstant(kSmiTagMask)),
      jsgraph()->IntPtrConstant(kSmiTag));
}

Node* ChangeLowering::LoadHeapObjectMap(Node* object, Node* control) {
  return graph()->NewNode(
      machine()->Load(MachineType::AnyTagged()), object,
      jsgraph()->IntPtrConstant(HeapObject::kMapOffset - kHeapObjectTag),
      graph()->start(), control);
}

Node* ChangeLowering::LoadMapBitField(Node* map) {
  return graph()->NewNode(
      machine()->Load(MachineType::Uint8()), map,
      jsgraph()->IntPtrConstant(Map::kBitFieldOffset - kHeapObjectTag),
      graph()->start(), graph()->start());
}

Node* ChangeLowering::LoadMapInstanceType(Node* map) {
  return graph()->NewNode(
      machine()->Load(MachineType::Uint8()), map,
      jsgraph()->IntPtrConstant(Map::kInstanceTypeOffset - kHeapObjectTag),
      graph()->start(), graph()->start());
}

Reduction ChangeLowering::ObjectIsSmi(Node* node) {
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
