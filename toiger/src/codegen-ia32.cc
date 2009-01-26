// Copyright 2006-2008 the V8 project authors. All rights reserved.
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

#include "bootstrapper.h"
#include "codegen-inl.h"
#include "debug.h"
#include "scopes.h"
#include "runtime.h"

namespace v8 { namespace internal {

#define __ masm_->

// -------------------------------------------------------------------------
// CodeGenState implementation.

CodeGenState::CodeGenState(CodeGenerator* owner)
    : owner_(owner),
      typeof_state_(NOT_INSIDE_TYPEOF),
      true_target_(NULL),
      false_target_(NULL),
      previous_(NULL) {
  owner_->set_state(this);
}


CodeGenState::CodeGenState(CodeGenerator* owner,
                           TypeofState typeof_state,
                           JumpTarget* true_target,
                           JumpTarget* false_target)
    : owner_(owner),
      typeof_state_(typeof_state),
      true_target_(true_target),
      false_target_(false_target),
      previous_(owner->state()) {
  owner_->set_state(this);
}


CodeGenState::~CodeGenState() {
  ASSERT(owner_->state() == this);
  owner_->set_state(previous_);
}


// -------------------------------------------------------------------------
// CodeGenerator implementation

CodeGenerator::CodeGenerator(int buffer_size, Handle<Script> script,
                             bool is_eval)
    : is_eval_(is_eval),
      script_(script),
      deferred_(8),
      masm_(new MacroAssembler(NULL, buffer_size)),
      scope_(NULL),
      frame_(NULL),
      allocator_(NULL),
      state_(NULL),
      break_stack_height_(0),
      loop_nesting_(0),
      function_return_is_shadowed_(false),
      in_spilled_code_(false) {
}


void CodeGenerator::SetFrame(VirtualFrame* new_frame,
                             RegisterFile* non_frame_registers) {
  RegisterFile saved_counts;
  if (has_valid_frame()) {
    frame_->DetachFromCodeGenerator();
    // The remaining register reference counts are the non-frame ones.
    allocator_->SaveTo(&saved_counts);
  }

  if (new_frame != NULL) {
    // Restore the non-frame register references that go with the new frame.
    allocator_->RestoreFrom(non_frame_registers);
    new_frame->AttachToCodeGenerator();
  }

  frame_ = new_frame;
  saved_counts.CopyTo(non_frame_registers);
}


void CodeGenerator::DeleteFrame() {
  if (has_valid_frame()) {
    frame_->DetachFromCodeGenerator();
    delete frame_;
    frame_ = NULL;
  }
}


// Calling conventions:
// ebp: frame pointer
// esp: stack pointer
// edi: caller's parameter pointer
// esi: callee's context

void CodeGenerator::GenCode(FunctionLiteral* fun) {
  // Record the position for debugging purposes.
  CodeForFunctionPosition(fun);

  ZoneList<Statement*>* body = fun->body();

  // Initialize state.
  ASSERT(scope_ == NULL);
  scope_ = fun->scope();
  ASSERT(allocator_ == NULL);
  RegisterAllocator register_allocator(this);
  allocator_ = &register_allocator;
  ASSERT(frame_ == NULL);
  frame_ = new VirtualFrame(this);
  function_return_.Initialize(this, JumpTarget::BIDIRECTIONAL);
  function_return_is_shadowed_ = false;
  set_in_spilled_code(false);

  // Adjust for function-level loop nesting.
  loop_nesting_ += fun->loop_nesting();

  {
    CodeGenState state(this);

    // Entry
    // stack: function, receiver, arguments, return address
    // esp: stack pointer
    // ebp: frame pointer
    // edi: caller's parameter pointer
    // esi: callee's context

    allocator_->Initialize();
    frame_->Enter();
    // tos: code slot
#ifdef DEBUG
    if (strlen(FLAG_stop_at) > 0 &&
        fun->name()->IsEqualTo(CStrVector(FLAG_stop_at))) {
      frame_->SpillAll();
      __ int3();
    }
#endif

    // Allocate space for locals and initialize them.
    frame_->AllocateStackSlots(scope_->num_stack_slots());

    // Allocate the arguments object and copy the parameters into it.
    if (scope_->arguments() != NULL) {
      ASSERT(scope_->arguments_shadow() != NULL);
      Comment cmnt(masm_, "[ Allocate arguments object");
      ArgumentsAccessStub stub(ArgumentsAccessStub::NEW_OBJECT);
      frame_->PushFunction();
      frame_->PushReceiverSlotAddress();
      frame_->Push(Smi::FromInt(scope_->num_parameters()));
      Result answer = frame_->CallStub(&stub, 3);
      frame_->Push(&answer);
    }

    if (scope_->num_heap_slots() > 0) {
      Comment cmnt(masm_, "[ allocate local context");
      // Allocate local context.
      // Get outer context and create a new context based on it.
      frame_->PushFunction();
      Result context = frame_->CallRuntime(Runtime::kNewContext, 1);

      if (kDebug) {
        JumpTarget verified_true(this);
        // Verify eax and esi are the same in debug mode
        __ cmp(context.reg(), Operand(esi));
        context.Unuse();
        verified_true.Branch(equal);
        frame_->SpillAll();
        __ int3();
        verified_true.Bind();
      }
      // Update context local.
      frame_->SaveContextRegister();
    }

    // TODO(1241774): Improve this code:
    // 1) only needed if we have a context
    // 2) no need to recompute context ptr every single time
    // 3) don't copy parameter operand code from SlotOperand!
    {
      Comment cmnt2(masm_, "[ copy context parameters into .context");

      // Note that iteration order is relevant here! If we have the same
      // parameter twice (e.g., function (x, y, x)), and that parameter
      // needs to be copied into the context, it must be the last argument
      // passed to the parameter that needs to be copied. This is a rare
      // case so we don't check for it, instead we rely on the copying
      // order: such a parameter is copied repeatedly into the same
      // context location and thus the last value is what is seen inside
      // the function.
      for (int i = 0; i < scope_->num_parameters(); i++) {
        Variable* par = scope_->parameter(i);
        Slot* slot = par->slot();
        if (slot != NULL && slot->type() == Slot::CONTEXT) {
          VirtualFrame::SpilledScope spilled_scope(this);
          ASSERT(!scope_->is_global_scope());  // no parameters in global scope
          __ mov(eax, frame_->ParameterAt(i));
          // Loads ecx with context; used below in RecordWrite.
          __ mov(SlotOperand(slot, edx), eax);
          int offset = FixedArray::kHeaderSize + slot->index() * kPointerSize;
          __ RecordWrite(edx, offset, eax, ebx);
        }
      }
    }

    // This section stores the pointer to the arguments object that
    // was allocated and copied into above. If the address was not
    // saved to TOS, we push ecx onto the stack.
    //
    // Store the arguments object.  This must happen after context
    // initialization because the arguments object may be stored in the
    // context.
    if (scope_->arguments() != NULL) {
      VirtualFrame::SpilledScope spilled_scope(this);
      Comment cmnt(masm_, "[ store arguments object");
      { Reference shadow_ref(this, scope_->arguments_shadow());
        ASSERT(shadow_ref.is_slot());
        { Reference arguments_ref(this, scope_->arguments());
          ASSERT(arguments_ref.is_slot());
          // Here we rely on the convenient property that references to slot
          // take up zero space in the frame (ie, it doesn't matter that the
          // stored value is actually below the reference on the frame).
          arguments_ref.SetValue(NOT_CONST_INIT);
        }
        shadow_ref.SetValue(NOT_CONST_INIT);
      }
      frame_->Drop();  // Value is no longer needed.
    }

    // Generate code to 'execute' declarations and initialize functions
    // (source elements). In case of an illegal redeclaration we need to
    // handle that instead of processing the declarations.
    if (scope_->HasIllegalRedeclaration()) {
      Comment cmnt(masm_, "[ illegal redeclarations");
      scope_->VisitIllegalRedeclaration(this);
    } else {
      Comment cmnt(masm_, "[ declarations");
      ProcessDeclarations(scope_->declarations());
      // Bail out if a stack-overflow exception occurred when processing
      // declarations.
      if (HasStackOverflow()) return;
    }

    if (FLAG_trace) {
      frame_->CallRuntime(Runtime::kTraceEnter, 0);
      // Ignore the return value.
    }
    CheckStack();

    // Compile the body of the function in a vanilla state. Don't
    // bother compiling all the code if the scope has an illegal
    // redeclaration.
    if (!scope_->HasIllegalRedeclaration()) {
      Comment cmnt(masm_, "[ function body");
#ifdef DEBUG
      bool is_builtin = Bootstrapper::IsActive();
      bool should_trace =
          is_builtin ? FLAG_trace_builtin_calls : FLAG_trace_calls;
      if (should_trace) {
        frame_->CallRuntime(Runtime::kDebugTrace, 0);
        // Ignore the return value.
      }
#endif
      VisitStatements(body);

      // Handle the return from the function.
      if (has_valid_frame()) {
        // If there is a valid frame, control flow can fall off the end of
        // the body.  In that case there is an implicit return statement.
        // Compiling a return statement will jump to the return sequence if
        // it is already generated or generate it if not.
        ASSERT(!function_return_is_shadowed_);
        Literal undefined(Factory::undefined_value());
        ReturnStatement statement(&undefined);
        statement.set_statement_pos(fun->end_position());
        VisitReturnStatement(&statement);
      } else if (function_return_.is_linked()) {
        // If the return target has dangling jumps to it, then we have not
        // yet generated the return sequence.  This can happen when (a)
        // control does not flow off the end of the body so we did not
        // compile an artificial return statement just above, and (b) there
        // are return statements in the body but (c) they are all shadowed.
        //
        // There is no valid frame here but it is safe (also necessary) to
        // load the return value into eax.
        __ mov(eax, Immediate(Factory::undefined_value()));
        function_return_.Bind();
        GenerateReturnSequence();
      }
    }
  }

  // Adjust for function-level loop nesting.
  loop_nesting_ -= fun->loop_nesting();

  // Code generation state must be reset.
  ASSERT(state_ == NULL);
  ASSERT(loop_nesting() == 0);
  ASSERT(!function_return_is_shadowed_);
  function_return_.Unuse();
  DeleteFrame();

  // Process any deferred code using the register allocator.
  ProcessDeferred();

  // There is no need to delete the register allocator, it is a
  // stack-allocated local.
  allocator_ = NULL;
  scope_ = NULL;
}


Operand CodeGenerator::SlotOperand(Slot* slot, Register tmp) {
  // Currently, this assertion will fail if we try to assign to
  // a constant variable that is constant because it is read-only
  // (such as the variable referring to a named function expression).
  // We need to implement assignments to read-only variables.
  // Ideally, we should do this during AST generation (by converting
  // such assignments into expression statements); however, in general
  // we may not be able to make the decision until past AST generation,
  // that is when the entire program is known.
  ASSERT(slot != NULL);
  int index = slot->index();
  switch (slot->type()) {
    case Slot::PARAMETER:
      return frame_->ParameterAt(index);

    case Slot::LOCAL:
      return frame_->LocalAt(index);

    case Slot::CONTEXT: {
      // Follow the context chain if necessary.
      ASSERT(!tmp.is(esi));  // do not overwrite context register
      Register context = esi;
      int chain_length = scope()->ContextChainLength(slot->var()->scope());
      for (int i = chain_length; i-- > 0;) {
        // Load the closure.
        // (All contexts, even 'with' contexts, have a closure,
        // and it is the same for all contexts inside a function.
        // There is no need to go to the function context first.)
        __ mov(tmp, ContextOperand(context, Context::CLOSURE_INDEX));
        // Load the function context (which is the incoming, outer context).
        __ mov(tmp, FieldOperand(tmp, JSFunction::kContextOffset));
        context = tmp;
      }
      // We may have a 'with' context now. Get the function context.
      // (In fact this mov may never be the needed, since the scope analysis
      // may not permit a direct context access in this case and thus we are
      // always at a function context. However it is safe to dereference be-
      // cause the function context of a function context is itself. Before
      // deleting this mov we should try to create a counter-example first,
      // though...)
      __ mov(tmp, ContextOperand(context, Context::FCONTEXT_INDEX));
      return ContextOperand(tmp, index);
    }

    default:
      UNREACHABLE();
      return Operand(eax);
  }
}


// Loads a value on TOS. If the result is a boolean value it may have
// been translated into control flow to the true and/or false targets.
// If force_control is true, control flow is forced and the function
// exits without a valid frame.
void CodeGenerator::LoadCondition(Expression* x,
                                  TypeofState typeof_state,
                                  JumpTarget* true_target,
                                  JumpTarget* false_target,
                                  bool force_control) {
  ASSERT(!in_spilled_code());
#ifdef DEBUG
  int original_height = frame_->height();
#endif
  { CodeGenState new_state(this, typeof_state, true_target, false_target);
    Visit(x);
  }

  if (force_control && has_valid_frame()) {
    // Convert the TOS value to a boolean in the condition code register.
    ToBoolean(true_target, false_target);
  }

  ASSERT(!(force_control && has_valid_frame()));
  ASSERT(!has_valid_frame() || frame_->height() == original_height + 1);
}


void CodeGenerator::Load(Expression* x, TypeofState typeof_state) {
#ifdef DEBUG
  int original_height = frame_->height();
#endif
  ASSERT(!in_spilled_code());
  JumpTarget true_target(this);
  JumpTarget false_target(this);
  LoadCondition(x, typeof_state, &true_target, &false_target, false);

  if (true_target.is_linked() || false_target.is_linked()) {
    // We have at least one condition value that has been "translated" into
    // a branch, thus it needs to be loaded explicitly.
    JumpTarget loaded(this);
    if (has_valid_frame()) {
      loaded.Jump();  // Don't lose the current TOS.
    }
    bool both = true_target.is_linked() && false_target.is_linked();
    // Load "true" if necessary.
    if (true_target.is_linked()) {
      true_target.Bind();
      VirtualFrame::SpilledScope spilled_scope(this);
      frame_->EmitPush(Immediate(Factory::true_value()));
    }
    // If both "true" and "false" need to be reincarnated jump across the
    // code for "false".
    if (both) {
      loaded.Jump();
    }
    // Load "false" if necessary.
    if (false_target.is_linked()) {
      false_target.Bind();
      VirtualFrame::SpilledScope spilled_scope(this);
      frame_->EmitPush(Immediate(Factory::false_value()));
    }
    // A value is loaded on all paths reaching this point.
    loaded.Bind();
  }
  ASSERT(has_valid_frame());
  ASSERT(frame_->height() == original_height + 1);
}


void CodeGenerator::LoadGlobal() {
  if (in_spilled_code()) {
    frame_->EmitPush(GlobalObject());
  } else {
    Result temp = allocator_->Allocate();
    __ mov(temp.reg(), GlobalObject());
    frame_->Push(&temp);
  }
}


void CodeGenerator::LoadGlobalReceiver() {
  Result temp = allocator_->Allocate();
  Register reg = temp.reg();
  __ mov(reg, GlobalObject());
  __ mov(reg, FieldOperand(reg, GlobalObject::kGlobalReceiverOffset));
  frame_->Push(&temp);
}


// TODO(1241834): Get rid of this function in favor of just using Load, now
// that we have the INSIDE_TYPEOF typeof state. => Need to handle global
// variables w/o reference errors elsewhere.
void CodeGenerator::LoadTypeofExpression(Expression* x) {
  Variable* variable = x->AsVariableProxy()->AsVariable();
  if (variable != NULL && !variable->is_this() && variable->is_global()) {
    // NOTE: This is somewhat nasty. We force the compiler to load
    // the variable as if through '<global>.<variable>' to make sure we
    // do not get reference errors.
    Slot global(variable, Slot::CONTEXT, Context::GLOBAL_INDEX);
    Literal key(variable->name());
    // TODO(1241834): Fetch the position from the variable instead of using
    // no position.
    Property property(&global, &key, RelocInfo::kNoPosition);
    Load(&property);
  } else {
    Load(x, INSIDE_TYPEOF);
  }
}


Reference::Reference(CodeGenerator* cgen, Expression* expression)
    : cgen_(cgen), expression_(expression), type_(ILLEGAL) {
  cgen->LoadReference(this);
}


Reference::~Reference() {
  cgen_->UnloadReference(this);
}


void CodeGenerator::LoadReference(Reference* ref) {
  // References are loaded from both spilled and unspilled code.  Set the
  // state to unspilled to allow that (and explicitly spill after
  // construction at the construction sites).
  bool was_in_spilled_code = in_spilled_code_;
  in_spilled_code_ = false;

  Comment cmnt(masm_, "[ LoadReference");
  Expression* e = ref->expression();
  Property* property = e->AsProperty();
  Variable* var = e->AsVariableProxy()->AsVariable();

  if (property != NULL) {
    // The expression is either a property or a variable proxy that rewrites
    // to a property.
    Load(property->obj());
    // We use a named reference if the key is a literal symbol, unless it is
    // a string that can be legally parsed as an integer.  This is because
    // otherwise we will not get into the slow case code that handles [] on
    // String objects.
    Literal* literal = property->key()->AsLiteral();
    uint32_t dummy;
    if (literal != NULL &&
        literal->handle()->IsSymbol() &&
        !String::cast(*(literal->handle()))->AsArrayIndex(&dummy)) {
      ref->set_type(Reference::NAMED);
    } else {
      Load(property->key());
      ref->set_type(Reference::KEYED);
    }
  } else if (var != NULL) {
    // The expression is a variable proxy that does not rewrite to a
    // property.  Global variables are treated as named property references.
    if (var->is_global()) {
      VirtualFrame::SpilledScope spilled_scope(this);
      LoadGlobal();
      ref->set_type(Reference::NAMED);
    } else {
      ASSERT(var->slot() != NULL);
      ref->set_type(Reference::SLOT);
    }
  } else {
    // Anything else is a runtime error.
    Load(e);
    frame_->CallRuntime(Runtime::kThrowReferenceError, 1);
  }

  in_spilled_code_ = was_in_spilled_code;
}


void CodeGenerator::UnloadReference(Reference* ref) {
  // Pop a reference from the stack while preserving TOS.
  Comment cmnt(masm_, "[ UnloadReference");
  frame_->Nip(ref->size());
}


class ToBooleanStub: public CodeStub {
 public:
  ToBooleanStub() { }

  void Generate(MacroAssembler* masm);

 private:
  Major MajorKey() { return ToBoolean; }
  int MinorKey() { return 0; }
};


// ECMA-262, section 9.2, page 30: ToBoolean(). Pop the top of stack and
// convert it to a boolean in the condition code register or jump to
// 'false_target'/'true_target' as appropriate.
void CodeGenerator::ToBoolean(JumpTarget* true_target,
                              JumpTarget* false_target) {
  Comment cmnt(masm_, "[ ToBoolean");

  // The value to convert should be popped from the stack.
  Result value = frame_->Pop();
  value.ToRegister();
  // Fast case checks.

  // 'false' => false.
  __ cmp(value.reg(), Factory::false_value());
  false_target->Branch(equal);

  // 'true' => true.
  __ cmp(value.reg(), Factory::true_value());
  true_target->Branch(equal);

  // 'undefined' => false.
  __ cmp(value.reg(), Factory::undefined_value());
  false_target->Branch(equal);

  // Smi => false iff zero.
  ASSERT(kSmiTag == 0);
  __ test(value.reg(), Operand(value.reg()));
  false_target->Branch(zero);
  __ test(value.reg(), Immediate(kSmiTagMask));
  true_target->Branch(zero);

  // Call the stub for all other cases.
  frame_->Push(&value);  // Undo the Pop() from above.
  ToBooleanStub stub;
  Result temp = frame_->CallStub(&stub, 1);
  // Convert the result to a condition code.
  __ test(temp.reg(), Operand(temp.reg()));
  temp.Unuse();
  true_target->Branch(not_equal);
  false_target->Jump();
}


class FloatingPointHelper : public AllStatic {
 public:
  // Code pattern for loading floating point values. Input values must
  // be either smi or heap number objects (fp values). Requirements:
  // operand_1 on TOS+1 , operand_2 on TOS+2; Returns operands as
  // floating point numbers on FPU stack.
  static void LoadFloatOperands(MacroAssembler* masm, Register scratch);
  // Test if operands are smi or number objects (fp). Requirements:
  // operand_1 in eax, operand_2 in edx; falls through on float
  // operands, jumps to the non_float label otherwise.
  static void CheckFloatOperands(MacroAssembler* masm,
                                 Label* non_float,
                                 Register scratch);
  // Allocate a heap number in new space with undefined value.
  // Returns tagged pointer in eax, or jumps to need_gc if new space is full.
  static void AllocateHeapNumber(MacroAssembler* masm,
                                 Label* need_gc,
                                 Register scratch1,
                                 Register scratch2);
};


// Flag that indicates whether or not the code for dealing with smis
// is inlined or should be dealt with in the stub.
enum GenericBinaryFlags {
  SMI_CODE_IN_STUB,
  SMI_CODE_INLINED
};


class GenericBinaryOpStub: public CodeStub {
 public:
  GenericBinaryOpStub(Token::Value op,
                      OverwriteMode mode,
                      GenericBinaryFlags flags)
      : op_(op), mode_(mode), flags_(flags) { }

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
    return OpBits::encode(op_) |
        ModeBits::encode(mode_) |
        FlagBits::encode(flags_);
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


class DeferredInlineBinaryOperation: public DeferredCode {
 public:
  DeferredInlineBinaryOperation(CodeGenerator* generator,
                                Token::Value op,
                                OverwriteMode mode,
                                GenericBinaryFlags flags)
      : DeferredCode(generator), stub_(op, mode, flags), op_(op) {
    set_comment("[ DeferredInlineBinaryOperation");
  }

  Result GenerateInlineCode();

  virtual void Generate();

 private:
  GenericBinaryOpStub stub_;
  Token::Value op_;
};


void DeferredInlineBinaryOperation::Generate() {
  Result left(generator());
  Result right(generator());
  enter()->Bind(&left, &right);
  generator()->frame()->Push(&left);
  generator()->frame()->Push(&right);
  Result answer = generator()->frame()->CallStub(&stub_, 2);
  exit()->Jump(&answer);
}


void CodeGenerator::GenericBinaryOperation(Token::Value op,
                                           StaticType* type,
                                           OverwriteMode overwrite_mode) {
  Comment cmnt(masm_, "[ BinaryOperation");
  Comment cmnt_token(masm_, Token::String(op));

  if (op == Token::COMMA) {
    // Simply discard left value.
    frame_->Nip(1);
    return;
  }

  // Set the flags based on the operation, type and loop nesting level.
  GenericBinaryFlags flags;
  switch (op) {
    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR:
    case Token::SHL:
    case Token::SHR:
    case Token::SAR:
      // Bit operations always assume they likely operate on Smis. Still only
      // generate the inline Smi check code if this operation is part of a loop.
      flags = (loop_nesting() > 0)
              ? SMI_CODE_INLINED
              : SMI_CODE_IN_STUB;
      break;

    default:
      // By default only inline the Smi check code for likely smis if this
      // operation is part of a loop.
      flags = ((loop_nesting() > 0) && type->IsLikelySmi())
              ? SMI_CODE_INLINED
              : SMI_CODE_IN_STUB;
      break;
  }

  if (flags == SMI_CODE_INLINED) {
    // Create a new deferred code for the slow-case part.
    DeferredInlineBinaryOperation* deferred =
        new DeferredInlineBinaryOperation(this, op, overwrite_mode, flags);
    // Generate the inline part of the code.
    // The operands are on the frame.
    Result answer = deferred->GenerateInlineCode();
    deferred->exit()->Bind(&answer);
    frame_->Push(&answer);
  } else {
    // Call the stub and push the result to the stack.
    GenericBinaryOpStub stub(op, overwrite_mode, flags);
    Result answer = frame_->CallStub(&stub, 2);
    frame_->Push(&answer);
  }
}


class DeferredInlinedSmiOperation: public DeferredCode {
 public:
  DeferredInlinedSmiOperation(CodeGenerator* generator,
                              Token::Value op,
                              Smi* value,
                              OverwriteMode overwrite_mode)
      : DeferredCode(generator),
        op_(op),
        value_(value),
        overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiOperation");
  }

  virtual void Generate();

 private:
  Token::Value op_;
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlinedSmiOperation::Generate() {
  Result left(generator());
  enter()->Bind(&left);
  generator()->frame()->Push(&left);
  generator()->frame()->Push(value_);
  GenericBinaryOpStub igostub(op_, overwrite_mode_, SMI_CODE_INLINED);
  Result answer = generator()->frame()->CallStub(&igostub, 2);
  exit()->Jump(&answer);
}


class DeferredInlinedSmiOperationReversed: public DeferredCode {
 public:
  DeferredInlinedSmiOperationReversed(CodeGenerator* generator,
                                      Token::Value op,
                                      Smi* value,
                                      OverwriteMode overwrite_mode)
      : DeferredCode(generator),
        op_(op),
        value_(value),
        overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiOperationReversed");
  }

  virtual void Generate();

 private:
  Token::Value op_;
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlinedSmiOperationReversed::Generate() {
  Result right(generator());
  enter()->Bind(&right);
  generator()->frame()->Push(value_);
  generator()->frame()->Push(&right);
  GenericBinaryOpStub igostub(op_, overwrite_mode_, SMI_CODE_INLINED);
  Result answer = generator()->frame()->CallStub(&igostub, 2);
  exit()->Jump(&answer);
}


class DeferredInlinedSmiAdd: public DeferredCode {
 public:
  DeferredInlinedSmiAdd(CodeGenerator* generator,
                        Smi* value,
                        OverwriteMode overwrite_mode)
      : DeferredCode(generator),
        value_(value),
        overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiAdd");
  }

