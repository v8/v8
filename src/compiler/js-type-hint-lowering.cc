// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-type-hint-lowering.h"

#include "src/compiler/access-builder.h"
#include "src/compiler/access-info.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/property-access-builder.h"
#include "src/compiler/simplified-operator.h"
#include "src/feedback-vector.h"
#include "src/type-hints.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

bool BinaryOperationHintToNumberOperationHint(
    BinaryOperationHint binop_hint, NumberOperationHint* number_hint) {
  switch (binop_hint) {
    case BinaryOperationHint::kSignedSmall:
      *number_hint = NumberOperationHint::kSignedSmall;
      return true;
    case BinaryOperationHint::kSignedSmallInputs:
      *number_hint = NumberOperationHint::kSignedSmallInputs;
      return true;
    case BinaryOperationHint::kSigned32:
      *number_hint = NumberOperationHint::kSigned32;
      return true;
    case BinaryOperationHint::kNumber:
      *number_hint = NumberOperationHint::kNumber;
      return true;
    case BinaryOperationHint::kNumberOrOddball:
      *number_hint = NumberOperationHint::kNumberOrOddball;
      return true;
    case BinaryOperationHint::kAny:
    case BinaryOperationHint::kNone:
    case BinaryOperationHint::kString:
      break;
  }
  return false;
}

}  // namespace

