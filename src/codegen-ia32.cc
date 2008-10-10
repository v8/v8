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

#define TOS (Operand(esp, 0))


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
                           Label* true_target,
                           Label* false_target)
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


// -----------------------------------------------------------------------------
// CodeGenerator implementation

#define __ masm_->

CodeGenerator::CodeGenerator(int buffer_size, Handle<Script> script,
                             bool is_eval)
    : is_eval_(is_eval),
      script_(script),
      deferred_(8),
      masm_(new MacroAssembler(NULL, buffer_size)),
      scope_(NULL),
      cc_reg_(no_condition),
      state_(NULL),
      is_inside_try_(false),
      break_stack_height_(0) {
}


// Calling conventions:
// ebp: frame pointer
// esp: stack pointer
// edi: caller's parameter pointer
// esi: callee's context

void CodeGenerator::GenCode(FunctionLiteral* fun) {
  // Record the position for debugging purposes.
  __ RecordPosition(fun->start_position());

  Scope* scope = fun->scope();
  ZoneList<Statement*>* body = fun->body();

  // Initialize state.
  { CodeGenState state(this);
    scope_ = scope;
    cc_reg_ = no_condition;

    // Entry
    // stack: function, receiver, arguments, return address
    // esp: stack pointer
    // ebp: frame pointer
    // edi: caller's parameter pointer
    // esi: callee's context

    { Comment cmnt(masm_, "[ enter JS frame");
      EnterJSFrame();
    }
    // tos: code slot
#ifdef DEBUG
    if (strlen(FLAG_stop_at) > 0 &&
        fun->name()->IsEqualTo(CStrVector(FLAG_stop_at))) {
      __ int3();
    }
#endif

    // This section now only allocates and copies the formals into the
    // arguments object. It saves the address in ecx, which is saved
    // at any point before either garbage collection or ecx is
    // overwritten.  The flag arguments_array_allocated communicates
    // with the store into the arguments variable and guards the lazy
    // pushes of ecx to TOS.  The flag arguments_array_saved notes
    // when the push has happened.
    bool arguments_object_allocated = false;
    bool arguments_object_saved = false;

    // Allocate arguments object.
    // The arguments object pointer needs to be saved in ecx, since we need
    // to store arguments into the context.
    if (scope->arguments() != NULL) {
      ASSERT(scope->arguments_shadow() != NULL);
      Comment cmnt(masm_, "[ allocate arguments object");
      ArgumentsAccessStub stub(ArgumentsAccessStub::NEW_OBJECT);
      __ lea(eax, ReceiverOperand());
      __ push(FunctionOperand());
      __ push(eax);
      __ push(Immediate(Smi::FromInt(scope->num_parameters())));
      __ CallStub(&stub);
      __ mov(ecx, Operand(eax));
      arguments_object_allocated = true;
    }

    // Allocate space for locals and initialize them.
    if (scope->num_stack_slots() > 0) {
      Comment cmnt(masm_, "[ allocate space for locals");
      __ Set(eax, Immediate(Factory::undefined_value()));
      for (int i = scope->num_stack_slots(); i-- > 0; ) __ push(eax);
    }

    if (scope->num_heap_slots() > 0) {
      Comment cmnt(masm_, "[ allocate local context");
      // Save the arguments object pointer, if any.
      if (arguments_object_allocated && !arguments_object_saved) {
        __ push(Operand(ecx));
        arguments_object_saved = true;
      }
      // Allocate local context.
      // Get outer context and create a new context based on it.
      __ push(FunctionOperand());
      __ CallRuntime(Runtime::kNewContext, 1);  // eax holds the result

      if (kDebug) {
        Label verified_true;
        // Verify eax and esi are the same in debug mode
        __ cmp(eax, Operand(esi));
        __ j(equal, &verified_true);
        __ int3();
        __ bind(&verified_true);
      }

      // Update context local.
      __ mov(Operand(ebp, StandardFrameConstants::kContextOffset), esi);
      // Restore the arguments array pointer, if any.
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
      for (int i = 0; i < scope->num_parameters(); i++) {
        Variable* par = scope->parameter(i);
        Slot* slot = par->slot();
        if (slot != NULL && slot->type() == Slot::CONTEXT) {
          // Save the arguments object pointer, if any.
          if (arguments_object_allocated && !arguments_object_saved) {
            __ push(Operand(ecx));
            arguments_object_saved = true;
          }
          ASSERT(!scope->is_global_scope());  // no parameters in global scope
          __ mov(eax, ParameterOperand(i));
          // Loads ecx with context; used below in RecordWrite.
          __ mov(SlotOperand(slot, ecx), eax);
          int offset = FixedArray::kHeaderSize + slot->index() * kPointerSize;
          __ RecordWrite(ecx, offset, eax, ebx);
        }
      }
    }

    // This section stores the pointer to the arguments object that
    // was allocated and copied into above. If the address was not
    // saved to TOS, we push ecx onto the stack.

    // Store the arguments object.
    // This must happen after context initialization because
    // the arguments object may be stored in the context
    if (arguments_object_allocated) {
      ASSERT(scope->arguments() != NULL);
      ASSERT(scope->arguments_shadow() != NULL);
      Comment cmnt(masm_, "[ store arguments object");
      { Reference shadow_ref(this, scope->arguments_shadow());
        ASSERT(shadow_ref.is_slot());
        { Reference arguments_ref(this, scope->arguments());
          ASSERT(arguments_ref.is_slot());
          // If the newly-allocated arguments object is already on the
          // stack, we make use of the convenient property that references
          // representing slots take up no space on the expression stack
          // (ie, it doesn't matter that the stored value is actually below
          // the reference).
          //
          // If the newly-allocated argument object is not already on
          // the stack, we rely on the property that loading a
          // zero-sized reference will not clobber the ecx register.
          if (!arguments_object_saved) {
            __ push(ecx);
          }
          arguments_ref.SetValue(NOT_CONST_INIT);
        }
        shadow_ref.SetValue(NOT_CONST_INIT);
      }
      __ pop(eax);  // Value is no longer needed.
    }

    // Generate code to 'execute' declarations and initialize
    // functions (source elements). In case of an illegal
    // redeclaration we need to handle that instead of processing the
    // declarations.
    if (scope->HasIllegalRedeclaration()) {
      Comment cmnt(masm_, "[ illegal redeclarations");
      scope->VisitIllegalRedeclaration(this);
    } else {
      Comment cmnt(masm_, "[ declarations");
      ProcessDeclarations(scope->declarations());
      // Bail out if a stack-overflow exception occurred when
      // processing declarations.
      if (HasStackOverflow()) return;
    }

    if (FLAG_trace) {
      __ CallRuntime(Runtime::kTraceEnter, 1);
      __ push(eax);
    }
    CheckStack();

    // Compile the body of the function in a vanilla state. Don't
    // bother compiling all the code if the scope has an illegal
    // redeclaration.
    if (!scope->HasIllegalRedeclaration()) {
      Comment cmnt(masm_, "[ function body");
#ifdef DEBUG
      bool is_builtin = Bootstrapper::IsActive();
      bool should_trace =
          is_builtin ? FLAG_trace_builtin_calls : FLAG_trace_calls;
      if (should_trace) {
        __ CallRuntime(Runtime::kDebugTrace, 1);
        __ push(eax);
      }
#endif
      VisitStatements(body);

      // Generate a return statement if necessary.
      if (body->is_empty() || body->last()->AsReturnStatement() == NULL) {
        Literal undefined(Factory::undefined_value());
        ReturnStatement statement(&undefined);
        statement.set_statement_pos(fun->end_position());
        VisitReturnStatement(&statement);
      }
    }
  }

  // Code generation state must be reset.
  scope_ = NULL;
  ASSERT(!has_cc());
  ASSERT(state_ == NULL);
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
      return ParameterOperand(index);

    case Slot::LOCAL: {
      ASSERT(0 <= index && index < scope()->num_stack_slots());
      const int kLocal0Offset = JavaScriptFrameConstants::kLocal0Offset;
      return Operand(ebp, kLocal0Offset - index * kPointerSize);
    }

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


// Loads a value on TOS. If it is a boolean value, the result may have been
// (partially) translated into branches, or it may have set the condition code
// register. If force_cc is set, the value is forced to set the condition code
// register and no value is pushed. If the condition code register was set,
// has_cc() is true and cc_reg_ contains the condition to test for 'true'.
void CodeGenerator::LoadCondition(Expression* x,
                                  TypeofState typeof_state,
                                  Label* true_target,
                                  Label* false_target,
                                  bool force_cc) {
  ASSERT(!has_cc());

  { CodeGenState new_state(this, typeof_state, true_target, false_target);
    Visit(x);
  }
  if (force_cc && !has_cc()) {
    ToBoolean(true_target, false_target);
  }
  ASSERT(has_cc() || !force_cc);
}


void CodeGenerator::Load(Expression* x, TypeofState typeof_state) {
  Label true_target;
  Label false_target;
  LoadCondition(x, typeof_state, &true_target, &false_target, false);

  if (has_cc()) {
    // convert cc_reg_ into a bool

    Label loaded, materialize_true;
    __ j(cc_reg_, &materialize_true);
    __ push(Immediate(Factory::false_value()));
    __ jmp(&loaded);
    __ bind(&materialize_true);
    __ push(Immediate(Factory::true_value()));
    __ bind(&loaded);
    cc_reg_ = no_condition;
  }

  if (true_target.is_linked() || false_target.is_linked()) {
    // we have at least one condition value
    // that has been "translated" into a branch,
    // thus it needs to be loaded explicitly again
    Label loaded;
    __ jmp(&loaded);  // don't lose current TOS
    bool both = true_target.is_linked() && false_target.is_linked();
    // reincarnate "true", if necessary
    if (true_target.is_linked()) {
      __ bind(&true_target);
      __ push(Immediate(Factory::true_value()));
    }
    // if both "true" and "false" need to be reincarnated,
    // jump across code for "false"
    if (both)
      __ jmp(&loaded);
    // reincarnate "false", if necessary
    if (false_target.is_linked()) {
      __ bind(&false_target);
      __ push(Immediate(Factory::false_value()));
    }
    // everything is loaded at this point
    __ bind(&loaded);
  }
  ASSERT(!has_cc());
}


void CodeGenerator::LoadGlobal() {
  __ push(GlobalObject());
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
      LoadGlobal();
      ref->set_type(Reference::NAMED);
    } else {
      ASSERT(var->slot() != NULL);
      ref->set_type(Reference::SLOT);
    }
  } else {
    // Anything else is a runtime error.
    Load(e);
    __ CallRuntime(Runtime::kThrowReferenceError, 1);
  }
}


