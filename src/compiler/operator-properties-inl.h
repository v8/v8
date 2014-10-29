// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_OPERATOR_PROPERTIES_INL_H_
#define V8_COMPILER_OPERATOR_PROPERTIES_INL_H_

#include "src/compiler/common-operator.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

inline bool OperatorProperties::HasValueInput(const Operator* op) {
  return op->ValueInputCount() > 0;
}

inline bool OperatorProperties::HasContextInput(const Operator* op) {
  IrOpcode::Value opcode = static_cast<IrOpcode::Value>(op->opcode());
  return IrOpcode::IsJsOpcode(opcode);
}

inline bool OperatorProperties::HasEffectInput(const Operator* op) {
  return op->EffectInputCount() > 0;
}

inline bool OperatorProperties::HasControlInput(const Operator* op) {
  return op->ControlInputCount() > 0;
}

inline bool OperatorProperties::HasFrameStateInput(const Operator* op) {
  if (!FLAG_turbo_deoptimization) {
    return false;
  }

  switch (op->opcode()) {
    case IrOpcode::kFrameState:
      return true;
    case IrOpcode::kJSCallRuntime: {
      const CallRuntimeParameters& p = CallRuntimeParametersOf(op);
      return Linkage::NeedsFrameState(p.id());
    }

    // Strict equality cannot lazily deoptimize.
    case IrOpcode::kJSStrictEqual:
    case IrOpcode::kJSStrictNotEqual:
      return false;

    // Calls
    case IrOpcode::kJSCallFunction:
    case IrOpcode::kJSCallConstruct:

    // Compare operations
    case IrOpcode::kJSEqual:
    case IrOpcode::kJSGreaterThan:
    case IrOpcode::kJSGreaterThanOrEqual:
    case IrOpcode::kJSHasProperty:
    case IrOpcode::kJSInstanceOf:
    case IrOpcode::kJSLessThan:
    case IrOpcode::kJSLessThanOrEqual:
    case IrOpcode::kJSNotEqual:

    // Binary operations
    case IrOpcode::kJSAdd:
    case IrOpcode::kJSBitwiseAnd:
    case IrOpcode::kJSBitwiseOr:
    case IrOpcode::kJSBitwiseXor:
    case IrOpcode::kJSDivide:
    case IrOpcode::kJSLoadNamed:
    case IrOpcode::kJSLoadProperty:
    case IrOpcode::kJSModulus:
    case IrOpcode::kJSMultiply:
    case IrOpcode::kJSShiftLeft:
    case IrOpcode::kJSShiftRight:
    case IrOpcode::kJSShiftRightLogical:
    case IrOpcode::kJSStoreNamed:
    case IrOpcode::kJSStoreProperty:
    case IrOpcode::kJSSubtract:

    // Conversions
    case IrOpcode::kJSToObject:

    // Other
    case IrOpcode::kJSDeleteProperty:
      return true;

    default:
      return false;
  }
}

inline int OperatorProperties::GetValueInputCount(const Operator* op) {
  return op->ValueInputCount();
}

inline int OperatorProperties::GetContextInputCount(const Operator* op) {
  return OperatorProperties::HasContextInput(op) ? 1 : 0;
}

inline int OperatorProperties::GetFrameStateInputCount(const Operator* op) {
  return OperatorProperties::HasFrameStateInput(op) ? 1 : 0;
}

inline int OperatorProperties::GetEffectInputCount(const Operator* op) {
  return op->EffectInputCount();
}

inline int OperatorProperties::GetControlInputCount(const Operator* op) {
  return op->ControlInputCount();
}

inline int OperatorProperties::GetTotalInputCount(const Operator* op) {
  return GetValueInputCount(op) + GetContextInputCount(op) +
         GetFrameStateInputCount(op) + GetEffectInputCount(op) +
         GetControlInputCount(op);
}

// -----------------------------------------------------------------------------
// Output properties.

inline bool OperatorProperties::HasValueOutput(const Operator* op) {
  return op->ValueOutputCount() > 0;
}

inline bool OperatorProperties::HasEffectOutput(const Operator* op) {
  return op->EffectOutputCount() > 0;
}

inline bool OperatorProperties::HasControlOutput(const Operator* op) {
  return op->ControlOutputCount() > 0;
}


inline int OperatorProperties::GetValueOutputCount(const Operator* op) {
  return op->ValueOutputCount();
}

inline int OperatorProperties::GetEffectOutputCount(const Operator* op) {
  return op->EffectOutputCount();
}

inline int OperatorProperties::GetControlOutputCount(const Operator* op) {
  return op->ControlOutputCount();
}


inline bool OperatorProperties::IsBasicBlockBegin(const Operator* op) {
  uint8_t opcode = op->opcode();
  return opcode == IrOpcode::kStart || opcode == IrOpcode::kEnd ||
         opcode == IrOpcode::kDead || opcode == IrOpcode::kLoop ||
         opcode == IrOpcode::kMerge || opcode == IrOpcode::kIfTrue ||
         opcode == IrOpcode::kIfFalse;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_OPERATOR_PROPERTIES_INL_H_