  virtual void Generate();

 private:
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlinedSmiAdd::Generate() {
  // Undo the optimistic add operation and call the shared stub.
  Result left(generator());  // Initially left + value_.
  enter()->Bind(&left);
  left.ToRegister();
  generator()->frame()->Spill(left.reg());
  __ sub(Operand(left.reg()), Immediate(value_));
  generator()->frame()->Push(&left);
  generator()->frame()->Push(value_);
  GenericBinaryOpStub igostub(Token::ADD, overwrite_mode_, SMI_CODE_INLINED);
  Result answer = generator()->frame()->CallStub(&igostub, 2);
  exit()->Jump(&answer);
}


class DeferredInlinedSmiAddReversed: public DeferredCode {
 public:
  DeferredInlinedSmiAddReversed(CodeGenerator* generator,
                                Smi* value,
                                OverwriteMode overwrite_mode)
      : DeferredCode(generator),
        value_(value),
        overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiAddReversed");
  }

  virtual void Generate();

 private:
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlinedSmiAddReversed::Generate() {
  // Undo the optimistic add operation and call the shared stub.
  Result right(generator());  // Initially value_ + right.
  enter()->Bind(&right);
  right.ToRegister();
  generator()->frame()->Spill(right.reg());
  __ sub(Operand(right.reg()), Immediate(value_));
  generator()->frame()->Push(value_);
  generator()->frame()->Push(&right);
  GenericBinaryOpStub igostub(Token::ADD, overwrite_mode_, SMI_CODE_INLINED);
  Result answer = generator()->frame()->CallStub(&igostub, 2);
  exit()->Jump(&answer);
}


class DeferredInlinedSmiSub: public DeferredCode {
 public:
  DeferredInlinedSmiSub(CodeGenerator* generator,
                        Smi* value,
                        OverwriteMode overwrite_mode)
      : DeferredCode(generator),
        value_(value),
        overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiSub");
  }

  virtual void Generate();

 private:
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlinedSmiSub::Generate() {
  // Undo the optimistic sub operation and call the shared stub.
  Result left(generator());  // Initially left - value_.
  enter()->Bind(&left);
  left.ToRegister();
  generator()->frame()->Spill(left.reg());
  __ add(Operand(left.reg()), Immediate(value_));
  generator()->frame()->Push(&left);
  generator()->frame()->Push(value_);
  GenericBinaryOpStub igostub(Token::SUB, overwrite_mode_, SMI_CODE_INLINED);
  Result answer = generator()->frame()->CallStub(&igostub, 2);
  exit()->Jump(&answer);
}


class DeferredInlinedSmiSubReversed: public DeferredCode {
 public:
  DeferredInlinedSmiSubReversed(CodeGenerator* generator,
                                Smi* value,
                                OverwriteMode overwrite_mode)
      : DeferredCode(generator),
        value_(value),
        overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiSubReversed");
  }

  virtual void Generate();

 private:
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlinedSmiSubReversed::Generate() {
  // Call the shared stub.
  Result right(generator());
  enter()->Bind(&right);
  generator()->frame()->Push(value_);
  generator()->frame()->Push(&right);
  GenericBinaryOpStub igostub(Token::SUB, overwrite_mode_, SMI_CODE_INLINED);
  Result answer = generator()->frame()->CallStub(&igostub, 2);
  exit()->Jump(&answer);
}


void CodeGenerator::SmiOperation(Token::Value op,
                                 StaticType* type,
                                 Handle<Object> value,
                                 bool reversed,
                                 OverwriteMode overwrite_mode) {
  // NOTE: This is an attempt to inline (a bit) more of the code for
  // some possible smi operations (like + and -) when (at least) one
  // of the operands is a literal smi. With this optimization, the
  // performance of the system is increased by ~15%, and the generated
  // code size is increased by ~1% (measured on a combination of
  // different benchmarks).

  // TODO(1217802): Optimize some special cases of operations
  // involving a smi literal (multiply by 2, shift by 0, etc.).

  // Get the literal value.
  Smi* smi_value = Smi::cast(*value);
  int int_value = smi_value->value();
  ASSERT(is_intn(int_value, kMaxSmiInlinedBits));

  switch (op) {
    case Token::ADD: {
      DeferredCode* deferred = NULL;
      if (!reversed) {
        deferred = new DeferredInlinedSmiAdd(this, smi_value, overwrite_mode);
      } else {
        deferred = new DeferredInlinedSmiAddReversed(this, smi_value,
                                                     overwrite_mode);
      }
      Result operand = frame_->Pop();
      operand.ToRegister();
      frame_->Spill(operand.reg());
      __ add(Operand(operand.reg()), Immediate(value));
      deferred->enter()->Branch(overflow, &operand, not_taken);
      __ test(Operand(operand.reg()), Immediate(kSmiTagMask));
      deferred->enter()->Branch(not_zero, &operand, not_taken);
      deferred->exit()->Bind(&operand);
      frame_->Push(&operand);
      break;
    }

    case Token::SUB: {
      DeferredCode* deferred = NULL;
      Result operand = frame_->Pop();
      Result answer(this);  // Only allocated a new register if reversed.
      if (!reversed) {
        operand.ToRegister();
        frame_->Spill(operand.reg());
        deferred = new DeferredInlinedSmiSub(this,
                                             smi_value,
                                             overwrite_mode);
        __ sub(Operand(operand.reg()), Immediate(value));
        answer = operand;
      } else {
        answer = allocator()->Allocate();
        ASSERT(answer.is_valid());
        deferred = new DeferredInlinedSmiSubReversed(this,
                                                     smi_value,
                                                     overwrite_mode);
        __ mov(answer.reg(), Immediate(value));
        if (operand.is_register()) {
          __ sub(answer.reg(), Operand(operand.reg()));
        } else {
          ASSERT(operand.is_constant());
          __ sub(Operand(answer.reg()), Immediate(operand.handle()));
        }
      }
      deferred->enter()->Branch(overflow, &operand, not_taken);
      __ test(answer.reg(), Immediate(kSmiTagMask));
      deferred->enter()->Branch(not_zero, &operand, not_taken);
      operand.Unuse();
      deferred->exit()->Bind(&answer);
      frame_->Push(&answer);
      break;
    }

    case Token::SAR: {
      if (reversed) {
        Result top = frame_->Pop();
        frame_->Push(value);
        frame_->Push(&top);
        GenericBinaryOperation(op, type, overwrite_mode);
      } else {
        // Only the least significant 5 bits of the shift value are used.
        // In the slow case, this masking is done inside the runtime call.
        int shift_value = int_value & 0x1f;
        DeferredCode* deferred =
          new DeferredInlinedSmiOperation(this, Token::SAR, smi_value,
                                          overwrite_mode);
        Result result = frame_->Pop();
        result.ToRegister();
        __ test(result.reg(), Immediate(kSmiTagMask));
        deferred->enter()->Branch(not_zero, &result, not_taken);
        frame_->Spill(result.reg());
        __ sar(result.reg(), shift_value);
        __ and_(result.reg(), ~kSmiTagMask);
        deferred->exit()->Bind(&result);
        frame_->Push(&result);
      }
      break;
    }

    case Token::SHR: {
      if (reversed) {
        Result top = frame_->Pop();
        frame_->Push(value);
        frame_->Push(&top);
        GenericBinaryOperation(op, type, overwrite_mode);
      } else {
        // Only the least significant 5 bits of the shift value are used.
        // In the slow case, this masking is done inside the runtime call.
        int shift_value = int_value & 0x1f;
        DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, Token::SHR, smi_value,
                                        overwrite_mode);
        Result operand = frame_->Pop();
        operand.ToRegister();
        __ test(operand.reg(), Immediate(kSmiTagMask));
        deferred->enter()->Branch(not_zero, &operand, not_taken);
        Result answer = allocator()->Allocate();
        ASSERT(answer.is_valid());
        __ mov(answer.reg(), Operand(operand.reg()));
        __ sar(answer.reg(), kSmiTagSize);
        __ shr(answer.reg(), shift_value);
        // A negative Smi shifted right two is in the positive Smi range.
        if (shift_value < 2) {
          __ test(answer.reg(), Immediate(0xc0000000));
          deferred->enter()->Branch(not_zero, &operand, not_taken);
        }
        operand.Unuse();
        ASSERT(kSmiTagSize == times_2);  // Adjust the code if not true.
        __ lea(answer.reg(),
               Operand(answer.reg(), answer.reg(), times_1, kSmiTag));
        deferred->exit()->Bind(&answer);
        frame_->Push(&answer);
      }
      break;
    }

    case Token::SHL: {
      if (reversed) {
        Result top = frame_->Pop();
        frame_->Push(value);
        frame_->Push(&top);
        GenericBinaryOperation(op, type, overwrite_mode);
      } else {
        // Only the least significant 5 bits of the shift value are used.
        // In the slow case, this masking is done inside the runtime call.
        int shift_value = int_value & 0x1f;
        DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, Token::SHL, smi_value,
                                        overwrite_mode);
        Result operand = frame_->Pop();
        operand.ToRegister();
        __ test(operand.reg(), Immediate(kSmiTagMask));
        deferred->enter()->Branch(not_zero, &operand, not_taken);
        Result answer = allocator()->Allocate();
        ASSERT(answer.is_valid());
        __ mov(answer.reg(), Operand(operand.reg()));
        ASSERT(kSmiTag == 0);  // adjust code if not the case
        if (shift_value == 0) {
          __ sar(answer.reg(), kSmiTagSize);
        } else if (shift_value > 1) {
          __ shl(answer.reg(), shift_value - 1);
        }  // We do no shifts, only the Smi conversion, if shift_value is 1.
        // Convert int result to Smi, checking that it is in int range.
        ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
        __ add(answer.reg(), Operand(answer.reg()));
        deferred->enter()->Branch(overflow, &operand, not_taken);
        operand.Unuse();
        deferred->exit()->Bind(&answer);
        frame_->Push(&answer);
      }
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_XOR:
    case Token::BIT_AND: {
      DeferredCode* deferred = NULL;
      if (!reversed) {
        deferred =  new DeferredInlinedSmiOperation(this, op, smi_value,
                                                    overwrite_mode);
      } else {
        deferred = new DeferredInlinedSmiOperationReversed(this, op, smi_value,
                                                           overwrite_mode);
      }
      Result operand = frame_->Pop();
      operand.ToRegister();
      __ test(operand.reg(), Immediate(kSmiTagMask));
      deferred->enter()->Branch(not_zero, &operand, not_taken);
      frame_->Spill(operand.reg());
      if (op == Token::BIT_AND) {
        __ and_(Operand(operand.reg()), Immediate(value));
      } else if (op == Token::BIT_XOR) {
        __ xor_(Operand(operand.reg()), Immediate(value));
      } else {
        ASSERT(op == Token::BIT_OR);
        __ or_(Operand(operand.reg()), Immediate(value));
      }
      deferred->exit()->Bind(&operand);
      frame_->Push(&operand);
      break;
    }

    default: {
      if (!reversed) {
        frame_->Push(value);
      } else {
        Result top = frame_->Pop();
        frame_->Push(value);
        frame_->Push(&top);
      }
      GenericBinaryOperation(op, type, overwrite_mode);
      break;
    }
  }
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


void CodeGenerator::Comparison(Condition cc,
                               bool strict,
                               JumpTarget* true_target,
                               JumpTarget* false_target) {
  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == equal);

  Result left_side(this);
  Result right_side(this);
  // Implement '>' and '<=' by reversal to obtain ECMA-262 conversion order.
  if (cc == greater || cc == less_equal) {
    cc = ReverseCondition(cc);
    left_side = frame_->Pop();
    right_side = frame_->Pop();
  } else {
    right_side = frame_->Pop();
    left_side = frame_->Pop();
  }
  left_side.ToRegister();
  right_side.ToRegister();
  ASSERT(left_side.is_valid());
  ASSERT(right_side.is_valid());
  // Check for the smi case.
  JumpTarget is_smi(this);
  Result temp = allocator_->Allocate();
  ASSERT(temp.is_valid());
  __ mov(temp.reg(), left_side.reg());
  __ or_(temp.reg(), Operand(right_side.reg()));
  __ test(temp.reg(), Immediate(kSmiTagMask));
  temp.Unuse();
  is_smi.Branch(zero, &left_side, &right_side, taken);

  // When non-smi, call out to the compare stub.  "parameters" setup by
  // calling code in edx and eax and "result" is returned in the flags.
  if (!left_side.reg().is(eax)) {
    right_side.ToRegister(eax);
    left_side.ToRegister(edx);
  } else if (!right_side.reg().is(edx)) {
    left_side.ToRegister(edx);
    right_side.ToRegister(eax);
  } else {
    frame_->Spill(eax);  // Can be multiply referenced, even now.
    frame_->Spill(edx);
    __ xchg(eax, edx);
    // If left_side and right_side become real (non-dummy) arguments
    // to CallStub, they need to be swapped in this case.
  }
  CompareStub stub(cc, strict);
  Result answer = frame_->CallStub(&stub, &right_side, &left_side, 0);
  if (cc == equal) {
    __ test(answer.reg(), Operand(answer.reg()));
  } else {
    __ cmp(answer.reg(), 0);
  }
  answer.Unuse();
  true_target->Branch(cc);
  false_target->Jump();

  is_smi.Bind(&left_side, &right_side);
  left_side.ToRegister();
  right_side.ToRegister();
  __ cmp(left_side.reg(), Operand(right_side.reg()));
  right_side.Unuse();
  left_side.Unuse();
  true_target->Branch(cc);
  false_target->Jump();
}


void CodeGenerator::SmiComparison(Condition cc,
                                  Handle<Object> smi_value,
                                  bool strict) {
  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == equal);
  ASSERT(is_intn(Smi::cast(*smi_value)->value(), kMaxSmiInlinedBits));

  JumpTarget is_smi(this);
  Result comparee = frame_->Pop();
  comparee.ToRegister();
  // Check whether the other operand is a smi.
  __ test(comparee.reg(), Immediate(kSmiTagMask));
  is_smi.Branch(zero, &comparee, taken);

  // Setup and call the compare stub, which expects arguments in edx
  // and eax.
  CompareStub stub(cc, strict);
  comparee.ToRegister(edx);
  Result value = allocator_->Allocate(eax);
  ASSERT(value.is_valid());
  __ Set(value.reg(), Immediate(smi_value));
  Result result = frame_->CallStub(&stub, &comparee, &value, 0);
  __ cmp(result.reg(), 0);
  result.Unuse();
  true_target()->Branch(cc);
  false_target()->Jump();

  is_smi.Bind(&comparee);
  comparee.ToRegister();
  // Test smi equality and comparison by signed int comparison.
  __ cmp(Operand(comparee.reg()), Immediate(smi_value));
  comparee.Unuse();
  true_target()->Branch(cc);
  false_target()->Jump();
}


class CallFunctionStub: public CodeStub {
 public:
  explicit CallFunctionStub(int argc) : argc_(argc) { }

  void Generate(MacroAssembler* masm);

 private:
  int argc_;

#ifdef DEBUG
  void Print() { PrintF("CallFunctionStub (args %d)\n", argc_); }
#endif

  Major MajorKey() { return CallFunction; }
  int MinorKey() { return argc_; }
};


// Call the function just below TOS on the stack with the given
// arguments. The receiver is the TOS.
void CodeGenerator::CallWithArguments(ZoneList<Expression*>* args,
                                      int position) {
  // Push the arguments ("left-to-right") on the stack.
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    Load(args->at(i));
  }

  // Record the position for debugging purposes.
  CodeForSourcePosition(position);

  // Use the shared code stub to call the function.
  CallFunctionStub call_function(arg_count);
  Result answer = frame_->CallStub(&call_function, arg_count + 1);
  // Restore context and replace function on the stack with the
  // result of the stub invocation.
  frame_->RestoreContextRegister();
  frame_->SetElementAt(0, &answer);
}


class DeferredStackCheck: public DeferredCode {
 public:
  explicit DeferredStackCheck(CodeGenerator* generator)
      : DeferredCode(generator) {
    set_comment("[ DeferredStackCheck");
  }

  virtual void Generate();
};


void DeferredStackCheck::Generate() {
  enter()->Bind();
  // The stack check can trigger the debugger.  Before calling it, all
  // values including constants must be spilled to the frame.
  generator()->frame()->SpillAll();
  StackCheckStub stub;
  Result ignored = generator()->frame()->CallStub(&stub, 0);
  ignored.Unuse();
  exit()->Jump();
}


void CodeGenerator::CheckStack() {
  if (FLAG_check_stack) {
    DeferredStackCheck* deferred = new DeferredStackCheck(this);
    ExternalReference stack_guard_limit =
        ExternalReference::address_of_stack_guard_limit();
    __ cmp(esp, Operand::StaticVariable(stack_guard_limit));
    deferred->enter()->Branch(below, not_taken);
    deferred->exit()->Bind();
  }
}


void CodeGenerator::VisitStatements(ZoneList<Statement*>* statements) {
  ASSERT(!in_spilled_code());
  for (int i = 0; has_valid_frame() && i < statements->length(); i++) {
    Visit(statements->at(i));
  }
}


void CodeGenerator::VisitBlock(Block* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ Block");
  CodeForStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);
  node->break_target()->Initialize(this);
  VisitStatements(node->statements());
  if (node->break_target()->is_linked()) {
    node->break_target()->Bind();
  }
}


void CodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  VirtualFrame::SpilledScope spilled_scope(this);
  frame_->EmitPush(Immediate(pairs));
  frame_->EmitPush(esi);
  frame_->EmitPush(Immediate(Smi::FromInt(is_eval() ? 1 : 0)));
  frame_->CallRuntime(Runtime::kDeclareGlobals, 3);
  // Return value is ignored.
}


void CodeGenerator::VisitDeclaration(Declaration* node) {
  Comment cmnt(masm_, "[ Declaration");
  CodeForStatementPosition(node);
  Variable* var = node->proxy()->var();
  ASSERT(var != NULL);  // must have been resolved
  Slot* slot = var->slot();

  // If it was not possible to allocate the variable at compile time,
  // we need to "declare" it at runtime to make sure it actually
  // exists in the local context.
  if (slot != NULL && slot->type() == Slot::LOOKUP) {
    // Variables with a "LOOKUP" slot were introduced as non-locals
    // during variable resolution and must have mode DYNAMIC.
    ASSERT(var->mode() == Variable::DYNAMIC);
    // For now, just do a runtime call.
    VirtualFrame::SpilledScope spilled_scope(this);
    frame_->EmitPush(esi);
    frame_->EmitPush(Immediate(var->name()));
    // Declaration nodes are always introduced in one of two modes.
    ASSERT(node->mode() == Variable::VAR || node->mode() == Variable::CONST);
    PropertyAttributes attr = node->mode() == Variable::VAR ? NONE : READ_ONLY;
    frame_->EmitPush(Immediate(Smi::FromInt(attr)));
    // Push initial value, if any.
    // Note: For variables we must not push an initial value (such as
    // 'undefined') because we may have a (legal) redeclaration and we
    // must not destroy the current value.
    if (node->mode() == Variable::CONST) {
      frame_->EmitPush(Immediate(Factory::the_hole_value()));
    } else if (node->fun() != NULL) {
      LoadAndSpill(node->fun());
    } else {
      frame_->EmitPush(Immediate(0));  // no initial value!
    }
    frame_->CallRuntime(Runtime::kDeclareContextSlot, 4);
    // Ignore the return value (declarations are statements).
    return;
  }

  ASSERT(!var->is_global());

  // If we have a function or a constant, we need to initialize the variable.
  Expression* val = NULL;
  if (node->mode() == Variable::CONST) {
    val = new Literal(Factory::the_hole_value());
  } else {
    val = node->fun();  // NULL if we don't have a function
  }

  if (val != NULL) {
    VirtualFrame::SpilledScope spilled_scope(this);
    // Set initial value.
    Reference target(this, node->proxy());
    ASSERT(target.is_slot());
    LoadAndSpill(val);
    target.SetValue(NOT_CONST_INIT);
    // Get rid of the assigned value (declarations are statements).  It's
    // safe to pop the value lying on top of the reference before unloading
    // the reference itself (which preserves the top of stack) because we
    // know that it is a zero-sized reference.
    frame_->Drop();
  }
}


void CodeGenerator::VisitExpressionStatement(ExpressionStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ ExpressionStatement");
  CodeForStatementPosition(node);
  Expression* expression = node->expression();
  expression->MarkAsStatement();
  Load(expression);
  // Remove the lingering expression result from the top of stack.
  frame_->Drop();
}


void CodeGenerator::VisitEmptyStatement(EmptyStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "// EmptyStatement");
  CodeForStatementPosition(node);
  // nothing to do
}


void CodeGenerator::VisitIfStatement(IfStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ IfStatement");
  // Generate different code depending on which parts of the if statement
  // are present or not.
  bool has_then_stm = node->HasThenStatement();
  bool has_else_stm = node->HasElseStatement();

  CodeForStatementPosition(node);
  JumpTarget exit(this);
  if (has_then_stm && has_else_stm) {
    JumpTarget then(this);
    JumpTarget else_(this);
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &then, &else_, true);
    if (then.is_linked()) {
      then.Bind();
      Visit(node->then_statement());
      if (has_valid_frame() && else_.is_linked()) {
        // We have fallen through from the then block and we need to compile
        // the else block.  Emit an unconditional jump around it.
        exit.Jump();
      }
    }
    if (else_.is_linked()) {
      else_.Bind();
      Visit(node->else_statement());
    }

  } else if (has_then_stm) {
    ASSERT(!has_else_stm);
    JumpTarget then(this);
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &then, &exit, true);
    if (then.is_linked()) {
      then.Bind();
      Visit(node->then_statement());
    }

  } else if (has_else_stm) {
    ASSERT(!has_then_stm);
    JumpTarget else_(this);
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &exit, &else_, true);
    if (else_.is_linked()) {
      else_.Bind();
      Visit(node->else_statement());
    }

  } else {
    ASSERT(!has_then_stm && !has_else_stm);
    // We only care about the condition's side effects (not its value
    // or control flow effect).  LoadCondition is called without
    // forcing control flow.
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &exit, &exit, false);
    if (has_valid_frame()) {
      // Control flow can fall off the end of the condition with a
      // value on the frame.
      frame_->Drop();
    }
  }

  if (exit.is_linked()) {
    exit.Bind();
  }
}


void CodeGenerator::CleanStack(int num_bytes) {
  ASSERT(num_bytes % kPointerSize == 0);
  frame_->Drop(num_bytes / kPointerSize);
}


void CodeGenerator::VisitContinueStatement(ContinueStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ ContinueStatement");
  CodeForStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  node->target()->continue_target()->Jump();
}


void CodeGenerator::VisitBreakStatement(BreakStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ BreakStatement");
  CodeForStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  node->target()->break_target()->Jump();
}


void CodeGenerator::VisitReturnStatement(ReturnStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ ReturnStatement");

  if (function_return_is_shadowed_) {
    // If the function return is shadowed, we spill all information
    // and just jump to the label.
    VirtualFrame::SpilledScope spilled_scope(this);
    CodeForStatementPosition(node);
    LoadAndSpill(node->expression());
    frame_->EmitPop(eax);
    function_return_.Jump();
  } else {
    // Load the returned value.
    CodeForStatementPosition(node);
    Load(node->expression());

    // Pop the result from the frame and prepare the frame for
    // returning thus making it easier to merge.
    Result result = frame_->Pop();
    frame_->PrepareForReturn();

    // Move the result into register eax where it belongs.
    result.ToRegister(eax);
    // TODO(203): Instead of explictly calling Unuse on the result, it
    // might be better to pass the result to Jump and Bind below.
    result.Unuse();

    // If the function return label is already bound, we reuse the
    // code by jumping to the return site.
    if (function_return_.is_bound()) {
      function_return_.Jump();
    } else {
      function_return_.Bind();
      GenerateReturnSequence();
    }
  }
}


void CodeGenerator::GenerateReturnSequence() {
  // The return value is a live (but not currently reference counted)
  // reference to eax.  This is safe because the current frame does not
  // contain a reference to eax (it is prepared for the return by spilling
  // all registers).
  ASSERT(has_valid_frame());
  if (FLAG_trace) {
    frame_->Push(eax);  // Materialize result on the stack.
    frame_->CallRuntime(Runtime::kTraceExit, 1);
  }

  // Add a label for checking the size of the code used for returning.
  Label check_exit_codesize;
  __ bind(&check_exit_codesize);

  // Leave the frame and return popping the arguments and the
  // receiver.
  frame_->Exit();
  __ ret((scope_->num_parameters() + 1) * kPointerSize);
  DeleteFrame();

  // Check that the size of the code used for returning matches what is
  // expected by the debugger.
  ASSERT_EQ(Debug::kIa32JSReturnSequenceLength,
            __ SizeOfCodeGeneratedSince(&check_exit_codesize));
}


void CodeGenerator::VisitWithEnterStatement(WithEnterStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ WithEnterStatement");
  CodeForStatementPosition(node);
  Load(node->expression());
  Result context(this);
  if (node->is_catch_block()) {
    context = frame_->CallRuntime(Runtime::kPushCatchContext, 1);
  } else {
    context = frame_->CallRuntime(Runtime::kPushContext, 1);
  }

  if (kDebug) {
    JumpTarget verified_true(this);
    // Verify that the result of the runtime call and the esi register are
    // the same in debug mode.
    __ cmp(context.reg(), Operand(esi));
    context.Unuse();
    verified_true.Branch(equal);
    frame_->SpillAll();
    __ int3();
    verified_true.Bind();
  }

  // Update context local.
  frame_->SaveContextRegister();
}


