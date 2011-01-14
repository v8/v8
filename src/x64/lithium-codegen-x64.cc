// Copyright 2011 the V8 project authors. All rights reserved.
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

#if defined(V8_TARGET_ARCH_X64)

#include "x64/lithium-codegen-x64.h"
#include "code-stubs.h"
#include "stub-cache.h"

namespace v8 {
namespace internal {


#define __ masm()->

bool LCodeGen::GenerateCode() {
  HPhase phase("Code generation", chunk());
  ASSERT(is_unused());
  status_ = GENERATING;
  return GeneratePrologue() &&
      GenerateBody() &&
      GenerateDeferredCode() &&
      GenerateSafepointTable();
}


void LCodeGen::FinishCode(Handle<Code> code) {
  ASSERT(is_done());
  code->set_stack_slots(StackSlotCount());
  code->set_safepoint_table_start(safepoints_.GetCodeOffset());
  PopulateDeoptimizationData(code);
}


void LCodeGen::Abort(const char* format, ...) {
  if (FLAG_trace_bailout) {
    SmartPointer<char> debug_name = graph()->debug_name()->ToCString();
    PrintF("Aborting LCodeGen in @\"%s\": ", *debug_name);
    va_list arguments;
    va_start(arguments, format);
    OS::VPrint(format, arguments);
    va_end(arguments);
    PrintF("\n");
  }
  status_ = ABORTED;
}


void LCodeGen::Comment(const char* format, ...) {
  if (!FLAG_code_comments) return;
  char buffer[4 * KB];
  StringBuilder builder(buffer, ARRAY_SIZE(buffer));
  va_list arguments;
  va_start(arguments, format);
  builder.AddFormattedList(format, arguments);
  va_end(arguments);

  // Copy the string before recording it in the assembler to avoid
  // issues when the stack allocated buffer goes out of scope.
  size_t length = builder.position();
  Vector<char> copy = Vector<char>::New(length + 1);
  memcpy(copy.start(), builder.Finalize(), copy.length());
  masm()->RecordComment(copy.start());
}


bool LCodeGen::GeneratePrologue() {
  Abort("Unimplemented: %s", "GeneratePrologue");
  return false;
}


bool LCodeGen::GenerateBody() {
  ASSERT(is_generating());
  bool emit_instructions = true;
  for (current_instruction_ = 0;
       !is_aborted() && current_instruction_ < instructions_->length();
       current_instruction_++) {
    LInstruction* instr = instructions_->at(current_instruction_);
    if (instr->IsLabel()) {
      LLabel* label = LLabel::cast(instr);
      emit_instructions = !label->HasReplacement();
    }

    if (emit_instructions) {
      Comment(";;; @%d: %s.", current_instruction_, instr->Mnemonic());
      instr->CompileToNative(this);
    }
  }
  return !is_aborted();
}


LInstruction* LCodeGen::GetNextInstruction() {
  if (current_instruction_ < instructions_->length() - 1) {
    return instructions_->at(current_instruction_ + 1);
  } else {
    return NULL;
  }
}


bool LCodeGen::GenerateDeferredCode() {
  ASSERT(is_generating());
  for (int i = 0; !is_aborted() && i < deferred_.length(); i++) {
    LDeferredCode* code = deferred_[i];
    __ bind(code->entry());
    code->Generate();
    __ jmp(code->exit());
  }

  // Deferred code is the last part of the instruction sequence. Mark
  // the generated code as done unless we bailed out.
  if (!is_aborted()) status_ = DONE;
  return !is_aborted();
}


bool LCodeGen::GenerateSafepointTable() {
  Abort("Unimplemented: %s", "GeneratePrologue");
  return false;
}


Register LCodeGen::ToRegister(int index) const {
  return Register::FromAllocationIndex(index);
}


XMMRegister LCodeGen::ToDoubleRegister(int index) const {
  return XMMRegister::FromAllocationIndex(index);
}


Register LCodeGen::ToRegister(LOperand* op) const {
  ASSERT(op->IsRegister());
  return ToRegister(op->index());
}


XMMRegister LCodeGen::ToDoubleRegister(LOperand* op) const {
  ASSERT(op->IsDoubleRegister());
  return ToDoubleRegister(op->index());
}


int LCodeGen::ToInteger32(LConstantOperand* op) const {
  Handle<Object> value = chunk_->LookupLiteral(op);
  ASSERT(chunk_->LookupLiteralRepresentation(op).IsInteger32());
  ASSERT(static_cast<double>(static_cast<int32_t>(value->Number())) ==
      value->Number());
  return static_cast<int32_t>(value->Number());
}


Operand LCodeGen::ToOperand(LOperand* op) const {
  // Does not handle registers. In X64 assembler, plain registers are not
  // representable as an Operand.
  ASSERT(op->IsStackSlot() || op->IsDoubleStackSlot());
  int index = op->index();
  if (index >= 0) {
    // Local or spill slot. Skip the frame pointer, function, and
    // context in the fixed part of the frame.
    return Operand(rbp, -(index + 3) * kPointerSize);
  } else {
    // Incoming parameter. Skip the return address.
    return Operand(rbp, -(index - 1) * kPointerSize);
  }
}


void LCodeGen::WriteTranslation(LEnvironment* environment,
                                Translation* translation) {
  if (environment == NULL) return;

  // The translation includes one command per value in the environment.
  int translation_size = environment->values()->length();
  // The output frame height does not include the parameters.
  int height = translation_size - environment->parameter_count();

  WriteTranslation(environment->outer(), translation);
  int closure_id = DefineDeoptimizationLiteral(environment->closure());
  translation->BeginFrame(environment->ast_id(), closure_id, height);
  for (int i = 0; i < translation_size; ++i) {
    LOperand* value = environment->values()->at(i);
    // spilled_registers_ and spilled_double_registers_ are either
    // both NULL or both set.
    if (environment->spilled_registers() != NULL && value != NULL) {
      if (value->IsRegister() &&
          environment->spilled_registers()[value->index()] != NULL) {
        translation->MarkDuplicate();
        AddToTranslation(translation,
                         environment->spilled_registers()[value->index()],
                         environment->HasTaggedValueAt(i));
      } else if (
          value->IsDoubleRegister() &&
          environment->spilled_double_registers()[value->index()] != NULL) {
        translation->MarkDuplicate();
        AddToTranslation(
            translation,
            environment->spilled_double_registers()[value->index()],
            false);
      }
    }

    AddToTranslation(translation, value, environment->HasTaggedValueAt(i));
  }
}


void LCodeGen::AddToTranslation(Translation* translation,
                                LOperand* op,
                                bool is_tagged) {
  if (op == NULL) {
    // TODO(twuerthinger): Introduce marker operands to indicate that this value
    // is not present and must be reconstructed from the deoptimizer. Currently
    // this is only used for the arguments object.
    translation->StoreArgumentsObject();
  } else if (op->IsStackSlot()) {
    if (is_tagged) {
      translation->StoreStackSlot(op->index());
    } else {
      translation->StoreInt32StackSlot(op->index());
    }
  } else if (op->IsDoubleStackSlot()) {
    translation->StoreDoubleStackSlot(op->index());
  } else if (op->IsArgument()) {
    ASSERT(is_tagged);
    int src_index = StackSlotCount() + op->index();
    translation->StoreStackSlot(src_index);
  } else if (op->IsRegister()) {
    Register reg = ToRegister(op);
    if (is_tagged) {
      translation->StoreRegister(reg);
    } else {
      translation->StoreInt32Register(reg);
    }
  } else if (op->IsDoubleRegister()) {
    XMMRegister reg = ToDoubleRegister(op);
    translation->StoreDoubleRegister(reg);
  } else if (op->IsConstantOperand()) {
    Handle<Object> literal = chunk()->LookupLiteral(LConstantOperand::cast(op));
    int src_index = DefineDeoptimizationLiteral(literal);
    translation->StoreLiteral(src_index);
  } else {
    UNREACHABLE();
  }
}


void LCodeGen::CallCode(Handle<Code> code,
                        RelocInfo::Mode mode,
                        LInstruction* instr) {
  Abort("Unimplemented: %s", "CallCode");
}


void LCodeGen::CallRuntime(Runtime::Function* function,
                           int num_arguments,
                           LInstruction* instr) {
  Abort("Unimplemented: %s", "CallRuntime");
}


void LCodeGen::RegisterLazyDeoptimization(LInstruction* instr) {
  // Create the environment to bailout to. If the call has side effects
  // execution has to continue after the call otherwise execution can continue
  // from a previous bailout point repeating the call.
  LEnvironment* deoptimization_environment;
  if (instr->HasDeoptimizationEnvironment()) {
    deoptimization_environment = instr->deoptimization_environment();
  } else {
    deoptimization_environment = instr->environment();
  }

  RegisterEnvironmentForDeoptimization(deoptimization_environment);
  RecordSafepoint(instr->pointer_map(),
                  deoptimization_environment->deoptimization_index());
}


void LCodeGen::RegisterEnvironmentForDeoptimization(LEnvironment* environment) {
  Abort("Unimplemented: %s", "RegisterEnvironmentForDeoptimization");
}


void LCodeGen::DeoptimizeIf(Condition cc, LEnvironment* environment) {
  Abort("Unimplemented: %s", "Deoptimiz");
}


void LCodeGen::PopulateDeoptimizationData(Handle<Code> code) {
  int length = deoptimizations_.length();
  if (length == 0) return;
  ASSERT(FLAG_deopt);
  Handle<DeoptimizationInputData> data =
      Factory::NewDeoptimizationInputData(length, TENURED);

  data->SetTranslationByteArray(*translations_.CreateByteArray());
  data->SetInlinedFunctionCount(Smi::FromInt(inlined_function_count_));

  Handle<FixedArray> literals =
      Factory::NewFixedArray(deoptimization_literals_.length(), TENURED);
  for (int i = 0; i < deoptimization_literals_.length(); i++) {
    literals->set(i, *deoptimization_literals_[i]);
  }
  data->SetLiteralArray(*literals);

  data->SetOsrAstId(Smi::FromInt(info_->osr_ast_id()));
  data->SetOsrPcOffset(Smi::FromInt(osr_pc_offset_));

  // Populate the deoptimization entries.
  for (int i = 0; i < length; i++) {
    LEnvironment* env = deoptimizations_[i];
    data->SetAstId(i, Smi::FromInt(env->ast_id()));
    data->SetTranslationIndex(i, Smi::FromInt(env->translation_index()));
    data->SetArgumentsStackHeight(i,
                                  Smi::FromInt(env->arguments_stack_height()));
  }
  code->set_deoptimization_data(*data);
}


int LCodeGen::DefineDeoptimizationLiteral(Handle<Object> literal) {
  int result = deoptimization_literals_.length();
  for (int i = 0; i < deoptimization_literals_.length(); ++i) {
    if (deoptimization_literals_[i].is_identical_to(literal)) return i;
  }
  deoptimization_literals_.Add(literal);
  return result;
}


void LCodeGen::PopulateDeoptimizationLiteralsWithInlinedFunctions() {
  ASSERT(deoptimization_literals_.length() == 0);

  const ZoneList<Handle<JSFunction> >* inlined_closures =
      chunk()->inlined_closures();

  for (int i = 0, length = inlined_closures->length();
       i < length;
       i++) {
    DefineDeoptimizationLiteral(inlined_closures->at(i));
  }

  inlined_function_count_ = deoptimization_literals_.length();
}


void LCodeGen::RecordSafepoint(LPointerMap* pointers,
                               int deoptimization_index) {
  const ZoneList<LOperand*>* operands = pointers->operands();
  Safepoint safepoint = safepoints_.DefineSafepoint(masm(),
                                                    deoptimization_index);
  for (int i = 0; i < operands->length(); i++) {
    LOperand* pointer = operands->at(i);
    if (pointer->IsStackSlot()) {
      safepoint.DefinePointerSlot(pointer->index());
    }
  }
}


void LCodeGen::RecordSafepointWithRegisters(LPointerMap* pointers,
                                            int arguments,
                                            int deoptimization_index) {
  const ZoneList<LOperand*>* operands = pointers->operands();
  Safepoint safepoint =
      safepoints_.DefineSafepointWithRegisters(
          masm(), arguments, deoptimization_index);
  for (int i = 0; i < operands->length(); i++) {
    LOperand* pointer = operands->at(i);
    if (pointer->IsStackSlot()) {
      safepoint.DefinePointerSlot(pointer->index());
    } else if (pointer->IsRegister()) {
      safepoint.DefinePointerRegister(ToRegister(pointer));
    }
  }
  // Register rsi always contains a pointer to the context.
  safepoint.DefinePointerRegister(rsi);
}


void LCodeGen::RecordPosition(int position) {
  if (!FLAG_debug_info || position == RelocInfo::kNoPosition) return;
  masm()->positions_recorder()->RecordPosition(position);
}


void LCodeGen::DoLabel(LLabel* label) {
  if (label->is_loop_header()) {
    Comment(";;; B%d - LOOP entry", label->block_id());
  } else {
    Comment(";;; B%d", label->block_id());
  }
  __ bind(label->label());
  current_block_ = label->block_id();
  LCodeGen::DoGap(label);
}


void LCodeGen::DoParallelMove(LParallelMove* move) {
  Abort("Unimplemented: %s", "DoParallelMove");
}


void LCodeGen::DoGap(LGap* gap) {
  for (int i = LGap::FIRST_INNER_POSITION;
       i <= LGap::LAST_INNER_POSITION;
       i++) {
    LGap::InnerPosition inner_pos = static_cast<LGap::InnerPosition>(i);
    LParallelMove* move = gap->GetParallelMove(inner_pos);
    if (move != NULL) DoParallelMove(move);
  }

  LInstruction* next = GetNextInstruction();
  if (next != NULL && next->IsLazyBailout()) {
    int pc = masm()->pc_offset();
    safepoints_.SetPcAfterGap(pc);
  }
}


void LCodeGen::DoParameter(LParameter* instr) {
  // Nothing to do.
}


void LCodeGen::DoCallStub(LCallStub* instr) {
  Abort("Unimplemented: %s", "DoCallStub");
}


void LCodeGen::DoUnknownOSRValue(LUnknownOSRValue* instr) {
  // Nothing to do.
}


void LCodeGen::DoModI(LModI* instr) {
  Abort("Unimplemented: %s", "DoModI");
}


void LCodeGen::DoDivI(LDivI* instr) {
  Abort("Unimplemented: %s", "DoDivI");}


void LCodeGen::DoMulI(LMulI* instr) {
  Abort("Unimplemented: %s", "DoMultI");}


void LCodeGen::DoBitI(LBitI* instr) {
  Abort("Unimplemented: %s", "DoBitI");}


void LCodeGen::DoShiftI(LShiftI* instr) {
  Abort("Unimplemented: %s", "DoShiftI");
}


void LCodeGen::DoSubI(LSubI* instr) {
  Abort("Unimplemented: %s", "DoSubI");
}


void LCodeGen::DoConstantI(LConstantI* instr) {
  Abort("Unimplemented: %s", "DoConstantI");
}


void LCodeGen::DoConstantD(LConstantD* instr) {
  Abort("Unimplemented: %s", "DoConstantI");
}


void LCodeGen::DoConstantT(LConstantT* instr) {
    ASSERT(instr->result()->IsRegister());
  __ Move(ToRegister(instr->result()), instr->value());
}


void LCodeGen::DoJSArrayLength(LJSArrayLength* instr) {
  Abort("Unimplemented: %s", "DoJSArrayLength");
}


void LCodeGen::DoFixedArrayLength(LFixedArrayLength* instr) {
  Abort("Unimplemented: %s", "DoFixedArrayLength");
}


void LCodeGen::DoValueOf(LValueOf* instr) {
  Abort("Unimplemented: %s", "DoValueOf");
}


void LCodeGen::DoBitNotI(LBitNotI* instr) {
  Abort("Unimplemented: %s", "DoBitNotI");
}


void LCodeGen::DoThrow(LThrow* instr) {
  Abort("Unimplemented: %s", "DoThrow");
}


void LCodeGen::DoAddI(LAddI* instr) {
  Abort("Unimplemented: %s", "DoAddI");
}


void LCodeGen::DoArithmeticD(LArithmeticD* instr) {
  Abort("Unimplemented: %s", "DoArithmeticD");
}


void LCodeGen::DoArithmeticT(LArithmeticT* instr) {
  Abort("Unimplemented: %s", "DoArithmeticT");
}


int LCodeGen::GetNextEmittedBlock(int block) {
  for (int i = block + 1; i < graph()->blocks()->length(); ++i) {
    LLabel* label = chunk_->GetLabel(i);
    if (!label->HasReplacement()) return i;
  }
  return -1;
}


void LCodeGen::EmitBranch(int left_block, int right_block, Condition cc) {
  Abort("Unimplemented: %s", "EmitBranch");
}


void LCodeGen::DoBranch(LBranch* instr) {
  Abort("Unimplemented: %s", "DoBranch");
}


void LCodeGen::EmitGoto(int block, LDeferredCode* deferred_stack_check) {
  Abort("Unimplemented: %s", "EmitGoto");
}


void LCodeGen::DoDeferredStackCheck(LGoto* instr) {
  Abort("Unimplemented: %s", "DoDeferredStackCheck");
}


void LCodeGen::DoGoto(LGoto* instr) {
  class DeferredStackCheck: public LDeferredCode {
   public:
    DeferredStackCheck(LCodeGen* codegen, LGoto* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() { codegen()->DoDeferredStackCheck(instr_); }
   private:
    LGoto* instr_;
  };

  DeferredStackCheck* deferred = NULL;
  if (instr->include_stack_check()) {
    deferred = new DeferredStackCheck(this, instr);
  }
  EmitGoto(instr->block_id(), deferred);
}


Condition LCodeGen::TokenToCondition(Token::Value op, bool is_unsigned) {
  Condition cond = no_condition;
  switch (op) {
    case Token::EQ:
    case Token::EQ_STRICT:
      cond = equal;
      break;
    case Token::LT:
      cond = is_unsigned ? below : less;
      break;
    case Token::GT:
      cond = is_unsigned ? above : greater;
      break;
    case Token::LTE:
      cond = is_unsigned ? below_equal : less_equal;
      break;
    case Token::GTE:
      cond = is_unsigned ? above_equal : greater_equal;
      break;
    case Token::IN:
    case Token::INSTANCEOF:
    default:
      UNREACHABLE();
  }
  return cond;
}


void LCodeGen::EmitCmpI(LOperand* left, LOperand* right) {
  Abort("Unimplemented: %s", "EmitCmpI");
}


void LCodeGen::DoCmpID(LCmpID* instr) {
  Abort("Unimplemented: %s", "DoCmpID");
}


void LCodeGen::DoCmpIDAndBranch(LCmpIDAndBranch* instr) {
  Abort("Unimplemented: %s", "DoCmpIDAndBranch");
}


void LCodeGen::DoCmpJSObjectEq(LCmpJSObjectEq* instr) {
  Abort("Unimplemented: %s", "DoCmpJSObjectEq");
}


void LCodeGen::DoCmpJSObjectEqAndBranch(LCmpJSObjectEqAndBranch* instr) {
  Abort("Unimplemented: %s", "DoCmpJSObjectAndBranch");
}


void LCodeGen::DoIsNull(LIsNull* instr) {
  Abort("Unimplemented: %s", "DoIsNull");
}


void LCodeGen::DoIsNullAndBranch(LIsNullAndBranch* instr) {
  Abort("Unimplemented: %s", "DoIsNullAndBranch");
}


Condition LCodeGen::EmitIsObject(Register input,
                                 Register temp1,
                                 Register temp2,
                                 Label* is_not_object,
                                 Label* is_object) {
  Abort("Unimplemented: %s", "EmitIsObject");
  return below_equal;
}


void LCodeGen::DoIsObject(LIsObject* instr) {
  Abort("Unimplemented: %s", "DoIsObject");
}


void LCodeGen::DoIsObjectAndBranch(LIsObjectAndBranch* instr) {
  Abort("Unimplemented: %s", "DoIsObjectAndBranch");
}


void LCodeGen::DoIsSmi(LIsSmi* instr) {
  Abort("Unimplemented: %s", "DoIsSmi");
}


void LCodeGen::DoIsSmiAndBranch(LIsSmiAndBranch* instr) {
  Abort("Unimplemented: %s", "DoIsSmiAndBranch");
}


InstanceType LHasInstanceType::TestType() {
  InstanceType from = hydrogen()->from();
  InstanceType to = hydrogen()->to();
  if (from == FIRST_TYPE) return to;
  ASSERT(from == to || to == LAST_TYPE);
  return from;
}



Condition LHasInstanceType::BranchCondition() {
  InstanceType from = hydrogen()->from();
  InstanceType to = hydrogen()->to();
  if (from == to) return equal;
  if (to == LAST_TYPE) return above_equal;
  if (from == FIRST_TYPE) return below_equal;
  UNREACHABLE();
  return equal;
}


void LCodeGen::DoHasInstanceType(LHasInstanceType* instr) {
  Abort("Unimplemented: %s", "DoHasInstanceType");
}


void LCodeGen::DoHasInstanceTypeAndBranch(LHasInstanceTypeAndBranch* instr) {
  Abort("Unimplemented: %s", "DoHasInstanceTypeAndBranch");
}


void LCodeGen::DoHasCachedArrayIndex(LHasCachedArrayIndex* instr) {
  Abort("Unimplemented: %s", "DoHasCachedArrayIndex");
}


void LCodeGen::DoHasCachedArrayIndexAndBranch(
    LHasCachedArrayIndexAndBranch* instr) {
  Abort("Unimplemented: %s", "DoHasCachedArrayIndexAndBranch");
}


// Branches to a label or falls through with the answer in the z flag.  Trashes
// the temp registers, but not the input.  Only input and temp2 may alias.
void LCodeGen::EmitClassOfTest(Label* is_true,
                               Label* is_false,
                               Handle<String>class_name,
                               Register input,
                               Register temp,
                               Register temp2) {
  Abort("Unimplemented: %s", "EmitClassOfTest");
}


void LCodeGen::DoClassOfTest(LClassOfTest* instr) {
  Abort("Unimplemented: %s", "DoClassOfTest");
}


void LCodeGen::DoClassOfTestAndBranch(LClassOfTestAndBranch* instr) {
  Abort("Unimplemented: %s", "DoClassOfTestAndBranch");
}


void LCodeGen::DoCmpMapAndBranch(LCmpMapAndBranch* instr) {
  Abort("Unimplemented: %s", "DoCmpMapAndBranch");
}


void LCodeGen::DoInstanceOf(LInstanceOf* instr) {
  Abort("Unimplemented: %s", "DoInstanceOf");
}


void LCodeGen::DoInstanceOfAndBranch(LInstanceOfAndBranch* instr) {
  Abort("Unimplemented: %s", "DoInstanceOfAndBranch");
}


void LCodeGen::DoInstanceOfKnownGlobal(LInstanceOfKnownGlobal* instr) {
  Abort("Unimplemented: %s", "DoInstanceOfKnowGLobal");
}


void LCodeGen::DoDeferredLInstanceOfKnownGlobal(LInstanceOfKnownGlobal* instr,
                                                Label* map_check) {
  Abort("Unimplemented: %s", "DoDeferredLInstanceOfKnownGlobakl");
}


void LCodeGen::DoCmpT(LCmpT* instr) {
  Abort("Unimplemented: %s", "DoCmpT");
}


void LCodeGen::DoCmpTAndBranch(LCmpTAndBranch* instr) {
  Abort("Unimplemented: %s", "DoCmpTAndBranch");
}


void LCodeGen::DoReturn(LReturn* instr) {
  if (FLAG_trace) {
    // Preserve the return value on the stack and rely on the runtime
    // call to return the value in the same register.
    __ push(rax);
    __ CallRuntime(Runtime::kTraceExit, 1);
  }
  __ movq(rsp, rbp);
  __ pop(rbp);
  __ ret((ParameterCount() + 1) * kPointerSize);

}


void LCodeGen::DoLoadGlobal(LLoadGlobal* instr) {
  Abort("Unimplemented: %s", "DoLoadGlobal");
}


void LCodeGen::DoStoreGlobal(LStoreGlobal* instr) {
  Abort("Unimplemented: %s", "DoStoreGlobal");
}


void LCodeGen::DoLoadNamedField(LLoadNamedField* instr) {
  Abort("Unimplemented: %s", "DoLoadNamedField");
}


void LCodeGen::DoLoadNamedGeneric(LLoadNamedGeneric* instr) {
  Abort("Unimplemented: %s", "DoLoadNamedGeneric");
}


void LCodeGen::DoLoadFunctionPrototype(LLoadFunctionPrototype* instr) {
  Abort("Unimplemented: %s", "DoLoadFunctionPrototype");
}


void LCodeGen::DoLoadElements(LLoadElements* instr) {
  Abort("Unimplemented: %s", "DoLoadElements");
}


void LCodeGen::DoAccessArgumentsAt(LAccessArgumentsAt* instr) {
  Abort("Unimplemented: %s", "DoAccessArgumentsAt");
}


void LCodeGen::DoLoadKeyedFastElement(LLoadKeyedFastElement* instr) {
  Abort("Unimplemented: %s", "DoLoadKeyedFastElement");
}


void LCodeGen::DoLoadKeyedGeneric(LLoadKeyedGeneric* instr) {
  Abort("Unimplemented: %s", "DoLoadKeyedGeneric");
}


void LCodeGen::DoArgumentsElements(LArgumentsElements* instr) {
  Abort("Unimplemented: %s", "DoArgumentsElements");
}


void LCodeGen::DoArgumentsLength(LArgumentsLength* instr) {
  Abort("Unimplemented: %s", "DoArgumentsLength");
}


void LCodeGen::DoApplyArguments(LApplyArguments* instr) {
  Abort("Unimplemented: %s", "DoApplyArguments");
}


void LCodeGen::DoPushArgument(LPushArgument* instr) {
  Abort("Unimplemented: %s", "DoPushArgument");
}


void LCodeGen::DoGlobalObject(LGlobalObject* instr) {
  Abort("Unimplemented: %s", "DoGlobalObject");
}


void LCodeGen::DoGlobalReceiver(LGlobalReceiver* instr) {
  Abort("Unimplemented: %s", "DoGlobalReceiver");
}


void LCodeGen::CallKnownFunction(Handle<JSFunction> function,
                                 int arity,
                                 LInstruction* instr) {
  Abort("Unimplemented: %s", "CallKnownFunction");
}


void LCodeGen::DoCallConstantFunction(LCallConstantFunction* instr) {
  Abort("Unimplemented: %s", "DoCallConstantFunction");
}


void LCodeGen::DoDeferredMathAbsTaggedHeapNumber(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoDeferredMathAbsTaggedHeapNumber");
}


void LCodeGen::DoMathAbs(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoMathAbs");
}


void LCodeGen::DoMathFloor(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoMathFloor");
}


void LCodeGen::DoMathRound(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoMathRound");
}


void LCodeGen::DoMathSqrt(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoMathSqrt");
}


void LCodeGen::DoMathPowHalf(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoMathPowHalf");
}


void LCodeGen::DoPower(LPower* instr) {
  Abort("Unimplemented: %s", "DoPower");
}


void LCodeGen::DoMathLog(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoMathLog");
}


void LCodeGen::DoMathCos(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoMathCos");
}


void LCodeGen::DoMathSin(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoMathSin");
}


void LCodeGen::DoUnaryMathOperation(LUnaryMathOperation* instr) {
  Abort("Unimplemented: %s", "DoUnaryMathOperation");
}


void LCodeGen::DoCallKeyed(LCallKeyed* instr) {
  Abort("Unimplemented: %s", "DoCallKeyed");
}


void LCodeGen::DoCallNamed(LCallNamed* instr) {
  Abort("Unimplemented: %s", "DoCallNamed");
}


void LCodeGen::DoCallFunction(LCallFunction* instr) {
  Abort("Unimplemented: %s", "DoCallFunction");
}


void LCodeGen::DoCallGlobal(LCallGlobal* instr) {
  Abort("Unimplemented: %s", "DoCallGlobal");
}


void LCodeGen::DoCallKnownGlobal(LCallKnownGlobal* instr) {
  Abort("Unimplemented: %s", "DoCallKnownGlobal");
}


void LCodeGen::DoCallNew(LCallNew* instr) {
  Abort("Unimplemented: %s", "DoCallNew");
}


void LCodeGen::DoCallRuntime(LCallRuntime* instr) {
  Abort("Unimplemented: %s", "DoCallRuntime");
}


void LCodeGen::DoStoreNamedField(LStoreNamedField* instr) {
  Abort("Unimplemented: %s", "DoStoreNamedField");
}


void LCodeGen::DoStoreNamedGeneric(LStoreNamedGeneric* instr) {
  Abort("Unimplemented: %s", "DoStoreNamedGeneric");
}


void LCodeGen::DoBoundsCheck(LBoundsCheck* instr) {
  Abort("Unimplemented: %s", "DoBoundsCheck");
}


void LCodeGen::DoStoreKeyedFastElement(LStoreKeyedFastElement* instr) {
  Abort("Unimplemented: %s", "DoStoreKeyedFastElement");
}


void LCodeGen::DoStoreKeyedGeneric(LStoreKeyedGeneric* instr) {
  Abort("Unimplemented: %s", "DoStoreKeyedGeneric");
}


void LCodeGen::DoInteger32ToDouble(LInteger32ToDouble* instr) {
  Abort("Unimplemented: %s", "DoInteger32ToDouble");
}


void LCodeGen::DoNumberTagI(LNumberTagI* instr) {
  Abort("Unimplemented: %s", "DoNumberTagI");
}


void LCodeGen::DoDeferredNumberTagI(LNumberTagI* instr) {
  Abort("Unimplemented: %s", "DoDeferredNumberTagI");
}


void LCodeGen::DoNumberTagD(LNumberTagD* instr) {
  Abort("Unimplemented: %s", "DoNumberTagD");
}


void LCodeGen::DoDeferredNumberTagD(LNumberTagD* instr) {
  Abort("Unimplemented: %s", "DoDeferredNumberTagD");
}


void LCodeGen::DoSmiTag(LSmiTag* instr) {
  Abort("Unimplemented: %s", "DoSmiTag");
}


void LCodeGen::DoSmiUntag(LSmiUntag* instr) {
  Abort("Unimplemented: %s", "DoSmiUntag");
}


void LCodeGen::EmitNumberUntagD(Register input_reg,
                                XMMRegister result_reg,
                                LEnvironment* env) {
  Abort("Unimplemented: %s", "EmitNumberUntagD");
}


void LCodeGen::DoDeferredTaggedToI(LTaggedToI* instr) {
  Abort("Unimplemented: %s", "DoDeferredTaggedToI");
}


void LCodeGen::DoTaggedToI(LTaggedToI* instr) {
  Abort("Unimplemented: %s", "DoTaggedToI");
}


void LCodeGen::DoNumberUntagD(LNumberUntagD* instr) {
  Abort("Unimplemented: %s", "DoNumberUntagD");
}


void LCodeGen::DoDoubleToI(LDoubleToI* instr) {
  Abort("Unimplemented: %s", "DoDoubleToI");
}


void LCodeGen::DoCheckSmi(LCheckSmi* instr) {
  Abort("Unimplemented: %s", "DoCheckSmi");
}


void LCodeGen::DoCheckInstanceType(LCheckInstanceType* instr) {
  Abort("Unimplemented: %s", "DoCheckInstanceType");
}


void LCodeGen::DoCheckFunction(LCheckFunction* instr) {
  Abort("Unimplemented: %s", "DoCheckFunction");
}


void LCodeGen::DoCheckMap(LCheckMap* instr) {
  Abort("Unimplemented: %s", "DoCheckMap");
}


void LCodeGen::LoadPrototype(Register result, Handle<JSObject> prototype) {
  Abort("Unimplemented: %s", "LoadPrototype");
}


void LCodeGen::DoCheckPrototypeMaps(LCheckPrototypeMaps* instr) {
  Abort("Unimplemented: %s", "DoCheckPrototypeMaps");
}


void LCodeGen::DoArrayLiteral(LArrayLiteral* instr) {
  Abort("Unimplemented: %s", "DoArrayLiteral");
}


void LCodeGen::DoObjectLiteral(LObjectLiteral* instr) {
  Abort("Unimplemented: %s", "DoObjectLiteral");
}


void LCodeGen::DoRegExpLiteral(LRegExpLiteral* instr) {
  Abort("Unimplemented: %s", "DoRegExpLiteral");
}


void LCodeGen::DoFunctionLiteral(LFunctionLiteral* instr) {
  Abort("Unimplemented: %s", "DoFunctionLiteral");
}


void LCodeGen::DoTypeof(LTypeof* instr) {
  Abort("Unimplemented: %s", "DoTypeof");
}


void LCodeGen::DoTypeofIs(LTypeofIs* instr) {
  Abort("Unimplemented: %s", "DoTypeofIs");
}


void LCodeGen::DoTypeofIsAndBranch(LTypeofIsAndBranch* instr) {
  Abort("Unimplemented: %s", "DoTypeofIsAndBranch");
}


Condition LCodeGen::EmitTypeofIs(Label* true_label,
                                 Label* false_label,
                                 Register input,
                                 Handle<String> type_name) {
  Abort("Unimplemented: %s", "EmitTypeofIs");
  return no_condition;
}


void LCodeGen::DoLazyBailout(LLazyBailout* instr) {
  // No code for lazy bailout instruction. Used to capture environment after a
  // call for populating the safepoint data with deoptimization data.
}


void LCodeGen::DoDeoptimize(LDeoptimize* instr) {
  DeoptimizeIf(no_condition, instr->environment());
}


void LCodeGen::DoDeleteProperty(LDeleteProperty* instr) {
  Abort("Unimplemented: %s", "DoDeleteProperty");
}


void LCodeGen::DoStackCheck(LStackCheck* instr) {
  Abort("Unimplemented: %s", "DoStackCheck");
}


void LCodeGen::DoOsrEntry(LOsrEntry* instr) {
  Abort("Unimplemented: %s", "DoOsrEntry");
}

#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_X64
