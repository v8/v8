// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-type-hint-lowering.h"

#include "src/compiler/js-graph.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/feedback-vector.h"
#include "src/type-hints.h"

namespace v8 {
namespace internal {
namespace compiler {

class JSSpeculativeBinopBuilder final {
 public:
  JSSpeculativeBinopBuilder(JSTypeHintLowering* lowering, const Operator* op,
                            Node* left, Node* right, Node* effect,
                            Node* control, FeedbackSlot slot)
      : lowering_(lowering),
        op_(op),
        left_(left),
        right_(right),
        effect_(effect),
        control_(control),
        slot_(slot) {}

  BinaryOperationHint GetBinaryOperationHint() {
    DCHECK_EQ(FeedbackSlotKind::kBinaryOp, feedback_vector()->GetKind(slot_));
    BinaryOpICNexus nexus(feedback_vector(), slot_);
    return nexus.GetBinaryOperationFeedback();
  }

  CompareOperationHint GetCompareOperationHint() {
    DCHECK_EQ(FeedbackSlotKind::kCompareOp, feedback_vector()->GetKind(slot_));
    CompareICNexus nexus(feedback_vector(), slot_);
    return nexus.GetCompareOperationFeedback();
  }

  bool GetBinaryNumberOperationHint(NumberOperationHint* hint) {
    switch (GetBinaryOperationHint()) {
      case BinaryOperationHint::kSignedSmall:
        *hint = NumberOperationHint::kSignedSmall;
        return true;
      case BinaryOperationHint::kSigned32:
        *hint = NumberOperationHint::kSigned32;
        return true;
      case BinaryOperationHint::kNumberOrOddball:
        *hint = NumberOperationHint::kNumberOrOddball;
        return true;
      case BinaryOperationHint::kAny:
      case BinaryOperationHint::kNone:
      case BinaryOperationHint::kString:
        break;
    }
    return false;
  }

  bool GetCompareNumberOperationHint(NumberOperationHint* hint) {
    switch (GetCompareOperationHint()) {
      case CompareOperationHint::kSignedSmall:
        *hint = NumberOperationHint::kSignedSmall;
        return true;
      case CompareOperationHint::kNumber:
        *hint = NumberOperationHint::kNumber;
        return true;
      case CompareOperationHint::kNumberOrOddball:
        *hint = NumberOperationHint::kNumberOrOddball;
        return true;
      case CompareOperationHint::kAny:
      case CompareOperationHint::kNone:
      case CompareOperationHint::kString:
      case CompareOperationHint::kReceiver:
      case CompareOperationHint::kInternalizedString:
        break;
    }
    return false;
  }

  const Operator* SpeculativeNumberOp(NumberOperationHint hint) {
    switch (op_->opcode()) {
      case IrOpcode::kJSAdd:
        return simplified()->SpeculativeNumberAdd(hint);
      case IrOpcode::kJSSubtract:
        return simplified()->SpeculativeNumberSubtract(hint);
      case IrOpcode::kJSMultiply:
        return simplified()->SpeculativeNumberMultiply(hint);
      case IrOpcode::kJSDivide:
        return simplified()->SpeculativeNumberDivide(hint);
      case IrOpcode::kJSModulus:
        return simplified()->SpeculativeNumberModulus(hint);
      case IrOpcode::kJSBitwiseAnd:
        return simplified()->SpeculativeNumberBitwiseAnd(hint);
      case IrOpcode::kJSBitwiseOr:
        return simplified()->SpeculativeNumberBitwiseOr(hint);
      case IrOpcode::kJSBitwiseXor:
        return simplified()->SpeculativeNumberBitwiseXor(hint);
      case IrOpcode::kJSShiftLeft:
        return simplified()->SpeculativeNumberShiftLeft(hint);
      case IrOpcode::kJSShiftRight:
        return simplified()->SpeculativeNumberShiftRight(hint);
      case IrOpcode::kJSShiftRightLogical:
        return simplified()->SpeculativeNumberShiftRightLogical(hint);
      default:
        break;
    }
    UNREACHABLE();
    return nullptr;
  }

  const Operator* SpeculativeCompareOp(NumberOperationHint hint) {
    switch (op_->opcode()) {
      case IrOpcode::kJSLessThan:
        return simplified()->SpeculativeNumberLessThan(hint);
      case IrOpcode::kJSGreaterThan:
        std::swap(left_, right_);  // a > b => b < a
        return simplified()->SpeculativeNumberLessThan(hint);
      case IrOpcode::kJSLessThanOrEqual:
        return simplified()->SpeculativeNumberLessThanOrEqual(hint);
      case IrOpcode::kJSGreaterThanOrEqual:
        std::swap(left_, right_);  // a >= b => b <= a
        return simplified()->SpeculativeNumberLessThanOrEqual(hint);
      default:
        break;
    }
    UNREACHABLE();
    return nullptr;
  }