void CodeGenerator::VisitWithExitStatement(WithExitStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ WithExitStatement");
  CodeForStatementPosition(node);
  // Pop context.
  __ mov(esi, ContextOperand(esi, Context::PREVIOUS_INDEX));
  // Update context local.
  frame_->SaveContextRegister();
}


int CodeGenerator::FastCaseSwitchMaxOverheadFactor() {
    return kFastSwitchMaxOverheadFactor;
}


int CodeGenerator::FastCaseSwitchMinCaseCount() {
    return kFastSwitchMinCaseCount;
}


// Generate a computed jump to a switch case.
void CodeGenerator::GenerateFastCaseSwitchJumpTable(
    SwitchStatement* node,
    int min_index,
    int range,
    JumpTarget* fail_label,
    Vector<JumpTarget*> case_targets,
    Vector<JumpTarget> case_labels) {
  // Notice: Internal references, used by both the jmp instruction and
  // the table entries, need to be relocated if the buffer grows. This
  // prevents the forward use of Labels, since a displacement cannot
  // survive relocation, and it also cannot safely be distinguished
  // from a real address.  Instead we put in zero-values as
  // placeholders, and fill in the addresses after the labels have been
  // bound.

  VirtualFrame::SpilledScope spilled_scope(this);
  frame_->EmitPop(eax);  // supposed Smi
  // check range of value, if outside [0..length-1] jump to default/end label.
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);

  // Test whether input is a HeapNumber that is really a Smi
  JumpTarget is_smi(this);
  __ test(eax, Immediate(kSmiTagMask));
  is_smi.Branch(equal);
  // It's a heap object, not a Smi or a Failure
  __ mov(ebx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ebx, FieldOperand(ebx, Map::kInstanceTypeOffset));
  __ cmp(ebx, HEAP_NUMBER_TYPE);
  fail_label->Branch(not_equal);
  // eax points to a heap number.
  frame_->EmitPush(eax);
  frame_->CallRuntime(Runtime::kNumberToSmi, 1);
  is_smi.Bind();

  if (min_index != 0) {
    __ sub(Operand(eax), Immediate(min_index << kSmiTagSize));
  }
  __ test(eax, Immediate(0x80000000 | kSmiTagMask));  // negative or not Smi
  fail_label->Branch(not_equal, not_taken);
  __ cmp(eax, range << kSmiTagSize);
  fail_label->Branch(greater_equal, not_taken);

  // 0 is placeholder.
  __ jmp(Operand(eax, eax, times_1, 0x0, RelocInfo::INTERNAL_REFERENCE));
  // calculate address to overwrite later with actual address of table.
  int32_t jump_table_ref = __ pc_offset() - sizeof(int32_t);

  __ Align(4);
  JumpTarget table_start(this);
  table_start.Bind();
  __ WriteInternalReference(jump_table_ref, *table_start.entry_label());

  for (int i = 0; i < range; i++) {
    // table entry, 0 is placeholder for case address
    __ dd(0x0, RelocInfo::INTERNAL_REFERENCE);
  }

  GenerateFastCaseSwitchCases(node, case_labels, &table_start);

  for (int i = 0, entry_pos = table_start.entry_label()->pos();
       i < range;
       i++, entry_pos += sizeof(uint32_t)) {
    __ WriteInternalReference(entry_pos, *case_targets[i]->entry_label());
  }
}


void CodeGenerator::VisitSwitchStatement(SwitchStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ SwitchStatement");
  CodeForStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);
  node->break_target()->Initialize(this);

  Load(node->tag());

  if (TryGenerateFastCaseSwitchStatement(node)) {
    return;
  }

  JumpTarget next_test(this);
  JumpTarget fall_through(this);
  JumpTarget default_entry(this);
  JumpTarget default_exit(this, JumpTarget::BIDIRECTIONAL);
  ZoneList<CaseClause*>* cases = node->cases();
  int length = cases->length();
  CaseClause* default_clause = NULL;

  for (int i = 0; i < length; i++) {
    CaseClause* clause = cases->at(i);
    if (clause->is_default()) {
      // Remember the default clause and compile it at the end.
      default_clause = clause;
      continue;
    }

    // Compile each non-default clause.
    Comment cmnt(masm_, "[ Case clause");
    // Label and compile the test.
    if (next_test.is_linked()) {
      // Recycle the same label for each test.
      next_test.Bind();
      next_test.Unuse();
    }
    // Duplicate the switch value.
    frame_->Dup();
    Load(clause->label());
    JumpTarget enter_body(this);
    Comparison(equal, true, &enter_body, &next_test);

    // Before entering the body from the test remove the switch value from
    // the frame.
    enter_body.Bind();
    frame_->Drop();

    // Label the body so that fall through is enabled.
    if (i > 0 && cases->at(i - 1)->is_default()) {
      // The previous case was the default.  This will be the target of a
      // possible backward edge.
      default_exit.Bind();
    } else if (fall_through.is_linked()) {
      // Recycle the same label for each fall through except for the default
      // case.
      fall_through.Bind();
      fall_through.Unuse();
    }
    VisitStatements(clause->statements());

    // If control flow can fall through from the body jump to the next body
    // or the end of the statement.
    if (has_valid_frame()) {
      if (i < length - 1 && cases->at(i + 1)->is_default()) {
        // The next case is the default.
        default_entry.Jump();
      } else {
        fall_through.Jump();
      }
    }
  }

  // The block at the final "test" label removes the switch value.
  next_test.Bind();
  frame_->Drop();

  // If there is a default clause, compile it now.
  if (default_clause != NULL) {
    Comment cmnt(masm_, "[ Default clause");
    default_entry.Bind();
    VisitStatements(default_clause->statements());
    // If control flow can fall out of the default and there is a case after
    // it, jump to that case's body.
    if (has_valid_frame() && default_exit.is_bound()) {
      default_exit.Jump();
    }
  }

  if (fall_through.is_linked()) {
    fall_through.Bind();
  }

  if (node->break_target()->is_linked()) {
    node->break_target()->Bind();
  }
}


void CodeGenerator::VisitLoopStatement(LoopStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ LoopStatement");
  CodeForStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);
  node->break_target()->Initialize(this);

  // Simple condition analysis.  ALWAYS_TRUE and ALWAYS_FALSE represent a
  // known result for the test expression, with no side effects.
  enum { ALWAYS_TRUE, ALWAYS_FALSE, DONT_KNOW } info = DONT_KNOW;
  if (node->cond() == NULL) {
    ASSERT(node->type() == LoopStatement::FOR_LOOP);
    info = ALWAYS_TRUE;
  } else {
    Literal* lit = node->cond()->AsLiteral();
    if (lit != NULL) {
      if (lit->IsTrue()) {
        info = ALWAYS_TRUE;
      } else if (lit->IsFalse()) {
        info = ALWAYS_FALSE;
      }
    }
  }

  switch (node->type()) {
    case LoopStatement::DO_LOOP: {
      JumpTarget body(this, JumpTarget::BIDIRECTIONAL);
      IncrementLoopNesting();

      // Label the top of the loop for the backward CFG edge.  If the test
      // is always true we can use the continue target, and if the test is
      // always false there is no need.
      if (info == ALWAYS_TRUE) {
        node->continue_target()->Initialize(this, JumpTarget::BIDIRECTIONAL);
        node->continue_target()->Bind();
      } else if (info == ALWAYS_FALSE) {
        node->continue_target()->Initialize(this);
        // There is no need, we will never jump back.
      } else {
        ASSERT(info == DONT_KNOW);
        node->continue_target()->Initialize(this);
        body.Bind();
      }

      CheckStack();  // TODO(1222600): ignore if body contains calls.
      Visit(node->body());

      // Compile the test.
      if (info == ALWAYS_TRUE) {
        // If control flow can fall off the end of the body, jump back to
        // the top.
        if (has_valid_frame()) {
          node->continue_target()->Jump();
        }
      } else if (info == ALWAYS_FALSE) {
        // If we had a continue in the body we have to bind its jump target.
        if (node->continue_target()->is_linked()) {
          node->continue_target()->Bind();
        }
      } else {
        ASSERT(info == DONT_KNOW);
        // We have to compile the test expression if it can be reached by
        // control flow falling out of the body or via continue.
        if (node->continue_target()->is_linked()) {
          node->continue_target()->Bind();
        }
        if (has_valid_frame()) {
          LoadCondition(node->cond(), NOT_INSIDE_TYPEOF,
                        &body, node->break_target(), true);
        }
      }
      break;
    }

    case LoopStatement::WHILE_LOOP: {
      IncrementLoopNesting();

      // If the test is never true and has no side effects there is no need
      // to compile the test or body.
      if (info == ALWAYS_FALSE) break;

      // Label the top of the loop with the continue target for the backward
      // CFG edge.
      node->continue_target()->Initialize(this, JumpTarget::BIDIRECTIONAL);
      node->continue_target()->Bind();

      // If the test is always true and has no side effects there is no need
      // to compile it.  We only compile the test when we do not know its
      // outcome or it may have side effects.
      if (info == DONT_KNOW) {
        JumpTarget body(this);
        LoadCondition(node->cond(), NOT_INSIDE_TYPEOF,
                      &body, node->break_target(), true);
        if (body.is_linked()) {
          body.Bind();
        }
      }

      if (has_valid_frame()) {
        CheckStack();  // TODO(1222600): ignore if body contains calls.
        Visit(node->body());

        // If control flow can fall out of the body, jump back to the top.
        if (has_valid_frame()) {
          node->continue_target()->Jump();
        }
      }
      break;
    }

    case LoopStatement::FOR_LOOP: {
      JumpTarget loop(this, JumpTarget::BIDIRECTIONAL);
      if (node->init() != NULL) {
        Visit(node->init());
      }

      IncrementLoopNesting();
      // If the test is never true and has no side effects there is no need
      // to compile the test or body.
      if (info == ALWAYS_FALSE) break;

      // Label the top of the loop for the backward CFG edge.  If there is
      // no update expression we can use the continue target.
      if (node->next() == NULL) {
        node->continue_target()->Initialize(this, JumpTarget::BIDIRECTIONAL);
        node->continue_target()->Bind();
      } else {
        node->continue_target()->Initialize(this);
        loop.Bind();
      }

      // If the test is always true and has no side effects there is no need
      // to compile it.  We only compile the test when we do not know its
      // outcome or it has side effects.
      if (info == DONT_KNOW) {
        JumpTarget body(this);
        LoadCondition(node->cond(), NOT_INSIDE_TYPEOF,
                      &body, node->break_target(), true);
        if (body.is_linked()) {
          body.Bind();
        }
      }

      if (has_valid_frame()) {
        CheckStack();  // TODO(1222600): ignore if body contains calls.
        Visit(node->body());

        if (node->next() == NULL) {
          // If there is no update statement and control flow can fall out
          // of the loop, jump to the continue label.
          if (has_valid_frame()) {
            node->continue_target()->Jump();
          }
        } else {
          // If there is an update statement and control flow can reach it
          // via falling out of the body of the loop or continuing, we
          // compile the update statement.
          if (node->continue_target()->is_linked()) {
            node->continue_target()->Bind();
          }
          if (has_valid_frame()) {
            // Record source position of the statement as this code which is
            // after the code for the body actually belongs to the loop
            // statement and not the body.
            CodeForStatementPosition(node);
            ASSERT(node->type() == LoopStatement::FOR_LOOP);
            Visit(node->next());
            loop.Jump();
          }
        }
      }
      break;
    }
  }

  DecrementLoopNesting();
  if (node->break_target()->is_linked()) {
    node->break_target()->Bind();
  }
}


void CodeGenerator::VisitForInStatement(ForInStatement* node) {
  ASSERT(!in_spilled_code());
  VirtualFrame::SpilledScope spilled_scope(this);
  Comment cmnt(masm_, "[ ForInStatement");
  CodeForStatementPosition(node);

  // We keep stuff on the stack while the body is executing.
  // Record it, so that a break/continue crossing this statement
  // can restore the stack.
  const int kForInStackSize = 5 * kPointerSize;
  break_stack_height_ += kForInStackSize;
  node->set_break_stack_height(break_stack_height_);
  node->break_target()->Initialize(this);
  node->continue_target()->Initialize(this);

  JumpTarget primitive(this);
  JumpTarget jsobject(this);
  JumpTarget fixed_array(this);
  JumpTarget entry(this, JumpTarget::BIDIRECTIONAL);
  JumpTarget end_del_check(this);
  JumpTarget cleanup(this);
  JumpTarget exit(this);

  // Get the object to enumerate over (converted to JSObject).
  LoadAndSpill(node->enumerable());

  // Both SpiderMonkey and kjs ignore null and undefined in contrast
  // to the specification.  12.6.4 mandates a call to ToObject.
  frame_->EmitPop(eax);

  // eax: value to be iterated over
  __ cmp(eax, Factory::undefined_value());
  exit.Branch(equal);
  __ cmp(eax, Factory::null_value());
  exit.Branch(equal);

  // Stack layout in body:
  // [iteration counter (smi)] <- slot 0
  // [length of array]         <- slot 1
  // [FixedArray]              <- slot 2
  // [Map or 0]                <- slot 3
  // [Object]                  <- slot 4

  // Check if enumerable is already a JSObject
  // eax: value to be iterated over
  __ test(eax, Immediate(kSmiTagMask));
  primitive.Branch(zero);
  __ mov(ecx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  __ cmp(ecx, FIRST_JS_OBJECT_TYPE);
  jsobject.Branch(above_equal);

  primitive.Bind();
  frame_->EmitPush(eax);
  frame_->InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION, 1);
  // function call returns the value in eax, which is where we want it below

  jsobject.Bind();
  // Get the set of properties (as a FixedArray or Map).
  // eax: value to be iterated over
  frame_->EmitPush(eax);  // push the object being iterated over (slot 4)

  frame_->EmitPush(eax);  // push the Object (slot 4) for the runtime call
  frame_->CallRuntime(Runtime::kGetPropertyNamesFast, 1);

  // If we got a Map, we can do a fast modification check.
  // Otherwise, we got a FixedArray, and we have to do a slow check.
  // eax: map or fixed array (result from call to
  // Runtime::kGetPropertyNamesFast)
  __ mov(edx, Operand(eax));
  __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));
  __ cmp(ecx, Factory::meta_map());
  fixed_array.Branch(not_equal);

  // Get enum cache
  // eax: map (result from call to Runtime::kGetPropertyNamesFast)
  __ mov(ecx, Operand(eax));
  __ mov(ecx, FieldOperand(ecx, Map::kInstanceDescriptorsOffset));
  // Get the bridge array held in the enumeration index field.
  __ mov(ecx, FieldOperand(ecx, DescriptorArray::kEnumerationIndexOffset));
  // Get the cache from the bridge array.
  __ mov(edx, FieldOperand(ecx, DescriptorArray::kEnumCacheBridgeCacheOffset));

  frame_->EmitPush(eax);  // <- slot 3
  frame_->EmitPush(edx);  // <- slot 2
  __ mov(eax, FieldOperand(edx, FixedArray::kLengthOffset));
  __ shl(eax, kSmiTagSize);
  frame_->EmitPush(eax);  // <- slot 1
  frame_->EmitPush(Immediate(Smi::FromInt(0)));  // <- slot 0
  entry.Jump();

  fixed_array.Bind();
  // eax: fixed array (result from call to Runtime::kGetPropertyNamesFast)
  frame_->EmitPush(Immediate(Smi::FromInt(0)));  // <- slot 3
  frame_->EmitPush(eax);  // <- slot 2

  // Push the length of the array and the initial index onto the stack.
  __ mov(eax, FieldOperand(eax, FixedArray::kLengthOffset));
  __ shl(eax, kSmiTagSize);
  frame_->EmitPush(eax);  // <- slot 1
  frame_->EmitPush(Immediate(Smi::FromInt(0)));  // <- slot 0

  // Condition.
  entry.Bind();
  __ mov(eax, frame_->ElementAt(0));  // load the current count
  __ cmp(eax, frame_->ElementAt(1));  // compare to the array length
  cleanup.Branch(above_equal);

  // Get the i'th entry of the array.
  __ mov(edx, frame_->ElementAt(2));
  __ mov(ebx, Operand(edx, eax, times_2,
                      FixedArray::kHeaderSize - kHeapObjectTag));

  // Get the expected map from the stack or a zero map in the
  // permanent slow case eax: current iteration count ebx: i'th entry
  // of the enum cache
  __ mov(edx, frame_->ElementAt(3));
  // Check if the expected map still matches that of the enumerable.
  // If not, we have to filter the key.
  // eax: current iteration count
  // ebx: i'th entry of the enum cache
  // edx: expected map value
  __ mov(ecx, frame_->ElementAt(4));
  __ mov(ecx, FieldOperand(ecx, HeapObject::kMapOffset));
  __ cmp(ecx, Operand(edx));
  end_del_check.Branch(equal);

  // Convert the entry to a string (or null if it isn't a property anymore).
  frame_->EmitPush(frame_->ElementAt(4));  // push enumerable
  frame_->EmitPush(ebx);  // push entry
  frame_->InvokeBuiltin(Builtins::FILTER_KEY, CALL_FUNCTION, 2);
  __ mov(ebx, Operand(eax));

  // If the property has been removed while iterating, we just skip it.
  __ cmp(ebx, Factory::null_value());
  node->continue_target()->Branch(equal);

  end_del_check.Bind();
  // Store the entry in the 'each' expression and take another spin in the
  // loop.  edx: i'th entry of the enum cache (or string there of)
  frame_->EmitPush(ebx);
  { Reference each(this, node->each());
    // Loading a reference may leave the frame in an unspilled state.
    frame_->SpillAll();
    if (!each.is_illegal()) {
      if (each.size() > 0) {
        frame_->EmitPush(frame_->ElementAt(each.size()));
      }
      // If the reference was to a slot we rely on the convenient property
      // that it doesn't matter whether a value (eg, ebx pushed above) is
      // right on top of or right underneath a zero-sized reference.
      each.SetValue(NOT_CONST_INIT);
      if (each.size() > 0) {
        // It's safe to pop the value lying on top of the reference before
        // unloading the reference itself (which preserves the top of stack,
        // ie, now the topmost value of the non-zero sized reference), since
        // we will discard the top of stack after unloading the reference
        // anyway.
        frame_->Drop();
      }
    }
  }
  // Unloading a reference may leave the frame in an unspilled state.
  frame_->SpillAll();

  // Discard the i'th entry pushed above or else the remainder of the
  // reference, whichever is currently on top of the stack.
  frame_->Drop();

  // Body.
  CheckStack();  // TODO(1222600): ignore if body contains calls.
  VisitAndSpill(node->body());

  // Next.
  node->continue_target()->Bind();
  frame_->EmitPop(eax);
  __ add(Operand(eax), Immediate(Smi::FromInt(1)));
  frame_->EmitPush(eax);
  entry.Jump();

  // Cleanup.
  cleanup.Bind();
  node->break_target()->Bind();
  frame_->Drop(5);

  // Exit.
  exit.Bind();

  break_stack_height_ -= kForInStackSize;
}


void CodeGenerator::VisitTryCatch(TryCatch* node) {
  ASSERT(!in_spilled_code());
  VirtualFrame::SpilledScope spilled_scope(this);
  Comment cmnt(masm_, "[ TryCatch");
  CodeForStatementPosition(node);

  JumpTarget try_block(this);
  JumpTarget exit(this);

  try_block.Call();
  // --- Catch block ---
  frame_->EmitPush(eax);

  // Store the caught exception in the catch variable.
  { Reference ref(this, node->catch_var());
    ASSERT(ref.is_slot());
    // Load the exception to the top of the stack.  Here we make use of the
    // convenient property that it doesn't matter whether a value is
    // immediately on top of or underneath a zero-sized reference.
    ref.SetValue(NOT_CONST_INIT);
  }

  // Remove the exception from the stack.
  frame_->Drop();

  VisitStatementsAndSpill(node->catch_block()->statements());
  if (has_valid_frame()) {
    exit.Jump();
  }


  // --- Try block ---
  try_block.Bind();

  frame_->PushTryHandler(TRY_CATCH_HANDLER);
  int handler_height = frame_->height();

  // Shadow the jump targets for all escapes from the try block, including
  // returns.  During shadowing, the original target is hidden as the
  // ShadowTarget and operations on the original actually affect the
  // shadowing target.
  //
  // We should probably try to unify the escaping targets and the return
  // target.
  int nof_escapes = node->escaping_targets()->length();
  List<ShadowTarget*> shadows(1 + nof_escapes);

  // Add the shadow target for the function return.
  static const int kReturnShadowIndex = 0;
  shadows.Add(new ShadowTarget(&function_return_));
  bool function_return_was_shadowed = function_return_is_shadowed_;
  function_return_is_shadowed_ = true;
  ASSERT(shadows[kReturnShadowIndex]->other_target() == &function_return_);

  // Add the remaining shadow targets.
  for (int i = 0; i < nof_escapes; i++) {
    shadows.Add(new ShadowTarget(node->escaping_targets()->at(i)));
  }

  // Generate code for the statements in the try block.
  VisitStatementsAndSpill(node->try_block()->statements());

  // Stop the introduced shadowing and count the number of required unlinks.
  // After shadowing stops, the original targets are unshadowed and the
  // ShadowTargets represent the formerly shadowing targets.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }
  function_return_is_shadowed_ = function_return_was_shadowed;

  // Get an external reference to the handler address.
  ExternalReference handler_address(Top::k_handler_address);

  // Make sure that there's nothing left on the stack above the
  // handler structure.
  if (FLAG_debug_code) {
    __ mov(eax, Operand::StaticVariable(handler_address));
    __ lea(eax, Operand(eax, StackHandlerConstants::kAddressDisplacement));
    __ cmp(esp, Operand(eax));
    __ Assert(equal, "stack pointer should point to top handler");
  }

  // If we can fall off the end of the try block, unlink from try chain.
  if (has_valid_frame()) {
    frame_->EmitPop(eax);
    __ mov(Operand::StaticVariable(handler_address), eax);  // TOS == next_sp
    frame_->Drop(StackHandlerConstants::kSize / kPointerSize - 1);
    // next_sp popped.
    if (nof_unlinks > 0) {
      exit.Jump();
    }
  }

  // Generate unlink code for the (formerly) shadowing targets that have been
  // jumped to.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      // Unlink from try chain; be careful not to destroy the TOS.
      //
      // Because we can be jumping here (to spilled code) from unspilled
      // code, we need to reestablish a spilled frame at this block.
      shadows[i]->Bind();
      frame_->SpillAll();

      // Reload sp from the top handler, because some statements that we
      // break from (eg, for...in) may have left stuff on the stack.
      __ mov(edx, Operand::StaticVariable(handler_address));
      const int kNextOffset = StackHandlerConstants::kNextOffset +
          StackHandlerConstants::kAddressDisplacement;
      __ lea(esp, Operand(edx, kNextOffset));
      frame_->Forget(frame_->height() - handler_height);

      frame_->EmitPop(Operand::StaticVariable(handler_address));
      frame_->Drop(StackHandlerConstants::kSize / kPointerSize - 1);
      // next_sp popped.

      if (!function_return_is_shadowed_ && i == kReturnShadowIndex) {
        frame_->PrepareForReturn();
      }
      shadows[i]->other_target()->Jump();
    }
  }

  exit.Bind();
}


