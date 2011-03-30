// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#include "v8.h"

#if defined(V8_TARGET_ARCH_MIPS)

#include "bootstrapper.h"
#include "code-stubs.h"
#include "codegen-inl.h"
#include "compiler.h"
#include "debug.h"
#include "ic-inl.h"
#include "jsregexp.h"
#include "jump-target-inl.h"
#include "parser.h"
#include "regexp-macro-assembler.h"
#include "regexp-stack.h"
#include "register-allocator-inl.h"
#include "runtime.h"
#include "scopes.h"
#include "stub-cache.h"
#include "virtual-frame-inl.h"
#include "virtual-frame-mips-inl.h"

namespace v8 {
namespace internal {


#define __ ACCESS_MASM(masm_)

// -------------------------------------------------------------------------
// Platform-specific DeferredCode functions.

void DeferredCode::SaveRegisters() {
  // On MIPS you either have a completely spilled frame or you
  // handle it yourself, but at the moment there's no automation
  // of registers and deferred code.
}


void DeferredCode::RestoreRegisters() {
}


// -------------------------------------------------------------------------
// Platform-specific RuntimeCallHelper functions.

void VirtualFrameRuntimeCallHelper::BeforeCall(MacroAssembler* masm) const {
  frame_state_->frame()->AssertIsSpilled();
}


void VirtualFrameRuntimeCallHelper::AfterCall(MacroAssembler* masm) const {
}


void StubRuntimeCallHelper::BeforeCall(MacroAssembler* masm) const {
  masm->EnterInternalFrame();
}


void StubRuntimeCallHelper::AfterCall(MacroAssembler* masm) const {
  masm->LeaveInternalFrame();
}


// -----------------------------------------------------------------------------
// CodeGenState implementation.

CodeGenState::CodeGenState(CodeGenerator* owner)
    : owner_(owner),
      previous_(owner->state()) {
  owner->set_state(this);
}


ConditionCodeGenState::ConditionCodeGenState(CodeGenerator* owner,
                           JumpTarget* true_target,
                           JumpTarget* false_target)
    : CodeGenState(owner),
      true_target_(true_target),
      false_target_(false_target) {
  owner->set_state(this);
}


TypeInfoCodeGenState::TypeInfoCodeGenState(CodeGenerator* owner,
                                           Slot* slot,
                                           TypeInfo type_info)
    : CodeGenState(owner),
      slot_(slot) {
  owner->set_state(this);
  old_type_info_ = owner->set_type_info(slot, type_info);
}


CodeGenState::~CodeGenState() {
  ASSERT(owner_->state() == this);
  owner_->set_state(previous_);
}


TypeInfoCodeGenState::~TypeInfoCodeGenState() {
  owner()->set_type_info(slot_, old_type_info_);
}


// -----------------------------------------------------------------------------
// CodeGenerator implementation.

CodeGenerator::CodeGenerator(MacroAssembler* masm)
    : deferred_(8),
      masm_(masm),
      info_(NULL),
      frame_(NULL),
      allocator_(NULL),
      cc_reg_(cc_always),
      state_(NULL),
      loop_nesting_(0),
      type_info_(NULL),
      function_return_(JumpTarget::BIDIRECTIONAL),
      function_return_is_shadowed_(false) {
}


// Calling conventions:
// fp: caller's frame pointer
// sp: stack pointer
// a1: called JS function
// cp: callee's context

void CodeGenerator::Generate(CompilationInfo* info) {
  UNIMPLEMENTED_MIPS();
}


int CodeGenerator::NumberOfSlot(Slot* slot) {
  UNIMPLEMENTED_MIPS();
  return 0;
}


MemOperand CodeGenerator::SlotOperand(Slot* slot, Register tmp) {
  UNIMPLEMENTED_MIPS();
  return MemOperand(zero_reg, 0);
}


MemOperand CodeGenerator::ContextSlotOperandCheckExtensions(
    Slot* slot,
    Register tmp,
    Register tmp2,
    JumpTarget* slow) {
  UNIMPLEMENTED_MIPS();
  return MemOperand(zero_reg, 0);
}


void CodeGenerator::LoadCondition(Expression* x,
                                  JumpTarget* true_target,
                                  JumpTarget* false_target,
                                  bool force_cc) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::Load(Expression* x) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::LoadGlobal() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::LoadGlobalReceiver(Register scratch) {
  UNIMPLEMENTED_MIPS();
}


ArgumentsAllocationMode CodeGenerator::ArgumentsMode() {
  UNIMPLEMENTED_MIPS();
  return EAGER_ARGUMENTS_ALLOCATION;
}


void CodeGenerator::StoreArgumentsObject(bool initial) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::LoadTypeofExpression(Expression* x) {
  UNIMPLEMENTED_MIPS();
}


Reference::Reference(CodeGenerator* cgen,
                     Expression* expression,
                     bool persist_after_get)
    : cgen_(cgen),
      expression_(expression),
      type_(ILLEGAL),
      persist_after_get_(persist_after_get) {
  UNIMPLEMENTED_MIPS();
}


Reference::~Reference() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::LoadReference(Reference* ref) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::UnloadReference(Reference* ref) {
  UNIMPLEMENTED_MIPS();
}


// ECMA-262, section 9.2, page 30: ToBoolean(). Convert the given
// register to a boolean in the condition code register. The code
// may jump to 'false_target' in case the register converts to 'false'.
void CodeGenerator::ToBoolean(JumpTarget* true_target,
                              JumpTarget* false_target) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenericBinaryOperation(Token::Value op,
                                           OverwriteMode overwrite_mode,
                                           GenerateInlineSmi inline_smi,
                                           int constant_rhs) {
  UNIMPLEMENTED_MIPS();
}


class DeferredInlineSmiOperation: public DeferredCode {
 public:
  DeferredInlineSmiOperation(Token::Value op,
                             int value,
                             bool reversed,
                             OverwriteMode overwrite_mode,
                             Register tos)
      : op_(op),
        value_(value),
        reversed_(reversed),
        overwrite_mode_(overwrite_mode),
        tos_register_(tos) {
    set_comment("[ DeferredInlinedSmiOperation");
  }