void CodeGenerator::UnloadReference(Reference* ref) {
  // Pop a reference from the stack while preserving TOS.
  Comment cmnt(masm_, "[ UnloadReference");
  int size = ref->size();
  if (size <= 0) {
    // Do nothing. No popping is necessary.
  } else if (size == 1) {
    __ pop(eax);
    __ mov(TOS, eax);
  } else {
    __ pop(eax);
    __ add(Operand(esp), Immediate(size * kPointerSize));
    __ push(eax);
  }
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
void CodeGenerator::ToBoolean(Label* true_target, Label* false_target) {
  Comment cmnt(masm_, "[ ToBoolean");

  // The value to convert should be popped from the stack.
  __ pop(eax);

  // Fast case checks.

  // 'false' => false.
  __ cmp(eax, Factory::false_value());
  __ j(equal, false_target);

  // 'true' => true.
  __ cmp(eax, Factory::true_value());
  __ j(equal, true_target);

  // 'undefined' => false.
  __ cmp(eax, Factory::undefined_value());
  __ j(equal, false_target);

  // Smi => false iff zero.
  ASSERT(kSmiTag == 0);
  __ test(eax, Operand(eax));
  __ j(zero, false_target);
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, true_target);

  // Call the stub for all other cases.
  __ push(eax);  // Undo the pop(eax) from above.
  ToBooleanStub stub;
  __ CallStub(&stub);
  // Convert result (eax) to condition code.
  __ test(eax, Operand(eax));

  ASSERT(not_equal == not_zero);
  cc_reg_ = not_equal;
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


class GenericBinaryOpStub: public CodeStub {
 public:
  GenericBinaryOpStub(Token::Value op, OverwriteMode mode)
      : op_(op), mode_(mode) { }

 private:
  Token::Value op_;
  OverwriteMode mode_;

  const char* GetName();

#ifdef DEBUG
  void Print() {
    PrintF("GenericBinaryOpStub (op %s), (mode %d)\n",
           Token::String(op_),
           static_cast<int>(mode_));
  }
#endif

  // Minor key encoding in 16 bits OOOOOOOOOOOOOOMM.
  class ModeBits: public BitField<OverwriteMode, 0, 2> {};
  class OpBits: public BitField<Token::Value, 2, 14> {};

  Major MajorKey() { return GenericBinaryOp; }
  int MinorKey() {
    // Encode the parameters in a unique 16 bit value.
    return OpBits::encode(op_) |
           ModeBits::encode(mode_);
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


void CodeGenerator::GenericBinaryOperation(Token::Value op,
                                               OverwriteMode overwrite_mode) {
  Comment cmnt(masm_, "[ BinaryOperation");
  Comment cmnt_token(masm_, Token::String(op));
  switch (op) {
    case Token::ADD:
    case Token::SUB:
    case Token::MUL:
    case Token::DIV:
    case Token::MOD: {
      GenericBinaryOpStub stub(op, overwrite_mode);
      __ CallStub(&stub);
      __ push(eax);
      break;
    }
    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR: {
      Label slow, exit;
      __ pop(eax);  // get y
      __ pop(edx);  // get x
      __ mov(ecx, Operand(edx));  // Prepare smi check.
      // tag check
      __ or_(ecx, Operand(eax));  // ecx = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &slow, taken);
      switch (op) {
        case Token::BIT_OR:  __ or_(eax, Operand(edx)); break;
        case Token::BIT_AND: __ and_(eax, Operand(edx)); break;
        case Token::BIT_XOR: __ xor_(eax, Operand(edx)); break;
        default: UNREACHABLE();
      }
      __ jmp(&exit);
      __ bind(&slow);
      __ push(edx);  // restore stack slots
      __ push(eax);
      GenericBinaryOpStub stub(op, overwrite_mode);
      __ CallStub(&stub);
      __ bind(&exit);
      __ push(eax);  // push the result to the stack
      break;
    }
    case Token::SHL:
    case Token::SHR:
    case Token::SAR: {
      Label slow, exit;
      __ pop(edx);  // get y
      __ pop(eax);  // get x
      // tag check
      __ mov(ecx, Operand(edx));
      __ or_(ecx, Operand(eax));  // ecx = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &slow, not_taken);
      // get copies of operands
      __ mov(ebx, Operand(eax));
      __ mov(ecx, Operand(edx));
      // remove tags from operands (but keep sign)
      __ sar(ebx, kSmiTagSize);
      __ sar(ecx, kSmiTagSize);
      // perform operation
      switch (op) {
        case Token::SAR:
          __ sar(ebx);
          // no checks of result necessary
          break;
        case Token::SHR:
          __ shr(ebx);
          // Check that the *unsigned* result fits in a smi.
          // neither of the two high-order bits can be set:
          // - 0x80000000: high bit would be lost when smi tagging.
          // - 0x40000000: this number would convert to negative when
          // smi tagging these two cases can only happen with shifts
          // by 0 or 1 when handed a valid smi.
          __ test(ebx, Immediate(0xc0000000));
          __ j(not_zero, &slow, not_taken);
          break;
        case Token::SHL:
          __ shl(ebx);
          // Check that the *signed* result fits in a smi.
          __ lea(ecx, Operand(ebx, 0x40000000));
          __ test(ecx, Immediate(0x80000000));
          __ j(not_zero, &slow, not_taken);
          break;
        default: UNREACHABLE();
      }
      // tag result and store it in TOS (eax)
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(eax, Operand(ebx, times_2, kSmiTag));
      __ jmp(&exit);
      // slow case
      __ bind(&slow);
      __ push(eax);  // restore stack
      __ push(edx);
        GenericBinaryOpStub stub(op, overwrite_mode);
      __ CallStub(&stub);
      __ bind(&exit);
      __ push(eax);
      break;
    }
    case Token::COMMA: {
      // simply discard left value
      __ pop(eax);
      __ add(Operand(esp), Immediate(kPointerSize));
      __ push(eax);
      break;
    }
    default: UNREACHABLE();
  }
}


class DeferredInlinedSmiOperation: public DeferredCode {
 public:
  DeferredInlinedSmiOperation(CodeGenerator* generator,
                              Token::Value op, int value,
                              OverwriteMode overwrite_mode) :
      DeferredCode(generator), op_(op), value_(value),
      overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiOperation");
  }
  virtual void Generate() {
    __ push(eax);
    __ push(Immediate(Smi::FromInt(value_)));
    GenericBinaryOpStub igostub(op_, overwrite_mode_);
    __ CallStub(&igostub);
  }

 private:
  Token::Value op_;
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiOperationReversed: public DeferredCode {
 public:
  DeferredInlinedSmiOperationReversed(CodeGenerator* generator,
                                      Token::Value op, int value,
                                      OverwriteMode overwrite_mode) :
      DeferredCode(generator), op_(op), value_(value),
      overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiOperationReversed");
  }
  virtual void Generate() {
    __ push(Immediate(Smi::FromInt(value_)));
    __ push(eax);
    GenericBinaryOpStub igostub(op_, overwrite_mode_);
    __ CallStub(&igostub);
  }

 private:
  Token::Value op_;
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiAdd: public DeferredCode {
 public:
  DeferredInlinedSmiAdd(CodeGenerator* generator, int value,
                        OverwriteMode overwrite_mode) :
      DeferredCode(generator), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiAdd");
  }

  virtual void Generate() {
    // Undo the optimistic add operation and call the shared stub.
    Immediate immediate(Smi::FromInt(value_));
    __ sub(Operand(eax), immediate);
    __ push(eax);
    __ push(immediate);
    GenericBinaryOpStub igostub(Token::ADD, overwrite_mode_);
    __ CallStub(&igostub);
  }

 private:
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiAddReversed: public DeferredCode {
 public:
  DeferredInlinedSmiAddReversed(CodeGenerator* generator, int value,
                        OverwriteMode overwrite_mode) :
      DeferredCode(generator), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiAddReversed");
  }

  virtual void Generate() {
    // Undo the optimistic add operation and call the shared stub.
    Immediate immediate(Smi::FromInt(value_));
    __ sub(Operand(eax), immediate);
    __ push(immediate);
    __ push(eax);
    GenericBinaryOpStub igostub(Token::ADD, overwrite_mode_);
    __ CallStub(&igostub);
  }

 private:
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiSub: public DeferredCode {
 public:
  DeferredInlinedSmiSub(CodeGenerator* generator, int value,
                        OverwriteMode overwrite_mode) :
      DeferredCode(generator), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiSub");
  }

  virtual void Generate() {
    // Undo the optimistic sub operation and call the shared stub.
    Immediate immediate(Smi::FromInt(value_));
    __ add(Operand(eax), immediate);
    __ push(eax);
    __ push(immediate);
    GenericBinaryOpStub igostub(Token::SUB, overwrite_mode_);
    __ CallStub(&igostub);
  }

 private:
  int value_;
  OverwriteMode overwrite_mode_;
};


class DeferredInlinedSmiSubReversed: public DeferredCode {
 public:
  // tos_reg is used to save the TOS value before reversing the operands
  // eax will contain the immediate value after undoing the optimistic sub.
  DeferredInlinedSmiSubReversed(CodeGenerator* generator, Register tos_reg,
                                OverwriteMode overwrite_mode) :
      DeferredCode(generator), tos_reg_(tos_reg),
      overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlinedSmiSubReversed");
  }

  virtual void Generate() {
    // Undo the optimistic sub operation and call the shared stub.
    __ add(eax, Operand(tos_reg_));
    __ push(eax);
    __ push(Operand(tos_reg_));
    GenericBinaryOpStub igostub(Token::SUB, overwrite_mode_);
    __ CallStub(&igostub);
  }

 private:
  Register tos_reg_;
  OverwriteMode overwrite_mode_;
};


void CodeGenerator::SmiOperation(Token::Value op,
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
  int int_value = Smi::cast(*value)->value();
  ASSERT(is_intn(int_value, kMaxSmiInlinedBits));

  switch (op) {
    case Token::ADD: {
      DeferredCode* deferred = NULL;
      if (!reversed) {
        deferred = new DeferredInlinedSmiAdd(this, int_value, overwrite_mode);
      } else {
        deferred = new DeferredInlinedSmiAddReversed(this, int_value,
                                                     overwrite_mode);
      }
      __ pop(eax);
      __ add(Operand(eax), Immediate(value));
      __ j(overflow, deferred->enter(), not_taken);
      __ test(eax, Immediate(kSmiTagMask));
      __ j(not_zero, deferred->enter(), not_taken);
      __ bind(deferred->exit());
      __ push(eax);
      break;
    }

    case Token::SUB: {
      DeferredCode* deferred = NULL;
      __ pop(eax);
      if (!reversed) {
        deferred = new DeferredInlinedSmiSub(this, int_value, overwrite_mode);
        __ sub(Operand(eax), Immediate(value));
      } else {
        deferred = new DeferredInlinedSmiSubReversed(this, edx, overwrite_mode);
        __ mov(edx, Operand(eax));
        __ mov(Operand(eax), Immediate(value));
        __ sub(eax, Operand(edx));
      }
      __ j(overflow, deferred->enter(), not_taken);
      __ test(eax, Immediate(kSmiTagMask));
      __ j(not_zero, deferred->enter(), not_taken);
      __ bind(deferred->exit());
      __ push(eax);
      break;
    }

    case Token::SAR: {
      if (reversed) {
        __ pop(eax);
        __ push(Immediate(value));
        __ push(eax);
        GenericBinaryOperation(op, overwrite_mode);
      } else {
        int shift_value = int_value & 0x1f;  // only least significant 5 bits
        DeferredCode* deferred =
          new DeferredInlinedSmiOperation(this, Token::SAR, shift_value,
                                          overwrite_mode);
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ j(not_zero, deferred->enter(), not_taken);
        __ sar(eax, shift_value);
        __ and_(eax, ~kSmiTagMask);
        __ bind(deferred->exit());
        __ push(eax);
      }
      break;
    }

    case Token::SHR: {
      if (reversed) {
        __ pop(eax);
        __ push(Immediate(value));
        __ push(eax);
        GenericBinaryOperation(op, overwrite_mode);
      } else {
        int shift_value = int_value & 0x1f;  // only least significant 5 bits
        DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, Token::SHR, shift_value,
                                        overwrite_mode);
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ mov(ebx, Operand(eax));
        __ j(not_zero, deferred->enter(), not_taken);
        __ sar(ebx, kSmiTagSize);
        __ shr(ebx, shift_value);
        __ test(ebx, Immediate(0xc0000000));
        __ j(not_zero, deferred->enter(), not_taken);
        // tag result and store it in TOS (eax)
        ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
        __ lea(eax, Operand(ebx, times_2, kSmiTag));
        __ bind(deferred->exit());
        __ push(eax);
      }
      break;
    }

    case Token::SHL: {
      if (reversed) {
        __ pop(eax);
        __ push(Immediate(value));
        __ push(eax);
        GenericBinaryOperation(op, overwrite_mode);
      } else {
        int shift_value = int_value & 0x1f;  // only least significant 5 bits
        DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, Token::SHL, shift_value,
                                        overwrite_mode);
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ mov(ebx, Operand(eax));
        __ j(not_zero, deferred->enter(), not_taken);
        __ sar(ebx, kSmiTagSize);
        __ shl(ebx, shift_value);
        __ lea(ecx, Operand(ebx, 0x40000000));
        __ test(ecx, Immediate(0x80000000));
        __ j(not_zero, deferred->enter(), not_taken);
        // tag result and store it in TOS (eax)
        ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
        __ lea(eax, Operand(ebx, times_2, kSmiTag));
        __ bind(deferred->exit());
        __ push(eax);
      }
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_XOR:
    case Token::BIT_AND: {
      DeferredCode* deferred = NULL;
      if (!reversed) {
        deferred =  new DeferredInlinedSmiOperation(this, op, int_value,
                                                    overwrite_mode);
      } else {
        deferred = new DeferredInlinedSmiOperationReversed(this, op, int_value,
                                                           overwrite_mode);
      }
      __ pop(eax);
      __ test(eax, Immediate(kSmiTagMask));
      __ j(not_zero, deferred->enter(), not_taken);
      if (op == Token::BIT_AND) {
        __ and_(Operand(eax), Immediate(value));
      } else if (op == Token::BIT_XOR) {
        __ xor_(Operand(eax), Immediate(value));
      } else {
        ASSERT(op == Token::BIT_OR);
        __ or_(Operand(eax), Immediate(value));
      }
      __ bind(deferred->exit());
      __ push(eax);
      break;
    }

    default: {
      if (!reversed) {
        __ push(Immediate(value));
      } else {
        __ pop(eax);
        __ push(Immediate(value));
        __ push(eax);
      }
      GenericBinaryOperation(op, overwrite_mode);
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


void CodeGenerator::Comparison(Condition cc, bool strict) {
  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == equal);

  // Implement '>' and '<=' by reversal to obtain ECMA-262 conversion order.
  if (cc == greater || cc == less_equal) {
    cc = ReverseCondition(cc);
    __ pop(edx);
    __ pop(eax);
  } else {
    __ pop(eax);
    __ pop(edx);
  }

  Label is_smi, done;
  CompareStub stub(cc, strict);

  __ mov(ecx, Operand(eax));
  __ or_(ecx, Operand(edx));
  __ test(ecx, Immediate(kSmiTagMask));
  __ j(zero, &is_smi, taken);

  // When non-smi, call out to the compare stub.  "parameters" setup by
  // calling code in edx and eax and "result" is returned in the flags.
  __ CallStub(&stub);
  __ cmp(eax, 0);
  __ jmp(&done);

  // Test smi equality by pointer comparison.
  __ bind(&is_smi);
  __ cmp(edx, Operand(eax));
  // Fall through to |done|.

  __ bind(&done);
  cc_reg_ = cc;
}


class SmiComparisonDeferred: public DeferredCode {
 public:
  SmiComparisonDeferred(CodeGenerator* generator,
                        Condition cc,
                        bool strict,
                        int value)
      : DeferredCode(generator), cc_(cc), strict_(strict), value_(value) {
    set_comment("[ ComparisonDeferred");
  }
  virtual void Generate();

 private:
  Condition cc_;
  bool strict_;
  int value_;
};


void SmiComparisonDeferred::Generate() {
  CompareStub stub(cc_, strict_);
  // Setup parameters and call stub.
  __ mov(edx, Operand(eax));
  __ mov(Operand(eax), Immediate(Smi::FromInt(value_)));
  __ CallStub(&stub);
  __ cmp(eax, 0);
  // "result" is returned in the flags
}


void CodeGenerator::SmiComparison(Condition cc,
                                      Handle<Object> value,
                                      bool strict) {
  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == equal);

  int int_value = Smi::cast(*value)->value();
  ASSERT(is_intn(int_value, kMaxSmiInlinedBits));

  SmiComparisonDeferred* deferred =
      new SmiComparisonDeferred(this, cc, strict, int_value);
  __ pop(eax);
  __ test(eax, Immediate(kSmiTagMask));
  __ j(not_zero, deferred->enter(), not_taken);
  // Test smi equality by pointer comparison.
  __ cmp(Operand(eax), Immediate(value));
  __ bind(deferred->exit());
  cc_reg_ = cc;
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
  for (int i = 0; i < args->length(); i++) {
    Load(args->at(i));
  }

  // Record the position for debugging purposes.
  __ RecordPosition(position);

  // Use the shared code stub to call the function.
  CallFunctionStub call_function(args->length());
  __ CallStub(&call_function);

  // Restore context and pop function from the stack.
  __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
  __ mov(TOS, eax);
}


void CodeGenerator::Branch(bool if_true, Label* L) {
  ASSERT(has_cc());
  Condition cc = if_true ? cc_reg_ : NegateCondition(cc_reg_);
  __ j(cc, L);
  cc_reg_ = no_condition;
}


void CodeGenerator::CheckStack() {
  if (FLAG_check_stack) {
    Label stack_is_ok;
    StackCheckStub stub;
    ExternalReference stack_guard_limit =
        ExternalReference::address_of_stack_guard_limit();
    __ cmp(esp, Operand::StaticVariable(stack_guard_limit));
    __ j(above_equal, &stack_is_ok, taken);
    __ CallStub(&stub);
    __ bind(&stack_is_ok);
  }
}


void CodeGenerator::VisitBlock(Block* node) {
  Comment cmnt(masm_, "[ Block");
  RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);
  VisitStatements(node->statements());
  __ bind(node->break_target());
}


void CodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  __ push(Immediate(pairs));
  __ push(Operand(esi));
  __ push(Immediate(Smi::FromInt(is_eval() ? 1 : 0)));
  __ CallRuntime(Runtime::kDeclareGlobals, 3);
  // Return value is ignored.
}


void CodeGenerator::VisitDeclaration(Declaration* node) {
  Comment cmnt(masm_, "[ Declaration");
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
    __ push(Operand(esi));
    __ push(Immediate(var->name()));
    // Declaration nodes are always introduced in one of two modes.
    ASSERT(node->mode() == Variable::VAR || node->mode() == Variable::CONST);
    PropertyAttributes attr = node->mode() == Variable::VAR ? NONE : READ_ONLY;
    __ push(Immediate(Smi::FromInt(attr)));
    // Push initial value, if any.
    // Note: For variables we must not push an initial value (such as
    // 'undefined') because we may have a (legal) redeclaration and we
    // must not destroy the current value.
    if (node->mode() == Variable::CONST) {
      __ push(Immediate(Factory::the_hole_value()));
    } else if (node->fun() != NULL) {
      Load(node->fun());
    } else {
      __ push(Immediate(0));  // no initial value!
    }
    __ CallRuntime(Runtime::kDeclareContextSlot, 4);
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
    // Set initial value.
    Reference target(this, node->proxy());
    ASSERT(target.is_slot());
    Load(val);
    target.SetValue(NOT_CONST_INIT);
    // Get rid of the assigned value (declarations are statements).  It's
    // safe to pop the value lying on top of the reference before unloading
    // the reference itself (which preserves the top of stack) because we
    // know that it is a zero-sized reference.
    __ pop(eax);  // Pop(no_reg);
  }
}


void CodeGenerator::VisitExpressionStatement(ExpressionStatement* node) {
  Comment cmnt(masm_, "[ ExpressionStatement");
  RecordStatementPosition(node);
  Expression* expression = node->expression();
  expression->MarkAsStatement();
  Load(expression);
  __ pop(eax);  // remove the lingering expression result from the top of stack
}


void CodeGenerator::VisitEmptyStatement(EmptyStatement* node) {
  Comment cmnt(masm_, "// EmptyStatement");
  // nothing to do
}


void CodeGenerator::VisitIfStatement(IfStatement* node) {
  Comment cmnt(masm_, "[ IfStatement");
  // Generate different code depending on which
  // parts of the if statement are present or not.
  bool has_then_stm = node->HasThenStatement();
  bool has_else_stm = node->HasElseStatement();

  RecordStatementPosition(node);
  Label exit;
  if (has_then_stm && has_else_stm) {
    Label then;
    Label else_;
    // if (cond)
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &then, &else_, true);
    Branch(false, &else_);
    // then
    __ bind(&then);
    Visit(node->then_statement());
    __ jmp(&exit);
    // else
    __ bind(&else_);
    Visit(node->else_statement());

  } else if (has_then_stm) {
    ASSERT(!has_else_stm);
    Label then;
    // if (cond)
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &then, &exit, true);
    Branch(false, &exit);
    // then
    __ bind(&then);
    Visit(node->then_statement());

  } else if (has_else_stm) {
    ASSERT(!has_then_stm);
    Label else_;
    // if (!cond)
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &exit, &else_, true);
    Branch(true, &exit);
    // else
    __ bind(&else_);
    Visit(node->else_statement());

  } else {
    ASSERT(!has_then_stm && !has_else_stm);
    // if (cond)
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &exit, &exit, false);
    if (has_cc()) {
      cc_reg_ = no_condition;
    } else {
      // No cc value set up, that means the boolean was pushed.
      // Pop it again, since it is not going to be used.
      __ pop(eax);
    }
  }

  // end
  __ bind(&exit);
}


void CodeGenerator::CleanStack(int num_bytes) {
  ASSERT(num_bytes >= 0);
  if (num_bytes > 0) {
    __ add(Operand(esp), Immediate(num_bytes));
  }
}


void CodeGenerator::VisitContinueStatement(ContinueStatement* node) {
  Comment cmnt(masm_, "[ ContinueStatement");
  RecordStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  __ jmp(node->target()->continue_target());
}


void CodeGenerator::VisitBreakStatement(BreakStatement* node) {
  Comment cmnt(masm_, "[ BreakStatement");
  RecordStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  __ jmp(node->target()->break_target());
}


void CodeGenerator::VisitReturnStatement(ReturnStatement* node) {
  Comment cmnt(masm_, "[ ReturnStatement");
  RecordStatementPosition(node);
  Load(node->expression());

  // Move the function result into eax
  __ pop(eax);

  // If we're inside a try statement or the return instruction
  // sequence has been generated, we just jump to that
  // point. Otherwise, we generate the return instruction sequence and
  // bind the function return label.
  if (is_inside_try_ || function_return_.is_bound()) {
    __ jmp(&function_return_);
  } else {
    __ bind(&function_return_);
    if (FLAG_trace) {
      __ push(eax);  // undo the pop(eax) from above
      __ CallRuntime(Runtime::kTraceExit, 1);
    }

    // Add a label for checking the size of the code used for returning.
    Label check_exit_codesize;
    __ bind(&check_exit_codesize);

    // Leave the frame and return popping the arguments and the
    // receiver.
    ExitJSFrame();
    __ ret((scope_->num_parameters() + 1) * kPointerSize);

    // Check that the size of the code used for returning matches what is
    // expected by the debugger.
    ASSERT_EQ(Debug::kIa32JSReturnSequenceLength,
              __ SizeOfCodeGeneratedSince(&check_exit_codesize));
  }
}


void CodeGenerator::VisitWithEnterStatement(WithEnterStatement* node) {
  Comment cmnt(masm_, "[ WithEnterStatement");
  RecordStatementPosition(node);
  Load(node->expression());
  __ CallRuntime(Runtime::kPushContext, 1);

  if (kDebug) {
    Label verified_true;
    // Verify eax and esi are the same in debug mode
    __ cmp(eax, Operand(esi));
    __ j(equal, &verified_true);
    __ int3();
    __ bind(&verified_true);
  }

  // Update context local.
  __ mov(Operand(ebp, StandardFrameConstants::kContextOffset), esi);
}


void CodeGenerator::VisitWithExitStatement(WithExitStatement* node) {
  Comment cmnt(masm_, "[ WithExitStatement");
  // Pop context.
  __ mov(esi, ContextOperand(esi, Context::PREVIOUS_INDEX));
  // Update context local.
  __ mov(Operand(ebp, StandardFrameConstants::kContextOffset), esi);
}

int CodeGenerator::FastCaseSwitchMaxOverheadFactor() {
    return kFastSwitchMaxOverheadFactor;
}

int CodeGenerator::FastCaseSwitchMinCaseCount() {
    return kFastSwitchMinCaseCount;
}

// Generate a computed jump to a switch case.
void CodeGenerator::GenerateFastCaseSwitchJumpTable(
    SwitchStatement* node, int min_index, int range, Label *fail_label,
    SmartPointer<Label*> &case_targets, SmartPointer<Label> &case_labels) {
  // Notice: Internal references, used by both the jmp instruction and
  // the table entries, need to be relocated if the buffer grows. This
  // prevents the forward use of Labels, since a displacement cannot
  // survive relocation, and it also cannot safely be distinguished
  // from a real address.  Instead we put in zero-values as
  // placeholders, and fill in the addresses after the labels have been
  // bound.

  __ pop(eax);  // supposed smi
  // check range of value, if outside [0..length-1] jump to default/end label.
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  if (min_index != 0) {
    __ sub(Operand(eax), Immediate(min_index << kSmiTagSize));
  }
  __ test(eax, Immediate(0x80000000 | kSmiTagMask));  // negative or not Smi
  __ j(not_equal, fail_label, not_taken);
  __ cmp(eax, range << kSmiTagSize);
  __ j(greater_equal, fail_label, not_taken);

  // 0 is placeholder.
  __ jmp(Operand(eax, times_2, 0x0, RelocInfo::INTERNAL_REFERENCE));
  // calculate address to overwrite later with actual address of table.
  int32_t jump_table_ref = __ pc_offset() - sizeof(int32_t);

  __ Align(4);
  Label table_start;
  __ bind(&table_start);
  __ WriteInternalReference(jump_table_ref, table_start);

  for (int i = 0; i < range; i++) {
    // table entry, 0 is placeholder for case address
    __ dd(0x0, RelocInfo::INTERNAL_REFERENCE);
  }

  GenerateFastCaseSwitchCases(node, case_labels);

  for (int i = 0, entry_pos = table_start.pos();
       i < range; i++, entry_pos += sizeof(uint32_t)) {
    __ WriteInternalReference(entry_pos, *case_targets[i]);
  }
}


void CodeGenerator::VisitSwitchStatement(SwitchStatement* node) {
  Comment cmnt(masm_, "[ SwitchStatement");
  RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);

  Load(node->tag());

  if (TryGenerateFastCaseSwitchStatement(node)) {
    return;
  }

  Label next, fall_through, default_case;
  ZoneList<CaseClause*>* cases = node->cases();
  int length = cases->length();

  for (int i = 0; i < length; i++) {
    CaseClause* clause = cases->at(i);
    Comment cmnt(masm_, "[ case clause");

    if (clause->is_default()) {
      // Continue matching cases. The program will execute the default case's
      // statements if it does not match any of the cases.
      __ jmp(&next);

      // Bind the default case label, so we can branch to it when we
      // have compared against all other cases.
      ASSERT(default_case.is_unused());  // at most one default clause
      __ bind(&default_case);
    } else {
      __ bind(&next);
      next.Unuse();
      __ mov(eax, TOS);
      __ push(eax);  // duplicate TOS
      Load(clause->label());
      Comparison(equal, true);
      Branch(false, &next);
    }

    // Entering the case statement for the first time. Remove the switch value
    // from the stack.
    __ pop(eax);

    // Generate code for the body.
    // This is also the target for the fall through from the previous case's
    // statements which has to skip over the matching code and the popping of
    // the switch value.
    __ bind(&fall_through);
    fall_through.Unuse();
    VisitStatements(clause->statements());
    __ jmp(&fall_through);
  }

  __ bind(&next);
  // Reached the end of the case statements without matching any of the cases.
  if (default_case.is_bound()) {
    // A default case exists -> execute its statements.
    __ jmp(&default_case);
  } else {
    // Remove the switch value from the stack.
    __ pop(eax);
  }

  __ bind(&fall_through);
  __ bind(node->break_target());
}


