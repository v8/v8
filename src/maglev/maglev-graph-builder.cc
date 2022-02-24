// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-graph-builder.h"

#include "src/compiler/feedback-source.h"
#include "src/compiler/heap-refs.h"
#include "src/handles/maybe-handles-inl.h"
#include "src/ic/handler-configuration.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/name-inl.h"
#include "src/objects/slots-inl.h"

namespace v8 {
namespace internal {

namespace maglev {

void MaglevGraphBuilder::VisitLdar() { SetAccumulator(LoadRegister(0)); }

void MaglevGraphBuilder::VisitLdaZero() {
  SetAccumulator(AddNewNode<SmiConstant>({}, Smi::zero()));
}
void MaglevGraphBuilder::VisitLdaSmi() {
  Smi constant = Smi::FromInt(iterator_.GetImmediateOperand(0));
  SetAccumulator(AddNewNode<SmiConstant>({}, constant));
}
void MaglevGraphBuilder::VisitLdaUndefined() {
  SetAccumulator(AddNewNode<RootConstant>({}, RootIndex::kUndefinedValue));
}
void MaglevGraphBuilder::VisitLdaNull() {
  SetAccumulator(AddNewNode<RootConstant>({}, RootIndex::kNullValue));
}
void MaglevGraphBuilder::VisitLdaTheHole() {
  SetAccumulator(AddNewNode<RootConstant>({}, RootIndex::kTheHoleValue));
}
void MaglevGraphBuilder::VisitLdaTrue() {
  SetAccumulator(AddNewNode<RootConstant>({}, RootIndex::kTrueValue));
}
void MaglevGraphBuilder::VisitLdaFalse() {
  SetAccumulator(AddNewNode<RootConstant>({}, RootIndex::kFalseValue));
}
void MaglevGraphBuilder::VisitLdaConstant() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaContextSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaImmutableContextSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaCurrentContextSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaImmutableCurrentContextSlot() {
  UNREACHABLE();
}
void MaglevGraphBuilder::VisitStar() {
  StoreRegister(
      iterator_.GetRegisterOperand(0), GetAccumulator(),
      bytecode_analysis().GetOutLivenessFor(iterator_.current_offset()));
}
void MaglevGraphBuilder::VisitMov() {
  StoreRegister(
      iterator_.GetRegisterOperand(1), LoadRegister(0),
      bytecode_analysis().GetOutLivenessFor(iterator_.current_offset()));
}
void MaglevGraphBuilder::VisitPushContext() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitPopContext() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestReferenceEqual() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestUndetectable() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestNull() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestUndefined() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestTypeOf() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaGlobal() {
  // LdaGlobal <name_index> <slot>

  static const int kNameOperandIndex = 0;
  static const int kSlotOperandIndex = 1;

  compiler::NameRef name = GetRefOperand<Name>(kNameOperandIndex);
  FeedbackSlot slot_index = GetSlotOperand(kSlotOperandIndex);
  ValueNode* context = GetContext();

  USE(slot_index);  // TODO(v8:7700): Use the feedback info.

  SetAccumulator(AddNewNode<LoadGlobal>({context}, name));
  MarkPossibleSideEffect();
}
void MaglevGraphBuilder::VisitLdaGlobalInsideTypeof() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaGlobal() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaContextSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaCurrentContextSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaLookupSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaLookupContextSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaLookupGlobalSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaLookupSlotInsideTypeof() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaLookupContextSlotInsideTypeof() {
  UNREACHABLE();
}
void MaglevGraphBuilder::VisitLdaLookupGlobalSlotInsideTypeof() {
  UNREACHABLE();
}
void MaglevGraphBuilder::VisitStaLookupSlot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaNamedProperty() {
  // LdaNamedProperty <object> <name_index> <slot>
  ValueNode* object = LoadRegister(0);

  // TODO(leszeks): Use JSHeapBroker here.
  FeedbackNexus nexus(feedback().object() /* TODO(v8:7700) */,
                      GetSlotOperand(2));

  if (nexus.ic_state() == InlineCacheState::UNINITIALIZED) {
    EnsureCheckpoint();
    AddNewNode<SoftDeopt>({});
  }

  if (nexus.ic_state() == InlineCacheState::MONOMORPHIC) {
    std::vector<MapAndHandler> maps_and_handlers;
    nexus.ExtractMapsAndHandlers(&maps_and_handlers);
    DCHECK_EQ(maps_and_handlers.size(), 1);
    MapAndHandler& map_and_handler = maps_and_handlers[0];
    if (map_and_handler.second->IsSmi()) {
      int handler = map_and_handler.second->ToSmi().value();
      LoadHandler::Kind kind = LoadHandler::KindBits::decode(handler);
      if (kind == LoadHandler::Kind::kField &&
          !LoadHandler::IsWasmStructBits::decode(handler)) {
        EnsureCheckpoint();
        AddNewNode<CheckMaps>({object},
                              MakeRef(broker(), map_and_handler.first));
        SetAccumulator(AddNewNode<LoadField>({object}, handler));
        return;
      }
    }
  }

  compiler::NameRef name = GetRefOperand<Name>(1);
  SetAccumulator(AddNewNode<LoadNamedGeneric>({object}, name));
  MarkPossibleSideEffect();
}
void MaglevGraphBuilder::VisitLdaNamedPropertyFromSuper() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaKeyedProperty() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLdaModuleVariable() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaModuleVariable() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaNamedProperty() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaNamedOwnProperty() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaKeyedProperty() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaKeyedPropertyAsDefine() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaInArrayLiteral() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitStaDataPropertyInLiteral() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCollectTypeProfile() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitAdd() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitSub() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitMul() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitDiv() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitMod() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitExp() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitBitwiseOr() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitBitwiseXor() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitBitwiseAnd() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitShiftLeft() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitShiftRight() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitShiftRightLogical() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitAddSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitSubSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitMulSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitDivSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitModSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitExpSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitBitwiseOrSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitBitwiseXorSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitBitwiseAndSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitShiftLeftSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitShiftRightSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitShiftRightLogicalSmi() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitInc() {
  // Inc <slot>

  FeedbackSlot slot_index = GetSlotOperand(0);
  ValueNode* value = GetAccumulator();

  ValueNode* node = AddNewNode<Increment>(
      {value}, compiler::FeedbackSource{feedback(), slot_index});
  SetAccumulator(node);
  MarkPossibleSideEffect();
}
void MaglevGraphBuilder::VisitDec() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitNegate() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitBitwiseNot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitToBooleanLogicalNot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitLogicalNot() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTypeOf() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitDeletePropertyStrict() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitDeletePropertySloppy() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitGetSuperConstructor() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCallAnyReceiver() { UNREACHABLE(); }

// TODO(leszeks): For all of these:
//   a) Read feedback and implement inlining
//   b) Wrap in a helper.
void MaglevGraphBuilder::VisitCallProperty() {
  ValueNode* function = LoadRegister(0);

  interpreter::RegisterList args = iterator_.GetRegisterListOperand(1);
  ValueNode* context = GetContext();

  static constexpr int kTheContext = 1;
  CallProperty* call_property = AddNewNode<CallProperty>(
      args.register_count() + kTheContext, function, context);
  // TODO(leszeks): Move this for loop into the CallProperty constructor,
  // pre-size the args array.
  for (int i = 0; i < args.register_count(); ++i) {
    call_property->set_arg(i, current_interpreter_frame_.get(args[i]));
  }
  SetAccumulator(call_property);
  MarkPossibleSideEffect();
}
void MaglevGraphBuilder::VisitCallProperty0() {
  ValueNode* function = LoadRegister(0);
  ValueNode* context = GetContext();

  CallProperty* call_property =
      AddNewNode<CallProperty>({function, context, LoadRegister(1)});
  SetAccumulator(call_property);
  MarkPossibleSideEffect();
}
void MaglevGraphBuilder::VisitCallProperty1() {
  ValueNode* function = LoadRegister(0);
  ValueNode* context = GetContext();

  CallProperty* call_property = AddNewNode<CallProperty>(
      {function, context, LoadRegister(1), LoadRegister(2)});
  SetAccumulator(call_property);
  MarkPossibleSideEffect();
}
void MaglevGraphBuilder::VisitCallProperty2() {
  ValueNode* function = LoadRegister(0);
  ValueNode* context = GetContext();

  CallProperty* call_property = AddNewNode<CallProperty>(
      {function, context, LoadRegister(1), LoadRegister(2), LoadRegister(3)});
  SetAccumulator(call_property);
  MarkPossibleSideEffect();
}
void MaglevGraphBuilder::VisitCallUndefinedReceiver() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCallUndefinedReceiver0() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCallUndefinedReceiver1() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCallUndefinedReceiver2() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCallWithSpread() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCallRuntime() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCallRuntimeForPair() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCallJSRuntime() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitInvokeIntrinsic() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitConstruct() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitConstructWithSpread() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestEqual() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestEqualStrict() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestLessThan() {
  // TestLessThan <src> <slot>

  ValueNode* left = LoadRegister(0);
  FeedbackSlot slot_index = GetSlotOperand(1);
  ValueNode* right = GetAccumulator();

  USE(slot_index);  // TODO(v8:7700): Use the feedback info.

  ValueNode* node = AddNewNode<LessThan>(
      {left, right}, compiler::FeedbackSource{feedback(), slot_index});
  SetAccumulator(node);
  MarkPossibleSideEffect();
}
void MaglevGraphBuilder::VisitTestGreaterThan() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestLessThanOrEqual() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestGreaterThanOrEqual() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestInstanceOf() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitTestIn() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitToName() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitToNumber() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitToNumeric() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitToObject() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitToString() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateRegExpLiteral() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateArrayLiteral() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateArrayFromIterable() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateEmptyArrayLiteral() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateObjectLiteral() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateEmptyObjectLiteral() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCloneObject() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitGetTemplateObject() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateClosure() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateBlockContext() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateCatchContext() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateFunctionContext() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateEvalContext() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateWithContext() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateMappedArguments() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateUnmappedArguments() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitCreateRestParameter() { UNREACHABLE(); }

void MaglevGraphBuilder::VisitJumpLoop() {
  int target = iterator_.GetJumpTargetOffset();
  BasicBlock* block =
      target == iterator_.current_offset()
          ? FinishBlock<JumpLoop>(next_offset(), {}, &jump_targets_[target])
          : FinishBlock<JumpLoop>(next_offset(), {},
                                  jump_targets_[target].block_ptr());

  merge_states_[target]->MergeLoop(*compilation_unit_,
                                   current_interpreter_frame_, block, target);
  block->set_predecessor_id(0);
}
void MaglevGraphBuilder::VisitJump() {
  BasicBlock* block = FinishBlock<Jump>(
      next_offset(), {}, &jump_targets_[iterator_.GetJumpTargetOffset()]);
  MergeIntoFrameState(block, iterator_.GetJumpTargetOffset());
  DCHECK_LT(next_offset(), bytecode().length());
}
void MaglevGraphBuilder::VisitJumpConstant() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitJumpIfNullConstant() { VisitJumpIfNull(); }
void MaglevGraphBuilder::VisitJumpIfNotNullConstant() { VisitJumpIfNotNull(); }
void MaglevGraphBuilder::VisitJumpIfUndefinedConstant() {
  VisitJumpIfUndefined();
}
void MaglevGraphBuilder::VisitJumpIfNotUndefinedConstant() {
  VisitJumpIfNotUndefined();
}
void MaglevGraphBuilder::VisitJumpIfUndefinedOrNullConstant() {
  VisitJumpIfUndefinedOrNull();
}
void MaglevGraphBuilder::VisitJumpIfTrueConstant() { VisitJumpIfTrue(); }
void MaglevGraphBuilder::VisitJumpIfFalseConstant() { VisitJumpIfFalse(); }
void MaglevGraphBuilder::VisitJumpIfJSReceiverConstant() {
  VisitJumpIfJSReceiver();
}
void MaglevGraphBuilder::VisitJumpIfToBooleanTrueConstant() {
  VisitJumpIfToBooleanTrue();
}
void MaglevGraphBuilder::VisitJumpIfToBooleanFalseConstant() {
  VisitJumpIfToBooleanFalse();
}

void MaglevGraphBuilder::MergeIntoFrameState(BasicBlock* predecessor,
                                             int target) {
  if (merge_states_[target] == nullptr) {
    DCHECK(!bytecode_analysis().IsLoopHeader(target));
    const compiler::BytecodeLivenessState* liveness =
        bytecode_analysis().GetInLivenessFor(target);
    // If there's no target frame state, allocate a new one.
    merge_states_[target] = zone()->New<MergePointInterpreterFrameState>(
        *compilation_unit_, current_interpreter_frame_, target,
        NumPredecessors(target), predecessor, liveness);
  } else {
    // If there already is a frame state, merge.
    merge_states_[target]->Merge(*compilation_unit_, current_interpreter_frame_,
                                 predecessor, target);
  }
}

void MaglevGraphBuilder::BuildBranchIfTrue(ValueNode* node, int true_target,
                                           int false_target) {
  // TODO(verwaest): Materialize true/false in the respective environments.
  if (GetOutLiveness()->AccumulatorIsLive()) SetAccumulator(node);
  BasicBlock* block = FinishBlock<BranchIfTrue>(next_offset(), {node},
                                                &jump_targets_[true_target],
                                                &jump_targets_[false_target]);
  MergeIntoFrameState(block, iterator_.GetJumpTargetOffset());
}
void MaglevGraphBuilder::BuildBranchIfToBooleanTrue(ValueNode* node,
                                                    int true_target,
                                                    int false_target) {
  // TODO(verwaest): Materialize true/false in the respective environments.
  if (GetOutLiveness()->AccumulatorIsLive()) SetAccumulator(node);
  BasicBlock* block = FinishBlock<BranchIfToBooleanTrue>(
      next_offset(), {node}, &jump_targets_[true_target],
      &jump_targets_[false_target]);
  MergeIntoFrameState(block, iterator_.GetJumpTargetOffset());
}
void MaglevGraphBuilder::VisitJumpIfToBooleanTrue() {
  BuildBranchIfToBooleanTrue(GetAccumulator(), iterator_.GetJumpTargetOffset(),
                             next_offset());
}
void MaglevGraphBuilder::VisitJumpIfToBooleanFalse() {
  BuildBranchIfToBooleanTrue(GetAccumulator(), next_offset(),
                             iterator_.GetJumpTargetOffset());
}
void MaglevGraphBuilder::VisitJumpIfTrue() {
  BuildBranchIfTrue(GetAccumulator(), iterator_.GetJumpTargetOffset(),
                    next_offset());
}
void MaglevGraphBuilder::VisitJumpIfFalse() {
  BuildBranchIfTrue(GetAccumulator(), next_offset(),
                    iterator_.GetJumpTargetOffset());
}
void MaglevGraphBuilder::VisitJumpIfNull() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitJumpIfNotNull() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitJumpIfUndefined() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitJumpIfNotUndefined() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitJumpIfUndefinedOrNull() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitJumpIfJSReceiver() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitSwitchOnSmiNoFeedback() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitForInEnumerate() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitForInPrepare() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitForInContinue() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitForInNext() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitForInStep() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitSetPendingMessage() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitThrow() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitReThrow() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitReturn() {
  FinishBlock<Return>(next_offset(), {GetAccumulator()});
}
void MaglevGraphBuilder::VisitThrowReferenceErrorIfHole() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitThrowSuperNotCalledIfHole() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitThrowSuperAlreadyCalledIfNotHole() {
  UNREACHABLE();
}
void MaglevGraphBuilder::VisitThrowIfNotSuperConstructor() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitSwitchOnGeneratorState() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitSuspendGenerator() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitResumeGenerator() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitGetIterator() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitDebugger() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitIncBlockCounter() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitAbort() { UNREACHABLE(); }
#define SHORT_STAR_VISITOR(Name, ...)                                         \
  void MaglevGraphBuilder::Visit##Name() {                                    \
    StoreRegister(                                                            \
        interpreter::Register::FromShortStar(interpreter::Bytecode::k##Name), \
        GetAccumulator(),                                                     \
        bytecode_analysis().GetOutLivenessFor(iterator_.current_offset()));   \
  }
SHORT_STAR_BYTECODE_LIST(SHORT_STAR_VISITOR)
#undef SHORT_STAR_VISITOR

void MaglevGraphBuilder::VisitWide() { UNREACHABLE(); }
void MaglevGraphBuilder::VisitExtraWide() { UNREACHABLE(); }
#define DEBUG_BREAK(Name, ...) \
  void MaglevGraphBuilder::Visit##Name() { UNREACHABLE(); }
DEBUG_BREAK_BYTECODE_LIST(DEBUG_BREAK)
#undef DEBUG_BREAK
void MaglevGraphBuilder::VisitIllegal() { UNREACHABLE(); }

}  // namespace maglev
}  // namespace internal
}  // namespace v8