void CodeGenerator::VisitTryFinally(TryFinally* node) {
  ASSERT(!in_spilled_code());
  VirtualFrame::SpilledScope spilled_scope(this);
  Comment cmnt(masm_, "[ TryFinally");
  CodeForStatementPosition(node);

  // State: Used to keep track of reason for entering the finally
  // block. Should probably be extended to hold information for
  // break/continue from within the try block.
  enum { FALLING, THROWING, JUMPING };

  JumpTarget unlink(this);
  JumpTarget try_block(this);
  JumpTarget finally_block(this);

  try_block.Call();

  frame_->EmitPush(eax);
  // In case of thrown exceptions, this is where we continue.
  __ Set(ecx, Immediate(Smi::FromInt(THROWING)));
  finally_block.Jump();


  // --- Try block ---
  try_block.Bind();

  frame_->PushTryHandler(TRY_FINALLY_HANDLER);
  int handler_height = frame_->height();

  // Shadow the jump targets for all escapes from the try block, including
  // returns.  During shadowing, the original target is hidden as the
  // ShadowTarget and operations on the original actually affect the
  // shadowing target.
  //
  // We should probably try to unify the escaping targets and the return
  // target.
  int nof_escapes = node->escaping_targets()->length();
  List<ShadowTarget*> shadows(1 + nof_escapes);

  // Add the shadow target for the function return.
  static const int kReturnShadowIndex = 0;
  shadows.Add(new ShadowTarget(&function_return_));
  bool function_return_was_shadowed = function_return_is_shadowed_;
  function_return_is_shadowed_ = true;
  ASSERT(shadows[kReturnShadowIndex]->other_target() == &function_return_);

  // Add the remaining shadow targets.
  for (int i = 0; i < nof_escapes; i++) {
    shadows.Add(new ShadowTarget(node->escaping_targets()->at(i)));
  }

  // Generate code for the statements in the try block.
  VisitStatementsAndSpill(node->try_block()->statements());

  // Stop the introduced shadowing and count the number of required unlinks.
  // After shadowing stops, the original targets are unshadowed and the
  // ShadowTargets represent the formerly shadowing targets.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }
  function_return_is_shadowed_ = function_return_was_shadowed;

  // If we can fall off the end of the try block, set the state on the stack
  // to FALLING.
  if (has_valid_frame()) {
    frame_->EmitPush(Immediate(Factory::undefined_value()));  // fake TOS
    __ Set(ecx, Immediate(Smi::FromInt(FALLING)));
    if (nof_unlinks > 0) {
      unlink.Jump();
    }
  }

  // Generate code to set the state for the (formerly) shadowing targets that
  // have been jumped to.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      // Because we can be jumping here (to spilled code) from
      // unspilled code, we need to reestablish a spilled frame at
      // this block.
      shadows[i]->Bind();
      frame_->SpillAll();
      if (i == kReturnShadowIndex) {
        // If this target shadowed the function return, materialize
        // the return value on the stack.
        frame_->EmitPush(eax);
      } else {
        // Fake TOS for targets that shadowed breaks and continues.
        frame_->EmitPush(Immediate(Factory::undefined_value()));
      }
      __ Set(ecx, Immediate(Smi::FromInt(JUMPING + i)));
      unlink.Jump();
    }
  }

  // Unlink from try chain; be careful not to destroy the TOS.
  unlink.Bind();
  // Reload sp from the top handler, because some statements that we
  // break from (eg, for...in) may have left stuff on the stack.
  // Preserve the TOS in a register across stack manipulation.
  frame_->EmitPop(eax);
  ExternalReference handler_address(Top::k_handler_address);
  __ mov(edx, Operand::StaticVariable(handler_address));
  const int kNextOffset = StackHandlerConstants::kNextOffset +
      StackHandlerConstants::kAddressDisplacement;
  __ lea(esp, Operand(edx, kNextOffset));
  frame_->Forget(frame_->height() - handler_height);

  frame_->EmitPop(Operand::StaticVariable(handler_address));
  frame_->Drop(StackHandlerConstants::kSize / kPointerSize - 1);
  // Next_sp popped.
  frame_->EmitPush(eax);

  // --- Finally block ---
  finally_block.Bind();

  // Push the state on the stack.
  frame_->EmitPush(ecx);

  // We keep two elements on the stack - the (possibly faked) result
  // and the state - while evaluating the finally block. Record it, so
  // that a break/continue crossing this statement can restore the
  // stack.
  const int kFinallyStackSize = 2 * kPointerSize;
  break_stack_height_ += kFinallyStackSize;

  // Generate code for the statements in the finally block.
  VisitStatementsAndSpill(node->finally_block()->statements());

  break_stack_height_ -= kFinallyStackSize;
  if (has_valid_frame()) {
    JumpTarget exit(this);
    // Restore state and return value or faked TOS.
    frame_->EmitPop(ecx);
    frame_->EmitPop(eax);

    // Generate code to jump to the right destination for all used
    // formerly shadowing targets.
    for (int i = 0; i <= nof_escapes; i++) {
      if (shadows[i]->is_bound()) {
        JumpTarget* original = shadows[i]->other_target();
        __ cmp(Operand(ecx), Immediate(Smi::FromInt(JUMPING + i)));
        if (!function_return_is_shadowed_ && i == kReturnShadowIndex) {
          JumpTarget skip(this);
          skip.Branch(not_equal);
          frame_->PrepareForReturn();
          original->Jump();
          skip.Bind();
        } else {
          original->Branch(equal);
        }
      }
    }

    // Check if we need to rethrow the exception.
    __ cmp(Operand(ecx), Immediate(Smi::FromInt(THROWING)));
    exit.Branch(not_equal);

    // Rethrow exception.
    frame_->EmitPush(eax);  // undo pop from above
    frame_->CallRuntime(Runtime::kReThrow, 1);

    // Done.
    exit.Bind();
  }
}


void CodeGenerator::VisitDebuggerStatement(DebuggerStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ DebuggerStatement");
  CodeForStatementPosition(node);
  // Spill everything, even constants, to the frame.
  frame_->SpillAll();
  frame_->CallRuntime(Runtime::kDebugBreak, 0);
  // Ignore the return value.
}


void CodeGenerator::InstantiateBoilerplate(Handle<JSFunction> boilerplate) {
  ASSERT(boilerplate->IsBoilerplate());

  // Push the boilerplate on the stack.
  frame_->Push(boilerplate);

  // Create a new closure.
  frame_->Push(esi);
  Result result = frame_->CallRuntime(Runtime::kNewClosure, 2);
  frame_->Push(&result);
}


void CodeGenerator::VisitFunctionLiteral(FunctionLiteral* node) {
  Comment cmnt(masm_, "[ FunctionLiteral");

  // Build the function boilerplate and instantiate it.
  Handle<JSFunction> boilerplate = BuildBoilerplate(node);
  // Check for stack-overflow exception.
  if (HasStackOverflow()) return;
  InstantiateBoilerplate(boilerplate);
}


void CodeGenerator::VisitFunctionBoilerplateLiteral(
    FunctionBoilerplateLiteral* node) {
  Comment cmnt(masm_, "[ FunctionBoilerplateLiteral");
  InstantiateBoilerplate(node->boilerplate());
}


void CodeGenerator::VisitConditional(Conditional* node) {
  Comment cmnt(masm_, "[ Conditional");
  JumpTarget then(this);
  JumpTarget else_(this);
  JumpTarget exit(this);
  LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &then, &else_, true);
  if (then.is_linked()) {
    then.Bind();
    Load(node->then_expression(), typeof_state());
    if (else_.is_linked()) {
      exit.Jump();
    }
  }

  if (else_.is_linked()) {
    else_.Bind();
    Load(node->else_expression(), typeof_state());
  }

  if (exit.is_linked()) {
    exit.Bind();
  }
}


void CodeGenerator::LoadFromSlot(Slot* slot, TypeofState typeof_state) {
  if (slot->type() == Slot::LOOKUP) {
    ASSERT(slot->var()->mode() == Variable::DYNAMIC);

    // For now, just do a runtime call.
    frame_->Push(esi);
    frame_->Push(slot->var()->name());

    Result value(this);
    if (typeof_state == INSIDE_TYPEOF) {
      value =
          frame_->CallRuntime(Runtime::kLoadContextSlotNoReferenceError, 2);
    } else {
      value = frame_->CallRuntime(Runtime::kLoadContextSlot, 2);
    }
    frame_->Push(&value);

  } else if (slot->var()->mode() == Variable::CONST) {
    // Const slots may contain 'the hole' value (the constant hasn't been
    // initialized yet) which needs to be converted into the 'undefined'
    // value.
    Comment cmnt(masm_, "[ Load const");
    JumpTarget exit(this);
    Result temp = allocator_->Allocate();
    ASSERT(temp.is_valid());
    __ mov(temp.reg(), SlotOperand(slot, temp.reg()));
    __ cmp(temp.reg(), Factory::the_hole_value());
    exit.Branch(not_equal, &temp);
    __ mov(temp.reg(), Factory::undefined_value());
    exit.Bind(&temp);
    frame_->Push(&temp);

  } else if (slot->type() == Slot::PARAMETER) {
    frame_->LoadParameterAt(slot->index());

  } else if (slot->type() == Slot::LOCAL) {
    frame_->LoadLocalAt(slot->index());

  } else {
    // The other remaining slot types (LOOKUP and GLOBAL) cannot reach
    // here.
    ASSERT(slot->type() == Slot::CONTEXT);
    Result temp = allocator_->Allocate();
    ASSERT(temp.is_valid());
    __ mov(temp.reg(), SlotOperand(slot, temp.reg()));
    frame_->Push(&temp);
  }
}


void CodeGenerator::StoreToSlot(Slot* slot, InitState init_state) {
  if (slot->type() == Slot::LOOKUP) {
    ASSERT(slot->var()->mode() == Variable::DYNAMIC);

    // For now, just do a runtime call.
    frame_->Push(esi);
    frame_->Push(slot->var()->name());

    Result value(this);
    if (init_state == CONST_INIT) {
      // Same as the case for a normal store, but ignores attribute
      // (e.g. READ_ONLY) of context slot so that we can initialize const
      // properties (introduced via eval("const foo = (some expr);")). Also,
      // uses the current function context instead of the top context.
      //
      // Note that we must declare the foo upon entry of eval(), via a
      // context slot declaration, but we cannot initialize it at the same
      // time, because the const declaration may be at the end of the eval
      // code (sigh...) and the const variable may have been used before
      // (where its value is 'undefined'). Thus, we can only do the
      // initialization when we actually encounter the expression and when
      // the expression operands are defined and valid, and thus we need the
      // split into 2 operations: declaration of the context slot followed
      // by initialization.
      value = frame_->CallRuntime(Runtime::kInitializeConstContextSlot, 3);
    } else {
      value = frame_->CallRuntime(Runtime::kStoreContextSlot, 3);
    }
    // Storing a variable must keep the (new) value on the expression
    // stack. This is necessary for compiling chained assignment
    // expressions.
    frame_->Push(&value);

  } else {
    ASSERT(slot->var()->mode() != Variable::DYNAMIC);

    JumpTarget exit(this);
    if (init_state == CONST_INIT) {
      ASSERT(slot->var()->mode() == Variable::CONST);
      // Only the first const initialization must be executed (the slot
      // still contains 'the hole' value). When the assignment is executed,
      // the code is identical to a normal store (see below).
      Comment cmnt(masm_, "[ Init const");
      Result temp = allocator_->Allocate();
      ASSERT(temp.is_valid());
      __ mov(temp.reg(), SlotOperand(slot, temp.reg()));
      __ cmp(temp.reg(), Factory::the_hole_value());
      temp.Unuse();
      exit.Branch(not_equal);
    }

    // We must execute the store.  Storing a variable must keep the (new)
    // value on the stack. This is necessary for compiling assignment
    // expressions.
    //
    // Note: We will reach here even with slot->var()->mode() ==
    // Variable::CONST because of const declarations which will initialize
    // consts to 'the hole' value and by doing so, end up calling this code.
    if (slot->type() == Slot::PARAMETER) {
      frame_->StoreToParameterAt(slot->index());
    } else if (slot->type() == Slot::LOCAL) {
      frame_->StoreToLocalAt(slot->index());
    } else {
      // The other slot types (LOOKUP and GLOBAL) cannot reach here.
      ASSERT(slot->type() == Slot::CONTEXT);
      frame_->Dup();
      Result value = frame_->Pop();
      value.ToRegister();
      Result start = allocator_->Allocate();
      ASSERT(start.is_valid());
      __ mov(SlotOperand(slot, start.reg()), value.reg());
      // RecordWrite may destroy the value registers.
      //
      // TODO(204): Avoid actually spilling when the value is not
      // needed (probably the common case).
      frame_->Spill(value.reg());
      int offset = FixedArray::kHeaderSize + slot->index() * kPointerSize;
      Result temp = allocator_->Allocate();
      ASSERT(temp.is_valid());
      __ RecordWrite(start.reg(), offset, value.reg(), temp.reg());
      // The results start, value, and temp are unused by going out of
      // scope.
    }

    // If we definitely did not jump over the assignment, we do not need
    // to bind the exit label.  Doing so can defeat peephole
    // optimization.
    if (exit.is_linked()) {
      exit.Bind();
    }
  }
}


void CodeGenerator::VisitSlot(Slot* node) {
  Comment cmnt(masm_, "[ Slot");
  LoadFromSlot(node, typeof_state());
}


void CodeGenerator::VisitVariableProxy(VariableProxy* node) {
  Comment cmnt(masm_, "[ VariableProxy");
  Variable* var = node->var();
  Expression* expr = var->rewrite();
  if (expr != NULL) {
    // We have to be wary of calling Visit directly on expressions.  Because
    // of special casing comparisons of the form typeof<expr> === "string",
    // we can return from a call from Visit (to a comparison or a unary
    // operation) without a virtual frame; which will probably crash if we
    // try to emit frame code before reestablishing a frame.  Here we're
    // safe as long as variable proxies can't rewrite into typeof
    // comparisons or unary logical not expressions.
    Visit(expr);
    ASSERT(has_valid_frame());
  } else {
    ASSERT(var->is_global());
    Reference ref(this, node);
    ref.GetValue(typeof_state());
  }
}


void CodeGenerator::VisitLiteral(Literal* node) {
  Comment cmnt(masm_, "[ Literal");
  if (node->handle()->IsSmi() && !IsInlineSmi(node)) {
    // To prevent long attacker-controlled byte sequences in code, larger
    // Smis are loaded in two steps via a temporary register.
    Result temp = allocator_->Allocate();
    ASSERT(temp.is_valid());
    int bits = reinterpret_cast<int>(*node->handle());
    __ Set(temp.reg(), Immediate(bits & 0x0000FFFF));
    __ xor_(temp.reg(), bits & 0xFFFF0000);
    frame_->Push(&temp);
  } else {
    frame_->Push(node->handle());
  }
}


class DeferredRegExpLiteral: public DeferredCode {
 public:
  DeferredRegExpLiteral(CodeGenerator* generator, RegExpLiteral* node)
      : DeferredCode(generator), node_(node) {
    set_comment("[ DeferredRegExpLiteral");
  }

  virtual void Generate();

 private:
  RegExpLiteral* node_;
};


void DeferredRegExpLiteral::Generate() {
  // The argument is actually passed in ecx.
  enter()->Bind();
  VirtualFrame::SpilledScope spilled_scope(generator());
  // If the entry is undefined we call the runtime system to compute the
  // literal.

  // Literal array (0).
  generator()->frame()->EmitPush(ecx);
  // Literal index (1).
  generator()->frame()->EmitPush(
      Immediate(Smi::FromInt(node_->literal_index())));
  // RegExp pattern (2).
  generator()->frame()->EmitPush(Immediate(node_->pattern()));
  // RegExp flags (3).
  generator()->frame()->EmitPush(Immediate(node_->flags()));
  generator()->frame()->CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  __ mov(ebx, Operand(eax));  // "caller" expects result in ebx
  // The result is actually returned in ebx.
  exit()->Jump();
}