void CodeGenerator::VisitLoopStatement(LoopStatement* node) {
  Comment cmnt(masm_, "[ LoopStatement");
  RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);

  // simple condition analysis
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

  Label loop, entry;

  // init
  if (node->init() != NULL) {
    ASSERT(node->type() == LoopStatement::FOR_LOOP);
    Visit(node->init());
  }
  if (node->type() != LoopStatement::DO_LOOP && info != ALWAYS_TRUE) {
    __ jmp(&entry);
  }

  // body
  __ bind(&loop);
  CheckStack();  // TODO(1222600): ignore if body contains calls.
  Visit(node->body());

  // next
  __ bind(node->continue_target());
  if (node->next() != NULL) {
    // Record source position of the statement as this code which is after the
    // code for the body actually belongs to the loop statement and not the
    // body.
    RecordStatementPosition(node);
    __ RecordPosition(node->statement_pos());
    ASSERT(node->type() == LoopStatement::FOR_LOOP);
    Visit(node->next());
  }

  // cond
  __ bind(&entry);
  switch (info) {
    case ALWAYS_TRUE:
      __ jmp(&loop);
      break;
    case ALWAYS_FALSE:
      break;
    case DONT_KNOW:
      LoadCondition(node->cond(), NOT_INSIDE_TYPEOF, &loop,
                    node->break_target(), true);
      Branch(true, &loop);
      break;
  }

  // exit
  __ bind(node->break_target());
}


void CodeGenerator::VisitForInStatement(ForInStatement* node) {
  Comment cmnt(masm_, "[ ForInStatement");
  RecordStatementPosition(node);

  // We keep stuff on the stack while the body is executing.
  // Record it, so that a break/continue crossing this statement
  // can restore the stack.
  const int kForInStackSize = 5 * kPointerSize;
  break_stack_height_ += kForInStackSize;
  node->set_break_stack_height(break_stack_height_);

  Label loop, next, entry, cleanup, exit, primitive, jsobject;
  Label end_del_check, fixed_array;

  // Get the object to enumerate over (converted to JSObject).
  Load(node->enumerable());

  // Both SpiderMonkey and kjs ignore null and undefined in contrast
  // to the specification.  12.6.4 mandates a call to ToObject.
  __ pop(eax);

  // eax: value to be iterated over
  __ cmp(eax, Factory::undefined_value());
  __ j(equal, &exit);
  __ cmp(eax, Factory::null_value());
  __ j(equal, &exit);

  // Stack layout in body:
  // [iteration counter (smi)] <- slot 0
  // [length of array]         <- slot 1
  // [FixedArray]              <- slot 2
  // [Map or 0]                <- slot 3
  // [Object]                  <- slot 4

  // Check if enumerable is already a JSObject
  // eax: value to be iterated over
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &primitive);
  __ mov(ecx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  __ cmp(ecx, FIRST_JS_OBJECT_TYPE);
  __ j(above_equal, &jsobject);

  __ bind(&primitive);
  __ push(eax);
  __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
  // function call returns the value in eax, which is where we want it below


  __ bind(&jsobject);

  // Get the set of properties (as a FixedArray or Map).
  // eax: value to be iterated over
  __ push(eax);  // push the object being iterated over (slot 4)

  __ push(eax);  // push the Object (slot 4) for the runtime call
  __ CallRuntime(Runtime::kGetPropertyNamesFast, 1);

  // If we got a Map, we can do a fast modification check.
  // Otherwise, we got a FixedArray, and we have to do a slow check.
  // eax: map or fixed array (result from call to
  // Runtime::kGetPropertyNamesFast)
  __ mov(edx, Operand(eax));
  __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));
  __ cmp(ecx, Factory::meta_map());
  __ j(not_equal, &fixed_array);

  // Get enum cache
  // eax: map (result from call to Runtime::kGetPropertyNamesFast)
  __ mov(ecx, Operand(eax));
  __ mov(ecx, FieldOperand(ecx, Map::kInstanceDescriptorsOffset));
  // Get the bridge array held in the enumeration index field.
  __ mov(ecx, FieldOperand(ecx, DescriptorArray::kEnumerationIndexOffset));
  // Get the cache from the bridge array.
  __ mov(edx, FieldOperand(ecx, DescriptorArray::kEnumCacheBridgeCacheOffset));

  __ push(eax);  // <- slot 3
  __ push(Operand(edx));  // <- slot 2
  __ mov(eax, FieldOperand(edx, FixedArray::kLengthOffset));
  __ shl(eax, kSmiTagSize);
  __ push(eax);  // <- slot 1
  __ push(Immediate(Smi::FromInt(0)));  // <- slot 0
  __ jmp(&entry);


  __ bind(&fixed_array);

  // eax: fixed array (result from call to Runtime::kGetPropertyNamesFast)
  __ push(Immediate(Smi::FromInt(0)));  // <- slot 3
  __ push(eax);  // <- slot 2

  // Push the length of the array and the initial index onto the stack.
  __ mov(eax, FieldOperand(eax, FixedArray::kLengthOffset));
  __ shl(eax, kSmiTagSize);
  __ push(eax);  // <- slot 1
  __ push(Immediate(Smi::FromInt(0)));  // <- slot 0
  __ jmp(&entry);

  // Body.
  __ bind(&loop);
  Visit(node->body());

  // Next.
  __ bind(node->continue_target());
  __ bind(&next);
  __ pop(eax);
  __ add(Operand(eax), Immediate(Smi::FromInt(1)));
  __ push(eax);

  // Condition.
  __ bind(&entry);

  __ mov(eax, Operand(esp, 0 * kPointerSize));  // load the current count
  __ cmp(eax, Operand(esp, kPointerSize));  // compare to the array length
  __ j(above_equal, &cleanup);

  // Get the i'th entry of the array.
  __ mov(edx, Operand(esp, 2 * kPointerSize));
  __ mov(ebx, Operand(edx, eax, times_2,
                      FixedArray::kHeaderSize - kHeapObjectTag));

  // Get the expected map from the stack or a zero map in the
  // permanent slow case eax: current iteration count ebx: i'th entry
  // of the enum cache
  __ mov(edx, Operand(esp, 3 * kPointerSize));
  // Check if the expected map still matches that of the enumerable.
  // If not, we have to filter the key.
  // eax: current iteration count
  // ebx: i'th entry of the enum cache
  // edx: expected map value
  __ mov(ecx, Operand(esp, 4 * kPointerSize));
  __ mov(ecx, FieldOperand(ecx, HeapObject::kMapOffset));
  __ cmp(ecx, Operand(edx));
  __ j(equal, &end_del_check);

  // Convert the entry to a string (or null if it isn't a property anymore).
  __ push(Operand(esp, 4 * kPointerSize));  // push enumerable
  __ push(Operand(ebx));  // push entry
  __ InvokeBuiltin(Builtins::FILTER_KEY, CALL_FUNCTION);
  __ mov(ebx, Operand(eax));

  // If the property has been removed while iterating, we just skip it.
  __ cmp(ebx, Factory::null_value());
  __ j(equal, &next);


  __ bind(&end_del_check);

  // Store the entry in the 'each' expression and take another spin in the loop.
  // edx: i'th entry of the enum cache (or string there of)
  __ push(ebx);
  { Reference each(this, node->each());
    if (!each.is_illegal()) {
      if (each.size() > 0) {
        __ push(Operand(esp, kPointerSize * each.size()));
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
        __ pop(eax);
      }
    }
  }
  // Discard the i'th entry pushed above or else the remainder of the
  // reference, whichever is currently on top of the stack.
  __ pop(eax);
  CheckStack();  // TODO(1222600): ignore if body contains calls.
  __ jmp(&loop);

  // Cleanup.
  __ bind(&cleanup);
  __ bind(node->break_target());
  __ add(Operand(esp), Immediate(5 * kPointerSize));

  // Exit.
  __ bind(&exit);

  break_stack_height_ -= kForInStackSize;
}


void CodeGenerator::VisitTryCatch(TryCatch* node) {
  Comment cmnt(masm_, "[ TryCatch");

  Label try_block, exit;

  __ call(&try_block);
  // --- Catch block ---
  __ push(eax);

  // Store the caught exception in the catch variable.
  { Reference ref(this, node->catch_var());
    ASSERT(ref.is_slot());
    // Load the exception to the top of the stack.  Here we make use of the
    // convenient property that it doesn't matter whether a value is
    // immediately on top of or underneath a zero-sized reference.
    ref.SetValue(NOT_CONST_INIT);
  }

  // Remove the exception from the stack.
  __ pop(edx);

  VisitStatements(node->catch_block()->statements());
  __ jmp(&exit);


  // --- Try block ---
  __ bind(&try_block);

  __ PushTryHandler(IN_JAVASCRIPT, TRY_CATCH_HANDLER);
  // TODO(1222589): remove the reliance of PushTryHandler on a cached TOS
  __ push(eax);  //

  // Introduce shadow labels for all escapes from the try block,
  // including returns. We should probably try to unify the escaping
  // labels and the return label.
  int nof_escapes = node->escaping_labels()->length();
  List<LabelShadow*> shadows(1 + nof_escapes);
  shadows.Add(new LabelShadow(&function_return_));
  for (int i = 0; i < nof_escapes; i++) {
    shadows.Add(new LabelShadow(node->escaping_labels()->at(i)));
  }

  // Generate code for the statements in the try block.
  bool was_inside_try = is_inside_try_;
  is_inside_try_ = true;
  VisitStatements(node->try_block()->statements());
  is_inside_try_ = was_inside_try;

  // Stop the introduced shadowing and count the number of required unlinks.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }

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

  // Unlink from try chain.
  __ pop(eax);
  __ mov(Operand::StaticVariable(handler_address), eax);  // TOS == next_sp
  __ add(Operand(esp), Immediate(StackHandlerConstants::kSize - kPointerSize));
  // next_sp popped.
  if (nof_unlinks > 0) __ jmp(&exit);

  // Generate unlink code for all used shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      // Unlink from try chain; be careful not to destroy the TOS.
      __ bind(shadows[i]);

      // Reload sp from the top handler, because some statements that we
      // break from (eg, for...in) may have left stuff on the stack.
      __ mov(edx, Operand::StaticVariable(handler_address));
      const int kNextOffset = StackHandlerConstants::kNextOffset +
          StackHandlerConstants::kAddressDisplacement;
      __ lea(esp, Operand(edx, kNextOffset));

      __ pop(Operand::StaticVariable(handler_address));
      __ add(Operand(esp),
             Immediate(StackHandlerConstants::kSize - kPointerSize));
      // next_sp popped.
      __ jmp(shadows[i]->shadowed());
    }
  }

  __ bind(&exit);
}


void CodeGenerator::VisitTryFinally(TryFinally* node) {
  Comment cmnt(masm_, "[ TryFinally");

  // State: Used to keep track of reason for entering the finally
  // block. Should probably be extended to hold information for
  // break/continue from within the try block.
  enum { FALLING, THROWING, JUMPING };

  Label exit, unlink, try_block, finally_block;

  __ call(&try_block);

  __ push(eax);
  // In case of thrown exceptions, this is where we continue.
  __ Set(ecx, Immediate(Smi::FromInt(THROWING)));
  __ jmp(&finally_block);


  // --- Try block ---
  __ bind(&try_block);

  __ PushTryHandler(IN_JAVASCRIPT, TRY_FINALLY_HANDLER);
  // TODO(1222589): remove the reliance of PushTryHandler on a cached TOS
  __ push(eax);

  // Introduce shadow labels for all escapes from the try block,
  // including returns. We should probably try to unify the escaping
  // labels and the return label.
  int nof_escapes = node->escaping_labels()->length();
  List<LabelShadow*> shadows(1 + nof_escapes);
  shadows.Add(new LabelShadow(&function_return_));
  for (int i = 0; i < nof_escapes; i++) {
    shadows.Add(new LabelShadow(node->escaping_labels()->at(i)));
  }

  // Generate code for the statements in the try block.
  bool was_inside_try = is_inside_try_;
  is_inside_try_ = true;
  VisitStatements(node->try_block()->statements());
  is_inside_try_ = was_inside_try;

  // Stop the introduced shadowing and count the number of required
  // unlinks.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }

  // Set the state on the stack to FALLING.
  __ push(Immediate(Factory::undefined_value()));  // fake TOS
  __ Set(ecx, Immediate(Smi::FromInt(FALLING)));
  if (nof_unlinks > 0) __ jmp(&unlink);

  // Generate code that sets the state for all used shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      __ bind(shadows[i]);
      if (shadows[i]->shadowed() == &function_return_) {
        // Materialize the return value on the stack.
        __ push(eax);
      } else {
        // Fake TOS for break and continue.
        __ push(Immediate(Factory::undefined_value()));
      }
      __ Set(ecx, Immediate(Smi::FromInt(JUMPING + i)));
      __ jmp(&unlink);
    }
  }

  // Unlink from try chain; be careful not to destroy the TOS.
  __ bind(&unlink);
  // Reload sp from the top handler, because some statements that we
  // break from (eg, for...in) may have left stuff on the stack.
  __ pop(eax);  // preserve the TOS in a register across stack manipulation
  ExternalReference handler_address(Top::k_handler_address);
  __ mov(edx, Operand::StaticVariable(handler_address));
  const int kNextOffset = StackHandlerConstants::kNextOffset +
      StackHandlerConstants::kAddressDisplacement;
  __ lea(esp, Operand(edx, kNextOffset));

  __ pop(Operand::StaticVariable(handler_address));
  __ add(Operand(esp), Immediate(StackHandlerConstants::kSize - kPointerSize));
  // next_sp popped.
  __ push(eax);  // preserve the TOS in a register across stack manipulation

  // --- Finally block ---
  __ bind(&finally_block);

  // Push the state on the stack.
  __ push(ecx);

  // We keep two elements on the stack - the (possibly faked) result
  // and the state - while evaluating the finally block. Record it, so
  // that a break/continue crossing this statement can restore the
  // stack.
  const int kFinallyStackSize = 2 * kPointerSize;
  break_stack_height_ += kFinallyStackSize;

  // Generate code for the statements in the finally block.
  VisitStatements(node->finally_block()->statements());

  // Restore state and return value or faked TOS.
  __ pop(ecx);
  __ pop(eax);
  break_stack_height_ -= kFinallyStackSize;

  // Generate code that jumps to the right destination for all used
  // shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_bound()) {
      __ cmp(Operand(ecx), Immediate(Smi::FromInt(JUMPING + i)));
      __ j(equal, shadows[i]->shadowed());
    }
  }

  // Check if we need to rethrow the exception.
  __ cmp(Operand(ecx), Immediate(Smi::FromInt(THROWING)));
  __ j(not_equal, &exit);

  // Rethrow exception.
  __ push(eax);  // undo pop from above
  __ CallRuntime(Runtime::kReThrow, 1);

  // Done.
  __ bind(&exit);
}


