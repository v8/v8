// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/codegen/arm64/register-arm64.h"
#include "src/maglev/arm64/maglev-assembler-arm64-inl.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/objects/feedback-cell.h"
#include "src/objects/js-function.h"

namespace v8 {
namespace internal {
namespace maglev {

#define __ masm->

// TODO(v8:7700): Remove this logic when all nodes are implemented.
class MaglevUnimplementedIRNode {
 public:
  MaglevUnimplementedIRNode() {}
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}
  template <typename NodeT>
  void Process(NodeT* node, const ProcessingState& state);
  bool has_unimplemented_node() const { return has_unimplemented_node_; }

 private:
  bool has_unimplemented_node_ = false;
};

#define UNIMPLEMENTED_NODE(Node, ...)                                     \
  void Node::SetValueLocationConstraints() {}                             \
                                                                          \
  void Node::GenerateCode(MaglevAssembler* masm,                          \
                          const ProcessingState& state) {                 \
    USE(__VA_ARGS__);                                                     \
  }                                                                       \
  template <>                                                             \
  void MaglevUnimplementedIRNode::Process(Node* node,                     \
                                          const ProcessingState& state) { \
    std::cerr << "Unimplemented Maglev IR Node: " #Node << std::endl;     \
    has_unimplemented_node_ = true;                                       \
  }

#define UNIMPLEMENTED_NODE_WITH_CALL(Node, ...)    \
  int Node::MaxCallStackArgs() const { return 0; } \
  UNIMPLEMENTED_NODE(Node, __VA_ARGS__)

// If we don't have a specialization, it means we have implemented the node.
template <typename NodeT>
void MaglevUnimplementedIRNode::Process(NodeT* node,
                                        const ProcessingState& state) {}

bool MaglevGraphHasUnimplementedNode(Graph* graph) {
  GraphProcessor<MaglevUnimplementedIRNode> processor;
  processor.ProcessGraph(graph);
  return processor.node_processor().has_unimplemented_node();
}

UNIMPLEMENTED_NODE(GenericAdd)
UNIMPLEMENTED_NODE(GenericSubtract)
UNIMPLEMENTED_NODE(GenericMultiply)
UNIMPLEMENTED_NODE(GenericDivide)
UNIMPLEMENTED_NODE(GenericModulus)
UNIMPLEMENTED_NODE(GenericExponentiate)
UNIMPLEMENTED_NODE(GenericBitwiseAnd)
UNIMPLEMENTED_NODE(GenericBitwiseOr)
UNIMPLEMENTED_NODE(GenericBitwiseXor)
UNIMPLEMENTED_NODE(GenericShiftLeft)
UNIMPLEMENTED_NODE(GenericShiftRight)
UNIMPLEMENTED_NODE(GenericShiftRightLogical)
UNIMPLEMENTED_NODE(GenericBitwiseNot)
UNIMPLEMENTED_NODE(GenericNegate)
UNIMPLEMENTED_NODE(GenericIncrement)
UNIMPLEMENTED_NODE(GenericDecrement)
UNIMPLEMENTED_NODE(GenericEqual)
UNIMPLEMENTED_NODE(GenericStrictEqual)
UNIMPLEMENTED_NODE(GenericLessThan)
UNIMPLEMENTED_NODE(GenericLessThanOrEqual)
UNIMPLEMENTED_NODE(GenericGreaterThan)
UNIMPLEMENTED_NODE(GenericGreaterThanOrEqual)
UNIMPLEMENTED_NODE(Int32SubtractWithOverflow)
UNIMPLEMENTED_NODE(Int32MultiplyWithOverflow)
UNIMPLEMENTED_NODE(Int32DivideWithOverflow)
UNIMPLEMENTED_NODE(Int32ModulusWithOverflow)
UNIMPLEMENTED_NODE(Int32BitwiseAnd)
UNIMPLEMENTED_NODE(Int32BitwiseOr)
UNIMPLEMENTED_NODE(Int32BitwiseXor)
UNIMPLEMENTED_NODE(Int32ShiftLeft)
UNIMPLEMENTED_NODE(Int32ShiftRight)
UNIMPLEMENTED_NODE(Int32ShiftRightLogical)
UNIMPLEMENTED_NODE(Int32BitwiseNot)
UNIMPLEMENTED_NODE(Int32NegateWithOverflow)
UNIMPLEMENTED_NODE(Int32IncrementWithOverflow)
UNIMPLEMENTED_NODE(Int32DecrementWithOverflow)
UNIMPLEMENTED_NODE(Int32Equal)
UNIMPLEMENTED_NODE(Int32StrictEqual)
UNIMPLEMENTED_NODE(Int32LessThan)
UNIMPLEMENTED_NODE(Int32LessThanOrEqual)
UNIMPLEMENTED_NODE(Int32GreaterThan)
UNIMPLEMENTED_NODE(Int32GreaterThanOrEqual)
UNIMPLEMENTED_NODE(Float64Add)
UNIMPLEMENTED_NODE(Float64Subtract)
UNIMPLEMENTED_NODE(Float64Multiply)
UNIMPLEMENTED_NODE(Float64Divide)
UNIMPLEMENTED_NODE_WITH_CALL(Float64Exponentiate)
UNIMPLEMENTED_NODE(Float64Modulus)
UNIMPLEMENTED_NODE(Float64Negate)
UNIMPLEMENTED_NODE(Float64Equal)
UNIMPLEMENTED_NODE(Float64StrictEqual)
UNIMPLEMENTED_NODE(Float64LessThan)
UNIMPLEMENTED_NODE(Float64LessThanOrEqual)
UNIMPLEMENTED_NODE(Float64GreaterThan)
UNIMPLEMENTED_NODE(Float64GreaterThanOrEqual)
UNIMPLEMENTED_NODE_WITH_CALL(Float64Ieee754Unary)
UNIMPLEMENTED_NODE_WITH_CALL(BuiltinStringFromCharCode)
UNIMPLEMENTED_NODE_WITH_CALL(BuiltinStringPrototypeCharCodeAt)
UNIMPLEMENTED_NODE(Call, receiver_mode_, target_type_, feedback_)
UNIMPLEMENTED_NODE_WITH_CALL(CallBuiltin)
UNIMPLEMENTED_NODE_WITH_CALL(CallRuntime)
UNIMPLEMENTED_NODE_WITH_CALL(CallWithArrayLike)
UNIMPLEMENTED_NODE_WITH_CALL(CallWithSpread)
UNIMPLEMENTED_NODE_WITH_CALL(CallKnownJSFunction)
UNIMPLEMENTED_NODE_WITH_CALL(Construct)
UNIMPLEMENTED_NODE_WITH_CALL(ConstructWithSpread)
UNIMPLEMENTED_NODE(ConvertReceiver, mode_)
UNIMPLEMENTED_NODE(ConvertHoleToUndefined)
UNIMPLEMENTED_NODE_WITH_CALL(CreateEmptyArrayLiteral)
UNIMPLEMENTED_NODE_WITH_CALL(CreateArrayLiteral)
UNIMPLEMENTED_NODE_WITH_CALL(CreateShallowArrayLiteral)
UNIMPLEMENTED_NODE_WITH_CALL(CreateObjectLiteral)
UNIMPLEMENTED_NODE_WITH_CALL(CreateEmptyObjectLiteral)
UNIMPLEMENTED_NODE_WITH_CALL(CreateShallowObjectLiteral)
UNIMPLEMENTED_NODE_WITH_CALL(CreateFunctionContext)
UNIMPLEMENTED_NODE_WITH_CALL(CreateClosure)
UNIMPLEMENTED_NODE_WITH_CALL(FastCreateClosure)
UNIMPLEMENTED_NODE_WITH_CALL(CreateRegExpLiteral)
UNIMPLEMENTED_NODE_WITH_CALL(DeleteProperty)
UNIMPLEMENTED_NODE_WITH_CALL(ForInPrepare)
UNIMPLEMENTED_NODE_WITH_CALL(ForInNext)
UNIMPLEMENTED_NODE(GeneratorRestoreRegister)
UNIMPLEMENTED_NODE_WITH_CALL(GetIterator)
UNIMPLEMENTED_NODE(GetSecondReturnedValue)
UNIMPLEMENTED_NODE_WITH_CALL(GetTemplateObject)
UNIMPLEMENTED_NODE(LoadTaggedField)
UNIMPLEMENTED_NODE(LoadDoubleField)
UNIMPLEMENTED_NODE(LoadTaggedElement)
UNIMPLEMENTED_NODE(LoadSignedIntDataViewElement, type_)
UNIMPLEMENTED_NODE(LoadDoubleDataViewElement)
UNIMPLEMENTED_NODE(LoadSignedIntTypedArrayElement, elements_kind_)
UNIMPLEMENTED_NODE(LoadUnsignedIntTypedArrayElement, elements_kind_)
UNIMPLEMENTED_NODE(LoadDoubleTypedArrayElement, elements_kind_)
UNIMPLEMENTED_NODE(LoadDoubleElement)
UNIMPLEMENTED_NODE_WITH_CALL(LoadGlobal)
UNIMPLEMENTED_NODE_WITH_CALL(LoadNamedGeneric)
UNIMPLEMENTED_NODE_WITH_CALL(LoadNamedFromSuperGeneric)
UNIMPLEMENTED_NODE_WITH_CALL(SetNamedGeneric)
UNIMPLEMENTED_NODE_WITH_CALL(DefineNamedOwnGeneric)
UNIMPLEMENTED_NODE_WITH_CALL(StoreInArrayLiteralGeneric)
UNIMPLEMENTED_NODE_WITH_CALL(StoreGlobal)
UNIMPLEMENTED_NODE_WITH_CALL(GetKeyedGeneric)
UNIMPLEMENTED_NODE_WITH_CALL(SetKeyedGeneric)
UNIMPLEMENTED_NODE_WITH_CALL(DefineKeyedOwnGeneric)
UNIMPLEMENTED_NODE(Phi)
UNIMPLEMENTED_NODE(RegisterInput)
UNIMPLEMENTED_NODE(CheckedSmiTagUint32)
UNIMPLEMENTED_NODE(UnsafeSmiTag)
UNIMPLEMENTED_NODE(CheckedInternalizedString, check_type_)
UNIMPLEMENTED_NODE_WITH_CALL(CheckedObjectToIndex)
UNIMPLEMENTED_NODE(CheckedTruncateNumberToInt32)
UNIMPLEMENTED_NODE(CheckedInt32ToUint32)
UNIMPLEMENTED_NODE(CheckedUint32ToInt32)
UNIMPLEMENTED_NODE(ChangeInt32ToFloat64)
UNIMPLEMENTED_NODE(ChangeUint32ToFloat64)
UNIMPLEMENTED_NODE(CheckedTruncateFloat64ToInt32)
UNIMPLEMENTED_NODE(CheckedTruncateFloat64ToUint32)
UNIMPLEMENTED_NODE(TruncateUint32ToInt32)
UNIMPLEMENTED_NODE(TruncateFloat64ToInt32)
UNIMPLEMENTED_NODE(Int32ToNumber)
UNIMPLEMENTED_NODE(Uint32ToNumber)
UNIMPLEMENTED_NODE(Float64Box)
UNIMPLEMENTED_NODE(HoleyFloat64Box)
UNIMPLEMENTED_NODE(CheckedFloat64Unbox)
UNIMPLEMENTED_NODE(LogicalNot)
UNIMPLEMENTED_NODE(SetPendingMessage)
UNIMPLEMENTED_NODE_WITH_CALL(StringAt)
UNIMPLEMENTED_NODE(StringLength)
UNIMPLEMENTED_NODE(ToBoolean)
UNIMPLEMENTED_NODE(ToBooleanLogicalNot)
UNIMPLEMENTED_NODE(TaggedEqual)
UNIMPLEMENTED_NODE(TaggedNotEqual)
UNIMPLEMENTED_NODE_WITH_CALL(TestInstanceOf)
UNIMPLEMENTED_NODE(TestUndetectable)
UNIMPLEMENTED_NODE(TestTypeOf, literal_)
UNIMPLEMENTED_NODE_WITH_CALL(ToName)
UNIMPLEMENTED_NODE_WITH_CALL(ToNumberOrNumeric)
UNIMPLEMENTED_NODE_WITH_CALL(ToObject)
UNIMPLEMENTED_NODE_WITH_CALL(ToString)
UNIMPLEMENTED_NODE(AssertInt32, condition_, reason_)
UNIMPLEMENTED_NODE(CheckDynamicValue)
UNIMPLEMENTED_NODE(CheckInt32IsSmi)
UNIMPLEMENTED_NODE(CheckUint32IsSmi)
UNIMPLEMENTED_NODE(CheckHeapObject)
UNIMPLEMENTED_NODE(CheckInt32Condition, condition_, reason_)
UNIMPLEMENTED_NODE(CheckJSArrayBounds)
UNIMPLEMENTED_NODE(CheckJSDataViewBounds, element_type_)
UNIMPLEMENTED_NODE(CheckJSObjectElementsBounds)
UNIMPLEMENTED_NODE(CheckJSTypedArrayBounds, elements_kind_)
UNIMPLEMENTED_NODE(CheckMaps, check_type_)
UNIMPLEMENTED_NODE(CheckMapsWithMigration, check_type_)
UNIMPLEMENTED_NODE(CheckNumber)
UNIMPLEMENTED_NODE(CheckSmi)
UNIMPLEMENTED_NODE(CheckString, check_type_)
UNIMPLEMENTED_NODE(CheckSymbol, check_type_)
UNIMPLEMENTED_NODE(CheckValue)
UNIMPLEMENTED_NODE(CheckInstanceType, check_type_)
UNIMPLEMENTED_NODE(DebugBreak)
UNIMPLEMENTED_NODE_WITH_CALL(GeneratorStore)
UNIMPLEMENTED_NODE(JumpLoopPrologue, loop_depth_, unit_)
UNIMPLEMENTED_NODE_WITH_CALL(StoreMap)
UNIMPLEMENTED_NODE(StoreDoubleField)
UNIMPLEMENTED_NODE(StoreSignedIntDataViewElement, type_)
UNIMPLEMENTED_NODE(StoreDoubleDataViewElement)
UNIMPLEMENTED_NODE(StoreTaggedFieldNoWriteBarrier)
UNIMPLEMENTED_NODE_WITH_CALL(StoreTaggedFieldWithWriteBarrier)
UNIMPLEMENTED_NODE_WITH_CALL(ThrowReferenceErrorIfHole)
UNIMPLEMENTED_NODE_WITH_CALL(ThrowSuperNotCalledIfHole)
UNIMPLEMENTED_NODE_WITH_CALL(ThrowSuperAlreadyCalledIfNotHole)
UNIMPLEMENTED_NODE_WITH_CALL(ThrowIfNotSuperConstructor)
UNIMPLEMENTED_NODE(BranchIfRootConstant)
UNIMPLEMENTED_NODE(BranchIfToBooleanTrue)
UNIMPLEMENTED_NODE(BranchIfReferenceCompare, operation_)
UNIMPLEMENTED_NODE(BranchIfInt32Compare, operation_)
UNIMPLEMENTED_NODE(BranchIfFloat64Compare, operation_)
UNIMPLEMENTED_NODE(BranchIfUndefinedOrNull)
UNIMPLEMENTED_NODE(BranchIfJSReceiver)
UNIMPLEMENTED_NODE(Switch)
UNIMPLEMENTED_NODE(JumpLoop)
UNIMPLEMENTED_NODE_WITH_CALL(Abort)
UNIMPLEMENTED_NODE(Deopt)

void Int32AddWithOverflow::SetValueLocationConstraints() {
  UseRegister(left_input());
  UseRegister(right_input());
  DefineAsRegister(this);
}

void Int32AddWithOverflow::GenerateCode(MaglevAssembler* masm,
                                        const ProcessingState& state) {
  Register left = ToRegister(left_input()).W();
  Register right = ToRegister(right_input()).W();
  Register out = ToRegister(result()).W();
  __ Add(out, left, right);
  // None of the mutated input registers should be a register input into the
  // eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
  __ EmitEagerDeoptIf(vs, DeoptimizeReason::kOverflow, this);
}

void CheckedSmiTagInt32::SetValueLocationConstraints() {
  UseRegister(input());
  DefineAsRegister(this);
}
void CheckedSmiTagInt32::GenerateCode(MaglevAssembler* masm,
                                      const ProcessingState& state) {
  Register reg = ToRegister(input()).W();
  Register out = ToRegister(result()).W();
  __ Add(out, reg, reg);
  // None of the mutated input registers should be a register input into the
  // eager deopt info.
  DCHECK_REGLIST_EMPTY(RegList{out} &
                       GetGeneralRegistersUsedAsInputs(eager_deopt_info()));
  __ EmitEagerDeoptIf(vs, DeoptimizeReason::kOverflow, this);
}

void IncreaseInterruptBudget::SetValueLocationConstraints() {}
void IncreaseInterruptBudget::GenerateCode(MaglevAssembler* masm,
                                           const ProcessingState& state) {
  UseScratchRegisterScope temps(masm);
  Register feedback_cell = temps.AcquireX();
  Register budget = temps.AcquireW();
  __ Ldr(feedback_cell,
         MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ LoadTaggedPointerField(
      feedback_cell,
      FieldMemOperand(feedback_cell, JSFunction::kFeedbackCellOffset));
  __ Ldr(budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
  __ Add(budget, budget, Immediate(amount()));
  __ Str(budget,
         FieldMemOperand(feedback_cell, FeedbackCell::kInterruptBudgetOffset));
}

int ReduceInterruptBudget::MaxCallStackArgs() const { return 1; }
void ReduceInterruptBudget::SetValueLocationConstraints() {}
void ReduceInterruptBudget::GenerateCode(MaglevAssembler* masm,
                                         const ProcessingState& state) {
  {
    UseScratchRegisterScope temps(masm);
    Register feedback_cell = temps.AcquireX();
    Register budget = temps.AcquireW();
    __ Ldr(feedback_cell,
           MemOperand(fp, StandardFrameConstants::kFunctionOffset));
    __ LoadTaggedPointerField(
        feedback_cell,
        FieldMemOperand(feedback_cell, JSFunction::kFeedbackCellOffset));
    __ Ldr(budget, FieldMemOperand(feedback_cell,
                                   FeedbackCell::kInterruptBudgetOffset));
    __ Sub(budget, budget, Immediate(amount()));
    __ Str(budget, FieldMemOperand(feedback_cell,
                                   FeedbackCell::kInterruptBudgetOffset));
  }

  ZoneLabelRef done(masm);
  __ JumpToDeferredIf(
      lt,
      [](MaglevAssembler* masm, ZoneLabelRef done,
         ReduceInterruptBudget* node) {
        {
          SaveRegisterStateForCall save_register_state(
              masm, node->register_snapshot());
          UseScratchRegisterScope temps(masm);
          Register function = temps.AcquireX();
          __ Move(kContextRegister, static_cast<Handle<HeapObject>>(
                                        masm->native_context().object()));
          __ Ldr(function,
                 MemOperand(fp, StandardFrameConstants::kFunctionOffset));
          __ PushArgument(function);
          __ CallRuntime(Runtime::kBytecodeBudgetInterruptWithStackCheck_Maglev,
                         1);
          save_register_state.DefineSafepointWithLazyDeopt(
              node->lazy_deopt_info());
        }
        __ B(*done);
      },
      done, this);
  __ bind(*done);
}

// ---
// Control nodes
// ---
void Return::SetValueLocationConstraints() {
  UseFixed(value_input(), kReturnRegister0);
}
void Return::GenerateCode(MaglevAssembler* masm, const ProcessingState& state) {
  DCHECK_EQ(ToRegister(value_input()), kReturnRegister0);
  // Read the formal number of parameters from the top level compilation unit
  // (i.e. the outermost, non inlined function).
  int formal_params_size =
      masm->compilation_info()->toplevel_compilation_unit()->parameter_count();

  // We're not going to continue execution, so we can use an arbitrary register
  // here instead of relying on temporaries from the register allocator.
  // We cannot use scratch registers, since they're used in LeaveFrame and
  // DropArguments.
  Register actual_params_size = x9;
  Register params_size = x10;

  // Compute the size of the actual parameters + receiver (in bytes).
  // TODO(leszeks): Consider making this an input into Return to re-use the
  // incoming argc's register (if it's still valid).
  __ Ldr(actual_params_size,
         MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ Mov(params_size, Immediate(formal_params_size));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  Label corrected_args_count;
  __ CompareAndBranch(params_size, actual_params_size, ge,
                      &corrected_args_count);
  __ Mov(params_size, actual_params_size);
  __ bind(&corrected_args_count);

  // Leave the frame.
  __ LeaveFrame(StackFrame::MAGLEV);

  // Drop receiver + arguments according to dynamic arguments size.
  __ DropArguments(params_size, TurboAssembler::kCountIncludesReceiver);
  __ Ret();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