void CodeGenerator::VisitRegExpLiteral(RegExpLiteral* node) {
  VirtualFrame::SpilledScope spilled_scope(this);
  Comment cmnt(masm_, "[ RegExp Literal");
  DeferredRegExpLiteral* deferred = new DeferredRegExpLiteral(this, node);

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ mov(ecx, frame_->Function());

  // Load the literals array of the function.
  __ mov(ecx, FieldOperand(ecx, JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ mov(ebx, FieldOperand(ecx, literal_offset));

  // Check whether we need to materialize the RegExp object.
  // If so, jump to the deferred code.
  __ cmp(ebx, Factory::undefined_value());
  deferred->enter()->Branch(equal, not_taken);
  deferred->exit()->Bind();

  // Push the literal.
  frame_->EmitPush(ebx);
}


// This deferred code stub will be used for creating the boilerplate
// by calling Runtime_CreateObjectLiteral.
// Each created boilerplate is stored in the JSFunction and they are
// therefore context dependent.
class DeferredObjectLiteral: public DeferredCode {
 public:
  DeferredObjectLiteral(CodeGenerator* generator,
                        ObjectLiteral* node)
      : DeferredCode(generator), node_(node) {
    set_comment("[ DeferredObjectLiteral");
  }

  virtual void Generate();

 private:
  ObjectLiteral* node_;
};


void DeferredObjectLiteral::Generate() {
  // The argument is actually passed in ecx.
  enter()->Bind();
  VirtualFrame::SpilledScope spilled_scope(generator());
  // If the entry is undefined we call the runtime system to compute
  // the literal.

  // Literal array (0).
  generator()->frame()->EmitPush(ecx);
  // Literal index (1).
  generator()->frame()->EmitPush(
      Immediate(Smi::FromInt(node_->literal_index())));
  // Constant properties (2).
  generator()->frame()->EmitPush(Immediate(node_->constant_properties()));
  generator()->frame()->CallRuntime(Runtime::kCreateObjectLiteralBoilerplate,
                                    3);
  __ mov(ebx, Operand(eax));
  // The result is actually returned in ebx.
  exit()->Jump();
}


void CodeGenerator::VisitObjectLiteral(ObjectLiteral* node) {
  VirtualFrame::SpilledScope spilled_scope(this);
  Comment cmnt(masm_, "[ ObjectLiteral");
  DeferredObjectLiteral* deferred = new DeferredObjectLiteral(this, node);

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ mov(ecx, frame_->Function());

  // Load the literals array of the function.
  __ mov(ecx, FieldOperand(ecx, JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ mov(ebx, FieldOperand(ecx, literal_offset));

  // Check whether we need to materialize the object literal boilerplate.
  // If so, jump to the deferred code.
  __ cmp(ebx, Factory::undefined_value());
  deferred->enter()->Branch(equal, not_taken);
  deferred->exit()->Bind();

  // Push the literal.
  frame_->EmitPush(ebx);
  // Clone the boilerplate object.
  frame_->CallRuntime(Runtime::kCloneObjectLiteralBoilerplate, 1);
  // Push the new cloned literal object as the result.
  frame_->EmitPush(eax);


  for (int i = 0; i < node->properties()->length(); i++) {
    ObjectLiteral::Property* property  = node->properties()->at(i);
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT: break;
      case ObjectLiteral::Property::COMPUTED: {
        Handle<Object> key(property->key()->handle());
        Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
        if (key->IsSymbol()) {
          __ mov(eax, frame_->Top());
          frame_->EmitPush(eax);
          LoadAndSpill(property->value());
          frame_->EmitPop(eax);
          __ Set(ecx, Immediate(key));
          frame_->CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
          frame_->Drop();
          // Ignore result.
          break;
        }
        // Fall through
      }
      case ObjectLiteral::Property::PROTOTYPE: {
        __ mov(eax, frame_->Top());
        frame_->EmitPush(eax);
        LoadAndSpill(property->key());
        LoadAndSpill(property->value());
        frame_->CallRuntime(Runtime::kSetProperty, 3);
        // Ignore result.
        break;
      }
      case ObjectLiteral::Property::SETTER: {
        // Duplicate the resulting object on the stack. The runtime
        // function will pop the three arguments passed in.
        __ mov(eax, frame_->Top());
        frame_->EmitPush(eax);
        LoadAndSpill(property->key());
        frame_->EmitPush(Immediate(Smi::FromInt(1)));
        LoadAndSpill(property->value());
        frame_->CallRuntime(Runtime::kDefineAccessor, 4);
        // Ignore result.
        break;
      }
      case ObjectLiteral::Property::GETTER: {
        // Duplicate the resulting object on the stack. The runtime
        // function will pop the three arguments passed in.
        __ mov(eax, frame_->Top());
        frame_->EmitPush(eax);
        LoadAndSpill(property->key());
        frame_->EmitPush(Immediate(Smi::FromInt(0)));
        LoadAndSpill(property->value());
        frame_->CallRuntime(Runtime::kDefineAccessor, 4);
        // Ignore result.
        break;
      }
      default: UNREACHABLE();
    }
  }
}


void CodeGenerator::VisitArrayLiteral(ArrayLiteral* node) {
  VirtualFrame::SpilledScope spilled_scope(this);
  Comment cmnt(masm_, "[ ArrayLiteral");

  // Call runtime to create the array literal.
  frame_->EmitPush(Immediate(node->literals()));
  // Load the function of this frame.
  __ mov(ecx, frame_->Function());
  // Load the literals array of the function.
  __ mov(ecx, FieldOperand(ecx, JSFunction::kLiteralsOffset));
  frame_->EmitPush(ecx);
  frame_->CallRuntime(Runtime::kCreateArrayLiteral, 2);

  // Push the resulting array literal on the stack.
  frame_->EmitPush(eax);

  // Generate code to set the elements in the array that are not
  // literals.
  for (int i = 0; i < node->values()->length(); i++) {
    Expression* value = node->values()->at(i);

    // If value is literal the property value is already
    // set in the boilerplate object.
    if (value->AsLiteral() == NULL) {
      // The property must be set by generated code.
      LoadAndSpill(value);

      // Get the value off the stack.
      frame_->EmitPop(eax);
      // Fetch the object literal while leaving on the stack.
      __ mov(ecx, frame_->Top());
      // Get the elements array.
      __ mov(ecx, FieldOperand(ecx, JSObject::kElementsOffset));

      // Write to the indexed properties array.
      int offset = i * kPointerSize + Array::kHeaderSize;
      __ mov(FieldOperand(ecx, offset), eax);

      // Update the write barrier for the array address.
      __ RecordWrite(ecx, offset, eax, ebx);
    }
  }
}


bool CodeGenerator::IsInlineSmi(Literal* literal) {
  if (literal == NULL || !literal->handle()->IsSmi()) return false;
  int int_value = Smi::cast(*literal->handle())->value();
  return is_intn(int_value, kMaxSmiInlinedBits);
}


void CodeGenerator::VisitAssignment(Assignment* node) {
  Comment cmnt(masm_, "[ Assignment");
  CodeForStatementPosition(node);

  { Reference target(this, node->target());
    if (target.is_illegal()) {
      // Fool the virtual frame into thinking that we left the assignment's
      // value on the frame.
      frame_->Push(Smi::FromInt(0));
      return;
    }

    if (node->op() == Token::ASSIGN ||
        node->op() == Token::INIT_VAR ||
        node->op() == Token::INIT_CONST) {
      Load(node->value());

    } else {
      VirtualFrame::SpilledScope spilled_scope(this);
      target.GetValueAndSpill(NOT_INSIDE_TYPEOF);
      Literal* literal = node->value()->AsLiteral();
      if (IsInlineSmi(literal)) {
        SmiOperation(node->binary_op(), node->type(), literal->handle(), false,
                     NO_OVERWRITE);
      } else {
        LoadAndSpill(node->value());
        GenericBinaryOperation(node->binary_op(), node->type());
      }
    }

    Variable* var = node->target()->AsVariableProxy()->AsVariable();
    if (var != NULL &&
        var->mode() == Variable::CONST &&
        node->op() != Token::INIT_VAR && node->op() != Token::INIT_CONST) {
      // Assignment ignored - leave the value on the stack.
    } else {
      CodeForSourcePosition(node->position());
      if (node->op() == Token::INIT_CONST) {
        // Dynamic constant initializations must use the function context
        // and initialize the actual constant declared. Dynamic variable
        // initializations are simply assignments and use SetValue.
        target.SetValue(CONST_INIT);
      } else {
        target.SetValue(NOT_CONST_INIT);
      }
    }
  }
}


void CodeGenerator::VisitThrow(Throw* node) {
  VirtualFrame::SpilledScope spilled_scope(this);
  Comment cmnt(masm_, "[ Throw");
  CodeForStatementPosition(node);

  LoadAndSpill(node->exception());
  frame_->CallRuntime(Runtime::kThrow, 1);
  frame_->EmitPush(eax);
}


void CodeGenerator::VisitProperty(Property* node) {
  Comment cmnt(masm_, "[ Property");
  Reference property(this, node);
  property.GetValue(typeof_state());
}


void CodeGenerator::VisitCall(Call* node) {
  Comment cmnt(masm_, "[ Call");

  ZoneList<Expression*>* args = node->arguments();

  CodeForStatementPosition(node);

  // Check if the function is a variable or a property.
  Expression* function = node->expression();
  Variable* var = function->AsVariableProxy()->AsVariable();
  Property* property = function->AsProperty();

  // ------------------------------------------------------------------------
  // Fast-case: Use inline caching.
  // ---
  // According to ECMA-262, section 11.2.3, page 44, the function to call
  // must be resolved after the arguments have been evaluated. The IC code
  // automatically handles this by loading the arguments before the function
  // is resolved in cache misses (this also holds for megamorphic calls).
  // ------------------------------------------------------------------------

  if (var != NULL && !var->is_this() && var->is_global()) {
    // ----------------------------------
    // JavaScript example: 'foo(1, 2, 3)'  // foo is global
    // ----------------------------------

    // Push the name of the function and the receiver onto the stack.
    frame_->Push(var->name());

    // Pass the global object as the receiver and let the IC stub
    // patch the stack to use the global proxy as 'this' in the
    // invoked function.
    LoadGlobal();

    // Load the arguments.
    int arg_count = args->length();
    for (int i = 0; i < arg_count; i++) {
      Load(args->at(i));
    }

    // Setup the receiver register and call the IC initialization code.
    Handle<Code> stub = (loop_nesting() > 0)
        ? ComputeCallInitializeInLoop(arg_count)
        : ComputeCallInitialize(arg_count);
    CodeForSourcePosition(node->position());
    Result result = frame_->CallCodeObject(stub,
                                           RelocInfo::CODE_TARGET_CONTEXT,
                                           arg_count + 1);
    frame_->RestoreContextRegister();

    // Replace the function on the stack with the result.
    frame_->SetElementAt(0, &result);

  } else if (var != NULL && var->slot() != NULL &&
             var->slot()->type() == Slot::LOOKUP) {
    // ----------------------------------
    // JavaScript example: 'with (obj) foo(1, 2, 3)'  // foo is in obj
    // ----------------------------------

    // Load the function
    frame_->Push(esi);
    frame_->Push(var->name());
    frame_->CallRuntime(Runtime::kLoadContextSlot, 2);
    // eax: slot value; edx: receiver

    // Load the receiver.
    frame_->Push(eax);
    frame_->Push(edx);

    // Call the function.
    CallWithArguments(args, node->position());

  } else if (property != NULL) {
    // Check if the key is a literal string.
    Literal* literal = property->key()->AsLiteral();

    if (literal != NULL && literal->handle()->IsSymbol()) {
      // ------------------------------------------------------------------
      // JavaScript example: 'object.foo(1, 2, 3)' or 'map["key"](1, 2, 3)'
      // ------------------------------------------------------------------

      // Push the name of the function and the receiver onto the stack.
      frame_->Push(literal->handle());
      Load(property->obj());

      // Load the arguments.
      int arg_count = args->length();
      for (int i = 0; i < arg_count; i++) {
        Load(args->at(i));
      }

      // Call the IC initialization code.
      Handle<Code> stub = (loop_nesting() > 0)
        ? ComputeCallInitializeInLoop(arg_count)
        : ComputeCallInitialize(arg_count);
      CodeForSourcePosition(node->position());
      Result result = frame_->CallCodeObject(stub,
                                             RelocInfo::CODE_TARGET,
                                             arg_count + 1);
      frame_->RestoreContextRegister();

      // Replace the function on the stack with the result.
      frame_->SetElementAt(0, &result);

    } else {
      // -------------------------------------------
      // JavaScript example: 'array[index](1, 2, 3)'
      // -------------------------------------------

      // Load the function to call from the property through a reference.
      Reference ref(this, property);
      ref.GetValue(NOT_INSIDE_TYPEOF);

      // Pass receiver to called function.
      // The reference's size is non-negative.
      frame_->SpillAll();
      frame_->EmitPush(frame_->ElementAt(ref.size()));

      // Call the function.
      CallWithArguments(args, node->position());
    }

  } else {
    // ----------------------------------
    // JavaScript example: 'foo(1, 2, 3)'  // foo is not global
    // ----------------------------------

    // Load the function.
    Load(function);

    // Pass the global proxy as the receiver.
    LoadGlobalReceiver();

    // Call the function.
    CallWithArguments(args, node->position());
  }
}


void CodeGenerator::VisitCallNew(CallNew* node) {
  Comment cmnt(masm_, "[ CallNew");
  CodeForStatementPosition(node);

  // According to ECMA-262, section 11.2.2, page 44, the function
  // expression in new calls must be evaluated before the
  // arguments. This is different from ordinary calls, where the
  // actual function to call is resolved after the arguments have been
  // evaluated.

  // Compute function to call and use the global object as the
  // receiver. There is no need to use the global proxy here because
  // it will always be replaced with a newly allocated object.
  Load(node->expression());
  LoadGlobal();

  // Push the arguments ("left-to-right") on the stack.
  ZoneList<Expression*>* args = node->arguments();
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    Load(args->at(i));
  }

  // TODO(205): Get rid of this spilling. It is only necessary because
  // we load the function from the non-virtual stack.
  frame_->SpillAll();

  // Constructors are called with the number of arguments in register
  // eax for now. Another option would be to have separate construct
  // call trampolines per different arguments counts encountered.
  __ Set(eax, Immediate(arg_count));

  // Load the function into temporary function slot as per calling
  // convention.
  __ mov(edi, frame_->ElementAt(arg_count + 1));

  // Call the construct call builtin that handles allocation and
  // constructor invocation.
  CodeForSourcePosition(node->position());
  Handle<Code> ic(Builtins::builtin(Builtins::JSConstructCall));
  Result result = frame_->CallCodeObject(ic,
                                         RelocInfo::CONSTRUCT_CALL,
                                         args->length() + 1);

  // Replace the function on the stack with the result.
  frame_->SetElementAt(0, &result);
}


void CodeGenerator::VisitCallEval(CallEval* node) {
  VirtualFrame::SpilledScope spilled_scope(this);
  Comment cmnt(masm_, "[ CallEval");

  // In a call to eval, we first call %ResolvePossiblyDirectEval to resolve
  // the function we need to call and the receiver of the call.
  // Then we call the resolved function using the given arguments.

  ZoneList<Expression*>* args = node->arguments();
  Expression* function = node->expression();

  CodeForStatementPosition(node);

  // Prepare stack for call to resolved function.
  LoadAndSpill(function);

  // Allocate a frame slot for the receiver.
  frame_->EmitPush(Immediate(Factory::undefined_value()));
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    LoadAndSpill(args->at(i));
  }

  // Prepare stack for call to ResolvePossiblyDirectEval.
  frame_->EmitPush(frame_->ElementAt(arg_count + 1));
  if (arg_count > 0) {
    frame_->EmitPush(frame_->ElementAt(arg_count));
  } else {
    frame_->EmitPush(Immediate(Factory::undefined_value()));
  }

  // Resolve the call.
  frame_->CallRuntime(Runtime::kResolvePossiblyDirectEval, 2);

  // Touch up stack with the right values for the function and the receiver.
  __ mov(edx, FieldOperand(eax, FixedArray::kHeaderSize));
  __ mov(frame_->ElementAt(arg_count + 1), edx);
  __ mov(edx, FieldOperand(eax, FixedArray::kHeaderSize + kPointerSize));
  __ mov(frame_->ElementAt(arg_count), edx);

  // Call the function.
  CodeForSourcePosition(node->position());

  CallFunctionStub call_function(arg_count);
  frame_->CallStub(&call_function, arg_count + 1);

  // Restore context and pop function from the stack.
  frame_->RestoreContextRegister();
  __ mov(frame_->Top(), eax);
}


void CodeGenerator::GenerateIsSmi(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  LoadAndSpill(args->at(0));
  frame_->EmitPop(eax);
  __ test(eax, Immediate(kSmiTagMask));
  true_target()->Branch(zero);
  false_target()->Jump();
}


void CodeGenerator::GenerateLog(ZoneList<Expression*>* args) {
  // Conditionally generate a log call.
  // Args:
  //   0 (literal string): The type of logging (corresponds to the flags).
  //     This is used to determine whether or not to generate the log call.
  //   1 (string): Format string.  Access the string at argument index 2
  //     with '%2s' (see Logger::LogRuntime for all the formats).
  //   2 (array): Arguments to the format string.
  ASSERT_EQ(args->length(), 3);
#ifdef ENABLE_LOGGING_AND_PROFILING
  if (ShouldGenerateLog(args->at(0))) {
    LoadAndSpill(args->at(1));
    LoadAndSpill(args->at(2));
    frame_->CallRuntime(Runtime::kLog, 2);
  }
#endif
  // Finally, we're expected to leave a value on the top of the stack.
  frame_->EmitPush(Immediate(Factory::undefined_value()));
}


void CodeGenerator::GenerateIsNonNegativeSmi(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  LoadAndSpill(args->at(0));
  frame_->EmitPop(eax);
  __ test(eax, Immediate(kSmiTagMask | 0x80000000));
  true_target()->Branch(zero);
  false_target()->Jump();
}


// This generates code that performs a charCodeAt() call or returns
// undefined in order to trigger the slow case, Runtime_StringCharCodeAt.
// It can handle flat and sliced strings, 8 and 16 bit characters and
// cons strings where the answer is found in the left hand branch of the
// cons.  The slow case will flatten the string, which will ensure that
// the answer is in the left hand side the next time around.
void CodeGenerator::GenerateFastCharCodeAt(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);

  JumpTarget slow_case(this);
  JumpTarget end(this);
  JumpTarget not_a_flat_string(this);
  JumpTarget not_a_cons_string_either(this);
  JumpTarget try_again_with_new_string(this, JumpTarget::BIDIRECTIONAL);
  JumpTarget ascii_string(this);
  JumpTarget got_char_code(this);

  // Load the string into eax and the index into ebx.
  LoadAndSpill(args->at(0));
  LoadAndSpill(args->at(1));
  frame_->EmitPop(ebx);
  frame_->EmitPop(eax);
  // If the receiver is a smi return undefined.
  ASSERT(kSmiTag == 0);
  __ test(eax, Immediate(kSmiTagMask));
  slow_case.Branch(zero, not_taken);

  // Check for negative or non-smi index.
  ASSERT(kSmiTag == 0);
  __ test(ebx, Immediate(kSmiTagMask | 0x80000000));
  slow_case.Branch(not_zero, not_taken);
  // Get rid of the smi tag on the index.
  __ sar(ebx, kSmiTagSize);

  try_again_with_new_string.Bind();
  // Get the type of the heap object into edi.
  __ mov(edx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(edi, FieldOperand(edx, Map::kInstanceTypeOffset));
  // We don't handle non-strings.
  __ test(edi, Immediate(kIsNotStringMask));
  slow_case.Branch(not_zero, not_taken);

  // Here we make assumptions about the tag values and the shifts needed.
  // See the comment in objects.h.
  ASSERT(kLongStringTag == 0);
  ASSERT(kMediumStringTag + String::kLongLengthShift ==
             String::kMediumLengthShift);
  ASSERT(kShortStringTag + String::kLongLengthShift ==
             String::kShortLengthShift);
  __ mov(ecx, Operand(edi));
  __ and_(ecx, kStringSizeMask);
  __ add(Operand(ecx), Immediate(String::kLongLengthShift));
  // Get the length field.
  __ mov(edx, FieldOperand(eax, String::kLengthOffset));
  __ shr(edx);  // ecx is implicit operand.
  // edx is now the length of the string.

  // Check for index out of range.
  __ cmp(ebx, Operand(edx));
  slow_case.Branch(greater_equal, not_taken);

  // We need special handling for non-flat strings.
  ASSERT(kSeqStringTag == 0);
  __ test(edi, Immediate(kStringRepresentationMask));
  not_a_flat_string.Branch(not_zero, not_taken);

  // Check for 1-byte or 2-byte string.
  __ test(edi, Immediate(kStringEncodingMask));
  ascii_string.Branch(not_zero, taken);

  // 2-byte string.
  // Load the 2-byte character code.
  __ movzx_w(eax,
             FieldOperand(eax, ebx, times_2, SeqTwoByteString::kHeaderSize));
  got_char_code.Jump();

  // ASCII string.
  ascii_string.Bind();
  // Load the byte.
  __ movzx_b(eax, FieldOperand(eax, ebx, times_1, SeqAsciiString::kHeaderSize));

  got_char_code.Bind();
  ASSERT(kSmiTag == 0);
  __ shl(eax, kSmiTagSize);
  frame_->EmitPush(eax);
  end.Jump();

  // Handle non-flat strings.
  not_a_flat_string.Bind();
  __ and_(edi, kStringRepresentationMask);
  __ cmp(edi, kConsStringTag);
  not_a_cons_string_either.Branch(not_equal, not_taken);

  // ConsString.
  // Get the first of the two strings.
  __ mov(eax, FieldOperand(eax, ConsString::kFirstOffset));
  try_again_with_new_string.Jump();

  not_a_cons_string_either.Bind();
  __ cmp(edi, kSlicedStringTag);
  slow_case.Branch(not_equal, not_taken);

  // SlicedString.
  // Add the offset to the index.
  __ add(ebx, FieldOperand(eax, SlicedString::kStartOffset));
  slow_case.Branch(overflow);
  // Get the underlying string.
  __ mov(eax, FieldOperand(eax, SlicedString::kBufferOffset));
  try_again_with_new_string.Jump();

  slow_case.Bind();
  frame_->EmitPush(Immediate(Factory::undefined_value()));

  end.Bind();
}


void CodeGenerator::GenerateIsArray(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  LoadAndSpill(args->at(0));
  // We need the CC bits to come out as not_equal in the case where the
  // object is a smi.  This can't be done with the usual test opcode so
  // we copy the object to ecx and do some destructive ops on it that
  // result in the right CC bits.
  frame_->EmitPop(eax);
  __ mov(ecx, Operand(eax));
  __ and_(ecx, kSmiTagMask);
  __ xor_(ecx, kSmiTagMask);
  false_target()->Branch(not_equal);
  // It is a heap object - get map.
  __ mov(eax, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(eax, FieldOperand(eax, Map::kInstanceTypeOffset));
  // Check if the object is a JS array or not.
  __ cmp(eax, JS_ARRAY_TYPE);
  true_target()->Branch(equal);
  false_target()->Jump();
}


void CodeGenerator::GenerateArgumentsLength(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 0);

  // Seed the result with the formal parameters count, which will be
  // used in case no arguments adaptor frame is found below the
  // current frame.
  __ Set(eax, Immediate(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to the arguments.length.
  ArgumentsAccessStub stub(ArgumentsAccessStub::READ_LENGTH);
  frame_->CallStub(&stub, 0);
  frame_->EmitPush(eax);
}


void CodeGenerator::GenerateValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  JumpTarget leave(this);
  LoadAndSpill(args->at(0));  // Load the object.
  __ mov(eax, frame_->Top());
  // if (object->IsSmi()) return object.
  __ test(eax, Immediate(kSmiTagMask));
  leave.Branch(zero, taken);
  // It is a heap object - get map.
  __ mov(ecx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return object.
  __ cmp(ecx, JS_VALUE_TYPE);
  leave.Branch(not_equal, not_taken);
  __ mov(eax, FieldOperand(eax, JSValue::kValueOffset));
  __ mov(frame_->Top(), eax);
  leave.Bind();
}


void CodeGenerator::GenerateSetValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);
  JumpTarget leave(this);
  LoadAndSpill(args->at(0));  // Load the object.
  LoadAndSpill(args->at(1));  // Load the value.
  __ mov(eax, frame_->ElementAt(1));
  __ mov(ecx, frame_->Top());
  // if (object->IsSmi()) return object.
  __ test(eax, Immediate(kSmiTagMask));
  leave.Branch(zero, taken);
  // It is a heap object - get map.
  __ mov(ebx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ebx, FieldOperand(ebx, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return object.
  __ cmp(ebx, JS_VALUE_TYPE);
  leave.Branch(not_equal, not_taken);
  // Store the value.
  __ mov(FieldOperand(eax, JSValue::kValueOffset), ecx);
  // Update the write barrier.
  __ RecordWrite(eax, JSValue::kValueOffset, ecx, ebx);
  // Leave.
  leave.Bind();
  __ mov(ecx, frame_->Top());
  frame_->Drop();
  __ mov(frame_->Top(), ecx);
}


void CodeGenerator::GenerateArgumentsAccess(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);

  // Load the key onto the stack and set register eax to the formal
  // parameters count for the currently executing function.
  LoadAndSpill(args->at(0));
  __ Set(eax, Immediate(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to arguments[key].
  ArgumentsAccessStub stub(ArgumentsAccessStub::READ_ELEMENT);
  frame_->CallStub(&stub, 0);
  __ mov(frame_->Top(), eax);
}


void CodeGenerator::GenerateObjectEquals(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);

  // Load the two objects into registers and perform the comparison.
  LoadAndSpill(args->at(0));
  LoadAndSpill(args->at(1));
  frame_->EmitPop(eax);
  frame_->EmitPop(ecx);
  __ cmp(eax, Operand(ecx));
  true_target()->Branch(equal);
  false_target()->Jump();
}


void CodeGenerator::VisitCallRuntime(CallRuntime* node) {
  VirtualFrame::SpilledScope spilled_scope(this);
  if (CheckForInlineRuntimeCall(node)) {
    return;
  }

  ZoneList<Expression*>* args = node->arguments();
  Comment cmnt(masm_, "[ CallRuntime");
  Runtime::Function* function = node->function();

  if (function == NULL) {
    // Prepare stack for calling JS runtime function.
    frame_->EmitPush(Immediate(node->name()));
    // Push the builtins object found in the current global object.
    __ mov(edx, GlobalObject());
    frame_->EmitPush(FieldOperand(edx, GlobalObject::kBuiltinsOffset));
  }

  // Push the arguments ("left-to-right").
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    LoadAndSpill(args->at(i));
  }

  if (function == NULL) {
    // Call the JS runtime function.
    Handle<Code> stub = ComputeCallInitialize(arg_count);
    __ Set(eax, Immediate(args->length()));
    frame_->CallCodeObject(stub, RelocInfo::CODE_TARGET, arg_count + 1);
    frame_->RestoreContextRegister();
    __ mov(frame_->Top(), eax);
  } else {
    // Call the C runtime function.
    frame_->CallRuntime(function, arg_count);
    frame_->EmitPush(eax);
  }
}


void CodeGenerator::VisitUnaryOperation(UnaryOperation* node) {
  // Note that because of NOT and an optimization in comparison of a typeof
  // expression to a literal string, this function can fail to leave a value
  // on top of the frame or in the cc register.
  Comment cmnt(masm_, "[ UnaryOperation");

  Token::Value op = node->op();

  if (op == Token::NOT) {
    LoadCondition(node->expression(), NOT_INSIDE_TYPEOF,
                  false_target(), true_target(), true);

  } else if (op == Token::DELETE) {
    Property* property = node->expression()->AsProperty();
    if (property != NULL) {
      Load(property->obj());
      Load(property->key());
      Result answer = frame_->InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION, 2);
      frame_->Push(&answer);
      return;
    }

    Variable* variable = node->expression()->AsVariableProxy()->AsVariable();
    if (variable != NULL) {
      Slot* slot = variable->slot();
      if (variable->is_global()) {
        LoadGlobal();
        frame_->Push(variable->name());
        Result answer = frame_->InvokeBuiltin(Builtins::DELETE,
                                              CALL_FUNCTION, 2);
        frame_->Push(&answer);
        return;

      } else if (slot != NULL && slot->type() == Slot::LOOKUP) {
        // lookup the context holding the named variable
        frame_->Push(esi);
        frame_->Push(variable->name());
        Result context = frame_->CallRuntime(Runtime::kLookupContext, 2);
        frame_->Push(&context);
        frame_->Push(variable->name());
        Result answer = frame_->InvokeBuiltin(Builtins::DELETE,
                                              CALL_FUNCTION, 2);
        frame_->Push(&answer);
        return;
      }

      // Default: Result of deleting non-global, not dynamically
      // introduced variables is false.
      frame_->Push(Factory::false_value());

    } else {
      // Default: Result of deleting expressions is true.
      Load(node->expression());  // may have side-effects
      frame_->SetElementAt(0, Factory::true_value());
    }

  } else if (op == Token::TYPEOF) {
    // Special case for loading the typeof expression; see comment on
    // LoadTypeofExpression().
    LoadTypeofExpression(node->expression());
    Result answer = frame_->CallRuntime(Runtime::kTypeof, 1);
    frame_->Push(&answer);

  } else {
    Load(node->expression());
    switch (op) {
      case Token::NOT:
      case Token::DELETE:
      case Token::TYPEOF:
        UNREACHABLE();  // handled above
        break;

      case Token::SUB: {
        UnarySubStub stub;
        // TODO(1222589): remove dependency of TOS being cached inside stub
        Result operand = frame_->Pop();
        operand.ToRegister(eax);
        Result answer = frame_->CallStub(&stub, &operand, 0);
        frame_->Push(&answer);
        break;
      }

      case Token::BIT_NOT: {
        // Smi check.
        JumpTarget smi_label(this);
        JumpTarget continue_label(this);
        Result operand = frame_->Pop();
        operand.ToRegister();
        __ test(operand.reg(), Immediate(kSmiTagMask));
        smi_label.Branch(zero, &operand, taken);

        frame_->Push(&operand);  // undo popping of TOS
        Result answer = frame_->InvokeBuiltin(Builtins::BIT_NOT,
                                              CALL_FUNCTION, 1);

        continue_label.Jump(&answer);
        smi_label.Bind(&answer);
        answer.ToRegister();
        frame_->Spill(answer.reg());
        __ not_(answer.reg());
        __ and_(answer.reg(), ~kSmiTagMask);  // Remove inverted smi-tag.
        continue_label.Bind(&answer);
        frame_->Push(&answer);
        break;
      }

      case Token::VOID: {
        frame_->SetElementAt(0, Factory::undefined_value());
        break;
      }

      case Token::ADD: {
        // Smi check.
        JumpTarget continue_label(this);
        Result operand = frame_->Pop();
        operand.ToRegister();
        __ test(operand.reg(), Immediate(kSmiTagMask));
        continue_label.Branch(zero, &operand, taken);

        frame_->Push(&operand);
        Result answer = frame_->InvokeBuiltin(Builtins::TO_NUMBER,
                                              CALL_FUNCTION, 1);

        continue_label.Bind(&answer);
        frame_->Push(&answer);
        break;
      }

      default:
        UNREACHABLE();
    }
  }
}


class DeferredCountOperation: public DeferredCode {
 public:
  DeferredCountOperation(CodeGenerator* generator,
                         bool is_postfix,
                         bool is_increment,
                         int result_offset)
      : DeferredCode(generator),
        is_postfix_(is_postfix),
        is_increment_(is_increment),
        result_offset_(result_offset) {
    set_comment("[ DeferredCountOperation");
  }

  virtual void Generate();

 private:
  bool is_postfix_;
  bool is_increment_;
  int result_offset_;
};


class RevertToNumberStub: public CodeStub {
 public:
  explicit RevertToNumberStub(bool is_increment)
     :  is_increment_(is_increment) { }

 private:
  bool is_increment_;

  Major MajorKey() { return RevertToNumber; }
  int MinorKey() { return is_increment_ ? 1 : 0; }
  void Generate(MacroAssembler* masm);

#ifdef DEBUG
  void Print() {
    PrintF("RevertToNumberStub (is_increment %s)\n",
           is_increment_ ? "true" : "false");
  }
#endif
};


class CounterOpStub: public CodeStub {
 public:
  CounterOpStub(int result_offset, bool is_postfix, bool is_increment)
     :  result_offset_(result_offset),
        is_postfix_(is_postfix),
        is_increment_(is_increment) { }

 private:
  int result_offset_;
  bool is_postfix_;
  bool is_increment_;

  Major MajorKey() { return CounterOp; }
  int MinorKey() {
    return ((result_offset_ << 2) |
            (is_postfix_ ? 2 : 0) |
            (is_increment_ ? 1 : 0));
  }
  void Generate(MacroAssembler* masm);

#ifdef DEBUG
  void Print() {
    PrintF("CounterOpStub (result_offset %d), (is_postfix %s),"
           " (is_increment %s)\n",
           result_offset_,
           is_postfix_ ? "true" : "false",
           is_increment_ ? "true" : "false");
  }
#endif
};


void DeferredCountOperation::Generate() {
  CodeGenerator* cgen = generator();

  Result value(cgen);
  enter()->Bind(&value);
  value.ToRegister(eax);  // The stubs below expect their argument in eax.

  if (is_postfix_) {
    RevertToNumberStub to_number_stub(is_increment_);
    value = generator()->frame()->CallStub(&to_number_stub, &value, 0);
  }

  CounterOpStub stub(result_offset_, is_postfix_, is_increment_);
  value = generator()->frame()->CallStub(&stub, &value, 0);
  exit()->Jump(&value);
}


void CodeGenerator::VisitCountOperation(CountOperation* node) {
  Comment cmnt(masm_, "[ CountOperation");

  bool is_postfix = node->is_postfix();
  bool is_increment = node->op() == Token::INC;

  Variable* var = node->expression()->AsVariableProxy()->AsVariable();
  bool is_const = (var != NULL && var->mode() == Variable::CONST);

  // Postfix: Make room for the result.
  if (is_postfix) {
    frame_->Push(Smi::FromInt(0));
  }

  { Reference target(this, node->expression());
    if (target.is_illegal()) {
      // Spoof the virtual frame to have the expected height (one higher
      // than on entry).
      if (!is_postfix) {
        frame_->Push(Smi::FromInt(0));
      }
      return;
    }
    target.TakeValue(NOT_INSIDE_TYPEOF);

    DeferredCountOperation* deferred =
        new DeferredCountOperation(this, is_postfix, is_increment,
                                   target.size() * kPointerSize);

    Result value = frame_->Pop();
    value.ToRegister();
    ASSERT(value.is_valid());

    // Postfix: Store the old value as the result.
    if (is_postfix) {
      Result old_value = value;
      frame_->SetElementAt(target.size(), &old_value);
    }

    // Perform optimistic increment/decrement.  Ensure the value is
    // writable.
    frame_->Spill(value.reg());
    ASSERT(allocator_->count(value.reg()) == 1);

    // In order to combine the overflow and the smi check, we need to
    // be able to allocate a byte register.  We attempt to do so
    // without spilling.  If we fail, we will generate separate
    // overflow and smi checks.
    //
    // We need to allocate and clear the temporary byte register
    // before performing the count operation since clearing the
    // register using xor will clear the overflow flag.
    Result tmp = allocator_->AllocateByteRegisterWithoutSpilling();
    if (tmp.is_valid()) {
      __ Set(tmp.reg(), Immediate(0));
    }

    if (is_increment) {
      __ add(Operand(value.reg()), Immediate(Smi::FromInt(1)));
    } else {
      __ sub(Operand(value.reg()), Immediate(Smi::FromInt(1)));
    }

    // If the count operation didn't overflow and the result is a
    // valid smi, we're done. Otherwise, we jump to the deferred
    // slow-case code.
    //
    // We combine the overflow and the smi check if we could
    // successfully allocate a temporary byte register.
    if (tmp.is_valid()) {
      __ setcc(overflow, tmp.reg());
      __ or_(Operand(value.reg()), tmp.reg());
      tmp.Unuse();
      __ test(value.reg(), Immediate(kSmiTagMask));
      deferred->enter()->Branch(not_zero, &value, not_taken);
    } else {
      deferred->enter()->Branch(overflow, &value, not_taken);
      __ test(value.reg(), Immediate(kSmiTagMask));
      deferred->enter()->Branch(not_zero, &value, not_taken);
    }

    // Store the new value in the target if not const.
    deferred->exit()->Bind(&value);
    frame_->Push(&value);
    if (!is_const) {
      target.SetValue(NOT_CONST_INIT);
    }
  }

  // Postfix: Discard the new value and use the old.
  if (is_postfix) {
    frame_->Drop();
  }
}


void CodeGenerator::VisitBinaryOperation(BinaryOperation* node) {
  // Note that due to an optimization in comparison operations (typeof
  // compared to a string literal), we can evaluate a binary expression such
  // as AND or OR and not leave a value on the frame or in the cc register.
  Comment cmnt(masm_, "[ BinaryOperation");
  Token::Value op = node->op();

  // According to ECMA-262 section 11.11, page 58, the binary logical
  // operators must yield the result of one of the two expressions
  // before any ToBoolean() conversions. This means that the value
  // produced by a && or || operator is not necessarily a boolean.

  // NOTE: If the left hand side produces a materialized value (not in
  // the CC register), we force the right hand side to do the
  // same. This is necessary because we may have to branch to the exit
  // after evaluating the left hand side (due to the shortcut
  // semantics), but the compiler must (statically) know if the result
  // of compiling the binary operation is materialized or not.

  if (op == Token::AND) {
    JumpTarget is_true(this);
    LoadCondition(node->left(), NOT_INSIDE_TYPEOF,
                  &is_true, false_target(), false);
    if (!has_valid_frame()) {
      if (is_true.is_linked()) {
        // Evaluate right side expression.
        is_true.Bind();
        LoadCondition(node->right(), NOT_INSIDE_TYPEOF,
                      true_target(), false_target(), false);
      }
    } else {
      // We have a materialized value on the frame.
      JumpTarget pop_and_continue(this);
      JumpTarget exit(this);

      // Avoid popping the result if it converts to 'false' using the
      // standard ToBoolean() conversion as described in ECMA-262, section
      // 9.2, page 30.
      //
      // Duplicate the TOS value. The duplicate will be popped by ToBoolean.
      frame_->Dup();
      ToBoolean(&pop_and_continue, &exit);

      // Pop the result of evaluating the first part.
      pop_and_continue.Bind();
      frame_->Drop();

      // Evaluate right side expression.
      is_true.Bind();
      Load(node->right());

      // Exit (always with a materialized value).
      exit.Bind();
    }

  } else if (op == Token::OR) {
    JumpTarget is_false(this);
    LoadCondition(node->left(), NOT_INSIDE_TYPEOF,
                  true_target(), &is_false, false);
    if (!has_valid_frame()) {
      if (is_false.is_linked()) {
        // Evaluate right side expression.
        is_false.Bind();
        LoadCondition(node->right(), NOT_INSIDE_TYPEOF,
                      true_target(), false_target(), false);
      }
    } else {
      // We have a materialized value on the frame.
      JumpTarget pop_and_continue(this);
      JumpTarget exit(this);

      // Avoid popping the result if it converts to 'true' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      // Duplicate the TOS value. The duplicate will be popped by ToBoolean.
      frame_->Dup();
      ToBoolean(&exit, &pop_and_continue);

      // Pop the result of evaluating the first part.
      pop_and_continue.Bind();
      frame_->Drop();

      // Evaluate right side expression.
      is_false.Bind();
      Load(node->right());

      // Exit (always with a materialized value).
      exit.Bind();
    }

  } else {
    // NOTE: The code below assumes that the slow cases (calls to runtime)
    // never return a constant/immutable object.
    OverwriteMode overwrite_mode = NO_OVERWRITE;
    if (node->left()->AsBinaryOperation() != NULL &&
        node->left()->AsBinaryOperation()->ResultOverwriteAllowed()) {
      overwrite_mode = OVERWRITE_LEFT;
    } else if (node->right()->AsBinaryOperation() != NULL &&
               node->right()->AsBinaryOperation()->ResultOverwriteAllowed()) {
      overwrite_mode = OVERWRITE_RIGHT;
    }

    // Optimize for the case where (at least) one of the expressions
    // is a literal small integer.
    Literal* lliteral = node->left()->AsLiteral();
    Literal* rliteral = node->right()->AsLiteral();

    if (IsInlineSmi(rliteral)) {
      Load(node->left());
      SmiOperation(node->op(), node->type(), rliteral->handle(), false,
                   overwrite_mode);
    } else if (IsInlineSmi(lliteral)) {
      Load(node->right());
      SmiOperation(node->op(), node->type(), lliteral->handle(), true,
                   overwrite_mode);
    } else {
      Load(node->left());
      Load(node->right());
      GenericBinaryOperation(node->op(), node->type(), overwrite_mode);
    }
  }
}


void CodeGenerator::VisitThisFunction(ThisFunction* node) {
  VirtualFrame::SpilledScope spilled_scope(this);
  frame_->EmitPush(frame_->Function());
}


class InstanceofStub: public CodeStub {
 public:
  InstanceofStub() { }

  void Generate(MacroAssembler* masm);

 private:
  Major MajorKey() { return Instanceof; }
  int MinorKey() { return 0; }
};


void CodeGenerator::VisitCompareOperation(CompareOperation* node) {
  Comment cmnt(masm_, "[ CompareOperation");

  // Get the expressions from the node.
  Expression* left = node->left();
  Expression* right = node->right();
  Token::Value op = node->op();

  // To make null checks efficient, we check if either left or right is the
  // literal 'null'. If so, we optimize the code by inlining a null check
  // instead of calling the (very) general runtime routine for checking
  // equality.
  if (op == Token::EQ || op == Token::EQ_STRICT) {
    bool left_is_null =
        left->AsLiteral() != NULL && left->AsLiteral()->IsNull();
    bool right_is_null =
        right->AsLiteral() != NULL && right->AsLiteral()->IsNull();
    // The 'null' value can only be equal to 'null' or 'undefined'.
    if (left_is_null || right_is_null) {
      Load(left_is_null ? right : left);
      Result operand = frame_->Pop();
      operand.ToRegister();
      __ cmp(operand.reg(), Factory::null_value());
      Condition cc = equal;

      // The 'null' value is only equal to 'undefined' if using non-strict
      // comparisons.
      if (op != Token::EQ_STRICT) {
        true_target()->Branch(cc);
        __ cmp(operand.reg(), Factory::undefined_value());
        true_target()->Branch(equal);
        __ test(operand.reg(), Immediate(kSmiTagMask));
        false_target()->Branch(equal);

        // It can be an undetectable object.
        // Use a scratch register in preference to spilling operand.reg().
        Result temp = allocator()->Allocate();
        ASSERT(temp.is_valid());
        __ mov(temp.reg(),
               FieldOperand(operand.reg(), HeapObject::kMapOffset));
        __ movzx_b(temp.reg(),
                   FieldOperand(temp.reg(), Map::kBitFieldOffset));
        __ test(temp.reg(), Immediate(1 << Map::kIsUndetectable));
        cc = not_zero;
      }
      operand.Unuse();
      true_target()->Branch(cc);
      false_target()->Jump();
      return;
    }
  }

  // To make typeof testing for natives implemented in JavaScript really
  // efficient, we generate special code for expressions of the form:
  // 'typeof <expression> == <string>'.
  UnaryOperation* operation = left->AsUnaryOperation();
  if ((op == Token::EQ || op == Token::EQ_STRICT) &&
      (operation != NULL && operation->op() == Token::TYPEOF) &&
      (right->AsLiteral() != NULL &&
       right->AsLiteral()->handle()->IsString())) {
    Handle<String> check(String::cast(*right->AsLiteral()->handle()));

    // Load the operand and move it to a register.
    LoadTypeofExpression(operation->expression());
    Result answer = frame_->Pop();
    answer.ToRegister();

    if (check->Equals(Heap::number_symbol())) {
      __ test(answer.reg(), Immediate(kSmiTagMask));
      true_target()->Branch(zero);
      frame_->Spill(answer.reg());
      __ mov(answer.reg(), FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ cmp(answer.reg(), Factory::heap_number_map());
      answer.Unuse();
      true_target()->Branch(equal);
      false_target()->Jump();

    } else if (check->Equals(Heap::string_symbol())) {
      __ test(answer.reg(), Immediate(kSmiTagMask));
      false_target()->Branch(zero);

      // It can be an undetectable string object.
      Result temp = allocator()->Allocate();
      ASSERT(temp.is_valid());
      __ mov(temp.reg(), FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ movzx_b(temp.reg(), FieldOperand(temp.reg(), Map::kBitFieldOffset));
      __ test(temp.reg(), Immediate(1 << Map::kIsUndetectable));
      false_target()->Branch(not_zero);
      __ mov(temp.reg(), FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ movzx_b(temp.reg(),
                 FieldOperand(temp.reg(), Map::kInstanceTypeOffset));
      __ cmp(temp.reg(), FIRST_NONSTRING_TYPE);
      temp.Unuse();
      answer.Unuse();
      true_target()->Branch(less);
      false_target()->Jump();

    } else if (check->Equals(Heap::boolean_symbol())) {
      __ cmp(answer.reg(), Factory::true_value());
      true_target()->Branch(equal);
      __ cmp(answer.reg(), Factory::false_value());
      answer.Unuse();
      true_target()->Branch(equal);
      false_target()->Jump();

    } else if (check->Equals(Heap::undefined_symbol())) {
      __ cmp(answer.reg(), Factory::undefined_value());
      true_target()->Branch(equal);

      __ test(answer.reg(), Immediate(kSmiTagMask));
      false_target()->Branch(zero);

      // It can be an undetectable object.
      frame_->Spill(answer.reg());
      __ mov(answer.reg(), FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ movzx_b(answer.reg(),
                 FieldOperand(answer.reg(), Map::kBitFieldOffset));
      __ test(answer.reg(), Immediate(1 << Map::kIsUndetectable));
      answer.Unuse();
      true_target()->Branch(not_zero);
      false_target()->Jump();

    } else if (check->Equals(Heap::function_symbol())) {
      __ test(answer.reg(), Immediate(kSmiTagMask));
      false_target()->Branch(zero);
      frame_->Spill(answer.reg());
      __ mov(answer.reg(), FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ movzx_b(answer.reg(),
                 FieldOperand(answer.reg(), Map::kInstanceTypeOffset));
      __ cmp(answer.reg(), JS_FUNCTION_TYPE);
      answer.Unuse();
      true_target()->Branch(equal);
      false_target()->Jump();

    } else if (check->Equals(Heap::object_symbol())) {
      __ test(answer.reg(), Immediate(kSmiTagMask));
      false_target()->Branch(zero);
      __ cmp(answer.reg(), Factory::null_value());
      true_target()->Branch(equal);

      // It can be an undetectable object.
      Result map = allocator()->Allocate();
      ASSERT(map.is_valid());
      __ mov(map.reg(), FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ movzx_b(map.reg(), FieldOperand(map.reg(), Map::kBitFieldOffset));
      __ test(map.reg(), Immediate(1 << Map::kIsUndetectable));
      false_target()->Branch(not_zero);
      __ mov(map.reg(), FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ movzx_b(map.reg(), FieldOperand(map.reg(), Map::kInstanceTypeOffset));
      __ cmp(map.reg(), FIRST_JS_OBJECT_TYPE);
      false_target()->Branch(less);
      __ cmp(map.reg(), LAST_JS_OBJECT_TYPE);
      answer.Unuse();
      map.Unuse();
      true_target()->Branch(less_equal);
      false_target()->Jump();
    } else {
      // Uncommon case: typeof testing against a string literal that is
      // never returned from the typeof operator.
      answer.Unuse();
      false_target()->Jump();
    }
    return;
  }

  Condition cc = no_condition;
  bool strict = false;
  switch (op) {
    case Token::EQ_STRICT:
      strict = true;
      // Fall through
    case Token::EQ:
      cc = equal;
      break;
    case Token::LT:
      cc = less;
      break;
    case Token::GT:
      cc = greater;
      break;
    case Token::LTE:
      cc = less_equal;
      break;
    case Token::GTE:
      cc = greater_equal;
      break;
    case Token::IN: {
      Load(left);
      Load(right);
      Result answer = frame_->InvokeBuiltin(Builtins::IN, CALL_FUNCTION, 2);
      frame_->Push(&answer);  // push the result
      return;
    }
    case Token::INSTANCEOF: {
      Load(left);
      Load(right);
      InstanceofStub stub;
      Result answer = frame_->CallStub(&stub, 2);
      answer.ToRegister();
      __ test(answer.reg(), Operand(answer.reg()));
      answer.Unuse();
      true_target()->Branch(zero);
      false_target()->Jump();
      return;
    }
    default:
      UNREACHABLE();
  }

  // Optimize for the case where (at least) one of the expressions
  // is a literal small integer.
  if (IsInlineSmi(left->AsLiteral())) {
    Load(right);
    SmiComparison(ReverseCondition(cc), left->AsLiteral()->handle(), strict);
  } else if (IsInlineSmi(right->AsLiteral())) {
    Load(left);
    SmiComparison(cc, right->AsLiteral()->handle(), strict);
  } else {
    Load(left);
    Load(right);
    Comparison(cc, strict, true_target(), false_target());
  }
}


#ifdef DEBUG
bool CodeGenerator::HasValidEntryRegisters() {
  return (allocator()->count(eax) == frame()->register_count(eax))
      && (allocator()->count(ebx) == frame()->register_count(ebx))
      && (allocator()->count(ecx) == frame()->register_count(ecx))
      && (allocator()->count(edx) == frame()->register_count(edx))
      && (allocator()->count(edi) == frame()->register_count(edi));
}
#endif


class DeferredReferenceGetKeyedValue: public DeferredCode {
 public:
  DeferredReferenceGetKeyedValue(CodeGenerator* generator, bool is_global)
      : DeferredCode(generator), is_global_(is_global) {
    set_comment("[ DeferredReferenceGetKeyedValue");
  }

  virtual void Generate();

  Label* patch_site() { return &patch_site_; }

 private:
  Label patch_site_;
  bool is_global_;
};


void DeferredReferenceGetKeyedValue::Generate() {
  CodeGenerator* cgen = generator();
  Handle<Code> ic(Builtins::builtin(Builtins::KeyedLoadIC_Initialize));
  Result receiver(cgen);
  Result key(cgen);
  enter()->Bind(&receiver, &key);
  cgen->frame()->Push(&receiver);  // First IC argument.
  cgen->frame()->Push(&key);       // Second IC argument.

  // Calculate the delta from the IC call instruction to the map check
  // cmp instruction in the inlined version.  This delta is stored in
  // a test(eax, delta) instruction after the call so that we can find
  // it in the IC initialization code and patch the cmp instruction.
  // This means that we cannot allow test instructions after calls to
  // KeyedLoadIC stubs in other places.
  //
  // The virtual frame should be spilled fully before the call so that
  // the call itself does not generate extra code to spill values,
  // which would invalidate the delta calculation.
  cgen->frame()->SpillAll();
  int delta_to_patch_site = __ SizeOfCodeGeneratedSince(patch_site());
  Result value(cgen);
  if (is_global_) {
    value = cgen->frame()->CallCodeObject(ic,
                                          RelocInfo::CODE_TARGET_CONTEXT,
                                          0);
  } else {
    value = cgen->frame()->CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
  }
  // The result needs to be specifically the eax register because the
  // offset to the patch site will be expected in a test eax
  // instruction.
  ASSERT(value.is_register() && value.reg().is(eax));
  __ test(value.reg(), Immediate(-delta_to_patch_site));
  __ IncrementCounter(&Counters::keyed_load_inline_miss, 1);
  exit()->Jump(&value);
}


#undef __
#define __ masm->

Handle<String> Reference::GetName() {
  ASSERT(type_ == NAMED);
  Property* property = expression_->AsProperty();
  if (property == NULL) {
    // Global variable reference treated as a named property reference.
    VariableProxy* proxy = expression_->AsVariableProxy();
    ASSERT(proxy->AsVariable() != NULL);
    ASSERT(proxy->AsVariable()->is_global());
    return proxy->name();
  } else {
    Literal* raw_name = property->key()->AsLiteral();
    ASSERT(raw_name != NULL);
    return Handle<String>(String::cast(*raw_name->handle()));
  }
}


void Reference::GetValue(TypeofState typeof_state) {
  ASSERT(!cgen_->in_spilled_code());
  ASSERT(!is_illegal());
  MacroAssembler* masm = cgen_->masm();
  switch (type_) {
    case SLOT: {
      Comment cmnt(masm, "[ Load from Slot");
      Slot* slot = expression_->AsVariableProxy()->AsVariable()->slot();
      ASSERT(slot != NULL);
      cgen_->LoadFromSlot(slot, typeof_state);
      break;
    }

    case NAMED: {
      // TODO(1241834): Make sure that it is safe to ignore the
      // distinction between expressions in a typeof and not in a
      // typeof. If there is a chance that reference errors can be
      // thrown below, we must distinguish between the two kinds of
      // loads (typeof expression loads must not throw a reference
      // error).
      VirtualFrame::SpilledScope spilled_scope(cgen_);
      VirtualFrame* frame = cgen_->frame();
      Comment cmnt(masm, "[ Load from named Property");
      Handle<String> name(GetName());
      Variable* var = expression_->AsVariableProxy()->AsVariable();
      Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
      // Setup the name register.
      __ mov(ecx, name);
      if (var != NULL) {
        ASSERT(var->is_global());
        frame->CallCodeObject(ic, RelocInfo::CODE_TARGET_CONTEXT, 0);
      } else {
        frame->CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
      }
      frame->EmitPush(eax);  // IC call leaves result in eax, push it out
      break;
    }

    case KEYED: {
      // TODO(1241834): Make sure that this it is safe to ignore the
      // distinction between expressions in a typeof and not in a typeof.
      Comment cmnt(masm, "[ Load from keyed Property");
      Variable* var = expression_->AsVariableProxy()->AsVariable();
      bool is_global = var != NULL;
      ASSERT(!is_global || var->is_global());
      // Inline array load code if inside of a loop.  We do not know
      // the receiver map yet, so we initially generate the code with
      // a check against an invalid map.  In the inline cache code, we
      // patch the map check if appropriate.
      if (cgen_->loop_nesting() > 0) {
        Comment cmnt(masm, "[ Inlined array index load");
        DeferredReferenceGetKeyedValue* deferred =
            new DeferredReferenceGetKeyedValue(cgen_, is_global);

        Result key = cgen_->frame()->Pop();
        Result receiver = cgen_->frame()->Pop();
        key.ToRegister();
        receiver.ToRegister();

        // Check that the receiver is not a smi (only needed if this
        // is not a load from the global context) and that it has the
        // expected map.
        if (!is_global) {
          __ test(receiver.reg(), Immediate(kSmiTagMask));
          deferred->enter()->Branch(zero, &receiver, &key, not_taken);
        }

        // Initially, use an invalid map. The map is patched in the IC
        // initialization code.
        __ bind(deferred->patch_site());
        __ cmp(FieldOperand(receiver.reg(), HeapObject::kMapOffset),
               Immediate(Factory::null_value()));
        deferred->enter()->Branch(not_equal, &receiver, &key, not_taken);

        // Check that the key is a smi.
        __ test(key.reg(), Immediate(kSmiTagMask));
        deferred->enter()->Branch(not_zero, &receiver, &key, not_taken);

        // Get the elements array from the receiver and check that it
        // is not a dictionary.
        Result elements = cgen_->allocator()->Allocate();
        ASSERT(elements.is_valid());
        __ mov(elements.reg(),
               FieldOperand(receiver.reg(), JSObject::kElementsOffset));
        __ cmp(FieldOperand(elements.reg(), HeapObject::kMapOffset),
               Immediate(Factory::hash_table_map()));
        deferred->enter()->Branch(equal, &receiver, &key, not_taken);

        // Shift the key to get the actual index value and check that
        // it is within bounds.
        Result index = cgen_->allocator()->Allocate();
        ASSERT(index.is_valid());
        __ mov(index.reg(), key.reg());
        __ sar(index.reg(), kSmiTagSize);
        __ cmp(index.reg(),
               FieldOperand(elements.reg(), Array::kLengthOffset));
        deferred->enter()->Branch(above_equal, &receiver, &key, not_taken);

        // Load and check that the result is not the hole.  We could
        // reuse the index or elements register for the value.
        //
        // TODO(206): Consider whether it makes sense to try some
        // heuristic about which register to reuse.  For example, if
        // one is eax, the we can reuse that one because the value
        // coming from the deferred code will be in eax.
        Result value = index;
        __ mov(value.reg(), Operand(elements.reg(),
                                    index.reg(),
                                    times_4,
                                    Array::kHeaderSize - kHeapObjectTag));
        elements.Unuse();
        index.Unuse();
        __ cmp(Operand(value.reg()), Immediate(Factory::the_hole_value()));
        deferred->enter()->Branch(equal, &receiver, &key, not_taken);
        __ IncrementCounter(&Counters::keyed_load_inline, 1);

        // Restore the receiver and key to the frame and push the
        // result on top of it.
        cgen_->frame()->Push(&receiver);
        cgen_->frame()->Push(&key);
        deferred->exit()->Bind(&value);
        cgen_->frame()->Push(&value);

      } else {
        VirtualFrame::SpilledScope spilled_scope(cgen_);
        VirtualFrame* frame = cgen_->frame();
        Comment cmnt(masm, "[ Load from keyed Property");
        Handle<Code> ic(Builtins::builtin(Builtins::KeyedLoadIC_Initialize));
        if (is_global) {
          frame->CallCodeObject(ic, RelocInfo::CODE_TARGET_CONTEXT, 0);
        } else {
          frame->CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
        }
        // Make sure that we do not have a test instruction after the
        // call.  A test instruction after the call is used to
        // indicate that we have generated an inline version of the
        // keyed load.  The explicit nop instruction is here because
        // the push that follows might be peep-hole optimized away.
        __ nop();
        frame->EmitPush(eax);  // IC call leaves result in eax, push it out
      }
      break;
    }

    default:
      UNREACHABLE();
  }
}


void Reference::TakeValue(TypeofState typeof_state) {
  // For non-constant frame-allocated slots, we invalidate the value in the
  // slot.  For all others, we fall back on GetValue.
  ASSERT(!cgen_->in_spilled_code());
  ASSERT(!is_illegal());
  if (type_ != SLOT) {
    GetValue(typeof_state);
    return;
  }

  Slot* slot = expression_->AsVariableProxy()->AsVariable()->slot();
  ASSERT(slot != NULL);
  if (slot->type() == Slot::LOOKUP ||
      slot->type() == Slot::CONTEXT ||
      slot->var()->mode() == Variable::CONST) {
    GetValue(typeof_state);
    return;
  }

  // Only non-constant, frame-allocated parameters and locals can reach
  // here.
  if (slot->type() == Slot::PARAMETER) {
    cgen_->frame()->TakeParameterAt(slot->index());
  } else {
    ASSERT(slot->type() == Slot::LOCAL);
    cgen_->frame()->TakeLocalAt(slot->index());
  }
}


void Reference::SetValue(InitState init_state) {
  ASSERT(!is_illegal());
  MacroAssembler* masm = cgen_->masm();
  VirtualFrame* frame = cgen_->frame();
  switch (type_) {
    case SLOT: {
      Comment cmnt(masm, "[ Store to Slot");
      Slot* slot = expression_->AsVariableProxy()->AsVariable()->slot();
      ASSERT(slot != NULL);
      cgen_->StoreToSlot(slot, init_state);
      break;
    }

    case NAMED: {
      VirtualFrame::SpilledScope spilled_scope(cgen_);
      Comment cmnt(masm, "[ Store to named Property");
      // Call the appropriate IC code.
      Handle<String> name(GetName());
      Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
      // TODO(1222589): Make the IC grab the values from the stack.
      frame->EmitPop(eax);
      // Setup the name register.
      __ mov(ecx, name);
      frame->CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
      frame->EmitPush(eax);  // IC call leaves result in eax, push it out
      break;
    }

    case KEYED: {
      VirtualFrame::SpilledScope spilled_scope(cgen_);
      Comment cmnt(masm, "[ Store to keyed Property");
      // Call IC code.
      Handle<Code> ic(Builtins::builtin(Builtins::KeyedStoreIC_Initialize));
      // TODO(1222589): Make the IC grab the values from the stack.
      frame->EmitPop(eax);
      frame->CallCodeObject(ic, RelocInfo::CODE_TARGET, 0);
      frame->EmitPush(eax);  // IC call leaves result in eax, push it out
      break;
    }

    default:
      UNREACHABLE();
  }
}


// NOTE: The stub does not handle the inlined cases (Smis, Booleans, undefined).
void ToBooleanStub::Generate(MacroAssembler* masm) {
  Label false_result, true_result, not_string;
  __ mov(eax, Operand(esp, 1 * kPointerSize));

  // 'null' => false.
  __ cmp(eax, Factory::null_value());
  __ j(equal, &false_result);

  // Get the map and type of the heap object.
  __ mov(edx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(edx, Map::kInstanceTypeOffset));

  // Undetectable => false.
  __ movzx_b(ebx, FieldOperand(edx, Map::kBitFieldOffset));
  __ and_(ebx, 1 << Map::kIsUndetectable);
  __ j(not_zero, &false_result);

  // JavaScript object => true.
  __ cmp(ecx, FIRST_JS_OBJECT_TYPE);
  __ j(above_equal, &true_result);

  // String value => false iff empty.
  __ cmp(ecx, FIRST_NONSTRING_TYPE);
  __ j(above_equal, &not_string);
  __ and_(ecx, kStringSizeMask);
  __ cmp(ecx, kShortStringTag);
  __ j(not_equal, &true_result);  // Empty string is always short.
  __ mov(edx, FieldOperand(eax, String::kLengthOffset));
  __ shr(edx, String::kShortLengthShift);
  __ j(zero, &false_result);
  __ jmp(&true_result);

  __ bind(&not_string);
  // HeapNumber => false iff +0, -0, or NaN.
  __ cmp(edx, Factory::heap_number_map());
  __ j(not_equal, &true_result);
  __ fldz();
  __ fld_d(FieldOperand(eax, HeapNumber::kValueOffset));
  __ fucompp();
  __ push(eax);
  __ fnstsw_ax();
  __ sahf();
  __ pop(eax);
  __ j(zero, &false_result);
  // Fall through to |true_result|.

  // Return 1/0 for true/false in eax.
  __ bind(&true_result);
  __ mov(eax, 1);
  __ ret(1 * kPointerSize);
  __ bind(&false_result);
  __ mov(eax, 0);
  __ ret(1 * kPointerSize);
}


#undef __
#define __ masm_->

Result DeferredInlineBinaryOperation::GenerateInlineCode() {
  // Perform fast-case smi code for the operation (left <op> right) and
  // returns the result in a Result.
  // If any fast-case tests fail, it jumps to the slow-case deferred code,
  // which calls the binary operation stub, with the arguments (in registers)
  // on top of the frame.

  VirtualFrame* frame = generator()->frame();
  // If operation is division or modulus, ensure
  // that the special registers needed are free.
  Result reg_eax(generator());  // Valid only if op is DIV or MOD.
  Result reg_edx(generator());  // Valid only if op is DIV or MOD.
  if (op_ == Token::DIV || op_ == Token::MOD) {
    reg_eax = generator()->allocator()->Allocate(eax);
    ASSERT(reg_eax.is_valid());
    reg_edx = generator()->allocator()->Allocate(edx);
    ASSERT(reg_edx.is_valid());
  }

  Result right = frame->Pop();
  Result left = frame->Pop();
  left.ToRegister();
  right.ToRegister();
  // Answer is used to compute the answer, leaving left and right unchanged.
  // It is also returned from this function.
  // It is used as a temporary register in a few places, as well.
  Result answer(generator());
  if (reg_eax.is_valid()) {
    answer = reg_eax;
  } else {
    answer = generator()->allocator()->Allocate();
  }
  ASSERT(answer.is_valid());
  // Perform the smi check.
  __ mov(answer.reg(), Operand(left.reg()));
  __ or_(answer.reg(), Operand(right.reg()));
  ASSERT(kSmiTag == 0);  // adjust zero check if not the case
  __ test(answer.reg(), Immediate(kSmiTagMask));
  enter()->Branch(not_zero, &left, &right, not_taken);

  // All operations start by copying the left argument into answer.
  __ mov(answer.reg(), Operand(left.reg()));
  switch (op_) {
    case Token::ADD:
      __ add(answer.reg(), Operand(right.reg()));  // add optimistically
      enter()->Branch(overflow, &left, &right, not_taken);
      break;

    case Token::SUB:
      __ sub(answer.reg(), Operand(right.reg()));  // subtract optimistically
      enter()->Branch(overflow, &left, &right, not_taken);
      break;


    case Token::MUL: {
      // If the smi tag is 0 we can just leave the tag on one operand.
      ASSERT(kSmiTag == 0);  // adjust code below if not the case
      // Remove tag from the left operand (but keep sign).
      // Left hand operand has been copied into answer.
      __ sar(answer.reg(), kSmiTagSize);
      // Do multiplication of smis, leaving result in answer.
      __ imul(answer.reg(), Operand(right.reg()));
      // Go slow on overflows.
      enter()->Branch(overflow, &left, &right, not_taken);
      // Check for negative zero result.  If product is zero,
      // and one argument is negative, go to slow case.
      // The frame is unchanged in this block, so local control flow can
      // use a Label rather than a JumpTarget.
      Label non_zero_result;
      __ test(answer.reg(), Operand(answer.reg()));
      __ j(not_zero, &non_zero_result, taken);
      __ mov(answer.reg(), Operand(left.reg()));
      __ or_(answer.reg(), Operand(right.reg()));
      enter()->Branch(negative, &left, &right, not_taken);
      __ xor_(answer.reg(), Operand(answer.reg()));  // Positive 0 is correct.
      __ bind(&non_zero_result);
      break;
    }

    case Token::DIV: {
      // Left hand argument has been copied into answer, which is eax.
      // Sign extend eax into edx:eax.
      __ cdq();
      // Check for 0 divisor.
      __ test(right.reg(), Operand(right.reg()));
      enter()->Branch(zero, &left, &right, not_taken);
      // Divide edx:eax by ebx.
      __ idiv(right.reg());
      // Check for negative zero result.  If result is zero, and divisor
      // is negative, return a floating point negative zero.
      // The frame is unchanged in this block, so local control flow can
      // use a Label rather than a JumpTarget.
      Label non_zero_result;
      __ test(left.reg(), Operand(left.reg()));
      __ j(not_zero, &non_zero_result, taken);
      __ test(right.reg(), Operand(right.reg()));
      enter()->Branch(negative, &left, &right, not_taken);
      __ bind(&non_zero_result);
      // Check for the corner case of dividing the most negative smi
      // by -1. We cannot use the overflow flag, since it is not set
      // by idiv instruction.
      ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
      __ cmp(reg_eax.reg(), 0x40000000);
      enter()->Branch(equal, &left, &right, not_taken);
      // Check that the remainder is zero.
      __ test(reg_edx.reg(), Operand(reg_edx.reg()));
      enter()->Branch(not_zero, &left, &right, not_taken);
      // Tag the result and store it in register temp.
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(answer.reg(), Operand(eax, eax, times_1, kSmiTag));
      break;
    }

    case Token::MOD: {
      // Left hand argument has been copied into answer, which is eax.
      // Sign extend eax into edx:eax.
      __ cdq();
      // Check for 0 divisor.
      __ test(right.reg(), Operand(right.reg()));
      enter()->Branch(zero, &left, &right, not_taken);

      // Divide edx:eax by ebx.
      __ idiv(right.reg());
      // Check for negative zero result.  If result is zero, and divisor
      // is negative, return a floating point negative zero.
      // The frame is unchanged in this block, so local control flow can
      // use a Label rather than a JumpTarget.
      Label non_zero_result;
      __ test(reg_edx.reg(), Operand(reg_edx.reg()));
      __ j(not_zero, &non_zero_result, taken);
      __ test(left.reg(), Operand(left.reg()));
      enter()->Branch(negative, &left, &right, not_taken);
      __ bind(&non_zero_result);
      // The answer is in edx.
      answer = reg_edx;
      break;
    }

    case Token::BIT_OR:
      __ or_(answer.reg(), Operand(right.reg()));
      break;

    case Token::BIT_AND:
      __ and_(answer.reg(), Operand(right.reg()));
      break;

    case Token::BIT_XOR:
      __ xor_(answer.reg(), Operand(right.reg()));
      break;

    case Token::SHL:
    case Token::SHR:
    case Token::SAR:
      // Move right into ecx.
      // Left is in two registers already, so even if left or answer is ecx,
      // we can move right to it, and use the other one.
      // Right operand must be in register cl because x86 likes it that way.
      if (right.reg().is(ecx)) {
        // Right is already in the right place.  Left may be in the
        // same register, which causes problems.  Use answer instead.
        if (left.reg().is(ecx)) {
          left = answer;
        }
      } else if (left.reg().is(ecx)) {
        __ mov(left.reg(), Operand(right.reg()));
        right = left;
        left = answer;  // Use copy of left in answer as left.
      } else if (answer.reg().is(ecx)) {
        __ mov(answer.reg(), Operand(right.reg()));
        right = answer;
      } else {
        Result reg_ecx = generator()->allocator()->Allocate(ecx);
        ASSERT(reg_ecx.is_valid());
        __ mov(reg_ecx.reg(), Operand(right.reg()));
        right = reg_ecx;
      }
      ASSERT(left.reg().is_valid());
      ASSERT(!left.reg().is(ecx));
      ASSERT(right.reg().is(ecx));
      answer.Unuse();  // Answer may now be being used for left or right.
      // We will modify left and right, which we do not do in any other
      // binary operation.  The exits to slow code need to restore the
      // original values of left and right, or at least values that give
      // the same answer.

      // We are modifying left and right.  They must be spilled!
      generator()->frame()->Spill(left.reg());
      generator()->frame()->Spill(right.reg());

      // Remove tags from operands (but keep sign).
      __ sar(left.reg(), kSmiTagSize);
      __ sar(ecx, kSmiTagSize);
      // Perform the operation.
      switch (op_) {
        case Token::SAR:
          __ sar(left.reg());
          // No checks of result necessary
          break;
        case Token::SHR: {
          __ shr(left.reg());
          // Check that the *unsigned* result fits in a smi.
          // Neither of the two high-order bits can be set:
          // - 0x80000000: high bit would be lost when smi tagging.
          // - 0x40000000: this number would convert to negative when
          // Smi tagging these two cases can only happen with shifts
          // by 0 or 1 when handed a valid smi.
          // If the answer cannot be represented by a SMI, restore
          // the left and right arguments, and jump to slow case.
          // The low bit of the left argument may be lost, but only
          // in a case where it is dropped anyway.
          JumpTarget result_ok(generator());
          __ test(left.reg(), Immediate(0xc0000000));
          result_ok.Branch(zero, &left, &right, taken);
          __ shl(left.reg());
          ASSERT(kSmiTag == 0);
          __ shl(left.reg(), kSmiTagSize);
          __ shl(right.reg(), kSmiTagSize);
          enter()->Jump(&left, &right);
          result_ok.Bind(&left, &right);
          break;
        }
        case Token::SHL: {
          __ shl(left.reg());
          // Check that the *signed* result fits in a smi.
          //
          // TODO(207): Can reduce registers from 4 to 3 by
          // preallocating ecx.
          JumpTarget result_ok(generator());
          Result smi_test_reg = generator()->allocator()->Allocate();
          ASSERT(smi_test_reg.is_valid());
          __ lea(smi_test_reg.reg(), Operand(left.reg(), 0x40000000));
          __ test(smi_test_reg.reg(), Immediate(0x80000000));
          smi_test_reg.Unuse();
          result_ok.Branch(zero, &left, &right, taken);
          __ shr(left.reg());
          ASSERT(kSmiTag == 0);
          __ shl(left.reg(), kSmiTagSize);
          __ shl(right.reg(), kSmiTagSize);
          enter()->Jump(&left, &right);
          result_ok.Bind(&left, &right);
          break;
        }
        default:
          UNREACHABLE();
      }
      // Smi-tag the result, in left, and make answer an alias for left.
      answer = left;
      answer.ToRegister();
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(answer.reg(),
             Operand(answer.reg(), answer.reg(), times_1, kSmiTag));
      break;

    default:
      UNREACHABLE();
      break;
  }
  return answer;
}


#undef __
#define __ masm->

void GenericBinaryOpStub::GenerateSmiCode(MacroAssembler* masm, Label* slow) {
  // Perform fast-case smi code for the operation (eax <op> ebx) and
  // leave result in register eax.

  // Prepare the smi check of both operands by or'ing them together
  // before checking against the smi mask.
  __ mov(ecx, Operand(ebx));
  __ or_(ecx, Operand(eax));

  switch (op_) {
    case Token::ADD:
      __ add(eax, Operand(ebx));  // add optimistically
      __ j(overflow, slow, not_taken);
      break;

    case Token::SUB:
      __ sub(eax, Operand(ebx));  // subtract optimistically
      __ j(overflow, slow, not_taken);
      break;

    case Token::DIV:
    case Token::MOD:
      // Sign extend eax into edx:eax.
      __ cdq();
      // Check for 0 divisor.
      __ test(ebx, Operand(ebx));
      __ j(zero, slow, not_taken);
      break;

    default:
      // Fall-through to smi check.
      break;
  }

  // Perform the actual smi check.
  ASSERT(kSmiTag == 0);  // adjust zero check if not the case
  __ test(ecx, Immediate(kSmiTagMask));
  __ j(not_zero, slow, not_taken);

  switch (op_) {
    case Token::ADD:
    case Token::SUB:
      // Do nothing here.
      break;

    case Token::MUL:
      // If the smi tag is 0 we can just leave the tag on one operand.
      ASSERT(kSmiTag == 0);  // adjust code below if not the case
      // Remove tag from one of the operands (but keep sign).
      __ sar(eax, kSmiTagSize);
      // Do multiplication.
      __ imul(eax, Operand(ebx));  // multiplication of smis; result in eax
      // Go slow on overflows.
      __ j(overflow, slow, not_taken);
      // Check for negative zero result.
      __ NegativeZeroTest(eax, ecx, slow);  // use ecx = x | y
      break;

    case Token::DIV:
      // Divide edx:eax by ebx.
      __ idiv(ebx);
      // Check for the corner case of dividing the most negative smi
      // by -1. We cannot use the overflow flag, since it is not set
      // by idiv instruction.
      ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
      __ cmp(eax, 0x40000000);
      __ j(equal, slow);
      // Check for negative zero result.
      __ NegativeZeroTest(eax, ecx, slow);  // use ecx = x | y
      // Check that the remainder is zero.
      __ test(edx, Operand(edx));
      __ j(not_zero, slow);
      // Tag the result and store it in register eax.
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(eax, Operand(eax, eax, times_1, kSmiTag));
      break;

    case Token::MOD:
      // Divide edx:eax by ebx.
      __ idiv(ebx);
      // Check for negative zero result.
      __ NegativeZeroTest(edx, ecx, slow);  // use ecx = x | y
      // Move remainder to register eax.
      __ mov(eax, Operand(edx));
      break;

    case Token::BIT_OR:
      __ or_(eax, Operand(ebx));
      break;

    case Token::BIT_AND:
      __ and_(eax, Operand(ebx));
      break;

    case Token::BIT_XOR:
      __ xor_(eax, Operand(ebx));
      break;

    case Token::SHL:
    case Token::SHR:
    case Token::SAR:
      // Move the second operand into register ecx.
      __ mov(ecx, Operand(ebx));
      // Remove tags from operands (but keep sign).
      __ sar(eax, kSmiTagSize);
      __ sar(ecx, kSmiTagSize);
      // Perform the operation.
      switch (op_) {
        case Token::SAR:
          __ sar(eax);
          // No checks of result necessary
          break;
        case Token::SHR:
          __ shr(eax);
          // Check that the *unsigned* result fits in a smi.
          // Neither of the two high-order bits can be set:
          // - 0x80000000: high bit would be lost when smi tagging.
          // - 0x40000000: this number would convert to negative when
          // Smi tagging these two cases can only happen with shifts
          // by 0 or 1 when handed a valid smi.
          __ test(eax, Immediate(0xc0000000));
          __ j(not_zero, slow, not_taken);
          break;
        case Token::SHL:
          __ shl(eax);
          // Check that the *signed* result fits in a smi.
          __ lea(ecx, Operand(eax, 0x40000000));
          __ test(ecx, Immediate(0x80000000));
          __ j(not_zero, slow, not_taken);
          break;
        default:
          UNREACHABLE();
      }
      // Tag the result and store it in register eax.
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(eax, Operand(eax, eax, times_1, kSmiTag));
      break;

    default:
      UNREACHABLE();
      break;
  }
}


void GenericBinaryOpStub::Generate(MacroAssembler* masm) {
  Label call_runtime;

  if (flags_ == SMI_CODE_IN_STUB) {
    // The fast case smi code wasn't inlined in the stub caller
    // code. Generate it here to speed up common operations.
    Label slow;
    __ mov(ebx, Operand(esp, 1 * kPointerSize));  // get y
    __ mov(eax, Operand(esp, 2 * kPointerSize));  // get x
    GenerateSmiCode(masm, &slow);
    __ ret(2 * kPointerSize);  // remove both operands

    // Too bad. The fast case smi code didn't succeed.
    __ bind(&slow);
  }

  // Setup registers.
  __ mov(eax, Operand(esp, 1 * kPointerSize));  // get y
  __ mov(edx, Operand(esp, 2 * kPointerSize));  // get x

  // Floating point case.
  switch (op_) {
    case Token::ADD:
    case Token::SUB:
    case Token::MUL:
    case Token::DIV: {
      // eax: y
      // edx: x
      FloatingPointHelper::CheckFloatOperands(masm, &call_runtime, ebx);
      // Fast-case: Both operands are numbers.
      // Allocate a heap number, if needed.
      Label skip_allocation;
      switch (mode_) {
        case OVERWRITE_LEFT:
          __ mov(eax, Operand(edx));
          // Fall through!
        case OVERWRITE_RIGHT:
          // If the argument in eax is already an object, we skip the
          // allocation of a heap number.
          __ test(eax, Immediate(kSmiTagMask));
          __ j(not_zero, &skip_allocation, not_taken);
          // Fall through!
        case NO_OVERWRITE:
          FloatingPointHelper::AllocateHeapNumber(masm,
                                                  &call_runtime,
                                                  ecx,
                                                  edx);
          __ bind(&skip_allocation);
          break;
        default: UNREACHABLE();
      }
      FloatingPointHelper::LoadFloatOperands(masm, ecx);

      switch (op_) {
        case Token::ADD: __ faddp(1); break;
        case Token::SUB: __ fsubp(1); break;
        case Token::MUL: __ fmulp(1); break;
        case Token::DIV: __ fdivp(1); break;
        default: UNREACHABLE();
      }
      __ fstp_d(FieldOperand(eax, HeapNumber::kValueOffset));
      __ ret(2 * kPointerSize);
    }
    case Token::MOD: {
      // For MOD we go directly to runtime in the non-smi case.
      break;
    }
    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR:
    case Token::SAR:
    case Token::SHL:
    case Token::SHR: {
      FloatingPointHelper::CheckFloatOperands(masm, &call_runtime, ebx);
      FloatingPointHelper::LoadFloatOperands(masm, ecx);

      Label non_int32_operands, non_smi_result, skip_allocation;
      // Reserve space for converted numbers.
      __ sub(Operand(esp), Immediate(2 * kPointerSize));

      // Check if right operand is int32.
      __ fist_s(Operand(esp, 1 * kPointerSize));
      __ fild_s(Operand(esp, 1 * kPointerSize));
      __ fucompp();
      __ fnstsw_ax();
      __ sahf();
      __ j(not_zero, &non_int32_operands);
      __ j(parity_even, &non_int32_operands);

      // Check if left operand is int32.
      __ fist_s(Operand(esp, 0 * kPointerSize));
      __ fild_s(Operand(esp, 0 * kPointerSize));
      __ fucompp();
      __ fnstsw_ax();
      __ sahf();
      __ j(not_zero, &non_int32_operands);
      __ j(parity_even, &non_int32_operands);

      // Get int32 operands and perform bitop.
      __ pop(eax);
      __ pop(ecx);
      switch (op_) {
        case Token::BIT_OR:  __ or_(eax, Operand(ecx)); break;
        case Token::BIT_AND: __ and_(eax, Operand(ecx)); break;
        case Token::BIT_XOR: __ xor_(eax, Operand(ecx)); break;
        case Token::SAR: __ sar(eax); break;
        case Token::SHL: __ shl(eax); break;
        case Token::SHR: __ shr(eax); break;
        default: UNREACHABLE();
      }

      // Check if result is non-negative and fits in a smi.
      __ test(eax, Immediate(0xc0000000));
      __ j(not_zero, &non_smi_result);

      // Tag smi result and return.
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(eax, Operand(eax, eax, times_1, kSmiTag));
      __ ret(2 * kPointerSize);

      // All ops except SHR return a signed int32 that we load in a HeapNumber.
      if (op_ != Token::SHR) {
        __ bind(&non_smi_result);
        // Allocate a heap number if needed.
        __ mov(ebx, Operand(eax));  // ebx: result
        switch (mode_) {
          case OVERWRITE_LEFT:
          case OVERWRITE_RIGHT:
            // If the operand was an object, we skip the
            // allocation of a heap number.
            __ mov(eax, Operand(esp, mode_ == OVERWRITE_RIGHT ?
                                1 * kPointerSize : 2 * kPointerSize));
            __ test(eax, Immediate(kSmiTagMask));
            __ j(not_zero, &skip_allocation, not_taken);
            // Fall through!
          case NO_OVERWRITE:
            FloatingPointHelper::AllocateHeapNumber(masm, &call_runtime,
                                                    ecx, edx);
            __ bind(&skip_allocation);
            break;
          default: UNREACHABLE();
        }
        // Store the result in the HeapNumber and return.
        __ mov(Operand(esp, 1 * kPointerSize), ebx);
        __ fild_s(Operand(esp, 1 * kPointerSize));
        __ fstp_d(FieldOperand(eax, HeapNumber::kValueOffset));
        __ ret(2 * kPointerSize);
      }
      __ bind(&non_int32_operands);
      // Restore stacks and operands before calling runtime.
      __ ffree(0);
      __ add(Operand(esp), Immediate(2 * kPointerSize));

      // SHR should return uint32 - go to runtime for non-smi/negative result.
      if (op_ == Token::SHR) __ bind(&non_smi_result);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      break;
    }
    default: UNREACHABLE(); break;
  }

  // If all else fails, use the runtime system to get the correct
  // result.
  __ bind(&call_runtime);
  switch (op_) {
    case Token::ADD:
      __ InvokeBuiltin(Builtins::ADD, JUMP_FUNCTION);
      break;
    case Token::SUB:
      __ InvokeBuiltin(Builtins::SUB, JUMP_FUNCTION);
      break;
    case Token::MUL:
      __ InvokeBuiltin(Builtins::MUL, JUMP_FUNCTION);
        break;
    case Token::DIV:
      __ InvokeBuiltin(Builtins::DIV, JUMP_FUNCTION);
      break;
    case Token::MOD:
      __ InvokeBuiltin(Builtins::MOD, JUMP_FUNCTION);
      break;
    case Token::BIT_OR:
      __ InvokeBuiltin(Builtins::BIT_OR, JUMP_FUNCTION);
      break;
    case Token::BIT_AND:
      __ InvokeBuiltin(Builtins::BIT_AND, JUMP_FUNCTION);
      break;
    case Token::BIT_XOR:
      __ InvokeBuiltin(Builtins::BIT_XOR, JUMP_FUNCTION);
      break;
    case Token::SAR:
      __ InvokeBuiltin(Builtins::SAR, JUMP_FUNCTION);
      break;
    case Token::SHL:
      __ InvokeBuiltin(Builtins::SHL, JUMP_FUNCTION);
      break;
    case Token::SHR:
      __ InvokeBuiltin(Builtins::SHR, JUMP_FUNCTION);
      break;
    default:
      UNREACHABLE();
  }
}


void FloatingPointHelper::AllocateHeapNumber(MacroAssembler* masm,
                                             Label* need_gc,
                                             Register scratch1,
                                             Register scratch2) {
  ExternalReference allocation_top =
      ExternalReference::new_space_allocation_top_address();
  ExternalReference allocation_limit =
      ExternalReference::new_space_allocation_limit_address();
  __ mov(Operand(scratch1), Immediate(allocation_top));
  __ mov(eax, Operand(scratch1, 0));
  __ lea(scratch2, Operand(eax, HeapNumber::kSize));  // scratch2: new top
  __ cmp(scratch2, Operand::StaticVariable(allocation_limit));
  __ j(above, need_gc, not_taken);

  __ mov(Operand(scratch1, 0), scratch2);  // store new top
  __ mov(Operand(eax, HeapObject::kMapOffset),
         Immediate(Factory::heap_number_map()));
  // Tag old top and use as result.
  __ add(Operand(eax), Immediate(kHeapObjectTag));
}


void FloatingPointHelper::LoadFloatOperands(MacroAssembler* masm,
                                            Register scratch) {
  Label load_smi_1, load_smi_2, done_load_1, done;
  __ mov(scratch, Operand(esp, 2 * kPointerSize));
  __ test(scratch, Immediate(kSmiTagMask));
  __ j(zero, &load_smi_1, not_taken);
  __ fld_d(FieldOperand(scratch, HeapNumber::kValueOffset));
  __ bind(&done_load_1);

  __ mov(scratch, Operand(esp, 1 * kPointerSize));
  __ test(scratch, Immediate(kSmiTagMask));
  __ j(zero, &load_smi_2, not_taken);
  __ fld_d(FieldOperand(scratch, HeapNumber::kValueOffset));
  __ jmp(&done);

  __ bind(&load_smi_1);
  __ sar(scratch, kSmiTagSize);
  __ push(scratch);
  __ fild_s(Operand(esp, 0));
  __ pop(scratch);
  __ jmp(&done_load_1);

  __ bind(&load_smi_2);
  __ sar(scratch, kSmiTagSize);
  __ push(scratch);
  __ fild_s(Operand(esp, 0));
  __ pop(scratch);

  __ bind(&done);
}


void FloatingPointHelper::CheckFloatOperands(MacroAssembler* masm,
                                             Label* non_float,
                                             Register scratch) {
  Label test_other, done;
  // Test if both operands are floats or smi -> scratch=k_is_float;
  // Otherwise scratch = k_not_float.
  __ test(edx, Immediate(kSmiTagMask));
  __ j(zero, &test_other, not_taken);  // argument in edx is OK
  __ mov(scratch, FieldOperand(edx, HeapObject::kMapOffset));
  __ cmp(scratch, Factory::heap_number_map());
  __ j(not_equal, non_float);  // argument in edx is not a number -> NaN

  __ bind(&test_other);
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &done);  // argument in eax is OK
  __ mov(scratch, FieldOperand(eax, HeapObject::kMapOffset));
  __ cmp(scratch, Factory::heap_number_map());
  __ j(not_equal, non_float);  // argument in eax is not a number -> NaN

  // Fall-through: Both operands are numbers.
  __ bind(&done);
}


void UnarySubStub::Generate(MacroAssembler* masm) {
  Label undo;
  Label slow;
  Label done;
  Label try_float;

  // Check whether the value is a smi.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(not_zero, &try_float, not_taken);

  // Enter runtime system if the value of the expression is zero
  // to make sure that we switch between 0 and -0.
  __ test(eax, Operand(eax));
  __ j(zero, &slow, not_taken);

  // The value of the expression is a smi that is not zero.  Try
  // optimistic subtraction '0 - value'.
  __ mov(edx, Operand(eax));
  __ Set(eax, Immediate(0));
  __ sub(eax, Operand(edx));
  __ j(overflow, &undo, not_taken);

  // If result is a smi we are done.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &done, taken);

  // Restore eax and enter runtime system.
  __ bind(&undo);
  __ mov(eax, Operand(edx));

  // Enter runtime system.
  __ bind(&slow);
  __ pop(ecx);  // pop return address
  __ push(eax);
  __ push(ecx);  // push return address
  __ InvokeBuiltin(Builtins::UNARY_MINUS, JUMP_FUNCTION);

  // Try floating point case.
  __ bind(&try_float);
  __ mov(edx, FieldOperand(eax, HeapObject::kMapOffset));
  __ cmp(edx, Factory::heap_number_map());
  __ j(not_equal, &slow);
  __ mov(edx, Operand(eax));
  // edx: operand
  FloatingPointHelper::AllocateHeapNumber(masm, &undo, ebx, ecx);
  // eax: allocated 'empty' number
  __ fld_d(FieldOperand(edx, HeapNumber::kValueOffset));
  __ fchs();
  __ fstp_d(FieldOperand(eax, HeapNumber::kValueOffset));

  __ bind(&done);

  __ StubReturn(1);
}


void ArgumentsAccessStub::GenerateReadLength(MacroAssembler* masm) {
  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor;
  __ mov(edx, Operand(ebp, StandardFrameConstants::kCallerFPOffset));
  __ mov(ecx, Operand(edx, StandardFrameConstants::kContextOffset));
  __ cmp(ecx, ArgumentsAdaptorFrame::SENTINEL);
  __ j(equal, &adaptor);

  // Nothing to do: The formal number of parameters has already been
  // passed in register eax by calling function. Just return it.
  __ ret(0);

  // Arguments adaptor case: Read the arguments length from the
  // adaptor frame and return it.
  __ bind(&adaptor);
  __ mov(eax, Operand(edx, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ ret(0);
}


void ArgumentsAccessStub::GenerateReadElement(MacroAssembler* masm) {
  // The displacement is used for skipping the frame pointer on the
  // stack. It is the offset of the last parameter (if any) relative
  // to the frame pointer.
  static const int kDisplacement = 1 * kPointerSize;

  // Check that the key is a smi.
  Label slow;
  __ mov(ebx, Operand(esp, 1 * kPointerSize));  // skip return address
  __ test(ebx, Immediate(kSmiTagMask));
  __ j(not_zero, &slow, not_taken);

  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor;
  __ mov(edx, Operand(ebp, StandardFrameConstants::kCallerFPOffset));
  __ mov(ecx, Operand(edx, StandardFrameConstants::kContextOffset));
  __ cmp(ecx, ArgumentsAdaptorFrame::SENTINEL);
  __ j(equal, &adaptor);

  // Check index against formal parameters count limit passed in
  // through register eax. Use unsigned comparison to get negative
  // check for free.
  __ cmp(ebx, Operand(eax));
  __ j(above_equal, &slow, not_taken);

  // Read the argument from the stack and return it.
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);  // shifting code depends on this
  __ lea(edx, Operand(ebp, eax, times_2, 0));
  __ neg(ebx);
  __ mov(eax, Operand(edx, ebx, times_2, kDisplacement));
  __ ret(0);

  // Arguments adaptor case: Check index against actual arguments
  // limit found in the arguments adaptor frame. Use unsigned
  // comparison to get negative check for free.
  __ bind(&adaptor);
  __ mov(ecx, Operand(edx, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ cmp(ebx, Operand(ecx));
  __ j(above_equal, &slow, not_taken);

  // Read the argument from the stack and return it.
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);  // shifting code depends on this
  __ lea(edx, Operand(edx, ecx, times_2, 0));
  __ neg(ebx);
  __ mov(eax, Operand(edx, ebx, times_2, kDisplacement));
  __ ret(0);

  // Slow-case: Handle non-smi or out-of-bounds access to arguments
  // by calling the runtime system.
  __ bind(&slow);
  __ TailCallRuntime(ExternalReference(Runtime::kGetArgumentsProperty), 1);
}


void ArgumentsAccessStub::GenerateNewObject(MacroAssembler* masm) {
  // The displacement is used for skipping the return address and the
  // frame pointer on the stack. It is the offset of the last
  // parameter (if any) relative to the frame pointer.
  static const int kDisplacement = 2 * kPointerSize;

  // Check if the calling frame is an arguments adaptor frame.
  Label runtime;
  __ mov(edx, Operand(ebp, StandardFrameConstants::kCallerFPOffset));
  __ mov(ecx, Operand(edx, StandardFrameConstants::kContextOffset));
  __ cmp(ecx, ArgumentsAdaptorFrame::SENTINEL);
  __ j(not_equal, &runtime);

  // Patch the arguments.length and the parameters pointer.
  __ mov(ecx, Operand(edx, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ mov(Operand(esp, 1 * kPointerSize), ecx);
  __ lea(edx, Operand(edx, ecx, times_2, kDisplacement));
  __ mov(Operand(esp, 2 * kPointerSize), edx);

  // Do the runtime call to allocate the arguments object.
  __ bind(&runtime);
  __ TailCallRuntime(ExternalReference(Runtime::kNewArgumentsFast), 3);
}


void CompareStub::Generate(MacroAssembler* masm) {
  Label call_builtin, done;

  // If we're doing a strict equality comparison, we generate code
  // to do fast comparison for objects and oddballs. Numbers and
  // strings still go through the usual slow-case code.
  if (strict_) {
    Label slow;
    __ test(eax, Immediate(kSmiTagMask));
    __ j(zero, &slow);

    // Get the type of the first operand.
    __ mov(ecx, FieldOperand(eax, HeapObject::kMapOffset));
    __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));

    // If the first object is an object, we do pointer comparison.
    ASSERT(LAST_TYPE == JS_FUNCTION_TYPE);
    Label non_object;
    __ cmp(ecx, FIRST_JS_OBJECT_TYPE);
    __ j(less, &non_object);
    __ sub(eax, Operand(edx));
    __ ret(0);

    // Check for oddballs: true, false, null, undefined.
    __ bind(&non_object);
    __ cmp(ecx, ODDBALL_TYPE);
    __ j(not_equal, &slow);

    // If the oddball isn't undefined, we do pointer comparison. For
    // the undefined value, we have to be careful and check for
    // 'undetectable' objects too.
    Label undefined;
    __ cmp(Operand(eax), Immediate(Factory::undefined_value()));
    __ j(equal, &undefined);
    __ sub(eax, Operand(edx));
    __ ret(0);

    // Undefined case: If the other operand isn't undefined too, we
    // have to check if it's 'undetectable'.
    Label check_undetectable;
    __ bind(&undefined);
    __ cmp(Operand(edx), Immediate(Factory::undefined_value()));
    __ j(not_equal, &check_undetectable);
    __ Set(eax, Immediate(0));
    __ ret(0);

    // Check for undetectability of the other operand.
    Label not_strictly_equal;
    __ bind(&check_undetectable);
    __ test(edx, Immediate(kSmiTagMask));
    __ j(zero, &not_strictly_equal);
    __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));
    __ movzx_b(ecx, FieldOperand(ecx, Map::kBitFieldOffset));
    __ and_(ecx, 1 << Map::kIsUndetectable);
    __ cmp(ecx, 1 << Map::kIsUndetectable);
    __ j(not_equal, &not_strictly_equal);
    __ Set(eax, Immediate(0));
    __ ret(0);

    // No cigar: Objects aren't strictly equal. Register eax contains
    // a non-smi value so it can't be 0. Just return.
    ASSERT(kHeapObjectTag != 0);
    __ bind(&not_strictly_equal);
    __ ret(0);

    // Fall through to the general case.
    __ bind(&slow);
  }

  // Save the return address (and get it off the stack).
  __ pop(ecx);

  // Push arguments.
  __ push(eax);
  __ push(edx);
  __ push(ecx);

  // Inlined floating point compare.
  // Call builtin if operands are not floating point or smi.
  FloatingPointHelper::CheckFloatOperands(masm, &call_builtin, ebx);
  FloatingPointHelper::LoadFloatOperands(masm, ecx);
  __ FCmp();

  // Jump to builtin for NaN.
  __ j(parity_even, &call_builtin, not_taken);

  // TODO(1243847): Use cmov below once CpuFeatures are properly hooked up.
  Label below_lbl, above_lbl;
  // use edx, eax to convert unsigned to signed comparison
  __ j(below, &below_lbl, not_taken);
  __ j(above, &above_lbl, not_taken);

  __ xor_(eax, Operand(eax));  // equal
  __ ret(2 * kPointerSize);

  __ bind(&below_lbl);
  __ mov(eax, -1);
  __ ret(2 * kPointerSize);

  __ bind(&above_lbl);
  __ mov(eax, 1);
  __ ret(2 * kPointerSize);  // eax, edx were pushed

  __ bind(&call_builtin);
  // must swap argument order
  __ pop(ecx);
  __ pop(edx);
  __ pop(eax);
  __ push(edx);
  __ push(eax);

  // Figure out which native to call and setup the arguments.
  Builtins::JavaScript builtin;
  if (cc_ == equal) {
    builtin = strict_ ? Builtins::STRICT_EQUALS : Builtins::EQUALS;
  } else {
    builtin = Builtins::COMPARE;
    int ncr;  // NaN compare result
    if (cc_ == less || cc_ == less_equal) {
      ncr = GREATER;
    } else {
      ASSERT(cc_ == greater || cc_ == greater_equal);  // remaining cases
      ncr = LESS;
    }
    __ push(Immediate(Smi::FromInt(ncr)));
  }

  // Restore return address on the stack.
  __ push(ecx);

  // Call the native; it returns -1 (less), 0 (equal), or 1 (greater)
  // tagged as a small integer.
  __ InvokeBuiltin(builtin, JUMP_FUNCTION);
}


void StackCheckStub::Generate(MacroAssembler* masm) {
  // Because builtins always remove the receiver from the stack, we
  // have to fake one to avoid underflowing the stack. The receiver
  // must be inserted below the return address on the stack so we
  // temporarily store that in a register.
  __ pop(eax);
  __ push(Immediate(Smi::FromInt(0)));
  __ push(eax);

  // Do tail-call to runtime routine.
  __ TailCallRuntime(ExternalReference(Runtime::kStackGuard), 1);
}


void CallFunctionStub::Generate(MacroAssembler* masm) {
  Label slow;

  // Get the function to call from the stack.
  // +2 ~ receiver, return address
  __ mov(edi, Operand(esp, (argc_ + 2) * kPointerSize));

  // Check that the function really is a JavaScript function.
  __ test(edi, Immediate(kSmiTagMask));
  __ j(zero, &slow, not_taken);
  // Get the map.
  __ mov(ecx, FieldOperand(edi, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  __ cmp(ecx, JS_FUNCTION_TYPE);
  __ j(not_equal, &slow, not_taken);

  // Fast-case: Just invoke the function.
  ParameterCount actual(argc_);
  __ InvokeFunction(edi, actual, JUMP_FUNCTION);

  // Slow-case: Non-function called.
  __ bind(&slow);
  __ Set(eax, Immediate(argc_));
  __ Set(ebx, Immediate(0));
  __ GetBuiltinEntry(edx, Builtins::CALL_NON_FUNCTION);
  Handle<Code> adaptor(Builtins::builtin(Builtins::ArgumentsAdaptorTrampoline));
  __ jmp(adaptor, RelocInfo::CODE_TARGET);
}


void RevertToNumberStub::Generate(MacroAssembler* masm) {
  // Revert optimistic increment/decrement.
  if (is_increment_) {
    __ sub(Operand(eax), Immediate(Smi::FromInt(1)));
  } else {
    __ add(Operand(eax), Immediate(Smi::FromInt(1)));
  }

  __ pop(ecx);
  __ push(eax);
  __ push(ecx);
  __ InvokeBuiltin(Builtins::TO_NUMBER, JUMP_FUNCTION);
  // Code never returns due to JUMP_FUNCTION.
}


void CounterOpStub::Generate(MacroAssembler* masm) {
  // Store to the result on the stack (skip return address) before
  // performing the count operation.
  if (is_postfix_) {
    __ mov(Operand(esp, result_offset_ + kPointerSize), eax);
  }

  // Revert optimistic increment/decrement but only for prefix
  // counts. For postfix counts it has already been reverted before
  // the conversion to numbers.
  if (!is_postfix_) {
    if (is_increment_) {
      __ sub(Operand(eax), Immediate(Smi::FromInt(1)));
    } else {
      __ add(Operand(eax), Immediate(Smi::FromInt(1)));
    }
  }

  // Compute the new value by calling the right JavaScript native.
  __ pop(ecx);
  __ push(eax);
  __ push(ecx);
  Builtins::JavaScript builtin = is_increment_ ? Builtins::INC : Builtins::DEC;
  __ InvokeBuiltin(builtin, JUMP_FUNCTION);
  // Code never returns due to JUMP_FUNCTION.
}


void CEntryStub::GenerateThrowTOS(MacroAssembler* masm) {
  ASSERT(StackHandlerConstants::kSize == 6 * kPointerSize);  // adjust this code
  ExternalReference handler_address(Top::k_handler_address);
  __ mov(edx, Operand::StaticVariable(handler_address));
  __ mov(ecx, Operand(edx, -1 * kPointerSize));  // get next in chain
  __ mov(Operand::StaticVariable(handler_address), ecx);
  __ mov(esp, Operand(edx));
  __ pop(edi);
  __ pop(ebp);
  __ pop(edx);  // remove code pointer
  __ pop(edx);  // remove state

  // Before returning we restore the context from the frame pointer if not NULL.
  // The frame pointer is NULL in the exception handler of a JS entry frame.
  __ xor_(esi, Operand(esi));  // tentatively set context pointer to NULL
  Label skip;
  __ cmp(ebp, 0);
  __ j(equal, &skip, not_taken);
  __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
  __ bind(&skip);

  __ ret(0);
}


void CEntryStub::GenerateCore(MacroAssembler* masm,
                              Label* throw_normal_exception,
                              Label* throw_out_of_memory_exception,
                              StackFrame::Type frame_type,
                              bool do_gc,
                              bool always_allocate_scope) {
  // eax: result parameter for PerformGC, if any
  // ebx: pointer to C function  (C callee-saved)
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // edi: number of arguments including receiver  (C callee-saved)
  // esi: pointer to the first argument (C callee-saved)

  if (do_gc) {
    __ mov(Operand(esp, 0 * kPointerSize), eax);  // Result.
    __ call(FUNCTION_ADDR(Runtime::PerformGC), RelocInfo::RUNTIME_ENTRY);
  }

  ExternalReference scope_depth =
      ExternalReference::heap_always_allocate_scope_depth();
  if (always_allocate_scope) {
    __ inc(Operand::StaticVariable(scope_depth));
  }

  // Call C function.
  __ mov(Operand(esp, 0 * kPointerSize), edi);  // argc.
  __ mov(Operand(esp, 1 * kPointerSize), esi);  // argv.
  __ call(Operand(ebx));
  // Result is in eax or edx:eax - do not destroy these registers!

  if (always_allocate_scope) {
    __ dec(Operand::StaticVariable(scope_depth));
  }

  // Check for failure result.
  Label failure_returned;
  ASSERT(((kFailureTag + 1) & kFailureTagMask) == 0);
  __ lea(ecx, Operand(eax, 1));
  // Lower 2 bits of ecx are 0 iff eax has failure tag.
  __ test(ecx, Immediate(kFailureTagMask));
  __ j(zero, &failure_returned, not_taken);

  // Exit the JavaScript to C++ exit frame.
  __ LeaveExitFrame(frame_type);
  __ ret(0);

  // Handling of failure.
  __ bind(&failure_returned);

  Label retry;
  // If the returned exception is RETRY_AFTER_GC continue at retry label
  ASSERT(Failure::RETRY_AFTER_GC == 0);
  __ test(eax, Immediate(((1 << kFailureTypeTagSize) - 1) << kFailureTagSize));
  __ j(zero, &retry, taken);

  Label continue_exception;
  // If the returned failure is EXCEPTION then promote Top::pending_exception().
  __ cmp(eax, reinterpret_cast<int32_t>(Failure::Exception()));
  __ j(not_equal, &continue_exception);

  // Retrieve the pending exception and clear the variable.
  ExternalReference pending_exception_address(Top::k_pending_exception_address);
  __ mov(eax, Operand::StaticVariable(pending_exception_address));
  __ mov(edx,
         Operand::StaticVariable(ExternalReference::the_hole_value_location()));
  __ mov(Operand::StaticVariable(pending_exception_address), edx);

  __ bind(&continue_exception);
  // Special handling of out of memory exception.
  __ cmp(eax, reinterpret_cast<int32_t>(Failure::OutOfMemoryException()));
  __ j(equal, throw_out_of_memory_exception);

  // Handle normal exception.
  __ jmp(throw_normal_exception);

  // Retry.
  __ bind(&retry);
}


void CEntryStub::GenerateThrowOutOfMemory(MacroAssembler* masm) {
  // Fetch top stack handler.
  ExternalReference handler_address(Top::k_handler_address);
  __ mov(edx, Operand::StaticVariable(handler_address));

  // Unwind the handlers until the ENTRY handler is found.
  Label loop, done;
  __ bind(&loop);
  // Load the type of the current stack handler.
  const int kStateOffset = StackHandlerConstants::kAddressDisplacement +
      StackHandlerConstants::kStateOffset;
  __ cmp(Operand(edx, kStateOffset), Immediate(StackHandler::ENTRY));
  __ j(equal, &done);
  // Fetch the next handler in the list.
  const int kNextOffset = StackHandlerConstants::kAddressDisplacement +
      StackHandlerConstants::kNextOffset;
  __ mov(edx, Operand(edx, kNextOffset));
  __ jmp(&loop);
  __ bind(&done);

  // Set the top handler address to next handler past the current ENTRY handler.
  __ mov(eax, Operand(edx, kNextOffset));
  __ mov(Operand::StaticVariable(handler_address), eax);

  // Set external caught exception to false.
  __ mov(eax, false);
  ExternalReference external_caught(Top::k_external_caught_exception_address);
  __ mov(Operand::StaticVariable(external_caught), eax);

  // Set pending exception and eax to out of memory exception.
  __ mov(eax, reinterpret_cast<int32_t>(Failure::OutOfMemoryException()));
  ExternalReference pending_exception(Top::k_pending_exception_address);
  __ mov(Operand::StaticVariable(pending_exception), eax);

  // Restore the stack to the address of the ENTRY handler
  __ mov(esp, Operand(edx));

  // Clear the context pointer;
  __ xor_(esi, Operand(esi));

  // Restore registers from handler.
  __ pop(edi);  // PP
  __ pop(ebp);  // FP
  __ pop(edx);  // Code
  __ pop(edx);  // State

  __ ret(0);
}


void CEntryStub::GenerateBody(MacroAssembler* masm, bool is_debug_break) {
  // eax: number of arguments including receiver
  // ebx: pointer to C function  (C callee-saved)
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // esi: current context (C callee-saved)
  // edi: caller's parameter pointer pp  (C callee-saved)

  // NOTE: Invocations of builtins may return failure objects
  // instead of a proper result. The builtin entry handles
  // this by performing a garbage collection and retrying the
  // builtin once.

  StackFrame::Type frame_type = is_debug_break ?
      StackFrame::EXIT_DEBUG :
      StackFrame::EXIT;

  // Enter the exit frame that transitions from JavaScript to C++.
  __ EnterExitFrame(frame_type);

  // eax: result parameter for PerformGC, if any (setup below)
  // ebx: pointer to builtin function  (C callee-saved)
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // edi: number of arguments including receiver (C callee-saved)
  // esi: argv pointer (C callee-saved)

  Label throw_out_of_memory_exception;
  Label throw_normal_exception;

  // Call into the runtime system. Collect garbage before the call if
  // running with --gc-greedy set.
  if (FLAG_gc_greedy) {
    Failure* failure = Failure::RetryAfterGC(0);
    __ mov(eax, Immediate(reinterpret_cast<int32_t>(failure)));
  }
  GenerateCore(masm, &throw_normal_exception,
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
  __ mov(eax, Immediate(reinterpret_cast<int32_t>(failure)));
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
  __ push(ebp);
  __ mov(ebp, Operand(esp));

  // Save callee-saved registers (C calling conventions).
  int marker = is_construct ? StackFrame::ENTRY_CONSTRUCT : StackFrame::ENTRY;
  // Push something that is not an arguments adaptor.
  __ push(Immediate(~ArgumentsAdaptorFrame::SENTINEL));
  __ push(Immediate(Smi::FromInt(marker)));  // @ function offset
  __ push(edi);
  __ push(esi);
  __ push(ebx);

  // Save copies of the top frame descriptor on the stack.
  ExternalReference c_entry_fp(Top::k_c_entry_fp_address);
  __ push(Operand::StaticVariable(c_entry_fp));

  // Call a faked try-block that does the invoke.
  __ call(&invoke);

  // Caught exception: Store result (exception) in the pending
  // exception field in the JSEnv and return a failure sentinel.
  ExternalReference pending_exception(Top::k_pending_exception_address);
  __ mov(Operand::StaticVariable(pending_exception), eax);
  __ mov(eax, reinterpret_cast<int32_t>(Failure::Exception()));
  __ jmp(&exit);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  __ PushTryHandler(IN_JS_ENTRY, JS_ENTRY_HANDLER);
  __ push(eax);  // flush TOS

  // Clear any pending exceptions.
  __ mov(edx,
         Operand::StaticVariable(ExternalReference::the_hole_value_location()));
  __ mov(Operand::StaticVariable(pending_exception), edx);

  // Fake a receiver (NULL).
  __ push(Immediate(0));  // receiver

  // Invoke the function by calling through JS entry trampoline
  // builtin and pop the faked function when we return. Notice that we
  // cannot store a reference to the trampoline code directly in this
  // stub, because the builtin stubs may not have been generated yet.
  if (is_construct) {
    ExternalReference construct_entry(Builtins::JSConstructEntryTrampoline);
    __ mov(edx, Immediate(construct_entry));
  } else {
    ExternalReference entry(Builtins::JSEntryTrampoline);
    __ mov(edx, Immediate(entry));
  }
  __ mov(edx, Operand(edx, 0));  // deref address
  __ lea(edx, FieldOperand(edx, Code::kHeaderSize));
  __ call(Operand(edx));

  // Unlink this frame from the handler chain.
  __ pop(Operand::StaticVariable(ExternalReference(Top::k_handler_address)));
  // Pop next_sp.
  __ add(Operand(esp), Immediate(StackHandlerConstants::kSize - kPointerSize));

  // Restore the top frame descriptor from the stack.
  __ bind(&exit);
  __ pop(Operand::StaticVariable(ExternalReference(Top::k_c_entry_fp_address)));

  // Restore callee-saved registers (C calling conventions).
  __ pop(ebx);
  __ pop(esi);
  __ pop(edi);
  __ add(Operand(esp), Immediate(2 * kPointerSize));  // remove markers

  // Restore frame pointer and return.
  __ pop(ebp);
  __ ret(0);
}


void InstanceofStub::Generate(MacroAssembler* masm) {
  // Get the object - go slow case if it's a smi.
  Label slow;
  __ mov(eax, Operand(esp, 2 * kPointerSize));  // 2 ~ return address, function
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &slow, not_taken);

  // Check that the left hand is a JS object.
  __ mov(eax, FieldOperand(eax, HeapObject::kMapOffset));  // ebx - object map
  __ movzx_b(ecx, FieldOperand(eax, Map::kInstanceTypeOffset));  // ecx - type
  __ cmp(ecx, FIRST_JS_OBJECT_TYPE);
  __ j(less, &slow, not_taken);
  __ cmp(ecx, LAST_JS_OBJECT_TYPE);
  __ j(greater, &slow, not_taken);

  // Get the prototype of the function.
  __ mov(edx, Operand(esp, 1 * kPointerSize));  // 1 ~ return address
  __ TryGetFunctionPrototype(edx, ebx, ecx, &slow);

  // Check that the function prototype is a JS object.
  __ mov(ecx, FieldOperand(ebx, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  __ cmp(ecx, FIRST_JS_OBJECT_TYPE);
  __ j(less, &slow, not_taken);
  __ cmp(ecx, LAST_JS_OBJECT_TYPE);
  __ j(greater, &slow, not_taken);

  // Register mapping: eax is object map and ebx is function prototype.
  __ mov(ecx, FieldOperand(eax, Map::kPrototypeOffset));

  // Loop through the prototype chain looking for the function prototype.
  Label loop, is_instance, is_not_instance;
  __ bind(&loop);
  __ cmp(ecx, Operand(ebx));
  __ j(equal, &is_instance);
  __ cmp(Operand(ecx), Immediate(Factory::null_value()));
  __ j(equal, &is_not_instance);
  __ mov(ecx, FieldOperand(ecx, HeapObject::kMapOffset));
  __ mov(ecx, FieldOperand(ecx, Map::kPrototypeOffset));
  __ jmp(&loop);

  __ bind(&is_instance);
  __ Set(eax, Immediate(0));
  __ ret(2 * kPointerSize);

  __ bind(&is_not_instance);
  __ Set(eax, Immediate(Smi::FromInt(1)));
  __ ret(2 * kPointerSize);

  // Slow-case: Go through the JavaScript implementation.
  __ bind(&slow);
  __ InvokeBuiltin(Builtins::INSTANCE_OF, JUMP_FUNCTION);
}


#undef __

} }  // namespace v8::internal