void CodeGenerator::VisitDebuggerStatement(DebuggerStatement* node) {
  Comment cmnt(masm_, "[ DebuggerStatement");
  RecordStatementPosition(node);
  __ CallRuntime(Runtime::kDebugBreak, 1);
  __ push(eax);
}


void CodeGenerator::InstantiateBoilerplate(Handle<JSFunction> boilerplate) {
  ASSERT(boilerplate->IsBoilerplate());

  // Push the boilerplate on the stack.
  __ push(Immediate(boilerplate));

  // Create a new closure.
  __ push(esi);
  __ CallRuntime(Runtime::kNewClosure, 2);
  __ push(eax);
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
  Label then, else_, exit;
  LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &then, &else_, true);
  Branch(false, &else_);
  __ bind(&then);
  Load(node->then_expression(), typeof_state());
  __ jmp(&exit);
  __ bind(&else_);
  Load(node->else_expression(), typeof_state());
  __ bind(&exit);
}


void CodeGenerator::LoadFromSlot(Slot* slot, TypeofState typeof_state) {
  if (slot->type() == Slot::LOOKUP) {
    ASSERT(slot->var()->mode() == Variable::DYNAMIC);

    // For now, just do a runtime call.
    __ push(esi);
    __ push(Immediate(slot->var()->name()));

    if (typeof_state == INSIDE_TYPEOF) {
      __ CallRuntime(Runtime::kLoadContextSlotNoReferenceError, 2);
    } else {
      __ CallRuntime(Runtime::kLoadContextSlot, 2);
    }
    __ push(eax);

  } else {
    // Note: We would like to keep the assert below, but it fires because of
    // some nasty code in LoadTypeofExpression() which should be removed...
    // ASSERT(slot->var()->mode() != Variable::DYNAMIC);
    if (slot->var()->mode() == Variable::CONST) {
      // Const slots may contain 'the hole' value (the constant hasn't been
      // initialized yet) which needs to be converted into the 'undefined'
      // value.
      Comment cmnt(masm_, "[ Load const");
      Label exit;
      __ mov(eax, SlotOperand(slot, ecx));
      __ cmp(eax, Factory::the_hole_value());
      __ j(not_equal, &exit);
      __ mov(eax, Factory::undefined_value());
      __ bind(&exit);
      __ push(eax);
    } else {
      __ push(SlotOperand(slot, ecx));
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
    Visit(expr);
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
    // Smis are loaded in two steps.
    int bits = reinterpret_cast<int>(*node->handle());
    __ mov(eax, bits & 0x0000FFFF);
    __ xor_(eax, bits & 0xFFFF0000);
    __ push(eax);
  } else {
    __ push(Immediate(node->handle()));
  }
}


class RegExpDeferred: public DeferredCode {
 public:
  RegExpDeferred(CodeGenerator* generator, RegExpLiteral* node)
      : DeferredCode(generator), node_(node) {
    set_comment("[ RegExpDeferred");
  }
  virtual void Generate();
 private:
  RegExpLiteral* node_;
};


void RegExpDeferred::Generate() {
  // If the entry is undefined we call the runtime system to computed
  // the literal.

  // Literal array (0).
  __ push(ecx);
  // Literal index (1).
  __ push(Immediate(Smi::FromInt(node_->literal_index())));
  // RegExp pattern (2).
  __ push(Immediate(node_->pattern()));
  // RegExp flags (3).
  __ push(Immediate(node_->flags()));
  __ CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  __ mov(ebx, Operand(eax));  // "caller" expects result in ebx
}


void CodeGenerator::VisitRegExpLiteral(RegExpLiteral* node) {
  Comment cmnt(masm_, "[ RegExp Literal");
  RegExpDeferred* deferred = new RegExpDeferred(this, node);

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ mov(ecx, FunctionOperand());

  // Load the literals array of the function.
  __ mov(ecx, FieldOperand(ecx, JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ mov(ebx, FieldOperand(ecx, literal_offset));

  // Check whether we need to materialize the RegExp object.
  // If so, jump to the deferred code.
  __ cmp(ebx, Factory::undefined_value());
  __ j(equal, deferred->enter(), not_taken);
  __ bind(deferred->exit());

  // Push the literal.
  __ push(ebx);
}


// This deferred code stub will be used for creating the boilerplate
// by calling Runtime_CreateObjectLiteral.
// Each created boilerplate is stored in the JSFunction and they are
// therefore context dependent.
class ObjectLiteralDeferred: public DeferredCode {
 public:
  ObjectLiteralDeferred(CodeGenerator* generator,
                        ObjectLiteral* node)
      : DeferredCode(generator), node_(node) {
    set_comment("[ ObjectLiteralDeferred");
  }
  virtual void Generate();
 private:
  ObjectLiteral* node_;
};


void ObjectLiteralDeferred::Generate() {
  // If the entry is undefined we call the runtime system to compute
  // the literal.

  // Literal array (0).
  __ push(Operand(ecx));
  // Literal index (1).
  __ push(Immediate(Smi::FromInt(node_->literal_index())));
  // Constant properties (2).
  __ push(Immediate(node_->constant_properties()));
  __ CallRuntime(Runtime::kCreateObjectLiteralBoilerplate, 3);
  __ mov(ebx, Operand(eax));
}


void CodeGenerator::VisitObjectLiteral(ObjectLiteral* node) {
  Comment cmnt(masm_, "[ ObjectLiteral");
  ObjectLiteralDeferred* deferred = new ObjectLiteralDeferred(this, node);

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ mov(ecx, FunctionOperand());

  // Load the literals array of the function.
  __ mov(ecx, FieldOperand(ecx, JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ mov(ebx, FieldOperand(ecx, literal_offset));

  // Check whether we need to materialize the object literal boilerplate.
  // If so, jump to the deferred code.
  __ cmp(ebx, Factory::undefined_value());
  __ j(equal, deferred->enter(), not_taken);
  __ bind(deferred->exit());

  // Push the literal.
  __ push(ebx);
  // Clone the boilerplate object.
  __ CallRuntime(Runtime::kCloneObjectLiteralBoilerplate, 1);
  // Push the new cloned literal object as the result.
  __ push(eax);


  for (int i = 0; i < node->properties()->length(); i++) {
    ObjectLiteral::Property* property  = node->properties()->at(i);
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT: break;
      case ObjectLiteral::Property::COMPUTED: {
        Handle<Object> key(property->key()->handle());
        Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
        if (key->IsSymbol()) {
          __ mov(eax, TOS);
          __ push(eax);
          Load(property->value());
          __ pop(eax);
          __ Set(ecx, Immediate(key));
          __ call(ic, RelocInfo::CODE_TARGET);
          __ add(Operand(esp), Immediate(kPointerSize));
          // Ignore result.
          break;
        }
        // Fall through
      }
      case ObjectLiteral::Property::PROTOTYPE: {
        __ mov(eax, TOS);
        __ push(eax);
        Load(property->key());
        Load(property->value());
        __ CallRuntime(Runtime::kSetProperty, 3);
        // Ignore result.
        break;
      }
      case ObjectLiteral::Property::SETTER: {
        // Duplicate the resulting object on the stack. The runtime
        // function will pop the three arguments passed in.
        __ mov(eax, TOS);
        __ push(eax);
        Load(property->key());
        __ push(Immediate(Smi::FromInt(1)));
        Load(property->value());
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        // Ignore result.
        break;
      }
      case ObjectLiteral::Property::GETTER: {
        // Duplicate the resulting object on the stack. The runtime
        // function will pop the three arguments passed in.
        __ mov(eax, TOS);
        __ push(eax);
        Load(property->key());
        __ push(Immediate(Smi::FromInt(0)));
        Load(property->value());
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        // Ignore result.
        break;
      }
      default: UNREACHABLE();
    }
  }
}


void CodeGenerator::VisitArrayLiteral(ArrayLiteral* node) {
  Comment cmnt(masm_, "[ ArrayLiteral");

  // Call runtime to create the array literal.
  __ push(Immediate(node->literals()));
  // Load the function of this frame.
  __ mov(ecx, FunctionOperand());
  // Load the literals array of the function.
  __ mov(ecx, FieldOperand(ecx, JSFunction::kLiteralsOffset));
  __ push(ecx);
  __ CallRuntime(Runtime::kCreateArrayLiteral, 2);

  // Push the resulting array literal on the stack.
  __ push(eax);

  // Generate code to set the elements in the array that are not
  // literals.
  for (int i = 0; i < node->values()->length(); i++) {
    Expression* value = node->values()->at(i);

    // If value is literal the property value is already
    // set in the boilerplate object.
    if (value->AsLiteral() == NULL) {
      // The property must be set by generated code.
      Load(value);

      // Get the value off the stack.
      __ pop(eax);
      // Fetch the object literal while leaving on the stack.
      __ mov(ecx, TOS);
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

  RecordStatementPosition(node);
  Reference target(this, node->target());
  if (target.is_illegal()) return;

  if (node->op() == Token::ASSIGN ||
      node->op() == Token::INIT_VAR ||
      node->op() == Token::INIT_CONST) {
    Load(node->value());

  } else {
    target.GetValue(NOT_INSIDE_TYPEOF);
    Literal* literal = node->value()->AsLiteral();
    if (IsInlineSmi(literal)) {
      SmiOperation(node->binary_op(), literal->handle(), false, NO_OVERWRITE);
    } else {
      Load(node->value());
      GenericBinaryOperation(node->binary_op());
    }
  }

  Variable* var = node->target()->AsVariableProxy()->AsVariable();
  if (var != NULL &&
      var->mode() == Variable::CONST &&
      node->op() != Token::INIT_VAR && node->op() != Token::INIT_CONST) {
    // Assignment ignored - leave the value on the stack.
  } else {
    __ RecordPosition(node->position());
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


void CodeGenerator::VisitThrow(Throw* node) {
  Comment cmnt(masm_, "[ Throw");

  Load(node->exception());
  __ RecordPosition(node->position());
  __ CallRuntime(Runtime::kThrow, 1);
  __ push(eax);
}


void CodeGenerator::VisitProperty(Property* node) {
  Comment cmnt(masm_, "[ Property");

  Reference property(this, node);
  property.GetValue(typeof_state());
}


void CodeGenerator::VisitCall(Call* node) {
  Comment cmnt(masm_, "[ Call");

  ZoneList<Expression*>* args = node->arguments();

  RecordStatementPosition(node);

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
    __ push(Immediate(var->name()));
    LoadGlobal();

    // Load the arguments.
    for (int i = 0; i < args->length(); i++) {
      Load(args->at(i));
    }

    // Setup the receiver register and call the IC initialization code.
    Handle<Code> stub = ComputeCallInitialize(args->length());
    __ RecordPosition(node->position());
    __ call(stub, RelocInfo::CODE_TARGET_CONTEXT);
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));

    // Overwrite the function on the stack with the result.
    __ mov(TOS, eax);

  } else if (var != NULL && var->slot() != NULL &&
             var->slot()->type() == Slot::LOOKUP) {
    // ----------------------------------
    // JavaScript example: 'with (obj) foo(1, 2, 3)'  // foo is in obj
    // ----------------------------------

    // Load the function
    __ push(Operand(esi));
    __ push(Immediate(var->name()));
    __ CallRuntime(Runtime::kLoadContextSlot, 2);
    // eax: slot value; edx: receiver

    // Load the receiver.
    __ push(eax);
    __ push(edx);

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
      __ push(Immediate(literal->handle()));
      Load(property->obj());

      // Load the arguments.
      for (int i = 0; i < args->length(); i++) Load(args->at(i));

      // Call the IC initialization code.
      Handle<Code> stub = ComputeCallInitialize(args->length());
      __ RecordPosition(node->position());
      __ call(stub, RelocInfo::CODE_TARGET);
      __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));

      // Overwrite the function on the stack with the result.
      __ mov(TOS, eax);

    } else {
      // -------------------------------------------
      // JavaScript example: 'array[index](1, 2, 3)'
      // -------------------------------------------

      // Load the function to call from the property through a reference.
      Reference ref(this, property);
      ref.GetValue(NOT_INSIDE_TYPEOF);

      // Pass receiver to called function.
      // The reference's size is non-negative.
      __ push(Operand(esp, ref.size() * kPointerSize));

      // Call the function.
      CallWithArguments(args, node->position());
    }

  } else {
    // ----------------------------------
    // JavaScript example: 'foo(1, 2, 3)'  // foo is not global
    // ----------------------------------

    // Load the function.
    Load(function);

    // Pass the global object as the receiver.
    LoadGlobal();

    // Call the function.
    CallWithArguments(args, node->position());
  }
}


void CodeGenerator::VisitCallNew(CallNew* node) {
  Comment cmnt(masm_, "[ CallNew");

  // According to ECMA-262, section 11.2.2, page 44, the function
  // expression in new calls must be evaluated before the
  // arguments. This is different from ordinary calls, where the
  // actual function to call is resolved after the arguments have been
  // evaluated.

  // Compute function to call and use the global object as the
  // receiver.
  Load(node->expression());
  LoadGlobal();

  // Push the arguments ("left-to-right") on the stack.
  ZoneList<Expression*>* args = node->arguments();
  for (int i = 0; i < args->length(); i++) Load(args->at(i));

  // Constructors are called with the number of arguments in register
  // eax for now. Another option would be to have separate construct
  // call trampolines per different arguments counts encountered.
  __ Set(eax, Immediate(args->length()));

  // Load the function into temporary function slot as per calling
  // convention.
  __ mov(edi, Operand(esp, (args->length() + 1) * kPointerSize));

  // Call the construct call builtin that handles allocation and
  // constructor invocation.
  __ RecordPosition(node->position());
  __ call(Handle<Code>(Builtins::builtin(Builtins::JSConstructCall)),
          RelocInfo::CONSTRUCT_CALL);
  __ mov(TOS, eax);  // discard the function and "push" the newly created object
}


void CodeGenerator::GenerateIsSmi(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  __ pop(eax);
  __ test(eax, Immediate(kSmiTagMask));
  cc_reg_ = zero;
}


void CodeGenerator::GenerateIsNonNegativeSmi(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  __ pop(eax);
  __ test(eax, Immediate(kSmiTagMask | 0x80000000));
  cc_reg_ = zero;
}


// This generates code that performs a charCodeAt() call or returns
// undefined in order to trigger the slow case, Runtime_StringCharCodeAt.
// It can handle flat and sliced strings, 8 and 16 bit characters and
// cons strings where the answer is found in the left hand branch of the
// cons.  The slow case will flatten the string, which will ensure that
// the answer is in the left hand side the next time around.
void CodeGenerator::GenerateFastCharCodeAt(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);

  Label slow_case;
  Label end;
  Label not_a_flat_string;
  Label not_a_cons_string_either;
  Label try_again_with_new_string;
  Label ascii_string;
  Label got_char_code;

  // Load the string into eax.
  Load(args->at(0));
  __ pop(eax);
  // If the receiver is a smi return undefined.
  ASSERT(kSmiTag == 0);
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &slow_case, not_taken);

  // Load the index into ebx.
  Load(args->at(1));
  __ pop(ebx);

  // Check for negative or non-smi index.
  ASSERT(kSmiTag == 0);
  __ test(ebx, Immediate(kSmiTagMask | 0x80000000));
  __ j(not_zero, &slow_case, not_taken);
  // Get rid of the smi tag on the index.
  __ sar(ebx, kSmiTagSize);

  __ bind(&try_again_with_new_string);
  // Get the type of the heap object into ecx.
  __ mov(edx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(edx, Map::kInstanceTypeOffset));
  // We don't handle non-strings.
  __ test(ecx, Immediate(kIsNotStringMask));
  __ j(not_zero, &slow_case, not_taken);

  // Get the length field.
  __ mov(edx, FieldOperand(eax, String::kLengthOffset));
  Label long_string;
  Label medium_string;
  Label string_length_shifted;
  // The code assumes the tags are disjoint.
  ASSERT((kLongStringTag & kMediumStringTag) == 0);
  ASSERT(kShortStringTag == 0);
  __ test(ecx, Immediate(kLongStringTag));
  __ j(not_zero, &long_string, not_taken);
  __ test(ecx, Immediate(kMediumStringTag));
  __ j(not_zero, &medium_string, taken);
  // Short string.
  __ shr(edx, String::kShortLengthShift);
  __ jmp(&string_length_shifted);

  // Medium string.
  __ bind(&medium_string);
  __ shr(edx, String::kMediumLengthShift - String::kLongLengthShift);
  // Fall through to long string.
  __ bind(&long_string);
  __ shr(edx, String::kLongLengthShift);

  __ bind(&string_length_shifted);
  ASSERT(kSmiTag == 0);
  // edx is now the length of the string.

  // Check for index out of range.
  __ cmp(ebx, Operand(edx));
  __ j(greater_equal, &slow_case, not_taken);

  // We need special handling for non-flat strings.
  ASSERT(kSeqStringTag == 0);
  __ test(ecx, Immediate(kStringRepresentationMask));
  __ j(not_zero, &not_a_flat_string, not_taken);

  // Check for 1-byte or 2-byte string.
  __ test(ecx, Immediate(kStringEncodingMask));
  __ j(not_zero, &ascii_string, taken);

  // 2-byte string.
  // Load the 2-byte character code.
  __ movzx_w(eax,
             FieldOperand(eax, ebx, times_2, SeqTwoByteString::kHeaderSize));
  __ jmp(&got_char_code);

  // ASCII string.
  __ bind(&ascii_string);
  // Load the byte.
  __ movzx_b(eax, FieldOperand(eax, ebx, times_1, SeqAsciiString::kHeaderSize));

  __ bind(&got_char_code);
  ASSERT(kSmiTag == 0);
  __ shl(eax, kSmiTagSize);
  __ push(eax);
  __ jmp(&end);


  // Handle non-flat strings.
  __ bind(&not_a_flat_string);
  __ and_(ecx, kStringRepresentationMask);
  __ cmp(ecx, kConsStringTag);
  __ j(not_equal, &not_a_cons_string_either, not_taken);

  // ConsString.
  // Get the first of the two strings.
  __ mov(eax, FieldOperand(eax, ConsString::kFirstOffset));
  __ jmp(&try_again_with_new_string);

  __ bind(&not_a_cons_string_either);
  __ cmp(ecx, kSlicedStringTag);
  __ j(not_equal, &slow_case, not_taken);

  // SlicedString.
  // Add the offset to the index.
  __ add(ebx, FieldOperand(eax, SlicedString::kStartOffset));
  __ j(overflow, &slow_case);
  // Get the underlying string.
  __ mov(eax, FieldOperand(eax, SlicedString::kBufferOffset));
  __ jmp(&try_again_with_new_string);

  __ bind(&slow_case);
  __ push(Immediate(Factory::undefined_value()));

  __ bind(&end);
}