  Node* BuildSpeculativeOperation(const Operator* op) {
    DCHECK_EQ(2, op->ValueInputCount());
    DCHECK_EQ(1, op->EffectInputCount());
    DCHECK_EQ(1, op->ControlInputCount());
    DCHECK_EQ(false, OperatorProperties::HasFrameStateInput(op));
    DCHECK_EQ(false, OperatorProperties::HasContextInput(op));
    DCHECK_EQ(1, op->EffectOutputCount());
    DCHECK_EQ(0, op->ControlOutputCount());
    Node* node = graph()->NewNode(op, left_, right_, effect_, control_);
    effect_ = node;  // Update the effect dependency.
    return node;
  }

  Node* BuildInvert(Node* input) {
    return graph()->NewNode(simplified()->BooleanNot(), input);
  }

  Node* TryBuildNumberBinop() {
    NumberOperationHint hint;
    if (GetBinaryNumberOperationHint(&hint)) {
      const Operator* op = SpeculativeNumberOp(hint);
      Node* node = BuildSpeculativeOperation(op);
      return node;
    }
    return nullptr;
  }

  Node* TryBuildNumberEqual(bool invert) {
    NumberOperationHint hint;
    if (GetCompareNumberOperationHint(&hint)) {
      const Operator* op = simplified()->SpeculativeNumberEqual(hint);
      Node* compare = BuildSpeculativeOperation(op);
      return invert ? BuildInvert(compare) : compare;
    }
    return nullptr;
  }

  Node* TryBuildNumberCompare() {
    NumberOperationHint hint;
    if (GetCompareNumberOperationHint(&hint)) {
      const Operator* op = SpeculativeCompareOp(hint);
      Node* node = BuildSpeculativeOperation(op);
      return node;
    }
    return nullptr;
  }

  Node* effect() const { return effect_; }
  JSGraph* jsgraph() const { return lowering_->jsgraph(); }
  Graph* graph() const { return jsgraph()->graph(); }
  JSOperatorBuilder* javascript() { return jsgraph()->javascript(); }
  SimplifiedOperatorBuilder* simplified() { return jsgraph()->simplified(); }
  CommonOperatorBuilder* common() { return jsgraph()->common(); }
  const Handle<FeedbackVector>& feedback_vector() const {
    return lowering_->feedback_vector();
  }

 private:
  JSTypeHintLowering* lowering_;
  const Operator* op_;
  Node* left_;
  Node* right_;
  Node* effect_;
  Node* control_;
  FeedbackSlot slot_;
};

JSTypeHintLowering::JSTypeHintLowering(JSGraph* jsgraph,
                                       Handle<FeedbackVector> feedback_vector)
    : jsgraph_(jsgraph), feedback_vector_(feedback_vector) {}

EarlyReduction JSTypeHintLowering::ReduceBinaryOperation(
    const Operator* op, Node* left, Node* right, Node* effect, Node* control,
    FeedbackSlot slot) {
  switch (op->opcode()) {
    case IrOpcode::kJSEqual:
    case IrOpcode::kJSStrictEqual: {
      JSSpeculativeBinopBuilder b(this, op, left, right, effect, control, slot);
      if (Node* node = b.TryBuildNumberEqual(false)) {
        return EarlyReduction(node, b.effect());
      }
      break;
    }
    case IrOpcode::kJSNotEqual:
    case IrOpcode::kJSStrictNotEqual: {
      JSSpeculativeBinopBuilder b(this, op, left, right, effect, control, slot);
      if (Node* node = b.TryBuildNumberEqual(true)) {
        return EarlyReduction(node, b.effect());
      }
      break;
    }
    case IrOpcode::kJSLessThan:
    case IrOpcode::kJSGreaterThan:
    case IrOpcode::kJSLessThanOrEqual:
    case IrOpcode::kJSGreaterThanOrEqual: {
      JSSpeculativeBinopBuilder b(this, op, left, right, effect, control, slot);
      if (Node* node = b.TryBuildNumberCompare()) {
        return EarlyReduction(node, b.effect());
      }
      break;
    }
    case IrOpcode::kJSBitwiseOr:
    case IrOpcode::kJSBitwiseXor:
    case IrOpcode::kJSBitwiseAnd:
    case IrOpcode::kJSShiftLeft:
    case IrOpcode::kJSShiftRight:
    case IrOpcode::kJSShiftRightLogical:
    case IrOpcode::kJSAdd:
    case IrOpcode::kJSSubtract:
    case IrOpcode::kJSMultiply:
    case IrOpcode::kJSDivide:
    case IrOpcode::kJSModulus: {
      JSSpeculativeBinopBuilder b(this, op, left, right, effect, control, slot);
      if (Node* node = b.TryBuildNumberBinop()) {
        return EarlyReduction(node, b.effect());
      }
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
  return EarlyReduction();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
