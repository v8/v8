// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_OPERATOR_PROPERTIES_INL_H_
#define V8_COMPILER_OPERATOR_PROPERTIES_INL_H_

#include "src/v8.h"

#include "src/compiler/opcodes.h"
#include "src/compiler/operator-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

inline int OperatorProperties::GetValueOutputCount(Operator* op) {
  return op->OutputCount();
}

inline int OperatorProperties::GetValueInputCount(Operator* op) {
  return op->InputCount();
}

inline int OperatorProperties::GetControlInputCount(Operator* op) {
  switch (op->opcode()) {
    case IrOpcode::kPhi:
    case IrOpcode::kEffectPhi:
      return 1;
#define OPCODE_CASE(x) case IrOpcode::k##x:
      CONTROL_OP_LIST(OPCODE_CASE)
#undef OPCODE_CASE
      return static_cast<ControlOperator*>(op)->ControlInputCount();
    default:
      // Operators that have write effects must have a control
      // dependency. Effect dependencies only ensure the correct order of
      // write/read operations without consideration of control flow. Without an
      // explicit control dependency writes can be float in the schedule too
      // early along a path that shouldn't generate a side-effect.
      return op->HasProperty(Operator::kNoWrite) ? 0 : 1;
  }
  return 0;
}

inline int OperatorProperties::GetEffectInputCount(Operator* op) {
  if (op->opcode() == IrOpcode::kEffectPhi) {
    return static_cast<Operator1<int>*>(op)->parameter();
  }
  if (op->HasProperty(Operator::kNoRead) && op->HasProperty(Operator::kNoWrite))
    return 0;  // no effects.
  return 1;
}

inline bool OperatorProperties::HasContextInput(Operator* op) {
  IrOpcode::Value opcode = static_cast<IrOpcode::Value>(op->opcode());
  return IrOpcode::IsJsOpcode(opcode);
}

inline bool OperatorProperties::IsBasicBlockBegin(Operator* op) {
  uint8_t opcode = op->opcode();
  return opcode == IrOpcode::kStart || opcode == IrOpcode::kEnd ||
         opcode == IrOpcode::kDead || opcode == IrOpcode::kLoop ||
         opcode == IrOpcode::kMerge || opcode == IrOpcode::kIfTrue ||
         opcode == IrOpcode::kIfFalse;
}

inline bool OperatorProperties::CanBeScheduled(Operator* op) { return true; }

inline bool OperatorProperties::HasFixedSchedulePosition(Operator* op) {
  IrOpcode::Value opcode = static_cast<IrOpcode::Value>(op->opcode());
  return (IrOpcode::IsControlOpcode(opcode)) ||
         opcode == IrOpcode::kParameter || opcode == IrOpcode::kEffectPhi ||
         opcode == IrOpcode::kPhi;
}

inline bool OperatorProperties::IsScheduleRoot(Operator* op) {
  uint8_t opcode = op->opcode();
  return opcode == IrOpcode::kEnd || opcode == IrOpcode::kEffectPhi ||
         opcode == IrOpcode::kPhi;
}

inline bool OperatorProperties::CanLazilyDeoptimize(Operator* op) {
  if (op->opcode() == IrOpcode::kCall) {
    CallOperator* call_op = reinterpret_cast<CallOperator*>(op);
    CallDescriptor* descriptor = call_op->parameter();
    return descriptor->CanLazilyDeoptimize();
  }
  if (op->opcode() == IrOpcode::kJSCallRuntime) {
    // TODO(jarin) At the moment, we only support lazy deoptimization for
    // the %DeoptimizeFunction runtime function.
    Runtime::FunctionId function =
        reinterpret_cast<Operator1<Runtime::FunctionId>*>(op)->parameter();
    return function == Runtime::kDeoptimizeFunction;
  }
  return false;
}
}
}
}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_OPERATOR_PROPERTIES_INL_H_