void CodeGenerator::GenerateIsArray(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  Label answer;
  // We need the CC bits to come out as not_equal in the case where the
  // object is a smi.  This can't be done with the usual test opcode so
  // we copy the object to ecx and do some destructive ops on it that
  // result in the right CC bits.
  __ pop(eax);
  __ mov(ecx, Operand(eax));
  __ and_(ecx, kSmiTagMask);
  __ xor_(ecx, kSmiTagMask);
  __ j(not_equal, &answer, not_taken);
  // It is a heap object - get map.
  __ mov(eax, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(eax, FieldOperand(eax, Map::kInstanceTypeOffset));
  // Check if the object is a JS array or not.
  __ cmp(eax, JS_ARRAY_TYPE);
  __ bind(&answer);
  cc_reg_ = equal;
}


void CodeGenerator::GenerateArgumentsLength(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 0);

  // Seed the result with the formal parameters count, which will be
  // used in case no arguments adaptor frame is found below the
  // current frame.
  __ Set(eax, Immediate(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to the arguments.length.
  ArgumentsAccessStub stub(ArgumentsAccessStub::READ_LENGTH);
  __ CallStub(&stub);
  __ push(eax);
}


void CodeGenerator::GenerateValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Label leave;
  Load(args->at(0));  // Load the object.
  __ mov(eax, TOS);
  // if (object->IsSmi()) return object.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &leave, taken);
  // It is a heap object - get map.
  __ mov(ecx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return object.
  __ cmp(ecx, JS_VALUE_TYPE);
  __ j(not_equal, &leave, not_taken);
  __ mov(eax, FieldOperand(eax, JSValue::kValueOffset));
  __ mov(TOS, eax);
  __ bind(&leave);
}


void CodeGenerator::GenerateSetValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);
  Label leave;
  Load(args->at(0));  // Load the object.
  Load(args->at(1));  // Load the value.
  __ mov(eax, (Operand(esp, kPointerSize)));
  __ mov(ecx, TOS);
  // if (object->IsSmi()) return object.
  __ test(eax, Immediate(kSmiTagMask));
  __ j(zero, &leave, taken);
  // It is a heap object - get map.
  __ mov(ebx, FieldOperand(eax, HeapObject::kMapOffset));
  __ movzx_b(ebx, FieldOperand(ebx, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return object.
  __ cmp(ebx, JS_VALUE_TYPE);
  __ j(not_equal, &leave, not_taken);
  // Store the value.
  __ mov(FieldOperand(eax, JSValue::kValueOffset), ecx);
  // Update the write barrier.
  __ RecordWrite(eax, JSValue::kValueOffset, ecx, ebx);
  // Leave.
  __ bind(&leave);
  __ mov(ecx, TOS);
  __ pop(eax);
  __ mov(TOS, ecx);
}


void CodeGenerator::GenerateArgumentsAccess(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);

  // Load the key onto the stack and set register eax to the formal
  // parameters count for the currently executing function.
  Load(args->at(0));
  __ Set(eax, Immediate(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to arguments[key].
  ArgumentsAccessStub stub(ArgumentsAccessStub::READ_ELEMENT);
  __ CallStub(&stub);
  __ mov(TOS, eax);
}


void CodeGenerator::GenerateObjectEquals(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);

  // Load the two objects into registers and perform the comparison.
  Load(args->at(0));
  Load(args->at(1));
  __ pop(eax);
  __ pop(ecx);
  __ cmp(eax, Operand(ecx));
  cc_reg_ = equal;
}


void CodeGenerator::VisitCallRuntime(CallRuntime* node) {
  if (CheckForInlineRuntimeCall(node)) return;

  ZoneList<Expression*>* args = node->arguments();
  Comment cmnt(masm_, "[ CallRuntime");
  Runtime::Function* function = node->function();

  if (function == NULL) {
    // Prepare stack for calling JS runtime function.
    __ push(Immediate(node->name()));
    // Push the builtins object found in the current global object.
    __ mov(edx, GlobalObject());
    __ push(FieldOperand(edx, GlobalObject::kBuiltinsOffset));
  }

  // Push the arguments ("left-to-right").
  for (int i = 0; i < args->length(); i++)
    Load(args->at(i));

  if (function != NULL) {
    // Call the C runtime function.
    __ CallRuntime(function, args->length());
    __ push(eax);
  } else {
    // Call the JS runtime function.
    Handle<Code> stub = ComputeCallInitialize(args->length());
    __ Set(eax, Immediate(args->length()));
    __ call(stub, RelocInfo::CODE_TARGET);
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
    __ mov(TOS, eax);
  }
}


void CodeGenerator::VisitUnaryOperation(UnaryOperation* node) {
  Comment cmnt(masm_, "[ UnaryOperation");

  Token::Value op = node->op();

  if (op == Token::NOT) {
    LoadCondition(node->expression(), NOT_INSIDE_TYPEOF,
                  false_target(), true_target(), true);
    cc_reg_ = NegateCondition(cc_reg_);

  } else if (op == Token::DELETE) {
    Property* property = node->expression()->AsProperty();
    if (property != NULL) {
      Load(property->obj());
      Load(property->key());
      __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION);
      __ push(eax);
      return;
    }

    Variable* variable = node->expression()->AsVariableProxy()->AsVariable();
    if (variable != NULL) {
      Slot* slot = variable->slot();
      if (variable->is_global()) {
        LoadGlobal();
        __ push(Immediate(variable->name()));
        __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION);
        __ push(eax);
        return;

      } else if (slot != NULL && slot->type() == Slot::LOOKUP) {
        // lookup the context holding the named variable
        __ push(Operand(esi));
        __ push(Immediate(variable->name()));
        __ CallRuntime(Runtime::kLookupContext, 2);
        // eax: context
        __ push(eax);
        __ push(Immediate(variable->name()));
        __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION);
        __ push(eax);
        return;
      }

      // Default: Result of deleting non-global, not dynamically
      // introduced variables is false.
      __ push(Immediate(Factory::false_value()));

    } else {
      // Default: Result of deleting expressions is true.
      Load(node->expression());  // may have side-effects
      __ Set(TOS, Immediate(Factory::true_value()));
    }

  } else if (op == Token::TYPEOF) {
    // Special case for loading the typeof expression; see comment on
    // LoadTypeofExpression().
    LoadTypeofExpression(node->expression());
    __ CallRuntime(Runtime::kTypeof, 1);
    __ push(eax);

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
        __ pop(eax);
        __ CallStub(&stub);
        __ push(eax);
        break;
      }

      case Token::BIT_NOT: {
        // Smi check.
        Label smi_label;
        Label continue_label;
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ j(zero, &smi_label, taken);

        __ push(eax);  // undo popping of TOS
        __ InvokeBuiltin(Builtins::BIT_NOT, CALL_FUNCTION);

        __ jmp(&continue_label);
        __ bind(&smi_label);
        __ not_(eax);
        __ and_(eax, ~kSmiTagMask);  // Remove inverted smi-tag.
        __ bind(&continue_label);
        __ push(eax);
        break;
      }

      case Token::VOID:
        __ mov(TOS, Factory::undefined_value());
        break;

      case Token::ADD: {
        // Smi check.
        Label continue_label;
        __ pop(eax);
        __ test(eax, Immediate(kSmiTagMask));
        __ j(zero, &continue_label);

        __ push(eax);
        __ InvokeBuiltin(Builtins::TO_NUMBER, CALL_FUNCTION);

        __ bind(&continue_label);
        __ push(eax);
        break;
      }

      default:
        UNREACHABLE();
    }
  }
}


