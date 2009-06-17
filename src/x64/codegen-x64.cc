// Copyright 2009 the V8 project authors. All rights reserved.
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
#include "macro-assembler.h"
#include "register-allocator-inl.h"
#include "codegen.h"
// TEST
#include "compiler.h"

namespace v8 {
namespace internal {

// -------------------------------------------------------------------------
// Platform-specific DeferredCode functions.

void DeferredCode::SaveRegisters() { UNIMPLEMENTED(); }

void DeferredCode::RestoreRegisters() { UNIMPLEMENTED(); }

// -------------------------------------------------------------------------
// CodeGenState implementation.

CodeGenState::CodeGenState(CodeGenerator* owner)
    : owner_(owner),
      typeof_state_(NOT_INSIDE_TYPEOF),
      destination_(NULL),
      previous_(NULL) {
  owner_->set_state(this);
}


CodeGenState::CodeGenState(CodeGenerator* owner,
                           TypeofState typeof_state,
                           ControlDestination* destination)
    : owner_(owner),
      typeof_state_(typeof_state),
      destination_(destination),
      previous_(owner->state()) {
  owner_->set_state(this);
}


CodeGenState::~CodeGenState() {
  ASSERT(owner_->state() == this);
  owner_->set_state(previous_);
}


// -----------------------------------------------------------------------------
// CodeGenerator implementation.

CodeGenerator::CodeGenerator(int buffer_size,
                             Handle<Script> script,
                             bool is_eval)
    : is_eval_(is_eval),
      script_(script),
      deferred_(8),
      masm_(new MacroAssembler(NULL, buffer_size)),
      scope_(NULL),
      frame_(NULL),
      allocator_(NULL),
      state_(NULL),
      loop_nesting_(0),
      function_return_is_shadowed_(false),
      in_spilled_code_(false) {
}

#define __ ACCESS_MASM(masm_)


void CodeGenerator::DeclareGlobals(Handle<FixedArray> a) {
  UNIMPLEMENTED();
}

void CodeGenerator::TestCodeGenerator() {
  // Compile a function from a string, and run it.
  Handle<JSFunction> test_function = Compiler::Compile(
      Factory::NewStringFromAscii(CStrVector("42")),
      Factory::NewStringFromAscii(CStrVector("CodeGeneratorTestScript")),
      0,
      0,
      NULL,
      NULL);

  Code* code_object = test_function->code();  // Local for debugging ease.
  USE(code_object);

  // Create a dummy function and context.
  Handle<JSFunction> bridge =
      Factory::NewFunction(Factory::empty_symbol(), Factory::undefined_value());
  Handle<Context> context =
    Factory::NewFunctionContext(Context::MIN_CONTEXT_SLOTS, bridge);

  test_function = Factory::NewFunctionFromBoilerplate(
      test_function,
      context);

  bool pending_exceptions;
  Handle<Object> result =
      Execution::Call(test_function,
                      Handle<Object>::cast(test_function),
                      0,
                      NULL,
                      &pending_exceptions);
  CHECK(result->IsSmi());
  CHECK_EQ(42, Smi::cast(*result)->value());
}


void CodeGenerator::GenCode(FunctionLiteral* function) {
  // Record the position for debugging purposes.
  CodeForFunctionPosition(function);
  // ZoneList<Statement*>* body = fun->body();

  // Initialize state.
  ASSERT(scope_ == NULL);
  scope_ = function->scope();
  ASSERT(allocator_ == NULL);
  RegisterAllocator register_allocator(this);
  allocator_ = &register_allocator;
  ASSERT(frame_ == NULL);
  frame_ = new VirtualFrame();
  set_in_spilled_code(false);

  // Adjust for function-level loop nesting.
  loop_nesting_ += function->loop_nesting();

  JumpTarget::set_compiling_deferred_code(false);

#ifdef DEBUG
  if (strlen(FLAG_stop_at) > 0 &&
      //    fun->name()->IsEqualTo(CStrVector(FLAG_stop_at))) {
      false) {
    frame_->SpillAll();
    __ int3();
  }
#endif

  // New scope to get automatic timing calculation.
  {  // NOLINT
    HistogramTimerScope codegen_timer(&Counters::code_generation);
    CodeGenState state(this);

    // Entry:
    // Stack: receiver, arguments, return address.
    // ebp: caller's frame pointer
    // esp: stack pointer
    // edi: called JS function
    // esi: callee's context
    allocator_->Initialize();
    frame_->Enter();

    Result return_register = allocator_->Allocate(rax);

    __ movq(return_register.reg(), Immediate(0x54));  // Smi 42

    GenerateReturnSequence(&return_register);
  }
}

void CodeGenerator::GenerateReturnSequence(Result* return_value) {
  // The return value is a live (but not currently reference counted)
  // reference to rax.  This is safe because the current frame does not
  // contain a reference to rax (it is prepared for the return by spilling
  // all registers).
  if (FLAG_trace) {
    frame_->Push(return_value);
    // *return_value = frame_->CallRuntime(Runtime::kTraceExit, 1);
  }
  return_value->ToRegister(rax);

  // Add a label for checking the size of the code used for returning.
  Label check_exit_codesize;
  masm_->bind(&check_exit_codesize);

  // Leave the frame and return popping the arguments and the
  // receiver.
  frame_->Exit();
  masm_->ret((scope_->num_parameters() + 1) * kPointerSize);
  DeleteFrame();

  // Check that the size of the code used for returning matches what is
  // expected by the debugger.
  // ASSERT_EQ(Debug::kIa32JSReturnSequenceLength,
  //          masm_->SizeOfCodeGeneratedSince(&check_exit_codesize));
}


void CodeGenerator::GenerateFastCaseSwitchJumpTable(SwitchStatement* a,
                                                    int b,
                                                    int c,
                                                    Label* d,
                                                    Vector<Label*> e,
                                                    Vector<Label> f) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitStatements(ZoneList<Statement*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitBlock(Block* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitDeclaration(Declaration* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitExpressionStatement(ExpressionStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitEmptyStatement(EmptyStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitIfStatement(IfStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitContinueStatement(ContinueStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitBreakStatement(BreakStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitReturnStatement(ReturnStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitWithEnterStatement(WithEnterStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitWithExitStatement(WithExitStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitSwitchStatement(SwitchStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitLoopStatement(LoopStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitForInStatement(ForInStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitTryCatch(TryCatch* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitTryFinally(TryFinally* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitDebuggerStatement(DebuggerStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitFunctionLiteral(FunctionLiteral* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitFunctionBoilerplateLiteral(
    FunctionBoilerplateLiteral* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitConditional(Conditional* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitSlot(Slot* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitVariableProxy(VariableProxy* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitLiteral(Literal* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitRegExpLiteral(RegExpLiteral* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitObjectLiteral(ObjectLiteral* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitArrayLiteral(ArrayLiteral* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitCatchExtensionObject(CatchExtensionObject* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitAssignment(Assignment* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitThrow(Throw* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitProperty(Property* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitCall(Call* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitCallEval(CallEval* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitCallNew(CallNew* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitCallRuntime(CallRuntime* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitUnaryOperation(UnaryOperation* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitCountOperation(CountOperation* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitBinaryOperation(BinaryOperation* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitCompareOperation(CompareOperation* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitThisFunction(ThisFunction* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateArgumentsAccess(ZoneList<Expression*>* args) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateArgumentsLength(ZoneList<Expression*>* args) {
  UNIMPLEMENTED();}

void CodeGenerator::GenerateFastCharCodeAt(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateIsArray(ZoneList<Expression*>* args) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateIsNonNegativeSmi(ZoneList<Expression*>* args) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateIsSmi(ZoneList<Expression*>* args) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateLog(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateObjectEquals(ZoneList<Expression*>* args) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateRandomPositiveSmi(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateFastMathOp(MathOp op, ZoneList<Expression*>* args) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateSetValueOf(ZoneList<Expression*>* args) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateValueOf(ZoneList<Expression*>* args) {
  UNIMPLEMENTED();
}

#undef __
// End of CodeGenerator implementation.

// -----------------------------------------------------------------------------
// Implementation of stubs.

//  Stub classes have public member named masm, not masm_.
#define __ ACCESS_MASM(masm)

class ToBooleanStub: public CodeStub {
 public:
  ToBooleanStub() { }

  void Generate(MacroAssembler* masm);

 private:
  Major MajorKey() { return ToBoolean; }
  int MinorKey() { return 0; }
};


void ToBooleanStub::Generate(MacroAssembler* masm) {
  Label false_result, true_result, not_string;
  __ movq(rax, Operand(rsp, 1 * kPointerSize));

  // 'null' => false.
  __ movq(kScratchRegister, Factory::null_value(), RelocInfo::EMBEDDED_OBJECT);
  __ cmpq(rax, kScratchRegister);
  __ j(equal, &false_result);

  // Get the map and type of the heap object.
  __ movq(rdx, FieldOperand(rax, HeapObject::kMapOffset));
  __ movzxbq(rcx, FieldOperand(rdx, Map::kInstanceTypeOffset));

  // Undetectable => false.
  __ movzxbq(rbx, FieldOperand(rdx, Map::kBitFieldOffset));
  __ and_(rbx, Immediate(1 << Map::kIsUndetectable));
  __ j(not_zero, &false_result);

  // JavaScript object => true.
  __ cmpq(rcx, Immediate(FIRST_JS_OBJECT_TYPE));
  __ j(above_equal, &true_result);

  // String value => false iff empty.
  __ cmpq(rcx, Immediate(FIRST_NONSTRING_TYPE));
  __ j(above_equal, &not_string);
  __ and_(rcx, Immediate(kStringSizeMask));
  __ cmpq(rcx, Immediate(kShortStringTag));
  __ j(not_equal, &true_result);  // Empty string is always short.
  __ movq(rdx, FieldOperand(rax, String::kLengthOffset));
  __ shr(rdx, Immediate(String::kShortLengthShift));
  __ j(zero, &false_result);
  __ jmp(&true_result);

  __ bind(&not_string);
  // HeapNumber => false iff +0, -0, or NaN.
  __ movq(kScratchRegister,
          Factory::heap_number_map(),
          RelocInfo::EMBEDDED_OBJECT);
  __ cmpq(rdx, kScratchRegister);
  __ j(not_equal, &true_result);
  // TODO(x64): Don't use fp stack, use MMX registers?
  __ fldz();  // Load zero onto fp stack
  // Load heap-number double value onto fp stack
  __ fld_d(FieldOperand(rax, HeapNumber::kValueOffset));
  __ fucompp();  // Compare and pop both values.
  __ movq(kScratchRegister, rax);
  __ fnstsw_ax();  // Store fp status word in ax, no checking for exceptions.
  __ testb(rax, Immediate(0x08));  // Test FP condition flag C3.
  __ movq(rax, kScratchRegister);
  __ j(zero, &false_result);
  // Fall through to |true_result|.

  // Return 1/0 for true/false in rax.
  __ bind(&true_result);
  __ movq(rax, Immediate(1));
  __ ret(1 * kPointerSize);
  __ bind(&false_result);
  __ xor_(rax, rax);
  __ ret(1 * kPointerSize);
}


// Flag that indicates whether or not the code that handles smi arguments
// should be placed in the stub, inlined, or omitted entirely.
enum GenericBinaryFlags {
  SMI_CODE_IN_STUB,
  SMI_CODE_INLINED
};


class GenericBinaryOpStub: public CodeStub {
 public:
  GenericBinaryOpStub(Token::Value op,
                      OverwriteMode mode,
                      GenericBinaryFlags flags)
      : op_(op), mode_(mode), flags_(flags) {
    ASSERT(OpBits::is_valid(Token::NUM_TOKENS));
  }

  void GenerateSmiCode(MacroAssembler* masm, Label* slow);

 private:
  Token::Value op_;
  OverwriteMode mode_;
  GenericBinaryFlags flags_;

  const char* GetName();

#ifdef DEBUG
  void Print() {
    PrintF("GenericBinaryOpStub (op %s), (mode %d, flags %d)\n",
           Token::String(op_),
           static_cast<int>(mode_),
           static_cast<int>(flags_));
  }
#endif

  // Minor key encoding in 16 bits FOOOOOOOOOOOOOMM.
  class ModeBits: public BitField<OverwriteMode, 0, 2> {};
  class OpBits: public BitField<Token::Value, 2, 13> {};
  class FlagBits: public BitField<GenericBinaryFlags, 15, 1> {};

  Major MajorKey() { return GenericBinaryOp; }
  int MinorKey() {
    // Encode the parameters in a unique 16 bit value.
    return OpBits::encode(op_)
           | ModeBits::encode(mode_)
           | FlagBits::encode(flags_);
  }
  void Generate(MacroAssembler* masm);
};


const char* GenericBinaryOpStub::GetName() {
  switch (op_) {
    case Token::ADD: return "GenericBinaryOpStub_ADD";
    case Token::SUB: return "GenericBinaryOpStub_SUB";
    case Token::MUL: return "GenericBinaryOpStub_MUL";
    case Token::DIV: return "GenericBinaryOpStub_DIV";
    case Token::BIT_OR: return "GenericBinaryOpStub_BIT_OR";
    case Token::BIT_AND: return "GenericBinaryOpStub_BIT_AND";
    case Token::BIT_XOR: return "GenericBinaryOpStub_BIT_XOR";
    case Token::SAR: return "GenericBinaryOpStub_SAR";
    case Token::SHL: return "GenericBinaryOpStub_SHL";
    case Token::SHR: return "GenericBinaryOpStub_SHR";
    default:         return "GenericBinaryOpStub";
  }
}


void GenericBinaryOpStub::GenerateSmiCode(MacroAssembler* masm, Label* slow) {
  // Perform fast-case smi code for the operation (rax <op> rbx) and
  // leave result in register rax.

  // Prepare the smi check of both operands by or'ing them together
  // before checking against the smi mask.
  __ movq(rcx, rbx);
  __ or_(rcx, rax);

  switch (op_) {
    case Token::ADD:
      __ addl(rax, rbx);  // add optimistically
      __ j(overflow, slow);
      __ movsxlq(rax, rax);  // Sign extend eax into rax.
      break;

    case Token::SUB:
      __ subl(rax, rbx);  // subtract optimistically
      __ j(overflow, slow);
      __ movsxlq(rax, rax);  // Sign extend eax into rax.
      break;

    case Token::DIV:
    case Token::MOD:
      // Sign extend rax into rdx:rax
      // (also sign extends eax into edx if eax is Smi).
      __ cqo();
      // Check for 0 divisor.
      __ testq(rbx, rbx);
      __ j(zero, slow);
      break;

    default:
      // Fall-through to smi check.
      break;
  }

  // Perform the actual smi check.
  ASSERT(kSmiTag == 0);  // adjust zero check if not the case
  __ testl(rcx, Immediate(kSmiTagMask));
  __ j(not_zero, slow);

  switch (op_) {
    case Token::ADD:
    case Token::SUB:
      // Do nothing here.
      break;

    case Token::MUL:
      // If the smi tag is 0 we can just leave the tag on one operand.
      ASSERT(kSmiTag == 0);  // adjust code below if not the case
      // Remove tag from one of the operands (but keep sign).
      __ sar(rax, Immediate(kSmiTagSize));
      // Do multiplication.
      __ imull(rax, rbx);  // multiplication of smis; result in eax
      // Go slow on overflows.
      __ j(overflow, slow);
      // Check for negative zero result.
      __ movsxlq(rax, rax);  // Sign extend eax into rax.
      __ NegativeZeroTest(rax, rcx, slow);  // use rcx = x | y
      break;

    case Token::DIV:
      // Divide rdx:rax by rbx (where rdx:rax is equivalent to the smi in eax).
      __ idiv(rbx);
      // Check that the remainder is zero.
      __ testq(rdx, rdx);
      __ j(not_zero, slow);
      // Check for the corner case of dividing the most negative smi
      // by -1. We cannot use the overflow flag, since it is not set
      // by idiv instruction.
      ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
      // TODO(X64):TODO(Smi): Smi implementation dependent constant.
      // Value is Smi::fromInt(-(1<<31)) / Smi::fromInt(-1)
      __ cmpq(rax, Immediate(0x40000000));
      __ j(equal, slow);
      // Check for negative zero result.
      __ NegativeZeroTest(rax, rcx, slow);  // use ecx = x | y
      // Tag the result and store it in register rax.
      ASSERT(kSmiTagSize == kTimes2);  // adjust code if not the case
      __ lea(rax, Operand(rax, rax, kTimes1, kSmiTag));
      break;

    case Token::MOD:
      // Divide rdx:rax by rbx.
      __ idiv(rbx);
      // Check for negative zero result.
      __ NegativeZeroTest(rdx, rcx, slow);  // use ecx = x | y
      // Move remainder to register rax.
      __ movq(rax, rdx);
      break;

    case Token::BIT_OR:
      __ or_(rax, rbx);
      break;

    case Token::BIT_AND:
      __ and_(rax, rbx);
      break;

    case Token::BIT_XOR:
      __ xor_(rax, rbx);
      break;

    case Token::SHL:
    case Token::SHR:
    case Token::SAR:
      // Move the second operand into register ecx.
      __ movq(rcx, rbx);
      // Remove tags from operands (but keep sign).
      __ sar(rax, Immediate(kSmiTagSize));
      __ sar(rcx, Immediate(kSmiTagSize));
      // Perform the operation.
      switch (op_) {
        case Token::SAR:
          __ sar(rax);
          // No checks of result necessary
          break;
        case Token::SHR:
          __ shrl(rax);  // ecx is implicit shift register
          // Check that the *unsigned* result fits in a smi.
          // Neither of the two high-order bits can be set:
          // - 0x80000000: high bit would be lost when smi tagging.
          // - 0x40000000: this number would convert to negative when
          // Smi tagging these two cases can only happen with shifts
          // by 0 or 1 when handed a valid smi.
          __ testq(rax, Immediate(0xc0000000));
          __ j(not_zero, slow);
          break;
        case Token::SHL:
          __ shll(rax);
          // TODO(Smi): Significant change if Smi changes.
          // Check that the *signed* result fits in a smi.
          // It does, if the 30th and 31st bits are equal, since then
          // shifting the SmiTag in at the bottom doesn't change the sign.
          ASSERT(kSmiTagSize == 1);
          __ cmpl(rax, Immediate(0xc0000000));
          __ j(sign, slow);
          __ movsxlq(rax, rax);  // Extend new sign of eax into rax.
          break;
        default:
          UNREACHABLE();
      }
      // Tag the result and store it in register eax.
      ASSERT(kSmiTagSize == kTimes2);  // adjust code if not the case
      __ lea(rax, Operand(rax, rax, kTimes1, kSmiTag));
      break;

    default:
      UNREACHABLE();
      break;
  }
}


void GenericBinaryOpStub::Generate(MacroAssembler* masm) {
}


void UnarySubStub::Generate(MacroAssembler* masm) {
}

class CompareStub: public CodeStub {
 public:
  CompareStub(Condition cc, bool strict) : cc_(cc), strict_(strict) { }

  void Generate(MacroAssembler* masm);

 private:
  Condition cc_;
  bool strict_;

  Major MajorKey() { return Compare; }

  int MinorKey() {
    // Encode the three parameters in a unique 16 bit value.
    ASSERT(static_cast<int>(cc_) < (1 << 15));
    return (static_cast<int>(cc_) << 1) | (strict_ ? 1 : 0);
  }

#ifdef DEBUG
  void Print() {
    PrintF("CompareStub (cc %d), (strict %s)\n",
           static_cast<int>(cc_),
           strict_ ? "true" : "false");
  }
#endif
};


void CompareStub::Generate(MacroAssembler* masm) {
}


void StackCheckStub::Generate(MacroAssembler* masm) {
}


class CallFunctionStub: public CodeStub {
 public:
  CallFunctionStub(int argc, InLoopFlag in_loop)
      : argc_(argc), in_loop_(in_loop) { }

  void Generate(MacroAssembler* masm);

 private:
  int argc_;
  InLoopFlag in_loop_;

#ifdef DEBUG
  void Print() { PrintF("CallFunctionStub (args %d)\n", argc_); }
#endif

  Major MajorKey() { return CallFunction; }
  int MinorKey() { return argc_; }
  InLoopFlag InLoop() { return in_loop_; }
};


void CallFunctionStub::Generate(MacroAssembler* masm) {
}


void InstanceofStub::Generate(MacroAssembler* masm) {
}



void ArgumentsAccessStub::GenerateNewObject(MacroAssembler* masm) {
  // The displacement is used for skipping the return address and the
  // frame pointer on the stack. It is the offset of the last
  // parameter (if any) relative to the frame pointer.
  static const int kDisplacement = 2 * kPointerSize;

  // Check if the calling frame is an arguments adaptor frame.
  Label runtime;
  __ movq(rdx, Operand(rbp, StandardFrameConstants::kCallerFPOffset));
  __ movq(rcx, Operand(rdx, StandardFrameConstants::kContextOffset));
  __ cmpq(rcx, Immediate(ArgumentsAdaptorFrame::SENTINEL));
  __ j(not_equal, &runtime);
  // Value in rcx is Smi encoded.

  // Patch the arguments.length and the parameters pointer.
  __ movq(rcx, Operand(rdx, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ movq(Operand(rsp, 1 * kPointerSize), rcx);
  __ lea(rdx, Operand(rdx, rcx, kTimes4, kDisplacement));
  __ movq(Operand(rsp, 2 * kPointerSize), rdx);

  // Do the runtime call to allocate the arguments object.
  __ bind(&runtime);
  __ TailCallRuntime(ExternalReference(Runtime::kNewArgumentsFast), 3);
}

void ArgumentsAccessStub::GenerateReadElement(MacroAssembler* masm) {
  // The key is in rdx and the parameter count is in rax.

  // The displacement is used for skipping the frame pointer on the
  // stack. It is the offset of the last parameter (if any) relative
  // to the frame pointer.
  static const int kDisplacement = 1 * kPointerSize;

  // Check that the key is a smi.
  Label slow;
  __ testl(rdx, Immediate(kSmiTagMask));
  __ j(not_zero, &slow);

  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor;
  __ movq(rbx, Operand(rbp, StandardFrameConstants::kCallerFPOffset));
  __ movq(rcx, Operand(rbx, StandardFrameConstants::kContextOffset));
  __ cmpq(rcx, Immediate(ArgumentsAdaptorFrame::SENTINEL));
  __ j(equal, &adaptor);

  // Check index against formal parameters count limit passed in
  // through register rax. Use unsigned comparison to get negative
  // check for free.
  __ cmpq(rdx, rax);
  __ j(above_equal, &slow);

  // Read the argument from the stack and return it.
  // Shifting code depends on SmiEncoding being equivalent to left shift:
  // we multiply by four to get pointer alignment.
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  __ lea(rbx, Operand(rbp, rax, kTimes4, 0));
  __ neg(rdx);
  __ movq(rax, Operand(rbx, rdx, kTimes4, kDisplacement));
  __ Ret();

  // Arguments adaptor case: Check index against actual arguments
  // limit found in the arguments adaptor frame. Use unsigned
  // comparison to get negative check for free.
  __ bind(&adaptor);
  __ movq(rcx, Operand(rbx, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ cmpq(rdx, rcx);
  __ j(above_equal, &slow);

  // Read the argument from the stack and return it.
  // Shifting code depends on SmiEncoding being equivalent to left shift:
  // we multiply by four to get pointer alignment.
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  __ lea(rbx, Operand(rbx, rcx, kTimes4, 0));
  __ neg(rdx);
  __ movq(rax, Operand(rbx, rdx, kTimes4, kDisplacement));
  __ Ret();

  // Slow-case: Handle non-smi or out-of-bounds access to arguments
  // by calling the runtime system.
  __ bind(&slow);
  __ pop(rbx);  // Return address.
  __ push(rdx);
  __ push(rbx);
  __ TailCallRuntime(ExternalReference(Runtime::kGetArgumentsProperty), 1);
}


void ArgumentsAccessStub::GenerateReadLength(MacroAssembler* masm) {
  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor;
  __ movq(rdx, Operand(rbp, StandardFrameConstants::kCallerFPOffset));
  __ movq(rcx, Operand(rdx, StandardFrameConstants::kContextOffset));
  __ cmpq(rcx, Immediate(ArgumentsAdaptorFrame::SENTINEL));
  __ j(equal, &adaptor);

  // Nothing to do: The formal number of parameters has already been
  // passed in register rax by calling function. Just return it.
  __ ret(0);

  // Arguments adaptor case: Read the arguments length from the
  // adaptor frame and return it.
  __ bind(&adaptor);
  __ movq(rax, Operand(rdx, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ ret(0);
}


void CEntryStub::GenerateThrowTOS(MacroAssembler* masm) {
  // Check that stack should contain frame pointer, code pointer, state and
  // return address in that order.
  ASSERT_EQ(StackHandlerConstants::kFPOffset + kPointerSize,
            StackHandlerConstants::kStateOffset);
  ASSERT_EQ(StackHandlerConstants::kStateOffset + kPointerSize,
            StackHandlerConstants::kPCOffset);

  ExternalReference handler_address(Top::k_handler_address);
  __ movq(kScratchRegister, handler_address);
  __ movq(rdx, Operand(kScratchRegister, 0));
  // get next in chain
  __ movq(rcx, Operand(rdx, 0));
  __ movq(Operand(kScratchRegister, 0), rcx);
  __ movq(rsp, rdx);
  __ pop(rbp);  // pop frame pointer
  __ pop(rdx);  // remove code pointer
  __ pop(rdx);  // remove state

  // Before returning we restore the context from the frame pointer if not NULL.
  // The frame pointer is NULL in the exception handler of a JS entry frame.
  __ xor_(rsi, rsi);  // tentatively set context pointer to NULL
  Label skip;
  __ cmpq(rbp, Immediate(0));
  __ j(equal, &skip);
  __ movq(rsi, Operand(rbp, StandardFrameConstants::kContextOffset));
  __ bind(&skip);

  __ ret(0);
}



void CEntryStub::GenerateCore(MacroAssembler* masm,
                              Label* throw_normal_exception,
                              Label* throw_out_of_memory_exception,
                              StackFrame::Type frame_type,
                              bool do_gc,
                              bool always_allocate_scope) {
  // rax: result parameter for PerformGC, if any.
  // rbx: pointer to C function  (C callee-saved).
  // rbp: frame pointer  (restored after C call).
  // rsp: stack pointer  (restored after C call).
  // rdi: number of arguments including receiver.
  // r15: pointer to the first argument (C callee-saved).
  //      This pointer is reused in LeaveExitFrame(), so it is stored in a
  //      callee-saved register.

  if (do_gc) {
    __ movq(Operand(rsp, 0), rax);  // Result.
    __ movq(kScratchRegister,
            FUNCTION_ADDR(Runtime::PerformGC),
            RelocInfo::RUNTIME_ENTRY);
    __ call(kScratchRegister);
  }

  ExternalReference scope_depth =
      ExternalReference::heap_always_allocate_scope_depth();
  if (always_allocate_scope) {
    __ movq(kScratchRegister, scope_depth);
    __ incl(Operand(kScratchRegister, 0));
  }

  // Call C function.
#ifdef __MSVC__
  // MSVC passes arguments in rcx, rdx, r8, r9
  __ movq(rcx, rdi);  // argc.
  __ movq(rdx, r15);  // argv.
#else  // ! defined(__MSVC__)
  // GCC passes arguments in rdi, rsi, rdx, rcx, r8, r9.
  // First argument is already in rdi.
  __ movq(rsi, r15);  // argv.
#endif
  __ call(rbx);
  // Result is in rax - do not destroy this register!

  if (always_allocate_scope) {
    __ movq(kScratchRegister, scope_depth);
    __ decl(Operand(kScratchRegister, 0));
  }

  // Check for failure result.
  Label failure_returned;
  ASSERT(((kFailureTag + 1) & kFailureTagMask) == 0);
  __ lea(rcx, Operand(rax, 1));
  // Lower 2 bits of rcx are 0 iff rax has failure tag.
  __ testl(rcx, Immediate(kFailureTagMask));
  __ j(zero, &failure_returned);

  // Exit the JavaScript to C++ exit frame.
  __ LeaveExitFrame(frame_type);
  __ ret(0);

  // Handling of failure.
  __ bind(&failure_returned);

  Label retry;
  // If the returned exception is RETRY_AFTER_GC continue at retry label
  ASSERT(Failure::RETRY_AFTER_GC == 0);
  __ testq(rax, Immediate(((1 << kFailureTypeTagSize) - 1) << kFailureTagSize));
  __ j(zero, &retry);

  Label continue_exception;
  // If the returned failure is EXCEPTION then promote Top::pending_exception().
  __ movq(kScratchRegister, Failure::Exception(), RelocInfo::NONE);
  __ cmpq(rax, kScratchRegister);
  __ j(not_equal, &continue_exception);

  // Retrieve the pending exception and clear the variable.
  ExternalReference pending_exception_address(Top::k_pending_exception_address);
  __ movq(kScratchRegister, pending_exception_address);
  __ movq(rax, Operand(kScratchRegister, 0));
  __ movq(rdx, ExternalReference::the_hole_value_location());
  __ movq(rdx, Operand(rdx, 0));
  __ movq(Operand(kScratchRegister, 0), rdx);

  __ bind(&continue_exception);
  // Special handling of out of memory exception.
  __ movq(kScratchRegister, Failure::OutOfMemoryException(), RelocInfo::NONE);
  __ cmpq(rax, kScratchRegister);
  __ j(equal, throw_out_of_memory_exception);

  // Handle normal exception.
  __ jmp(throw_normal_exception);

  // Retry.
  __ bind(&retry);
}


void CEntryStub::GenerateThrowOutOfMemory(MacroAssembler* masm) {
  // Fetch top stack handler.
  ExternalReference handler_address(Top::k_handler_address);
  __ movq(kScratchRegister, handler_address);
  __ movq(rdx, Operand(kScratchRegister, 0));

  // Unwind the handlers until the ENTRY handler is found.
  Label loop, done;
  __ bind(&loop);
  // Load the type of the current stack handler.
  __ cmpq(Operand(rdx, StackHandlerConstants::kStateOffset),
         Immediate(StackHandler::ENTRY));
  __ j(equal, &done);
  // Fetch the next handler in the list.
  __ movq(rdx, Operand(rdx, StackHandlerConstants::kNextOffset));
  __ jmp(&loop);
  __ bind(&done);

  // Set the top handler address to next handler past the current ENTRY handler.
  __ movq(rax, Operand(rdx, StackHandlerConstants::kNextOffset));
  __ store_rax(handler_address);

  // Set external caught exception to false.
  __ movq(rax, Immediate(false));
  ExternalReference external_caught(Top::k_external_caught_exception_address);
  __ store_rax(external_caught);

  // Set pending exception and rax to out of memory exception.
  __ movq(rax, Failure::OutOfMemoryException(), RelocInfo::NONE);
  ExternalReference pending_exception(Top::k_pending_exception_address);
  __ store_rax(pending_exception);

  // Restore the stack to the address of the ENTRY handler
  __ movq(rsp, rdx);

  // Clear the context pointer;
  __ xor_(rsi, rsi);

  // Restore registers from handler.

  __ pop(rbp);  // FP
  ASSERT_EQ(StackHandlerConstants::kFPOffset + kPointerSize,
            StackHandlerConstants::kStateOffset);
  __ pop(rdx);  // State

  ASSERT_EQ(StackHandlerConstants::kStateOffset + kPointerSize,
            StackHandlerConstants::kPCOffset);
  __ ret(0);
}


void CEntryStub::GenerateBody(MacroAssembler* masm, bool is_debug_break) {
  // rax: number of arguments including receiver
  // rbx: pointer to C function  (C callee-saved)
  // rbp: frame pointer  (restored after C call)
  // rsp: stack pointer  (restored after C call)
  // rsi: current context (C callee-saved)
  // rdi: caller's parameter pointer pp  (C callee-saved)

  // NOTE: Invocations of builtins may return failure objects
  // instead of a proper result. The builtin entry handles
  // this by performing a garbage collection and retrying the
  // builtin once.

  StackFrame::Type frame_type = is_debug_break ?
      StackFrame::EXIT_DEBUG :
      StackFrame::EXIT;

  // Enter the exit frame that transitions from JavaScript to C++.
  __ EnterExitFrame(frame_type);

  // rax: result parameter for PerformGC, if any (setup below).
  //      Holds the result of a previous call to GenerateCore that
  //      returned a failure. On next call, it's used as parameter
  //      to Runtime::PerformGC.
  // rbx: pointer to builtin function  (C callee-saved).
  // rbp: frame pointer  (restored after C call).
  // rsp: stack pointer  (restored after C call).
  // rdi: number of arguments including receiver (destroyed by C call).
  //      The rdi register is not callee-save in Unix 64-bit ABI, so
  //      we must treat it as volatile.
  // r15: argv pointer (C callee-saved).

  Label throw_out_of_memory_exception;
  Label throw_normal_exception;

  // Call into the runtime system. Collect garbage before the call if
  // running with --gc-greedy set.
  if (FLAG_gc_greedy) {
    Failure* failure = Failure::RetryAfterGC(0);
    __ movq(rax, failure, RelocInfo::NONE);
  }
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               frame_type,
               FLAG_gc_greedy,
               false);

  // Do space-specific GC and retry runtime call.
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               frame_type,
               true,
               false);

  // Do full GC and retry runtime call one final time.
  Failure* failure = Failure::InternalError();
  __ movq(rax, failure, RelocInfo::NONE);
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               frame_type,
               true,
               true);

  __ bind(&throw_out_of_memory_exception);
  GenerateThrowOutOfMemory(masm);
  // control flow for generated will not return.

  __ bind(&throw_normal_exception);
  GenerateThrowTOS(masm);
}


void JSEntryStub::GenerateBody(MacroAssembler* masm, bool is_construct) {
  Label invoke, exit;

  // Setup frame.
  __ push(rbp);
  __ movq(rbp, rsp);

  // Save callee-saved registers (X64 calling conventions).
  int marker = is_construct ? StackFrame::ENTRY_CONSTRUCT : StackFrame::ENTRY;
  // Push something that is not an arguments adaptor.
  __ push(Immediate(ArgumentsAdaptorFrame::NON_SENTINEL));
  __ push(Immediate(Smi::FromInt(marker)));  // @ function offset
  __ push(r12);
  __ push(r13);
  __ push(r14);
  __ push(r15);
  __ push(rdi);
  __ push(rsi);
  __ push(rbx);
  // TODO(X64): Push XMM6-XMM15 (low 64 bits) as well, or make them
  // callee-save in JS code as well.

  // Save copies of the top frame descriptor on the stack.
  ExternalReference c_entry_fp(Top::k_c_entry_fp_address);
  __ load_rax(c_entry_fp);
  __ push(rax);

  // Call a faked try-block that does the invoke.
  __ call(&invoke);

  // Caught exception: Store result (exception) in the pending
  // exception field in the JSEnv and return a failure sentinel.
  ExternalReference pending_exception(Top::k_pending_exception_address);
  __ store_rax(pending_exception);
  __ movq(rax, Failure::Exception(), RelocInfo::NONE);
  __ jmp(&exit);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  __ PushTryHandler(IN_JS_ENTRY, JS_ENTRY_HANDLER);

  // Clear any pending exceptions.
  __ load_rax(ExternalReference::the_hole_value_location());
  __ store_rax(pending_exception);

  // Fake a receiver (NULL).
  __ push(Immediate(0));  // receiver

  // Invoke the function by calling through JS entry trampoline
  // builtin and pop the faked function when we return. We load the address
  // from an external reference instead of inlining the call target address
  // directly in the code, because the builtin stubs may not have been
  // generated yet at the time this code is generated.
  if (is_construct) {
    ExternalReference construct_entry(Builtins::JSConstructEntryTrampoline);
    __ load_rax(construct_entry);
  } else {
    ExternalReference entry(Builtins::JSEntryTrampoline);
    __ load_rax(entry);
  }
  __ lea(kScratchRegister, FieldOperand(rax, Code::kHeaderSize));
  __ call(kScratchRegister);

  // Unlink this frame from the handler chain.
  __ movq(kScratchRegister, ExternalReference(Top::k_handler_address));
  __ pop(Operand(kScratchRegister, 0));
  // Pop next_sp.
  __ addq(rsp, Immediate(StackHandlerConstants::kSize - kPointerSize));

  // Restore the top frame descriptor from the stack.
  __ bind(&exit);
  __ movq(kScratchRegister, ExternalReference(Top::k_c_entry_fp_address));
  __ pop(Operand(kScratchRegister, 0));

  // Restore callee-saved registers (X64 conventions).
  __ pop(rbx);
  __ pop(rsi);
  __ pop(rdi);
  __ pop(r15);
  __ pop(r14);
  __ pop(r13);
  __ pop(r12);
  __ addq(rsp, Immediate(2 * kPointerSize));  // remove markers

  // Restore frame pointer and return.
  __ pop(rbp);
  __ ret(0);
}

#undef __

} }  // namespace v8::internal
