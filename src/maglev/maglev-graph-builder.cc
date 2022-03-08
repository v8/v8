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

// TODO(v8:7700): Clean up after all bytecodes are supported.
#define MAGLEV_UNIMPLEMENTED_BYTECODE(Name)                             \
  void MaglevGraphBuilder::Visit##Name() {                              \
    std::cerr << "Maglev: Can't compile, bytecode " #Name               \
                 " is not supported\n";                                 \
    found_unsupported_bytecode_ = true;                                 \
    this_field_will_be_unused_once_all_bytecodes_are_supported_ = true; \
  }

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
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaConstant)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaContextSlot)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaImmutableContextSlot)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaCurrentContextSlot)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaImmutableCurrentContextSlot)
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
MAGLEV_UNIMPLEMENTED_BYTECODE(PushContext)
MAGLEV_UNIMPLEMENTED_BYTECODE(PopContext)
MAGLEV_UNIMPLEMENTED_BYTECODE(TestReferenceEqual)
MAGLEV_UNIMPLEMENTED_BYTECODE(TestUndetectable)
MAGLEV_UNIMPLEMENTED_BYTECODE(TestNull)
MAGLEV_UNIMPLEMENTED_BYTECODE(TestUndefined)
MAGLEV_UNIMPLEMENTED_BYTECODE(TestTypeOf)
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
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaGlobalInsideTypeof)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaGlobal)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaContextSlot)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaCurrentContextSlot)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaLookupSlot)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaLookupContextSlot)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaLookupGlobalSlot)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaLookupSlotInsideTypeof)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaLookupContextSlotInsideTypeof)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaLookupGlobalSlotInsideTypeof)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaLookupSlot)
void MaglevGraphBuilder::VisitLdaNamedProperty() {
  // LdaNamedProperty <object> <name_index> <slot>
  ValueNode* object = LoadRegister(0);
  FeedbackNexus nexus = feedback_nexus(2);

  if (nexus.ic_state() == InlineCacheState::UNINITIALIZED) {
    EnsureCheckpoint();
    AddNewNode<SoftDeopt>({});
  } else if (nexus.ic_state() == InlineCacheState::MONOMORPHIC) {
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

  ValueNode* context = GetContext();
  compiler::NameRef name = GetRefOperand<Name>(1);
  SetAccumulator(AddNewNode<LoadNamedGeneric>({context, object}, name));
  MarkPossibleSideEffect();
}
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaNamedPropertyFromSuper)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaKeyedProperty)
MAGLEV_UNIMPLEMENTED_BYTECODE(LdaModuleVariable)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaModuleVariable)

void MaglevGraphBuilder::VisitStaNamedProperty() {
  // StaNamedProperty <object> <name_index> <slot>
  ValueNode* object = LoadRegister(0);
  FeedbackNexus nexus = feedback_nexus(2);

  if (nexus.ic_state() == InlineCacheState::UNINITIALIZED) {
    EnsureCheckpoint();
    AddNewNode<SoftDeopt>({});
  } else if (nexus.ic_state() == InlineCacheState::MONOMORPHIC) {
    std::vector<MapAndHandler> maps_and_handlers;
    nexus.ExtractMapsAndHandlers(&maps_and_handlers);
    DCHECK_EQ(maps_and_handlers.size(), 1);
    MapAndHandler& map_and_handler = maps_and_handlers[0];
    if (map_and_handler.second->IsSmi()) {
      int handler = map_and_handler.second->ToSmi().value();
      StoreHandler::Kind kind = StoreHandler::KindBits::decode(handler);
      if (kind == StoreHandler::Kind::kField) {
        EnsureCheckpoint();
        AddNewNode<CheckMaps>({object},
                              MakeRef(broker(), map_and_handler.first));
        ValueNode* value = GetAccumulator();
        AddNewNode<StoreField>({object, value}, handler);
        return;
      }
    }
  }

  // TODO(victorgomes): Generic store.
  UNREACHABLE();
}