class CountOperationDeferred: public DeferredCode {
 public:
  CountOperationDeferred(CodeGenerator* generator,
                         bool is_postfix,
                         bool is_increment,
                         int result_offset)
      : DeferredCode(generator),
        is_postfix_(is_postfix),
        is_increment_(is_increment),
        result_offset_(result_offset) {
    set_comment("[ CountOperationDeferred");
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


void CountOperationDeferred::Generate() {
  if (is_postfix_) {
    RevertToNumberStub to_number_stub(is_increment_);
    __ CallStub(&to_number_stub);
  }
  CounterOpStub stub(result_offset_, is_postfix_, is_increment_);
  __ CallStub(&stub);
}


void CodeGenerator::VisitCountOperation(CountOperation* node) {
  Comment cmnt(masm_, "[ CountOperation");

  bool is_postfix = node->is_postfix();
  bool is_increment = node->op() == Token::INC;

  Variable* var = node->expression()->AsVariableProxy()->AsVariable();
  bool is_const = (var != NULL && var->mode() == Variable::CONST);

  // Postfix: Make room for the result.
  if (is_postfix) __ push(Immediate(0));

  { Reference target(this, node->expression());
    if (target.is_illegal()) return;
    target.GetValue(NOT_INSIDE_TYPEOF);

    int result_offset = target.size() * kPointerSize;
    CountOperationDeferred* deferred =
        new CountOperationDeferred(this, is_postfix,
                                   is_increment, result_offset);

    __ pop(eax);  // Load TOS into eax for calculations below

    // Postfix: Store the old value as the result.
    if (is_postfix) __ mov(Operand(esp, result_offset), eax);

    // Perform optimistic increment/decrement.
    if (is_increment) {
      __ add(Operand(eax), Immediate(Smi::FromInt(1)));
    } else {
      __ sub(Operand(eax), Immediate(Smi::FromInt(1)));
    }

    // If the count operation didn't overflow and the result is a
    // valid smi, we're done. Otherwise, we jump to the deferred
    // slow-case code.
    __ j(overflow, deferred->enter(), not_taken);
    __ test(eax, Immediate(kSmiTagMask));
    __ j(not_zero, deferred->enter(), not_taken);

    // Store the new value in the target if not const.
    __ bind(deferred->exit());
    __ push(eax);  // Push the new value to TOS
    if (!is_const) target.SetValue(NOT_CONST_INIT);
  }

  // Postfix: Discard the new value and use the old.
  if (is_postfix) __ pop(eax);
}


void CodeGenerator::VisitBinaryOperation(BinaryOperation* node) {
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
    Label is_true;
    LoadCondition(node->left(), NOT_INSIDE_TYPEOF, &is_true,
                  false_target(), false);
    if (has_cc()) {
      Branch(false, false_target());

      // Evaluate right side expression.
      __ bind(&is_true);
      LoadCondition(node->right(), NOT_INSIDE_TYPEOF, true_target(),
                    false_target(), false);

    } else {
      Label pop_and_continue, exit;

      // Avoid popping the result if it converts to 'false' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
       // Duplicate the TOS value. The duplicate will be popped by ToBoolean.
      __ mov(eax, TOS);
      __ push(eax);
      ToBoolean(&pop_and_continue, &exit);
      Branch(false, &exit);

      // Pop the result of evaluating the first part.
      __ bind(&pop_and_continue);
      __ pop(eax);

      // Evaluate right side expression.
      __ bind(&is_true);
      Load(node->right());

      // Exit (always with a materialized value).
      __ bind(&exit);
    }

  } else if (op == Token::OR) {
    Label is_false;
    LoadCondition(node->left(), NOT_INSIDE_TYPEOF, true_target(),
                  &is_false, false);
    if (has_cc()) {
      Branch(true, true_target());

      // Evaluate right side expression.
      __ bind(&is_false);
      LoadCondition(node->right(), NOT_INSIDE_TYPEOF, true_target(),
                    false_target(), false);

    } else {
      Label pop_and_continue, exit;

      // Avoid popping the result if it converts to 'true' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      // Duplicate the TOS value. The duplicate will be popped by ToBoolean.
      __ mov(eax, TOS);
      __ push(eax);
      ToBoolean(&exit, &pop_and_continue);
      Branch(true, &exit);

      // Pop the result of evaluating the first part.
      __ bind(&pop_and_continue);
      __ pop(eax);

      // Evaluate right side expression.
      __ bind(&is_false);
      Load(node->right());

      // Exit (always with a materialized value).
      __ bind(&exit);
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
      SmiOperation(node->op(), rliteral->handle(), false, overwrite_mode);

    } else if (IsInlineSmi(lliteral)) {
      Load(node->right());
      SmiOperation(node->op(), lliteral->handle(), true, overwrite_mode);

    } else {
      Load(node->left());
      Load(node->right());
      GenericBinaryOperation(node->op(), overwrite_mode);
    }
  }
}


void CodeGenerator::VisitThisFunction(ThisFunction* node) {
  __ push(FunctionOperand());
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

  // NOTE: To make null checks efficient, we check if either left or
  // right is the literal 'null'. If so, we optimize the code by
  // inlining a null check instead of calling the (very) general
  // runtime routine for checking equality.

  bool left_is_null =
    left->AsLiteral() != NULL && left->AsLiteral()->IsNull();
  bool right_is_null =
    right->AsLiteral() != NULL && right->AsLiteral()->IsNull();

  if (op == Token::EQ || op == Token::EQ_STRICT) {
    // The 'null' value is only equal to 'null' or 'undefined'.
    if (left_is_null || right_is_null) {
      Load(left_is_null ? right : left);
      Label exit, undetectable;
      __ pop(eax);
      __ cmp(eax, Factory::null_value());

      // The 'null' value is only equal to 'undefined' if using
      // non-strict comparisons.
      if (op != Token::EQ_STRICT) {
        __ j(equal, &exit);
        __ cmp(eax, Factory::undefined_value());

        // NOTE: it can be an undetectable object.
        __ j(equal, &exit);
        __ test(eax, Immediate(kSmiTagMask));

        __ j(not_equal, &undetectable);
        __ jmp(false_target());

        __ bind(&undetectable);
        __ mov(edx, FieldOperand(eax, HeapObject::kMapOffset));
        __ movzx_b(ecx, FieldOperand(edx, Map::kBitFieldOffset));
        __ and_(ecx, 1 << Map::kIsUndetectable);
        __ cmp(ecx, 1 << Map::kIsUndetectable);
      }

      __ bind(&exit);

      cc_reg_ = equal;
      return;
    }
  }


  // NOTE: To make typeof testing for natives implemented in
  // JavaScript really efficient, we generate special code for
  // expressions of the form: 'typeof <expression> == <string>'.

  UnaryOperation* operation = left->AsUnaryOperation();
  if ((op == Token::EQ || op == Token::EQ_STRICT) &&
      (operation != NULL && operation->op() == Token::TYPEOF) &&
      (right->AsLiteral() != NULL &&
       right->AsLiteral()->handle()->IsString())) {
    Handle<String> check(String::cast(*right->AsLiteral()->handle()));

    // Load the operand, move it to register edx, and restore TOS.
    LoadTypeofExpression(operation->expression());
    __ pop(edx);

    if (check->Equals(Heap::number_symbol())) {
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, true_target());
      __ mov(edx, FieldOperand(edx, HeapObject::kMapOffset));
      __ cmp(edx, Factory::heap_number_map());
      cc_reg_ = equal;

    } else if (check->Equals(Heap::string_symbol())) {
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, false_target());

      __ mov(edx, FieldOperand(edx, HeapObject::kMapOffset));

      // NOTE: it might be an undetectable string object
      __ movzx_b(ecx, FieldOperand(edx, Map::kBitFieldOffset));
      __ and_(ecx, 1 << Map::kIsUndetectable);
      __ cmp(ecx, 1 << Map::kIsUndetectable);
      __ j(equal, false_target());

      __ movzx_b(ecx, FieldOperand(edx, Map::kInstanceTypeOffset));
      __ cmp(ecx, FIRST_NONSTRING_TYPE);
      cc_reg_ = less;

    } else if (check->Equals(Heap::boolean_symbol())) {
      __ cmp(edx, Factory::true_value());
      __ j(equal, true_target());
      __ cmp(edx, Factory::false_value());
      cc_reg_ = equal;

    } else if (check->Equals(Heap::undefined_symbol())) {
      __ cmp(edx, Factory::undefined_value());
      __ j(equal, true_target());

      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, false_target());

      // NOTE: it can be an undetectable object.
      __ mov(edx, FieldOperand(edx, HeapObject::kMapOffset));
      __ movzx_b(ecx, FieldOperand(edx, Map::kBitFieldOffset));
      __ and_(ecx, 1 << Map::kIsUndetectable);
      __ cmp(ecx, 1 << Map::kIsUndetectable);

      cc_reg_ = equal;

    } else if (check->Equals(Heap::function_symbol())) {
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, false_target());
      __ mov(edx, FieldOperand(edx, HeapObject::kMapOffset));
      __ movzx_b(edx, FieldOperand(edx, Map::kInstanceTypeOffset));
      __ cmp(edx, JS_FUNCTION_TYPE);
      cc_reg_ = equal;

    } else if (check->Equals(Heap::object_symbol())) {
      __ test(edx, Immediate(kSmiTagMask));
      __ j(zero, false_target());

      __ mov(ecx, FieldOperand(edx, HeapObject::kMapOffset));
      __ cmp(edx, Factory::null_value());
      __ j(equal, true_target());

      // NOTE: it might be an undetectable object
      __ movzx_b(edx, FieldOperand(ecx, Map::kBitFieldOffset));
      __ and_(edx, 1 << Map::kIsUndetectable);
      __ cmp(edx, 1 << Map::kIsUndetectable);
      __ j(equal, false_target());

      __ movzx_b(ecx, FieldOperand(ecx, Map::kInstanceTypeOffset));
      __ cmp(ecx, FIRST_JS_OBJECT_TYPE);
      __ j(less, false_target());
      __ cmp(ecx, LAST_JS_OBJECT_TYPE);
      cc_reg_ = less_equal;

    } else {
      // Uncommon case: Typeof testing against a string literal that
      // is never returned from the typeof operator.
      __ jmp(false_target());
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
      __ InvokeBuiltin(Builtins::IN, CALL_FUNCTION);
      __ push(eax);  // push the result
      return;
    }
    case Token::INSTANCEOF: {
      Load(left);
      Load(right);
      InstanceofStub stub;
      __ CallStub(&stub);
      __ test(eax, Operand(eax));
      cc_reg_ = zero;
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
    return;
  }
  if (IsInlineSmi(right->AsLiteral())) {
    Load(left);
    SmiComparison(cc, right->AsLiteral()->handle(), strict);
    return;
  }

  Load(left);
  Load(right);
  Comparison(cc, strict);
}


void CodeGenerator::RecordStatementPosition(Node* node) {
  if (FLAG_debug_info) {
    int pos = node->statement_pos();
    if (pos != RelocInfo::kNoPosition) {
      __ RecordStatementPosition(pos);
    }
  }
}


void CodeGenerator::EnterJSFrame() {
  __ push(ebp);
  __ mov(ebp, Operand(esp));

  // Store the context and the function in the frame.
  __ push(esi);
  __ push(edi);

  // Clear the function slot when generating debug code.
  if (FLAG_debug_code) {
    __ Set(edi, Immediate(reinterpret_cast<int>(kZapValue)));
  }
}


void CodeGenerator::ExitJSFrame() {
  // Record the location of the JS exit code for patching when setting
  // break point.
  __ RecordJSReturn();

  // Avoid using the leave instruction here, because it is too
  // short. We need the return sequence to be a least the size of a
  // call instruction to support patching the exit code in the
  // debugger. See VisitReturnStatement for the full return sequence.
  __ mov(esp, Operand(ebp));
  __ pop(ebp);
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
    MacroAssembler* masm = cgen_->masm();
    __ RecordPosition(property->position());
    Literal* raw_name = property->key()->AsLiteral();
    ASSERT(raw_name != NULL);
    return Handle<String>(String::cast(*raw_name->handle()));
  }
}


