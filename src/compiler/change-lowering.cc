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
  Node* effect = control;
  switch (node->opcode()) {
    case IrOpcode::kChangeBitToBool:
      return ChangeBitToBool(node->InputAt(0), control);
    case IrOpcode::kChangeBoolToBit:
      return ChangeBoolToBit(node->InputAt(0));
    case IrOpcode::kChangeInt32ToTagged:
      return ChangeInt32ToTagged(node->InputAt(0), effect, control);
    case IrOpcode::kChangeTaggedToFloat64:
      return ChangeTaggedToFloat64(node->InputAt(0), effect, control);
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
  const int smi_shift_size = (machine()->is64() ? SmiTagging<8>::kSmiShiftSize
                                                : SmiTagging<4>::kSmiShiftSize);
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


Reduction ChangeLowering::ChangeInt32ToTagged(Node* val, Node* effect,
                                              Node* control) {
  if (machine()->is64()) {
    return Replace(
        graph()->NewNode(machine()->WordShl(), val, SmiShiftBitsConstant()));
  }

  Node* context = jsgraph()->SmiConstant(0);

  Node* add = graph()->NewNode(machine()->Int32AddWithOverflow(), val, val);
  Node* ovf = graph()->NewNode(common()->Projection(1), add);

  Node* branch = graph()->NewNode(common()->Branch(), ovf, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* number = graph()->NewNode(machine()->ChangeInt32ToFloat64(), val);

  // TODO(bmeurer): Inline allocation if possible.
  const Runtime::Function* fn =
      Runtime::FunctionForId(Runtime::kAllocateHeapNumber);
  DCHECK_EQ(0, fn->nargs);
  CallDescriptor* desc = linkage()->GetRuntimeCallDescriptor(
      fn->function_id, 0, Operator::kNoProperties);
  Node* heap_number = graph()->NewNode(
      common()->Call(desc), jsgraph()->CEntryStubConstant(),
      jsgraph()->ExternalConstant(ExternalReference(fn, isolate())),
      jsgraph()->ZeroConstant(), context, effect, if_true);

  Node* store = graph()->NewNode(
      machine()->Store(kMachFloat64, kNoWriteBarrier), heap_number,
      HeapNumberValueIndexConstant(), number, effect, heap_number);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* smi = graph()->NewNode(common()->Projection(0), add);

  Node* merge = graph()->NewNode(common()->Merge(2), store, if_false);
  Node* phi = graph()->NewNode(common()->Phi(2), heap_number, smi, merge);

  return Replace(phi);
}


Reduction ChangeLowering::ChangeTaggedToFloat64(Node* val, Node* effect,
                                                Node* control) {
  STATIC_ASSERT(kSmiTagMask == 1);

  Node* tag = graph()->NewNode(machine()->WordAnd(), val,
                               jsgraph()->Int32Constant(kSmiTagMask));
  Node* branch = graph()->NewNode(common()->Branch(), tag, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* load = graph()->NewNode(machine()->Load(kMachFloat64), val,
                                HeapNumberValueIndexConstant(), if_true);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* integer =
      graph()->NewNode(machine()->WordSar(), val, SmiShiftBitsConstant());
  Node* number = graph()->NewNode(
      machine()->ChangeInt32ToFloat64(),
      machine()->is64()
          ? graph()->NewNode(machine()->ConvertInt64ToInt32(), integer)
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

}  // namespace compiler
}  // namespace internal
}  // namespace v8