MAGLEV_UNIMPLEMENTED_BYTECODE(StaNamedOwnProperty)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaKeyedProperty)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaKeyedPropertyAsDefine)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaInArrayLiteral)
MAGLEV_UNIMPLEMENTED_BYTECODE(StaDataPropertyInLiteral)
MAGLEV_UNIMPLEMENTED_BYTECODE(CollectTypeProfile)
MAGLEV_UNIMPLEMENTED_BYTECODE(Add)
MAGLEV_UNIMPLEMENTED_BYTECODE(Sub)
MAGLEV_UNIMPLEMENTED_BYTECODE(Mul)
MAGLEV_UNIMPLEMENTED_BYTECODE(Div)
MAGLEV_UNIMPLEMENTED_BYTECODE(Mod)
MAGLEV_UNIMPLEMENTED_BYTECODE(Exp)
MAGLEV_UNIMPLEMENTED_BYTECODE(BitwiseOr)
MAGLEV_UNIMPLEMENTED_BYTECODE(BitwiseXor)
MAGLEV_UNIMPLEMENTED_BYTECODE(BitwiseAnd)
MAGLEV_UNIMPLEMENTED_BYTECODE(ShiftLeft)
MAGLEV_UNIMPLEMENTED_BYTECODE(ShiftRight)
MAGLEV_UNIMPLEMENTED_BYTECODE(ShiftRightLogical)
MAGLEV_UNIMPLEMENTED_BYTECODE(AddSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(SubSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(MulSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(DivSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(ModSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(ExpSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(BitwiseOrSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(BitwiseXorSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(BitwiseAndSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(ShiftLeftSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(ShiftRightSmi)
MAGLEV_UNIMPLEMENTED_BYTECODE(ShiftRightLogicalSmi)
void MaglevGraphBuilder::VisitInc() {
  // Inc <slot>

  FeedbackSlot slot_index = GetSlotOperand(0);
  ValueNode* value = GetAccumulator();

  ValueNode* node = AddNewNode<Increment>(
      {value}, compiler::FeedbackSource{feedback(), slot_index});
  SetAccumulator(node);
  MarkPossibleSideEffect();
}
MAGLEV_UNIMPLEMENTED_BYTECODE(Dec)
MAGLEV_UNIMPLEMENTED_BYTECODE(Negate)
MAGLEV_UNIMPLEMENTED_BYTECODE(BitwiseNot)
MAGLEV_UNIMPLEMENTED_BYTECODE(ToBooleanLogicalNot)
MAGLEV_UNIMPLEMENTED_BYTECODE(LogicalNot)
MAGLEV_UNIMPLEMENTED_BYTECODE(TypeOf)
MAGLEV_UNIMPLEMENTED_BYTECODE(DeletePropertyStrict)
MAGLEV_UNIMPLEMENTED_BYTECODE(DeletePropertySloppy)
MAGLEV_UNIMPLEMENTED_BYTECODE(GetSuperConstructor)
MAGLEV_UNIMPLEMENTED_BYTECODE(CallAnyReceiver)

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
MAGLEV_UNIMPLEMENTED_BYTECODE(CallUndefinedReceiver)
MAGLEV_UNIMPLEMENTED_BYTECODE(CallUndefinedReceiver0)
MAGLEV_UNIMPLEMENTED_BYTECODE(CallUndefinedReceiver1)
MAGLEV_UNIMPLEMENTED_BYTECODE(CallUndefinedReceiver2)
MAGLEV_UNIMPLEMENTED_BYTECODE(CallWithSpread)
MAGLEV_UNIMPLEMENTED_BYTECODE(CallRuntime)
MAGLEV_UNIMPLEMENTED_BYTECODE(CallRuntimeForPair)
MAGLEV_UNIMPLEMENTED_BYTECODE(CallJSRuntime)
MAGLEV_UNIMPLEMENTED_BYTECODE(InvokeIntrinsic)
MAGLEV_UNIMPLEMENTED_BYTECODE(Construct)
MAGLEV_UNIMPLEMENTED_BYTECODE(ConstructWithSpread)
MAGLEV_UNIMPLEMENTED_BYTECODE(TestEqual)
MAGLEV_UNIMPLEMENTED_BYTECODE(TestEqualStrict)

template <typename RelNodeT>
void MaglevGraphBuilder::VisitRelNode() {
  // Test[RelationComparison] <src> <slot>

  ValueNode* left = LoadRegister(0);
  FeedbackSlot slot_index = GetSlotOperand(1);
  ValueNode* right = GetAccumulator();

  USE(slot_index);  // TODO(v8:7700): Use the feedback info.

  ValueNode* node = AddNewNode<RelNodeT>(
      {left, right}, compiler::FeedbackSource{feedback(), slot_index});
  SetAccumulator(node);
  MarkPossibleSideEffect();
}

void MaglevGraphBuilder::VisitTestLessThan() { VisitRelNode<LessThan>(); }
void MaglevGraphBuilder::VisitTestLessThanOrEqual() {
  VisitRelNode<LessThanOrEqual>();
}
void MaglevGraphBuilder::VisitTestGreaterThan() { VisitRelNode<GreaterThan>(); }
void MaglevGraphBuilder::VisitTestGreaterThanOrEqual() {
  VisitRelNode<GreaterThanOrEqual>();
}

MAGLEV_UNIMPLEMENTED_BYTECODE(TestInstanceOf)
MAGLEV_UNIMPLEMENTED_BYTECODE(TestIn)
MAGLEV_UNIMPLEMENTED_BYTECODE(ToName)
MAGLEV_UNIMPLEMENTED_BYTECODE(ToNumber)
MAGLEV_UNIMPLEMENTED_BYTECODE(ToNumeric)
MAGLEV_UNIMPLEMENTED_BYTECODE(ToObject)
MAGLEV_UNIMPLEMENTED_BYTECODE(ToString)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateRegExpLiteral)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateArrayLiteral)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateArrayFromIterable)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateEmptyArrayLiteral)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateObjectLiteral)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateEmptyObjectLiteral)
MAGLEV_UNIMPLEMENTED_BYTECODE(CloneObject)
MAGLEV_UNIMPLEMENTED_BYTECODE(GetTemplateObject)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateClosure)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateBlockContext)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateCatchContext)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateFunctionContext)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateEvalContext)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateWithContext)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateMappedArguments)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateUnmappedArguments)
MAGLEV_UNIMPLEMENTED_BYTECODE(CreateRestParameter)

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
MAGLEV_UNIMPLEMENTED_BYTECODE(JumpConstant)
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
MAGLEV_UNIMPLEMENTED_BYTECODE(JumpIfNull)
MAGLEV_UNIMPLEMENTED_BYTECODE(JumpIfNotNull)
MAGLEV_UNIMPLEMENTED_BYTECODE(JumpIfUndefined)
MAGLEV_UNIMPLEMENTED_BYTECODE(JumpIfNotUndefined)
MAGLEV_UNIMPLEMENTED_BYTECODE(JumpIfUndefinedOrNull)
MAGLEV_UNIMPLEMENTED_BYTECODE(JumpIfJSReceiver)
MAGLEV_UNIMPLEMENTED_BYTECODE(SwitchOnSmiNoFeedback)
MAGLEV_UNIMPLEMENTED_BYTECODE(ForInEnumerate)
MAGLEV_UNIMPLEMENTED_BYTECODE(ForInPrepare)
MAGLEV_UNIMPLEMENTED_BYTECODE(ForInContinue)
MAGLEV_UNIMPLEMENTED_BYTECODE(ForInNext)
MAGLEV_UNIMPLEMENTED_BYTECODE(ForInStep)
MAGLEV_UNIMPLEMENTED_BYTECODE(SetPendingMessage)
MAGLEV_UNIMPLEMENTED_BYTECODE(Throw)
MAGLEV_UNIMPLEMENTED_BYTECODE(ReThrow)
void MaglevGraphBuilder::VisitReturn() {
  FinishBlock<Return>(next_offset(), {GetAccumulator()});
}
MAGLEV_UNIMPLEMENTED_BYTECODE(ThrowReferenceErrorIfHole)
MAGLEV_UNIMPLEMENTED_BYTECODE(ThrowSuperNotCalledIfHole)
MAGLEV_UNIMPLEMENTED_BYTECODE(ThrowSuperAlreadyCalledIfNotHole)
MAGLEV_UNIMPLEMENTED_BYTECODE(ThrowIfNotSuperConstructor)
MAGLEV_UNIMPLEMENTED_BYTECODE(SwitchOnGeneratorState)
MAGLEV_UNIMPLEMENTED_BYTECODE(SuspendGenerator)
MAGLEV_UNIMPLEMENTED_BYTECODE(ResumeGenerator)
MAGLEV_UNIMPLEMENTED_BYTECODE(GetIterator)
MAGLEV_UNIMPLEMENTED_BYTECODE(Debugger)
MAGLEV_UNIMPLEMENTED_BYTECODE(IncBlockCounter)
MAGLEV_UNIMPLEMENTED_BYTECODE(Abort)
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