void Reference::GetValue(TypeofState typeof_state) {
  ASSERT(!is_illegal());
  ASSERT(!cgen_->has_cc());
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
      // TODO(1241834): Make sure that this it is safe to ignore the
      // distinction between expressions in a typeof and not in a typeof. If
      // there is a chance that reference errors can be thrown below, we
      // must distinguish between the two kinds of loads (typeof expression
      // loads must not throw a reference error).
      Comment cmnt(masm, "[ Load from named Property");
      Handle<String> name(GetName());
      Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
      // Setup the name register.
      __ mov(ecx, name);

      Variable* var = expression_->AsVariableProxy()->AsVariable();
      if (var != NULL) {
        ASSERT(var->is_global());
        __ call(ic, RelocInfo::CODE_TARGET_CONTEXT);
      } else {
        __ call(ic, RelocInfo::CODE_TARGET);
      }
      __ push(eax);  // IC call leaves result in eax, push it out
      break;
    }

    case KEYED: {
      // TODO(1241834): Make sure that this it is safe to ignore the
      // distinction between expressions in a typeof and not in a typeof.
      Comment cmnt(masm, "[ Load from keyed Property");
      Property* property = expression_->AsProperty();
      ASSERT(property != NULL);
      __ RecordPosition(property->position());
      Handle<Code> ic(Builtins::builtin(Builtins::KeyedLoadIC_Initialize));

      Variable* var = expression_->AsVariableProxy()->AsVariable();
      if (var != NULL) {
        ASSERT(var->is_global());
        __ call(ic, RelocInfo::CODE_TARGET_CONTEXT);
      } else {
        __ call(ic, RelocInfo::CODE_TARGET);
      }
      __ push(eax);  // IC call leaves result in eax, push it out
      break;
    }

    default:
      UNREACHABLE();
  }
}


void Reference::SetValue(InitState init_state) {
  ASSERT(!is_illegal());
  ASSERT(!cgen_->has_cc());
  MacroAssembler* masm = cgen_->masm();
  switch (type_) {
    case SLOT: {
      Comment cmnt(masm, "[ Store to Slot");
      Slot* slot = expression_->AsVariableProxy()->AsVariable()->slot();
      ASSERT(slot != NULL);
      if (slot->type() == Slot::LOOKUP) {
        ASSERT(slot->var()->mode() == Variable::DYNAMIC);

        // For now, just do a runtime call.
        __ push(esi);
        __ push(Immediate(slot->var()->name()));

        if (init_state == CONST_INIT) {
          // Same as the case for a normal store, but ignores attribute
          // (e.g. READ_ONLY) of context slot so that we can initialize
          // const properties (introduced via eval("const foo = (some
          // expr);")). Also, uses the current function context instead of
          // the top context.
          //
          // Note that we must declare the foo upon entry of eval(), via a
          // context slot declaration, but we cannot initialize it at the
          // same time, because the const declaration may be at the end of
          // the eval code (sigh...) and the const variable may have been
          // used before (where its value is 'undefined'). Thus, we can only
          // do the initialization when we actually encounter the expression
          // and when the expression operands are defined and valid, and
          // thus we need the split into 2 operations: declaration of the
          // context slot followed by initialization.
          __ CallRuntime(Runtime::kInitializeConstContextSlot, 3);
        } else {
          __ CallRuntime(Runtime::kStoreContextSlot, 3);
        }
        // Storing a variable must keep the (new) value on the expression
        // stack. This is necessary for compiling chained assignment
        // expressions.
        __ push(eax);

      } else {
        ASSERT(slot->var()->mode() != Variable::DYNAMIC);

        Label exit;
        if (init_state == CONST_INIT) {
          ASSERT(slot->var()->mode() == Variable::CONST);
          // Only the first const initialization must be executed (the slot
          // still contains 'the hole' value). When the assignment is
          // executed, the code is identical to a normal store (see below).
          Comment cmnt(masm, "[ Init const");
          __ mov(eax, cgen_->SlotOperand(slot, ecx));
          __ cmp(eax, Factory::the_hole_value());
          __ j(not_equal, &exit);
        }

        // We must execute the store.  Storing a variable must keep the
        // (new) value on the stack. This is necessary for compiling
        // assignment expressions.
        //
        // Note: We will reach here even with slot->var()->mode() ==
        // Variable::CONST because of const declarations which will
        // initialize consts to 'the hole' value and by doing so, end up
        // calling this code.
        __ pop(eax);
        __ mov(cgen_->SlotOperand(slot, ecx), eax);
        __ push(eax);  // RecordWrite may destroy the value in eax.
        if (slot->type() == Slot::CONTEXT) {
          // ecx is loaded with context when calling SlotOperand above.
          int offset = FixedArray::kHeaderSize + slot->index() * kPointerSize;
          __ RecordWrite(ecx, offset, eax, ebx);
        }
        // If we definitely did not jump over the assignment, we do not need
        // to bind the exit label.  Doing so can defeat peephole
        // optimization.
        if (init_state == CONST_INIT) __ bind(&exit);
      }
      break;
    }

    case NAMED: {
      Comment cmnt(masm, "[ Store to named Property");
      // Call the appropriate IC code.
      Handle<String> name(GetName());
      Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
      // TODO(1222589): Make the IC grab the values from the stack.
      __ pop(eax);
      // Setup the name register.
      __ mov(ecx, name);
      __ call(ic, RelocInfo::CODE_TARGET);
      __ push(eax);  // IC call leaves result in eax, push it out
      break;
    }

    case KEYED: {
      Comment cmnt(masm, "[ Store to keyed Property");
      Property* property = expression_->AsProperty();
      ASSERT(property != NULL);
      __ RecordPosition(property->position());
      // Call IC code.
      Handle<Code> ic(Builtins::builtin(Builtins::KeyedStoreIC_Initialize));
      // TODO(1222589): Make the IC grab the values from the stack.
      __ pop(eax);
      __ call(ic, RelocInfo::CODE_TARGET);
      __ push(eax);  // IC call leaves result in eax, push it out
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


void GenericBinaryOpStub::Generate(MacroAssembler* masm) {
  Label call_runtime;
  __ mov(eax, Operand(esp, 1 * kPointerSize));  // Get y.
  __ mov(edx, Operand(esp, 2 * kPointerSize));  // Get x.

  // 1. Smi case.
  switch (op_) {
    case Token::ADD: {
      // eax: y.
      // edx: x.
      Label revert;
      __ mov(ecx, Operand(eax));
      __ or_(ecx, Operand(edx));  // ecx = x | y.
      __ add(eax, Operand(edx));  // Add y optimistically.
      // Go slow-path in case of overflow.
      __ j(overflow, &revert, not_taken);
      // Go slow-path in case of non-smi operands.
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &revert, not_taken);
      __ ret(2 * kPointerSize);  // Remove all operands.

      // Revert optimistic add.
      __ bind(&revert);
      __ sub(eax, Operand(edx));
      break;
    }
    case Token::SUB: {
      // eax: y.
      // edx: x.
      Label revert;
      __ mov(ecx, Operand(edx));
      __ or_(ecx, Operand(eax));  // ecx = x | y.
      __ sub(edx, Operand(eax));  // Subtract y optimistically.
      // Go slow-path in case of overflow.
      __ j(overflow, &revert, not_taken);
      // Go slow-path in case of non-smi operands.
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &revert, not_taken);
      __ mov(eax, Operand(edx));
      __ ret(2 * kPointerSize);  // Remove all operands.

      // Revert optimistic sub.
      __ bind(&revert);
      __ add(edx, Operand(eax));
      break;
    }
    case Token::MUL: {
      // eax: y
      // edx: x
      // a) both operands smi and result fits into a smi -> return.
      // b) at least one of operands non-smi -> non_smi_operands.
      // c) result does not fit in a smi -> non_smi_result.
      Label non_smi_operands, non_smi_result;
      // Tag check.
      __ mov(ecx, Operand(edx));
      __ or_(ecx, Operand(eax));  // ecx = x | y.
      ASSERT(kSmiTag == 0);  // Adjust code below.
      __ test(ecx, Immediate(kSmiTagMask));
      // Jump if not both smi; check if float numbers.
      __ j(not_zero, &non_smi_operands, not_taken);

      // Get copies of operands.
      __ mov(ebx, Operand(eax));
      __ mov(ecx, Operand(edx));
      // If the smi tag is 0 we can just leave the tag on one operand.
      ASSERT(kSmiTag == 0);  // adjust code below
      // Remove tag from one of the operands (but keep sign).
      __ sar(ecx, kSmiTagSize);
      // Do multiplication.
      __ imul(eax, Operand(ecx));  // Multiplication of Smis; result in eax.
      // Go slow on overflows.
      __ j(overflow, &non_smi_result, not_taken);
      // ...but operands OK for float arithmetic.

      // If the result is +0 we may need to check if the result should
      // really be -0. Welcome to the -0 fan club.
      __ NegativeZeroTest(eax, ebx, edx, ecx, &non_smi_result);

      __ ret(2 * kPointerSize);

      __ bind(&non_smi_result);
      // TODO(1243132): Do not check float operands here.
      __ bind(&non_smi_operands);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      break;
    }
    case Token::DIV: {
      // eax: y
      // edx: x
      Label non_smi_operands, non_smi_result, division_by_zero;
      __ mov(ebx, Operand(eax));  // Get y
      __ mov(eax, Operand(edx));  // Get x

      __ cdq();  // Sign extend eax into edx:eax.
      // Tag check.
      __ mov(ecx, Operand(ebx));
      __ or_(ecx, Operand(eax));  // ecx = x | y.
      ASSERT(kSmiTag == 0);  // Adjust code below.
      __ test(ecx, Immediate(kSmiTagMask));
      // Jump if not both smi; check if float numbers.
      __ j(not_zero, &non_smi_operands, not_taken);
      __ test(ebx, Operand(ebx));  // Check for 0 divisor.
      __ j(zero, &division_by_zero, not_taken);

      __ idiv(ebx);
      // Check for the corner case of dividing the most negative smi by -1.
      // (We cannot use the overflow flag, since it is not set by idiv.)
      ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
      __ cmp(eax, 0x40000000);
      __ j(equal, &non_smi_result);
      // If the result is +0 we may need to check if the result should
      // really be -0. Welcome to the -0 fan club.
      __ NegativeZeroTest(eax, ecx, &non_smi_result);  // Use ecx = x | y.
      __ test(edx, Operand(edx));
      // Use floats if there's a remainder.
      __ j(not_zero, &non_smi_result, not_taken);
      __ shl(eax, kSmiTagSize);
      __ ret(2 * kPointerSize);  // Remove all operands.

      __ bind(&division_by_zero);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      __ jmp(&call_runtime);  // Division by zero must go through runtime.

      __ bind(&non_smi_result);
      // TODO(1243132): Do not check float operands here.
      __ bind(&non_smi_operands);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      break;
    }
    case Token::MOD: {
      Label slow;
      __ mov(ebx, Operand(eax));  // get y
      __ mov(eax, Operand(edx));  // get x
      __ cdq();  // sign extend eax into edx:eax
      // tag check
      __ mov(ecx, Operand(ebx));
      __ or_(ecx, Operand(eax));  // ecx = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ test(ecx, Immediate(kSmiTagMask));
      __ j(not_zero, &slow, not_taken);
      __ test(ebx, Operand(ebx));  // test for y == 0
      __ j(zero, &slow);

      // Fast case: Do integer division and use remainder.
      __ idiv(ebx);
      __ NegativeZeroTest(edx, ecx, &slow);  // use ecx = x | y
      __ mov(eax, Operand(edx));
      __ ret(2 * kPointerSize);

      // Slow case: Call runtime operator implementation.
      __ bind(&slow);
      __ mov(eax, Operand(esp, 1 * kPointerSize));
      __ mov(edx, Operand(esp, 2 * kPointerSize));
      // Fall through to |call_runtime|.
      break;
    }
    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR:
    case Token::SAR:
    case Token::SHL:
    case Token::SHR: {
      // Smi-case for bitops should already have been inlined.
      break;
    }
    default: {
      UNREACHABLE();
    }
  }

  // 2. Floating point case.
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
      __ lea(eax, Operand(eax, times_2, kSmiTag));
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

  // 3. If all else fails, use the runtime system to get the correct result.
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
                              bool do_gc) {
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

  // Call C function.
  __ mov(Operand(esp, 0 * kPointerSize), edi);  // argc.
  __ mov(Operand(esp, 1 * kPointerSize), esi);  // argv.
  __ call(Operand(ebx));
  // Result is in eax or edx:eax - do not destroy these registers!

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

#ifdef DEBUG
  if (FLAG_gc_greedy) {
    Failure* failure = Failure::RetryAfterGC(0, NEW_SPACE);
    __ mov(Operand(eax), Immediate(reinterpret_cast<int32_t>(failure)));
  }
  GenerateCore(masm, &throw_normal_exception,
               &throw_out_of_memory_exception,
               frame_type,
               FLAG_gc_greedy);
#else
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               frame_type,
               false);
#endif

  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               frame_type,
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
  __ mov(eax, Handle<Failure>(Failure::Exception()));
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
    __ mov(Operand(edx), Immediate(construct_entry));
  } else {
    ExternalReference entry(Builtins::JSEntryTrampoline);
    __ mov(Operand(edx), Immediate(entry));
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
