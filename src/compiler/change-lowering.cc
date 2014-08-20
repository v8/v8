// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/change-lowering.h"

#include "src/compiler/js-graph.h"

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
    case IrOpcode::kChangeFloat64ToTagged:
      return ChangeFloat64ToTagged(node->InputAt(0), control);
    case IrOpcode::kChangeInt32ToTagged:
      return ChangeInt32ToTagged(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedToFloat64:
      return ChangeTaggedToFloat64(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedToInt32:
      return ChangeTaggedToInt32(node->InputAt(0), control);
    default:
      return NoChange();
  }
  UNREACHABLE();
  return NoChange();
}


Node* ChangeLowering::HeapNumberValueIndexConstant() {
  STATIC_ASSERT(HeapNumber::kValueOffset % kPointerSize == 0);
  const int heap_number_value_offset =
      ((HeapNumber::kValueOffset / kPointerSize) * (machine()->is64() ? 8 : 4));
  return jsgraph()->Int32Constant(heap_number_value_offset - kHeapObjectTag);
}


Node* ChangeLowering::SmiShiftBitsConstant() {
  // TODO(turbofan): Work-around for weird GCC 4.6 linker issue:
  // src/compiler/change-lowering.cc:46: undefined reference to
  // `v8::internal::SmiTagging<4u>::kSmiShiftSize'
  // src/compiler/change-lowering.cc:46: undefined reference to
  // `v8::internal::SmiTagging<8u>::kSmiShiftSize'
  STATIC_ASSERT(SmiTagging<4>::kSmiShiftSize == 0);
  STATIC_ASSERT(SmiTagging<8>::kSmiShiftSize == 31);
  const int smi_shift_size = machine()->is64() ? 31 : 0;
  return jsgraph()->Int32Constant(smi_shift_size + kSmiTagSize);
}


Reduction ChangeLowering::ChangeBitToBool(Node* val, Node* control) {
  Node* branch = graph()->NewNode(common()->Branch(), val, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* true_value = jsgraph()->TrueConstant();

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* false_value = jsgraph()->FalseConstant();

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi =
      graph()->NewNode(common()->Phi(2), true_value, false_value, merge);

  return Replace(phi);
}


Reduction ChangeLowering::ChangeBoolToBit(Node* val) {
  return Replace(
      graph()->NewNode(machine()->WordEqual(), val, jsgraph()->TrueConstant()));
}


Reduction ChangeLowering::ChangeFloat64ToTagged(Node* val, Node* control) {
  return Replace(AllocateHeapNumberWithValue(val, control));
}


Reduction ChangeLowering::ChangeInt32ToTagged(Node* val, Node* control) {
  if (machine()->is64()) {
    return Replace(
        graph()->NewNode(machine()->Word64Shl(),
                         graph()->NewNode(machine()->ChangeInt32ToInt64(), val),
                         SmiShiftBitsConstant()));
  }

  Node* add = graph()->NewNode(machine()->Int32AddWithOverflow(), val, val);
  Node* ovf = graph()->NewNode(common()->Projection(1), add);

  Node* branch = graph()->NewNode(common()->Branch(), ovf, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* heap_number = AllocateHeapNumberWithValue(
      graph()->NewNode(machine()->ChangeInt32ToFloat64(), val), if_true);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* smi = graph()->NewNode(common()->Projection(0), add);

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi = graph()->NewNode(common()->Phi(2), heap_number, smi, merge);

  return Replace(phi);
}


Reduction ChangeLowering::ChangeTaggedToInt32(Node* val, Node* control) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagMask == 1);

  Node* tag = graph()->NewNode(machine()->WordAnd(), val,
                               jsgraph()->Int32Constant(kSmiTagMask));
  Node* branch = graph()->NewNode(common()->Branch(), tag, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* load = graph()->NewNode(
      machine()->Load(kMachFloat64), val, HeapNumberValueIndexConstant(),
      graph()->NewNode(common()->ControlEffect(), if_true));
  Node* change = graph()->NewNode(machine()->TruncateFloat64ToInt32(), load);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* integer =
      graph()->NewNode(machine()->WordSar(), val, SmiShiftBitsConstant());
  Node* number =
      machine()->is64()
          ? graph()->NewNode(machine()->TruncateInt64ToInt32(), integer)
          : integer;

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi = graph()->NewNode(common()->Phi(2), change, number, merge);

  return Replace(phi);
}


Reduction ChangeLowering::ChangeTaggedToFloat64(Node* val, Node* control) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagMask == 1);

  Node* tag = graph()->NewNode(machine()->WordAnd(), val,
                               jsgraph()->Int32Constant(kSmiTagMask));
  Node* branch = graph()->NewNode(common()->Branch(), tag, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* load = graph()->NewNode(
      machine()->Load(kMachFloat64), val, HeapNumberValueIndexConstant(),
      graph()->NewNode(common()->ControlEffect(), if_true));

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* integer =
      graph()->NewNode(machine()->WordSar(), val, SmiShiftBitsConstant());
  Node* number = graph()->NewNode(
      machine()->ChangeInt32ToFloat64(),
      machine()->is64()
          ? graph()->NewNode(machine()->TruncateInt64ToInt32(), integer)
          : integer);

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi = graph()->NewNode(common()->Phi(2), load, number, merge);

  return Replace(phi);
}


Isolate* ChangeLowering::isolate() const { return jsgraph()->isolate(); }


Graph* ChangeLowering::graph() const { return jsgraph()->graph(); }


CommonOperatorBuilder* ChangeLowering::common() const {
  return jsgraph()->common();
}


Node* ChangeLowering::AllocateHeapNumberWithValue(Node* value, Node* control) {
  // The AllocateHeapNumber() runtime function does not use the context, so we
  // can safely pass in Smi zero here.
  Node* context = jsgraph()->ZeroConstant();
  Node* effect = graph()->NewNode(common()->ValueEffect(1), value);
  const Runtime::Function* function =
      Runtime::FunctionForId(Runtime::kAllocateHeapNumber);
  DCHECK_EQ(0, function->nargs);
  CallDescriptor* desc = linkage()->GetRuntimeCallDescriptor(
      function->function_id, 0, Operator::kNoProperties);
  Node* heap_number = graph()->NewNode(
      common()->Call(desc), jsgraph()->CEntryStubConstant(),
      jsgraph()->ExternalConstant(ExternalReference(function, isolate())),
      jsgraph()->Int32Constant(function->nargs), context, effect, control);
  Node* store = graph()->NewNode(
      machine()->Store(kMachFloat64, kNoWriteBarrier), heap_number,
      HeapNumberValueIndexConstant(), value, heap_number, control);
  return graph()->NewNode(common()->Finish(1), heap_number, store);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