class JSSpeculativeBinopBuilder final {
 public:
  JSSpeculativeBinopBuilder(const JSTypeHintLowering* lowering,
                            const Operator* op, Node* left, Node* right,
                            Node* effect, Node* control, FeedbackSlot slot)
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
    return BinaryOperationHintToNumberOperationHint(GetBinaryOperationHint(),
                                                    hint);
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
      case CompareOperationHint::kSymbol:
      case CompareOperationHint::kReceiver:
      case CompareOperationHint::kInternalizedString:
        break;
    }
    return false;
  }

  const Operator* SpeculativeNumberOp(NumberOperationHint hint) {
    switch (op_->opcode()) {
      case IrOpcode::kJSAdd:
        if (hint == NumberOperationHint::kSignedSmall ||
            hint == NumberOperationHint::kSigned32) {
          return simplified()->SpeculativeSafeIntegerAdd(hint);
        } else {
          return simplified()->SpeculativeNumberAdd(hint);
        }
      case IrOpcode::kJSSubtract:
        if (hint == NumberOperationHint::kSignedSmall ||
            hint == NumberOperationHint::kSigned32) {
          return simplified()->SpeculativeSafeIntegerSubtract(hint);
        } else {
          return simplified()->SpeculativeNumberSubtract(hint);
        }
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
  }

  const Operator* SpeculativeCompareOp(NumberOperationHint hint) {
    switch (op_->opcode()) {
      case IrOpcode::kJSEqual:
        return simplified()->SpeculativeNumberEqual(hint);
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
  }

  Node* BuildSpeculativeOperation(const Operator* op) {
    DCHECK_EQ(2, op->ValueInputCount());
    DCHECK_EQ(1, op->EffectInputCount());
    DCHECK_EQ(1, op->ControlInputCount());
    DCHECK_EQ(false, OperatorProperties::HasFrameStateInput(op));
    DCHECK_EQ(false, OperatorProperties::HasContextInput(op));
    DCHECK_EQ(1, op->EffectOutputCount());
    DCHECK_EQ(0, op->ControlOutputCount());
    return graph()->NewNode(op, left_, right_, effect_, control_);
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

  Node* TryBuildNumberCompare() {
    NumberOperationHint hint;
    if (GetCompareNumberOperationHint(&hint)) {
      const Operator* op = SpeculativeCompareOp(hint);
      Node* node = BuildSpeculativeOperation(op);
      return node;
    }
    return nullptr;
  }

  JSGraph* jsgraph() const { return lowering_->jsgraph(); }
  Graph* graph() const { return jsgraph()->graph(); }
  JSOperatorBuilder* javascript() { return jsgraph()->javascript(); }
  SimplifiedOperatorBuilder* simplified() { return jsgraph()->simplified(); }
  CommonOperatorBuilder* common() { return jsgraph()->common(); }
  const Handle<FeedbackVector>& feedback_vector() const {
    return lowering_->feedback_vector();
  }

 private:
  const JSTypeHintLowering* lowering_;
  const Operator* op_;
  Node* left_;
  Node* right_;
  Node* effect_;
  Node* control_;
  FeedbackSlot slot_;
};

JSTypeHintLowering::JSTypeHintLowering(JSGraph* jsgraph,
                                       Handle<FeedbackVector> feedback_vector,
                                       Handle<Context> native_context,
                                       CompilationDependencies* dependencies,
                                       Flags flags, Zone* local_zone)
    : jsgraph_(jsgraph),
      flags_(flags),
      feedback_vector_(feedback_vector),
      native_context_(native_context),
      dependencies_(dependencies),
      local_zone_(local_zone) {}

Graph* JSTypeHintLowering::graph() const { return jsgraph_->graph(); }

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceBinaryOperation(
    const Operator* op, Node* left, Node* right, Node* effect, Node* control,
    FeedbackSlot slot) const {
  switch (op->opcode()) {
    case IrOpcode::kJSStrictEqual: {
      DCHECK(!slot.IsInvalid());
      CompareICNexus nexus(feedback_vector(), slot);
      if (Node* node = TryBuildSoftDeopt(
              nexus, effect, control,
              DeoptimizeReason::kInsufficientTypeFeedbackForCompareOperation)) {
        return LoweringResult::Exit(node);
      }
      // TODO(turbofan): Should we generally support early lowering of
      // JSStrictEqual operators here?
      break;
    }
    case IrOpcode::kJSEqual:
    case IrOpcode::kJSLessThan:
    case IrOpcode::kJSGreaterThan:
    case IrOpcode::kJSLessThanOrEqual:
    case IrOpcode::kJSGreaterThanOrEqual: {
      DCHECK(!slot.IsInvalid());
      CompareICNexus nexus(feedback_vector(), slot);
      if (Node* node = TryBuildSoftDeopt(
              nexus, effect, control,
              DeoptimizeReason::kInsufficientTypeFeedbackForCompareOperation)) {
        return LoweringResult::Exit(node);
      }
      JSSpeculativeBinopBuilder b(this, op, left, right, effect, control, slot);
      if (Node* node = b.TryBuildNumberCompare()) {
        return LoweringResult::SideEffectFree(node, node, control);
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
      DCHECK(!slot.IsInvalid());
      BinaryOpICNexus nexus(feedback_vector(), slot);
      if (Node* node = TryBuildSoftDeopt(
              nexus, effect, control,
              DeoptimizeReason::kInsufficientTypeFeedbackForBinaryOperation)) {
        return LoweringResult::Exit(node);
      }
      JSSpeculativeBinopBuilder b(this, op, left, right, effect, control, slot);
      if (Node* node = b.TryBuildNumberBinop()) {
        return LoweringResult::SideEffectFree(node, node, control);
      }
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceForInNextOperation(
    Node* receiver, Node* cache_array, Node* cache_type, Node* index,
    Node* effect, Node* control, FeedbackSlot slot) const {
  DCHECK(!slot.IsInvalid());
  ForInICNexus nexus(feedback_vector(), slot);
  if (Node* node = TryBuildSoftDeopt(
          nexus, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForForIn)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult
JSTypeHintLowering::ReduceForInPrepareOperation(Node* enumerator, Node* effect,
                                                Node* control,
                                                FeedbackSlot slot) const {
  DCHECK(!slot.IsInvalid());
  ForInICNexus nexus(feedback_vector(), slot);
  if (Node* node = TryBuildSoftDeopt(
          nexus, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForForIn)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceToNumberOperation(
    Node* input, Node* effect, Node* control, FeedbackSlot slot) const {
  DCHECK(!slot.IsInvalid());
  BinaryOpICNexus nexus(feedback_vector(), slot);
  NumberOperationHint hint;
  if (BinaryOperationHintToNumberOperationHint(
          nexus.GetBinaryOperationFeedback(), &hint)) {
    Node* node = jsgraph()->graph()->NewNode(
        jsgraph()->simplified()->SpeculativeToNumber(hint), input, effect,
        control);
    return LoweringResult::SideEffectFree(node, node, control);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceCallOperation(
    const Operator* op, Node* const* args, int arg_count, Node* effect,
    Node* control, FeedbackSlot slot) const {
  DCHECK(op->opcode() == IrOpcode::kJSCall ||
         op->opcode() == IrOpcode::kJSCallWithSpread);
  DCHECK(!slot.IsInvalid());
  CallICNexus nexus(feedback_vector(), slot);
  if (Node* node = TryBuildSoftDeopt(
          nexus, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForCall)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceConstructOperation(
    const Operator* op, Node* const* args, int arg_count, Node* effect,
    Node* control, FeedbackSlot slot) const {
  DCHECK(op->opcode() == IrOpcode::kJSConstruct ||
         op->opcode() == IrOpcode::kJSConstructWithSpread);
  DCHECK(!slot.IsInvalid());
  CallICNexus nexus(feedback_vector(), slot);
  if (Node* node = TryBuildSoftDeopt(
          nexus, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForConstruct)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceLoadNamedOperation(
    const Operator* op, Node* receiver, Node* effect, Node* control,
    FeedbackSlot slot) const {
  DCHECK_EQ(IrOpcode::kJSLoadNamed, op->opcode());
  DCHECK(!slot.IsInvalid());
  LoadICNexus nexus(feedback_vector(), slot);
  if (Node* node = TryBuildSoftDeopt(
          nexus, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess)) {
    return LoweringResult::Exit(node);
  }

  // Try to extract maps from the feedback vector. Give up if the IC
  // is megamorphic or unitialized.
  MapHandles receiver_maps;
  if (nexus.ExtractMaps(&receiver_maps) == 0) {
    return LoweringResult::NoChange();
  }

  // Extract information about accesses from the maps.
  NamedAccess const& p = NamedAccessOf(op);
  AccessInfoFactory access_info_factory(dependencies(), native_context(),
                                        graph()->zone());

  ZoneVector<PropertyAccessInfo> access_infos(local_zone());
  if (!access_info_factory.ComputePropertyAccessInfos(
          receiver_maps, p.name(), AccessMode::kLoad, &access_infos)) {
    return LoweringResult::NoChange();
  }

  // Bail out if not monomorphic.
  if (access_infos.size() != 1) return LoweringResult::NoChange();

  // We only lower data fields and data constants.
  PropertyAccessInfo const& access_info = access_infos[0];
  if (!access_info.IsDataField() && !access_info.IsDataConstant()) {
    return LoweringResult::NoChange();
  }

  PropertyAccessBuilder access_builder(jsgraph(), dependencies());
  if (!access_builder.TryBuildStringCheck(access_info.receiver_maps(),
                                          &receiver, &effect, control) &&
      !access_builder.TryBuildNumberCheck(access_info.receiver_maps(),
                                          &receiver, &effect, control)) {
    receiver = access_builder.BuildCheckHeapObject(receiver, &effect, control);
    access_builder.BuildCheckMaps(receiver, &effect, control,
                                  access_info.receiver_maps());
  }

  Handle<JSObject> holder;
  if (access_info.holder().ToHandle(&holder)) {
    access_builder.AssumePrototypesStable(native_context(),
                                          access_info.receiver_maps(), holder);
  }

  Node* value = nullptr;
  if (access_info.IsDataConstant()) {
    value = jsgraph()->Constant(access_info.constant());
  } else {
    DCHECK(access_info.IsDataField());
    value = access_builder.BuildLoadDataField(p.name(), access_info, receiver,
                                              &effect, &control);
  }
  return LoweringResult::SideEffectFree(value, effect, control);
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceLoadKeyedOperation(
    const Operator* op, Node* obj, Node* key, Node* effect, Node* control,
    FeedbackSlot slot) const {
  DCHECK_EQ(IrOpcode::kJSLoadProperty, op->opcode());
  DCHECK(!slot.IsInvalid());
  KeyedLoadICNexus nexus(feedback_vector(), slot);
  if (Node* node = TryBuildSoftDeopt(
          nexus, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericKeyedAccess)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult
JSTypeHintLowering::ReduceStoreNamedOperation(const Operator* op, Node* obj,
                                              Node* val, Node* effect,
                                              Node* control,
                                              FeedbackSlot slot) const {
  DCHECK(op->opcode() == IrOpcode::kJSStoreNamed ||
         op->opcode() == IrOpcode::kJSStoreNamedOwn);
  DCHECK(!slot.IsInvalid());
  StoreICNexus nexus(feedback_vector(), slot);
  if (Node* node = TryBuildSoftDeopt(
          nexus, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult
JSTypeHintLowering::ReduceStoreKeyedOperation(const Operator* op, Node* obj,
                                              Node* key, Node* val,
                                              Node* effect, Node* control,
                                              FeedbackSlot slot) const {
  DCHECK_EQ(IrOpcode::kJSStoreProperty, op->opcode());
  DCHECK(!slot.IsInvalid());
  KeyedStoreICNexus nexus(feedback_vector(), slot);
  if (Node* node = TryBuildSoftDeopt(
          nexus, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericKeyedAccess)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

Node* JSTypeHintLowering::TryBuildSoftDeopt(FeedbackNexus& nexus, Node* effect,
                                            Node* control,
                                            DeoptimizeReason reason) const {
  if ((flags() & kBailoutOnUninitialized) && nexus.IsUninitialized()) {
    Node* deoptimize = jsgraph()->graph()->NewNode(
        jsgraph()->common()->Deoptimize(DeoptimizeKind::kSoft, reason),
        jsgraph()->Dead(), effect, control);
    Node* frame_state = NodeProperties::FindFrameStateBefore(deoptimize);
    deoptimize->ReplaceInput(0, frame_state);
    return deoptimize;
  }
  return nullptr;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