  virtual void Generate();
  // This stub makes explicit calls to SaveRegisters(), RestoreRegisters() and
  // Exit(). Currently on MIPS SaveRegisters() and RestoreRegisters() are empty
  // methods, it is the responsibility of the deferred code to save and restore
  // registers.
  virtual bool AutoSaveAndRestore() { return false; }

  void JumpToNonSmiInput(Condition cond, Register cmp1, const Operand& cmp2);
  void JumpToAnswerOutOfRange(Condition cond,
                              Register cmp1,
                              const Operand& cmp2);

 private:
  void GenerateNonSmiInput();
  void GenerateAnswerOutOfRange();
  void WriteNonSmiAnswer(Register answer,
                         Register heap_number,
                         Register scratch);

  Token::Value op_;
  int value_;
  bool reversed_;
  OverwriteMode overwrite_mode_;
  Register tos_register_;
  Label non_smi_input_;
  Label answer_out_of_range_;
};


// For bit operations we try harder and handle the case where the input is not
// a Smi but a 32bits integer without calling the generic stub.
void DeferredInlineSmiOperation::JumpToNonSmiInput(Condition cond,
                                                   Register cmp1,
                                                   const Operand& cmp2) {
  UNIMPLEMENTED_MIPS();
}


// For bit operations the result is always 32bits so we handle the case where
// the result does not fit in a Smi without calling the generic stub.
void DeferredInlineSmiOperation::JumpToAnswerOutOfRange(Condition cond,
                                                        Register cmp1,
                                                        const Operand& cmp2) {
  UNIMPLEMENTED_MIPS();
}


// On entry the non-constant side of the binary operation is in tos_register_
// and the constant smi side is nowhere.  The tos_register_ is not used by the
// virtual frame.  On exit the answer is in the tos_register_ and the virtual
// frame is unchanged.
void DeferredInlineSmiOperation::Generate() {
  UNIMPLEMENTED_MIPS();
}


// Convert and write the integer answer into heap_number.
void DeferredInlineSmiOperation::WriteNonSmiAnswer(Register answer,
                                                   Register heap_number,
                                                   Register scratch) {
  UNIMPLEMENTED_MIPS();
}


void DeferredInlineSmiOperation::GenerateNonSmiInput() {
  UNIMPLEMENTED_MIPS();
}


void DeferredInlineSmiOperation::GenerateAnswerOutOfRange() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::SmiOperation(Token::Value op,
                                 Handle<Object> value,
                                 bool reversed,
                                 OverwriteMode mode) {
  UNIMPLEMENTED_MIPS();
}


// On MIPS we load registers condReg1 and condReg2 with the values which should
// be compared. With the CodeGenerator::cc_reg_ condition, functions will be
// able to evaluate correctly the condition. (eg CodeGenerator::Branch)
void CodeGenerator::Comparison(Condition cc,
                               Expression* left,
                               Expression* right,
                               bool strict) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::CallWithArguments(ZoneList<Expression*>* args,
                                      CallFunctionFlags flags,
                                      int position) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::CallApplyLazy(Expression* applicand,
                                  Expression* receiver,
                                  VariableProxy* arguments,
                                  int position) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::Branch(bool if_true, JumpTarget* target) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::CheckStack() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitStatements(ZoneList<Statement*>* statements) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitBlock(Block* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitDeclaration(Declaration* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitExpressionStatement(ExpressionStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitEmptyStatement(EmptyStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitIfStatement(IfStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitContinueStatement(ContinueStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitBreakStatement(BreakStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitReturnStatement(ReturnStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateReturnSequence() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitWithEnterStatement(WithEnterStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitWithExitStatement(WithExitStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitSwitchStatement(SwitchStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitDoWhileStatement(DoWhileStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitWhileStatement(WhileStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitForStatement(ForStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitForInStatement(ForInStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitTryCatchStatement(TryCatchStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitTryFinallyStatement(TryFinallyStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitDebuggerStatement(DebuggerStatement* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::InstantiateFunction(
    Handle<SharedFunctionInfo> function_info,
    bool pretenure) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitFunctionLiteral(FunctionLiteral* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitSharedFunctionInfoLiteral(
    SharedFunctionInfoLiteral* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitConditional(Conditional* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::LoadFromSlot(Slot* slot, TypeofState typeof_state) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::LoadFromSlotCheckForArguments(Slot* slot,
                                                  TypeofState state) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::StoreToSlot(Slot* slot, InitState init_state) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::LoadFromGlobalSlotCheckExtensions(Slot* slot,
                                                      TypeofState typeof_state,
                                                      JumpTarget* slow) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::EmitDynamicLoadFromSlotFastCase(Slot* slot,
                                                    TypeofState typeof_state,
                                                    JumpTarget* slow,
                                                    JumpTarget* done) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitSlot(Slot* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitVariableProxy(VariableProxy* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitLiteral(Literal* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitRegExpLiteral(RegExpLiteral* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitObjectLiteral(ObjectLiteral* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitArrayLiteral(ArrayLiteral* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitCatchExtensionObject(CatchExtensionObject* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::EmitSlotAssignment(Assignment* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::EmitNamedPropertyAssignment(Assignment* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::EmitKeyedPropertyAssignment(Assignment* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitAssignment(Assignment* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitThrow(Throw* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitProperty(Property* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitCall(Call* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitCallNew(CallNew* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateClassOf(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateValueOf(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateSetValueOf(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsSmi(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateLog(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsNonNegativeSmi(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateMathPow(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateMathSqrt(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


class DeferredStringCharCodeAt : public DeferredCode {
 public:
  DeferredStringCharCodeAt(Register object,
                           Register index,
                           Register scratch,
                           Register result)
      : result_(result),
        char_code_at_generator_(object,
                                index,
                                scratch,
                                result,
                                &need_conversion_,
                                &need_conversion_,
                                &index_out_of_range_,
                                STRING_INDEX_IS_NUMBER) {}

  StringCharCodeAtGenerator* fast_case_generator() {
    return &char_code_at_generator_;
  }

  virtual void Generate() {
    UNIMPLEMENTED_MIPS();
  }

 private:
  Register result_;

  Label need_conversion_;
  Label index_out_of_range_;

  StringCharCodeAtGenerator char_code_at_generator_;
};


void CodeGenerator::GenerateStringCharCodeAt(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


class DeferredStringCharFromCode : public DeferredCode {
 public:
  DeferredStringCharFromCode(Register code,
                             Register result)
      : char_from_code_generator_(code, result) {}

  StringCharFromCodeGenerator* fast_case_generator() {
    return &char_from_code_generator_;
  }

  virtual void Generate() {
    VirtualFrameRuntimeCallHelper call_helper(frame_state());
    char_from_code_generator_.GenerateSlow(masm(), call_helper);
  }

 private:
  StringCharFromCodeGenerator char_from_code_generator_;
};


void CodeGenerator::GenerateStringCharFromCode(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


class DeferredStringCharAt : public DeferredCode {
 public:
  DeferredStringCharAt(Register object,
                       Register index,
                       Register scratch1,
                       Register scratch2,
                       Register result)
      : result_(result),
        char_at_generator_(object,
                           index,
                           scratch1,
                           scratch2,
                           result,
                           &need_conversion_,
                           &need_conversion_,
                           &index_out_of_range_,
                           STRING_INDEX_IS_NUMBER) {}

  StringCharAtGenerator* fast_case_generator() {
    return &char_at_generator_;
  }

  virtual void Generate() {
  UNIMPLEMENTED_MIPS();
}

 private:
  Register result_;

  Label need_conversion_;
  Label index_out_of_range_;

  StringCharAtGenerator char_at_generator_;
};


void CodeGenerator::GenerateStringCharAt(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsArray(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsRegExp(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsObject(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsSpecObject(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


class DeferredIsStringWrapperSafeForDefaultValueOf : public DeferredCode {
 public:
  DeferredIsStringWrapperSafeForDefaultValueOf(Register object,
                                               Register map_result,
                                               Register scratch1,
                                               Register scratch2)
      : object_(object),
        map_result_(map_result),
        scratch1_(scratch1),
        scratch2_(scratch2) { }

  virtual void Generate() {
    UNIMPLEMENTED_MIPS();
  }

 private:
  Register object_;
  Register map_result_;
  Register scratch1_;
  Register scratch2_;
};


void CodeGenerator::GenerateIsStringWrapperSafeForDefaultValueOf(
    ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsFunction(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsUndetectableObject(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsConstructCall(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateArgumentsLength(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateArguments(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateRandomHeapNumber(
    ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateStringAdd(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateSubString(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateStringCompare(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateRegExpExec(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateRegExpConstructResult(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


class DeferredSearchCache: public DeferredCode {
 public:
  DeferredSearchCache(Register dst, Register cache, Register key)
      : dst_(dst), cache_(cache), key_(key) {
    set_comment("[ DeferredSearchCache");
  }

  virtual void Generate();

 private:
  Register dst_, cache_, key_;
};


void DeferredSearchCache::Generate() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateGetFromCache(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateNumberToString(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


class DeferredSwapElements: public DeferredCode {
 public:
  DeferredSwapElements(Register object, Register index1, Register index2)
      : object_(object), index1_(index1), index2_(index2) {
    set_comment("[ DeferredSwapElements");
  }

  virtual void Generate();

 private:
  Register object_, index1_, index2_;
};


void DeferredSwapElements::Generate() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateSwapElements(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateCallFunction(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateMathSin(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateMathCos(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateMathLog(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateObjectEquals(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateIsRegExpEquivalent(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateHasCachedArrayIndex(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateGetCachedArrayIndex(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateFastAsciiArrayJoin(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitCallRuntime(CallRuntime* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitUnaryOperation(UnaryOperation* node) {
  UNIMPLEMENTED_MIPS();
}


class DeferredCountOperation: public DeferredCode {
 public:
  DeferredCountOperation(Register value,
                         bool is_increment,
                         bool is_postfix,
                         int target_size)
      : value_(value),
        is_increment_(is_increment),
        is_postfix_(is_postfix),
        target_size_(target_size) {}

  virtual void Generate() {
    UNIMPLEMENTED_MIPS();
  }

 private:
  Register value_;
  bool is_increment_;
  bool is_postfix_;
  int target_size_;
};


void CodeGenerator::VisitCountOperation(CountOperation* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::GenerateLogicalBooleanOperation(BinaryOperation* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitBinaryOperation(BinaryOperation* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitThisFunction(ThisFunction* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitCompareOperation(CompareOperation* node) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::VisitCompareToNull(CompareToNull* node) {
  UNIMPLEMENTED_MIPS();
}


class DeferredReferenceGetNamedValue: public DeferredCode {
 public:
  explicit DeferredReferenceGetNamedValue(Register receiver,
                                          Handle<String> name,
                                          bool is_contextual)
      : receiver_(receiver),
        name_(name),
        is_contextual_(is_contextual),
        is_dont_delete_(false) {
    set_comment(is_contextual
                ? "[ DeferredReferenceGetNamedValue (contextual)"
                : "[ DeferredReferenceGetNamedValue");
  }

  virtual void Generate();

  void set_is_dont_delete(bool value) {
    ASSERT(is_contextual_);
    is_dont_delete_ = value;
  }

 private:
  Register receiver_;
  Handle<String> name_;
  bool is_contextual_;
  bool is_dont_delete_;
};



void DeferredReferenceGetNamedValue::Generate() {
  UNIMPLEMENTED_MIPS();
}


class DeferredReferenceGetKeyedValue: public DeferredCode {
 public:
  DeferredReferenceGetKeyedValue(Register key, Register receiver)
      : key_(key), receiver_(receiver) {
    set_comment("[ DeferredReferenceGetKeyedValue");
  }

  virtual void Generate();

 private:
  Register key_;
  Register receiver_;
};


void DeferredReferenceGetKeyedValue::Generate() {
  UNIMPLEMENTED_MIPS();
}


class DeferredReferenceSetKeyedValue: public DeferredCode {
 public:
  DeferredReferenceSetKeyedValue(Register value,
                                 Register key,
                                 Register receiver)
      : value_(value), key_(key), receiver_(receiver) {
    set_comment("[ DeferredReferenceSetKeyedValue");
  }

  virtual void Generate();

 private:
  Register value_;
  Register key_;
  Register receiver_;
};


void DeferredReferenceSetKeyedValue::Generate() {
  UNIMPLEMENTED_MIPS();
}


class DeferredReferenceSetNamedValue: public DeferredCode {
 public:
  DeferredReferenceSetNamedValue(Register value,
                                 Register receiver,
                                 Handle<String> name)
      : value_(value), receiver_(receiver), name_(name) {
    set_comment("[ DeferredReferenceSetNamedValue");
  }

  virtual void Generate();

 private:
  Register value_;
  Register receiver_;
  Handle<String> name_;
};


void DeferredReferenceSetNamedValue::Generate() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::EmitNamedLoad(Handle<String> name, bool is_contextual) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::EmitNamedStore(Handle<String> name, bool is_contextual) {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::EmitKeyedLoad() {
  UNIMPLEMENTED_MIPS();
}


void CodeGenerator::EmitKeyedStore(StaticType* key_type,
                                   WriteBarrierCharacter wb_info) {
  UNIMPLEMENTED_MIPS();
}


#ifdef DEBUG
bool CodeGenerator::HasValidEntryRegisters() {
  UNIMPLEMENTED_MIPS();
  return false;
}
#endif


#undef __
#define __ ACCESS_MASM(masm)

// -----------------------------------------------------------------------------
// Reference support.


Handle<String> Reference::GetName() {
  UNIMPLEMENTED_MIPS();
  return Handle<String>();
}


void Reference::DupIfPersist() {
  UNIMPLEMENTED_MIPS();
}


void Reference::GetValue() {
  UNIMPLEMENTED_MIPS();
}


void Reference::SetValue(InitState init_state, WriteBarrierCharacter wb_info) {
  UNIMPLEMENTED_MIPS();
}


const char* GenericBinaryOpStub::GetName() {
  UNIMPLEMENTED_MIPS();
  return name_;
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS
