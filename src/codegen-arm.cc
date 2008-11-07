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
// VirtualFrame implementation.

VirtualFrame::VirtualFrame(CodeGenerator* cgen) {
  ASSERT(cgen->scope() != NULL);

  masm_ = cgen->masm();
  frame_local_count_ = cgen->scope()->num_stack_slots();
  parameter_count_ = cgen->scope()->num_parameters();
}


void VirtualFrame::Enter() {
  Comment cmnt(masm_, "[ Enter JS frame");
#ifdef DEBUG
  { Label done, fail;
    __ tst(r1, Operand(kSmiTagMask));
    __ b(eq, &fail);
    __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
    __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
    __ cmp(r2, Operand(JS_FUNCTION_TYPE));
    __ b(eq, &done);
    __ bind(&fail);
    __ stop("CodeGenerator::EnterJSFrame - r1 not a function");
    __ bind(&done);
  }
#endif  // DEBUG

  __ stm(db_w, sp, r1.bit() | cp.bit() | fp.bit() | lr.bit());
  // Adjust FP to point to saved FP.
  __ add(fp, sp, Operand(2 * kPointerSize));
}


void VirtualFrame::Exit() {
  Comment cmnt(masm_, "[ Exit JS frame");
  // Drop the execution stack down to the frame pointer and restore the caller
  // frame pointer and return address.
  __ mov(sp, fp);
  __ ldm(ia_w, sp, fp.bit() | lr.bit());
}


void VirtualFrame::AllocateLocals() {
  if (frame_local_count_ > 0) {
    Comment cmnt(masm_, "[ Allocate space for locals");
      // Initialize stack slots with 'undefined' value.
    __ mov(ip, Operand(Factory::undefined_value()));
    for (int i = 0; i < frame_local_count_; i++) {
      __ push(ip);
    }
  }
}


void VirtualFrame::Drop(int count) {
  ASSERT(count >= 0);
  if (count > 0) {
    __ add(sp, sp, Operand(count * kPointerSize));
  }
}


void VirtualFrame::Pop() { Drop(1); }


void VirtualFrame::Pop(Register reg) {
  __ pop(reg);
}


void VirtualFrame::Push(Register reg) {
  __ push(reg);
}


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
      cc_reg_(al),
      state_(NULL),
      break_stack_height_(0) {
}


// Calling conventions:
// r0: the number of arguments
// fp: frame pointer
// sp: stack pointer
// pp: caller's parameter pointer
// cp: callee's context

void CodeGenerator::GenCode(FunctionLiteral* fun) {
  ZoneList<Statement*>* body = fun->body();

  // Initialize state.
  ASSERT(scope_ == NULL);
  scope_ = fun->scope();
  ASSERT(frame_ == NULL);
  VirtualFrame virtual_frame(this);
  frame_ = &virtual_frame;
  cc_reg_ = al;
  {
    CodeGenState state(this);

    // Entry
    // stack: function, receiver, arguments, return address
    // r0: number of arguments
    // sp: stack pointer
    // fp: frame pointer
    // pp: caller's parameter pointer
    // cp: callee's context

    frame_->Enter();
    // tos: code slot
#ifdef DEBUG
    if (strlen(FLAG_stop_at) > 0 &&
        fun->name()->IsEqualTo(CStrVector(FLAG_stop_at))) {
      __ stop("stop-at");
    }
#endif

    // Allocate space for locals and initialize them.
    frame_->AllocateLocals();

    if (scope_->num_heap_slots() > 0) {
      // Allocate local context.
      // Get outer context and create a new context based on it.
      __ ldr(r0, frame_->Function());
      frame_->Push(r0);
      __ CallRuntime(Runtime::kNewContext, 1);  // r0 holds the result

      if (kDebug) {
        Label verified_true;
        __ cmp(r0, Operand(cp));
        __ b(eq, &verified_true);
        __ stop("NewContext: r0 is expected to be the same as cp");
        __ bind(&verified_true);
      }
      // Update context local.
      __ str(cp, frame_->Context());
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
          ASSERT(!scope_->is_global_scope());  // no parameters in global scope
          __ ldr(r1, frame_->Parameter(i));
          // Loads r2 with context; used below in RecordWrite.
          __ str(r1, SlotOperand(slot, r2));
          // Load the offset into r3.
          int slot_offset =
              FixedArray::kHeaderSize + slot->index() * kPointerSize;
          __ mov(r3, Operand(slot_offset));
          __ RecordWrite(r2, r3, r1);
        }
      }
    }

    // Store the arguments object.  This must happen after context
    // initialization because the arguments object may be stored in the
    // context.
    if (scope_->arguments() != NULL) {
      ASSERT(scope_->arguments_shadow() != NULL);
      Comment cmnt(masm_, "[ allocate arguments object");
      { Reference shadow_ref(this, scope_->arguments_shadow());
        { Reference arguments_ref(this, scope_->arguments());
          ArgumentsAccessStub stub(ArgumentsAccessStub::NEW_OBJECT);
          __ ldr(r2, frame_->Function());
          // The receiver is below the arguments, the return address,
          // and the frame pointer on the stack.
          const int kReceiverDisplacement = 2 + scope_->num_parameters();
          __ add(r1, fp, Operand(kReceiverDisplacement * kPointerSize));
          __ mov(r0, Operand(Smi::FromInt(scope_->num_parameters())));
          __ stm(db_w, sp, r0.bit() | r1.bit() | r2.bit());
          __ CallStub(&stub);
          frame_->Push(r0);
          arguments_ref.SetValue(NOT_CONST_INIT);
        }
        shadow_ref.SetValue(NOT_CONST_INIT);
      }
      frame_->Pop();  // Value is no longer needed.
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
      __ CallRuntime(Runtime::kTraceEnter, 0);
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
        __ CallRuntime(Runtime::kDebugTrace, 0);
        // Ignore the return value.
      }
#endif
      VisitStatements(body);
    }
  }

  // exit
  // r0: result
  // sp: stack pointer
  // fp: frame pointer
  // pp: parameter pointer
  // cp: callee's context
  __ mov(r0, Operand(Factory::undefined_value()));

  __ bind(&function_return_);
  if (FLAG_trace) {
    // Push the return value on the stack as the parameter.
    // Runtime::TraceExit returns the parameter as it is.
    frame_->Push(r0);
    __ CallRuntime(Runtime::kTraceExit, 1);
  }

  // Tear down the frame which will restore the caller's frame pointer and the
  // link register.
  frame_->Exit();

  __ add(sp, sp, Operand((scope_->num_parameters() + 1) * kPointerSize));
  __ mov(pc, lr);

  // Code generation state must be reset.
  scope_ = NULL;
  frame_ = NULL;
  ASSERT(!has_cc());
  ASSERT(state_ == NULL);
}


MemOperand CodeGenerator::SlotOperand(Slot* slot, Register tmp) {
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
      return frame_->Parameter(index);

    case Slot::LOCAL:
      return frame_->Local(index);

    case Slot::CONTEXT: {
      // Follow the context chain if necessary.
      ASSERT(!tmp.is(cp));  // do not overwrite context register
      Register context = cp;
      int chain_length = scope()->ContextChainLength(slot->var()->scope());
      for (int i = chain_length; i-- > 0;) {
        // Load the closure.
        // (All contexts, even 'with' contexts, have a closure,
        // and it is the same for all contexts inside a function.
        // There is no need to go to the function context first.)
        __ ldr(tmp, ContextOperand(context, Context::CLOSURE_INDEX));
        // Load the function context (which is the incoming, outer context).
        __ ldr(tmp, FieldMemOperand(tmp, JSFunction::kContextOffset));
        context = tmp;
      }
      // We may have a 'with' context now. Get the function context.
      // (In fact this mov may never be the needed, since the scope analysis
      // may not permit a direct context access in this case and thus we are
      // always at a function context. However it is safe to dereference be-
      // cause the function context of a function context is itself. Before
      // deleting this mov we should try to create a counter-example first,
      // though...)
      __ ldr(tmp, ContextOperand(context, Context::FCONTEXT_INDEX));
      return ContextOperand(tmp, index);
    }

    default:
      UNREACHABLE();
      return MemOperand(r0, 0);
  }
}


// Loads a value on TOS. If it is a boolean value, the result may have been
// (partially) translated into branches, or it may have set the condition
// code register. If force_cc is set, the value is forced to set the
// condition code register and no value is pushed. If the condition code
// register was set, has_cc() is true and cc_reg_ contains the condition to
// test for 'true'.
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
    // Convert the TOS value to a boolean in the condition code register.
    // Visiting an expression may possibly choose neither (a) to leave a
    // value in the condition code register nor (b) to leave a value in TOS
    // (eg, by compiling to only jumps to the targets).  In that case the
    // code generated by ToBoolean is wrong because it assumes the value of
    // the expression in TOS.  So long as there is always a value in TOS or
    // the condition code register when control falls through to here (there
    // is), the code generated by ToBoolean is dead and therefore safe.
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
    __ b(cc_reg_, &materialize_true);
    __ mov(r0, Operand(Factory::false_value()));
    frame_->Push(r0);
    __ b(&loaded);
    __ bind(&materialize_true);
    __ mov(r0, Operand(Factory::true_value()));
    frame_->Push(r0);
    __ bind(&loaded);
    cc_reg_ = al;
  }

  if (true_target.is_linked() || false_target.is_linked()) {
    // we have at least one condition value
    // that has been "translated" into a branch,
    // thus it needs to be loaded explicitly again
    Label loaded;
    __ b(&loaded);  // don't lose current TOS
    bool both = true_target.is_linked() && false_target.is_linked();
    // reincarnate "true", if necessary
    if (true_target.is_linked()) {
      __ bind(&true_target);
      __ mov(r0, Operand(Factory::true_value()));
      frame_->Push(r0);
    }
    // if both "true" and "false" need to be reincarnated,
    // jump across code for "false"
    if (both)
      __ b(&loaded);
    // reincarnate "false", if necessary
    if (false_target.is_linked()) {
      __ bind(&false_target);
      __ mov(r0, Operand(Factory::false_value()));
      frame_->Push(r0);
    }
    // everything is loaded at this point
    __ bind(&loaded);
  }
  ASSERT(!has_cc());
}


void CodeGenerator::LoadGlobal() {
  __ ldr(r0, GlobalObject());
  frame_->Push(r0);
}


void CodeGenerator::LoadGlobalReceiver(Register scratch) {
  __ ldr(scratch, ContextOperand(cp, Context::GLOBAL_INDEX));
  __ ldr(scratch,
         FieldMemOperand(scratch, GlobalObject::kGlobalReceiverOffset));
  frame_->Push(scratch);
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
  if (size > 0) {
    frame_->Pop(r0);
    frame_->Drop(size);
    frame_->Push(r0);
  }
}


// ECMA-262, section 9.2, page 30: ToBoolean(). Convert the given
// register to a boolean in the condition code register. The code
// may jump to 'false_target' in case the register converts to 'false'.
void CodeGenerator::ToBoolean(Label* true_target,
                              Label* false_target) {
  // Note: The generated code snippet does not change stack variables.
  //       Only the condition code should be set.
  frame_->Pop(r0);

  // Fast case checks

  // Check if the value is 'false'.
  __ cmp(r0, Operand(Factory::false_value()));
  __ b(eq, false_target);

  // Check if the value is 'true'.
  __ cmp(r0, Operand(Factory::true_value()));
  __ b(eq, true_target);

  // Check if the value is 'undefined'.
  __ cmp(r0, Operand(Factory::undefined_value()));
  __ b(eq, false_target);

  // Check if the value is a smi.
  __ cmp(r0, Operand(Smi::FromInt(0)));
  __ b(eq, false_target);
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, true_target);

  // Slow case: call the runtime.
  frame_->Push(r0);
  __ CallRuntime(Runtime::kToBool, 1);
  // Convert the result (r0) to a condition code.
  __ cmp(r0, Operand(Factory::false_value()));

  cc_reg_ = ne;
}


class GetPropertyStub : public CodeStub {
 public:
  GetPropertyStub() { }

 private:
  Major MajorKey() { return GetProperty; }
  int MinorKey() { return 0; }
  void Generate(MacroAssembler* masm);
};


class SetPropertyStub : public CodeStub {
 public:
  SetPropertyStub() { }

 private:
  Major MajorKey() { return SetProperty; }
  int MinorKey() { return 0; }
  void Generate(MacroAssembler* masm);
};


class GenericBinaryOpStub : public CodeStub {
 public:
  explicit GenericBinaryOpStub(Token::Value op) : op_(op) { }

 private:
  Token::Value op_;

  Major MajorKey() { return GenericBinaryOp; }
  int MinorKey() { return static_cast<int>(op_); }
  void Generate(MacroAssembler* masm);

  const char* GetName() {
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

#ifdef DEBUG
  void Print() { PrintF("GenericBinaryOpStub (%s)\n", Token::String(op_)); }
#endif
};


class InvokeBuiltinStub : public CodeStub {
 public:
  enum Kind { Inc, Dec, ToNumber };
  InvokeBuiltinStub(Kind kind, int argc) : kind_(kind), argc_(argc) { }

 private:
  Kind kind_;
  int argc_;

  Major MajorKey() { return InvokeBuiltin; }
  int MinorKey() { return (argc_ << 3) | static_cast<int>(kind_); }
  void Generate(MacroAssembler* masm);

#ifdef DEBUG
  void Print() {
    PrintF("InvokeBuiltinStub (kind %d, argc, %d)\n",
           static_cast<int>(kind_),
           argc_);
  }
#endif
};


void CodeGenerator::GenericBinaryOperation(Token::Value op) {
  // sp[0] : y
  // sp[1] : x
  // result : r0

  // Stub is entered with a call: 'return address' is in lr.
  switch (op) {
    case Token::ADD:  // fall through.
    case Token::SUB:  // fall through.
    case Token::MUL:
    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR:
    case Token::SHL:
    case Token::SHR:
    case Token::SAR: {
      frame_->Pop(r0);  // r0 : y
      frame_->Pop(r1);  // r1 : x
      GenericBinaryOpStub stub(op);
      __ CallStub(&stub);
      break;
    }

    case Token::DIV: {
      __ mov(r0, Operand(1));
      __ InvokeBuiltin(Builtins::DIV, CALL_JS);
      break;
    }

    case Token::MOD: {
      __ mov(r0, Operand(1));
      __ InvokeBuiltin(Builtins::MOD, CALL_JS);
      break;
    }

    case Token::COMMA:
      frame_->Pop(r0);
      // simply discard left value
      frame_->Pop();
      break;

    default:
      // Other cases should have been handled before this point.
      UNREACHABLE();
      break;
  }
}


class DeferredInlinedSmiOperation: public DeferredCode {
 public:
  DeferredInlinedSmiOperation(CodeGenerator* generator, Token::Value op,
                              int value, bool reversed) :
      DeferredCode(generator), op_(op), value_(value), reversed_(reversed) {
    set_comment("[ DeferredInlinedSmiOperation");
  }

  virtual void Generate() {
    switch (op_) {
      case Token::ADD: {
        if (reversed_) {
          // revert optimistic add
          __ sub(r0, r0, Operand(Smi::FromInt(value_)));
          __ mov(r1, Operand(Smi::FromInt(value_)));  // x
        } else {
          // revert optimistic add
          __ sub(r1, r0, Operand(Smi::FromInt(value_)));
          __ mov(r0, Operand(Smi::FromInt(value_)));
        }
        break;
      }

      case Token::SUB: {
        if (reversed_) {
          // revert optimistic sub
          __ rsb(r0, r0, Operand(Smi::FromInt(value_)));
          __ mov(r1, Operand(Smi::FromInt(value_)));
        } else {
          __ add(r1, r0, Operand(Smi::FromInt(value_)));
          __ mov(r0, Operand(Smi::FromInt(value_)));
        }
        break;
      }

      case Token::BIT_OR:
      case Token::BIT_XOR:
      case Token::BIT_AND: {
        if (reversed_) {
          __ mov(r1, Operand(Smi::FromInt(value_)));
        } else {
          __ mov(r1, Operand(r0));
          __ mov(r0, Operand(Smi::FromInt(value_)));
        }
        break;
      }

      case Token::SHL:
      case Token::SHR:
      case Token::SAR: {
        if (!reversed_) {
          __ mov(r1, Operand(r0));
          __ mov(r0, Operand(Smi::FromInt(value_)));
        } else {
          UNREACHABLE();  // should have been handled in SmiOperation
        }
        break;
      }

      default:
        // other cases should have been handled before this point.
        UNREACHABLE();
        break;
    }

    GenericBinaryOpStub igostub(op_);
    __ CallStub(&igostub);
  }

 private:
  Token::Value op_;
  int value_;
  bool reversed_;
};


void CodeGenerator::SmiOperation(Token::Value op,
                                 Handle<Object> value,
                                 bool reversed) {
  // NOTE: This is an attempt to inline (a bit) more of the code for
  // some possible smi operations (like + and -) when (at least) one
  // of the operands is a literal smi. With this optimization, the
  // performance of the system is increased by ~15%, and the generated
  // code size is increased by ~1% (measured on a combination of
  // different benchmarks).

  // sp[0] : operand

  int int_value = Smi::cast(*value)->value();

  Label exit;
  frame_->Pop(r0);

  switch (op) {
    case Token::ADD: {
      DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, op, int_value, reversed);

      __ add(r0, r0, Operand(value), SetCC);
      __ b(vs, deferred->enter());
      __ tst(r0, Operand(kSmiTagMask));
      __ b(ne, deferred->enter());
      __ bind(deferred->exit());
      break;
    }

    case Token::SUB: {
      DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, op, int_value, reversed);

      if (!reversed) {
        __ sub(r0, r0, Operand(value), SetCC);
      } else {
        __ rsb(r0, r0, Operand(value), SetCC);
      }
      __ b(vs, deferred->enter());
      __ tst(r0, Operand(kSmiTagMask));
      __ b(ne, deferred->enter());
      __ bind(deferred->exit());
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_XOR:
    case Token::BIT_AND: {
      DeferredCode* deferred =
        new DeferredInlinedSmiOperation(this, op, int_value, reversed);
      __ tst(r0, Operand(kSmiTagMask));
      __ b(ne, deferred->enter());
      switch (op) {
        case Token::BIT_OR:  __ orr(r0, r0, Operand(value)); break;
        case Token::BIT_XOR: __ eor(r0, r0, Operand(value)); break;
        case Token::BIT_AND: __ and_(r0, r0, Operand(value)); break;
        default: UNREACHABLE();
      }
      __ bind(deferred->exit());
      break;
    }

    case Token::SHL:
    case Token::SHR:
    case Token::SAR: {
      if (reversed) {
        __ mov(ip, Operand(value));
        frame_->Push(ip);
        frame_->Push(r0);
        GenericBinaryOperation(op);

      } else {
        int shift_value = int_value & 0x1f;  // least significant 5 bits
        DeferredCode* deferred =
          new DeferredInlinedSmiOperation(this, op, shift_value, false);
        __ tst(r0, Operand(kSmiTagMask));
        __ b(ne, deferred->enter());
        __ mov(r2, Operand(r0, ASR, kSmiTagSize));  // remove tags
        switch (op) {
          case Token::SHL: {
            __ mov(r2, Operand(r2, LSL, shift_value));
            // check that the *unsigned* result fits in a smi
            __ add(r3, r2, Operand(0x40000000), SetCC);
            __ b(mi, deferred->enter());
            break;
          }
          case Token::SHR: {
            // LSR by immediate 0 means shifting 32 bits.
            if (shift_value != 0) {
              __ mov(r2, Operand(r2, LSR, shift_value));
            }
            // check that the *unsigned* result fits in a smi
            // neither of the two high-order bits can be set:
            // - 0x80000000: high bit would be lost when smi tagging
            // - 0x40000000: this number would convert to negative when
            // smi tagging these two cases can only happen with shifts
            // by 0 or 1 when handed a valid smi
            __ and_(r3, r2, Operand(0xc0000000), SetCC);
            __ b(ne, deferred->enter());
            break;
          }
          case Token::SAR: {
            if (shift_value != 0) {
              // ASR by immediate 0 means shifting 32 bits.
              __ mov(r2, Operand(r2, ASR, shift_value));
            }
            break;
          }
          default: UNREACHABLE();
        }
        __ mov(r0, Operand(r2, LSL, kSmiTagSize));
        __ bind(deferred->exit());
      }
      break;
    }

    default:
      if (!reversed) {
        frame_->Push(r0);
        __ mov(r0, Operand(value));
        frame_->Push(r0);
      } else {
        __ mov(ip, Operand(value));
        frame_->Push(ip);
        frame_->Push(r0);
      }
      GenericBinaryOperation(op);
      break;
  }

  __ bind(&exit);
}


void CodeGenerator::Comparison(Condition cc, bool strict) {
  // sp[0] : y
  // sp[1] : x
  // result : cc register

  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == eq);

  Label exit, smi;
  // Implement '>' and '<=' by reversal to obtain ECMA-262 conversion order.
  if (cc == gt || cc == le) {
    cc = ReverseCondition(cc);
    frame_->Pop(r1);
    frame_->Pop(r0);
  } else {
    frame_->Pop(r0);
    frame_->Pop(r1);
  }
  __ orr(r2, r0, Operand(r1));
  __ tst(r2, Operand(kSmiTagMask));
  __ b(eq, &smi);

  // Perform non-smi comparison by runtime call.
  frame_->Push(r1);

  // Figure out which native to call and setup the arguments.
  Builtins::JavaScript native;
  int argc;
  if (cc == eq) {
    native = strict ? Builtins::STRICT_EQUALS : Builtins::EQUALS;
    argc = 1;
  } else {
    native = Builtins::COMPARE;
    int ncr;  // NaN compare result
    if (cc == lt || cc == le) {
      ncr = GREATER;
    } else {
      ASSERT(cc == gt || cc == ge);  // remaining cases
      ncr = LESS;
    }
    frame_->Push(r0);
    __ mov(r0, Operand(Smi::FromInt(ncr)));
    argc = 2;
  }

  // Call the native; it returns -1 (less), 0 (equal), or 1 (greater)
  // tagged as a small integer.
  frame_->Push(r0);
  __ mov(r0, Operand(argc));
  __ InvokeBuiltin(native, CALL_JS);
  __ cmp(r0, Operand(0));
  __ b(&exit);

  // test smi equality by pointer comparison.
  __ bind(&smi);
  __ cmp(r1, Operand(r0));

  __ bind(&exit);
  cc_reg_ = cc;
}


class CallFunctionStub: public CodeStub {
 public:
  explicit CallFunctionStub(int argc) : argc_(argc) {}

  void Generate(MacroAssembler* masm);

 private:
  int argc_;

#if defined(DEBUG)
  void Print() { PrintF("CallFunctionStub (argc %d)\n", argc_); }
#endif  // defined(DEBUG)

  Major MajorKey() { return CallFunction; }
  int MinorKey() { return argc_; }
};


// Call the function on the stack with the given arguments.
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
  __ ldr(cp, frame_->Context());
  frame_->Pop();  // discard the TOS
}


void CodeGenerator::Branch(bool if_true, Label* L) {
  ASSERT(has_cc());
  Condition cc = if_true ? cc_reg_ : NegateCondition(cc_reg_);
  __ b(cc, L);
  cc_reg_ = al;
}


void CodeGenerator::CheckStack() {
  if (FLAG_check_stack) {
    Comment cmnt(masm_, "[ check stack");
    StackCheckStub stub;
    __ CallStub(&stub);
  }
}


void CodeGenerator::VisitBlock(Block* node) {
  Comment cmnt(masm_, "[ Block");
  if (FLAG_debug_info) RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);
  VisitStatements(node->statements());
  __ bind(node->break_target());
}


void CodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  __ mov(r0, Operand(pairs));
  frame_->Push(r0);
  frame_->Push(cp);
  __ mov(r0, Operand(Smi::FromInt(is_eval() ? 1 : 0)));
  frame_->Push(r0);
  __ CallRuntime(Runtime::kDeclareGlobals, 3);
  // The result is discarded.
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
    frame_->Push(cp);
    __ mov(r0, Operand(var->name()));
    frame_->Push(r0);
    // Declaration nodes are always declared in only two modes.
    ASSERT(node->mode() == Variable::VAR || node->mode() == Variable::CONST);
    PropertyAttributes attr = node->mode() == Variable::VAR ? NONE : READ_ONLY;
    __ mov(r0, Operand(Smi::FromInt(attr)));
    frame_->Push(r0);
    // Push initial value, if any.
    // Note: For variables we must not push an initial value (such as
    // 'undefined') because we may have a (legal) redeclaration and we
    // must not destroy the current value.
    if (node->mode() == Variable::CONST) {
      __ mov(r0, Operand(Factory::the_hole_value()));
      frame_->Push(r0);
    } else if (node->fun() != NULL) {
      Load(node->fun());
    } else {
      __ mov(r0, Operand(0));  // no initial value!
      frame_->Push(r0);
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
    // know it is a zero-sized reference.
    frame_->Pop();
  }
}


void CodeGenerator::VisitExpressionStatement(ExpressionStatement* node) {
  Comment cmnt(masm_, "[ ExpressionStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Expression* expression = node->expression();
  expression->MarkAsStatement();
  Load(expression);
  frame_->Pop();
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

  if (FLAG_debug_info) RecordStatementPosition(node);

  Label exit;
  if (has_then_stm && has_else_stm) {
    Comment cmnt(masm_, "[ IfThenElse");
    Label then;
    Label else_;
    // if (cond)
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &then, &else_, true);
    Branch(false, &else_);
    // then
    __ bind(&then);
    Visit(node->then_statement());
    __ b(&exit);
    // else
    __ bind(&else_);
    Visit(node->else_statement());

  } else if (has_then_stm) {
    Comment cmnt(masm_, "[ IfThen");
    ASSERT(!has_else_stm);
    Label then;
    // if (cond)
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &then, &exit, true);
    Branch(false, &exit);
    // then
    __ bind(&then);
    Visit(node->then_statement());

  } else if (has_else_stm) {
    Comment cmnt(masm_, "[ IfElse");
    ASSERT(!has_then_stm);
    Label else_;
    // if (!cond)
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &exit, &else_, true);
    Branch(true, &exit);
    // else
    __ bind(&else_);
    Visit(node->else_statement());

  } else {
    Comment cmnt(masm_, "[ If");
    ASSERT(!has_then_stm && !has_else_stm);
    // if (cond)
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &exit, &exit, false);
    if (has_cc()) {
      cc_reg_ = al;
    } else {
      frame_->Pop();
    }
  }

  // end
  __ bind(&exit);
}


void CodeGenerator::CleanStack(int num_bytes) {
  ASSERT(num_bytes % kPointerSize == 0);
  frame_->Drop(num_bytes / kPointerSize);
}


void CodeGenerator::VisitContinueStatement(ContinueStatement* node) {
  Comment cmnt(masm_, "[ ContinueStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  __ b(node->target()->continue_target());
}


void CodeGenerator::VisitBreakStatement(BreakStatement* node) {
  Comment cmnt(masm_, "[ BreakStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  __ b(node->target()->break_target());
}


void CodeGenerator::VisitReturnStatement(ReturnStatement* node) {
  Comment cmnt(masm_, "[ ReturnStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Load(node->expression());
  // Move the function result into r0.
  frame_->Pop(r0);

  __ b(&function_return_);
}


void CodeGenerator::VisitWithEnterStatement(WithEnterStatement* node) {
  Comment cmnt(masm_, "[ WithEnterStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Load(node->expression());
  __ CallRuntime(Runtime::kPushContext, 1);
  if (kDebug) {
    Label verified_true;
    __ cmp(r0, Operand(cp));
    __ b(eq, &verified_true);
    __ stop("PushContext: r0 is expected to be the same as cp");
    __ bind(&verified_true);
  }
  // Update context local.
  __ str(cp, frame_->Context());
}


void CodeGenerator::VisitWithExitStatement(WithExitStatement* node) {
  Comment cmnt(masm_, "[ WithExitStatement");
  // Pop context.
  __ ldr(cp, ContextOperand(cp, Context::PREVIOUS_INDEX));
  // Update context local.
  __ str(cp, frame_->Context());
}


int CodeGenerator::FastCaseSwitchMaxOverheadFactor() {
    return kFastSwitchMaxOverheadFactor;
}

int CodeGenerator::FastCaseSwitchMinCaseCount() {
    return kFastSwitchMinCaseCount;
}


void CodeGenerator::GenerateFastCaseSwitchJumpTable(
    SwitchStatement* node,
    int min_index,
    int range,
    Label* fail_label,
    Vector<Label*> case_targets,
    Vector<Label> case_labels) {

  ASSERT(kSmiTag == 0 && kSmiTagSize <= 2);

  frame_->Pop(r0);

  // Test for a Smi value in a HeapNumber.
  Label is_smi;
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &is_smi);
  __ ldr(r1, MemOperand(r0, HeapObject::kMapOffset - kHeapObjectTag));
  __ ldrb(r1, MemOperand(r1, Map::kInstanceTypeOffset - kHeapObjectTag));
  __ cmp(r1, Operand(HEAP_NUMBER_TYPE));
  __ b(ne, fail_label);
  frame_->Push(r0);
  __ CallRuntime(Runtime::kNumberToSmi, 1);
  __ bind(&is_smi);

  if (min_index != 0) {
    // Small positive numbers can be immediate operands.
    if (min_index < 0) {
      // If min_index is Smi::kMinValue, -min_index is not a Smi.
      if (Smi::IsValid(-min_index)) {
        __ add(r0, r0, Operand(Smi::FromInt(-min_index)));
      } else {
        __ add(r0, r0, Operand(Smi::FromInt(-min_index - 1)));
        __ add(r0, r0, Operand(Smi::FromInt(1)));
      }
    } else {
      __ sub(r0, r0, Operand(Smi::FromInt(min_index)));
    }
  }
  __ tst(r0, Operand(0x80000000 | kSmiTagMask));
  __ b(ne, fail_label);
  __ cmp(r0, Operand(Smi::FromInt(range)));
  __ b(ge, fail_label);
  __ add(pc, pc, Operand(r0, LSL, 2 - kSmiTagSize));
  // One extra instruction offsets the table, so the table's start address is
  // the pc-register at the above add.
  __ stop("Unreachable: Switch table alignment");

  // Table containing branch operations.
  for (int i = 0; i < range; i++) {
    __ b(case_targets[i]);
  }

  GenerateFastCaseSwitchCases(node, case_labels);
}


void CodeGenerator::VisitSwitchStatement(SwitchStatement* node) {
  Comment cmnt(masm_, "[ SwitchStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
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
      __ b(&next);

      // Bind the default case label, so we can branch to it when we
      // have compared against all other cases.
      ASSERT(default_case.is_unused());  // at most one default clause
      __ bind(&default_case);
    } else {
      __ bind(&next);
      next.Unuse();
      __ ldr(r0, frame_->Top());
      frame_->Push(r0);  // duplicate TOS
      Load(clause->label());
      Comparison(eq, true);
      Branch(false, &next);
    }

    // Entering the case statement for the first time. Remove the switch value
    // from the stack.
    frame_->Pop();

    // Generate code for the body.
    // This is also the target for the fall through from the previous case's
    // statements which has to skip over the matching code and the popping of
    // the switch value.
    __ bind(&fall_through);
    fall_through.Unuse();
    VisitStatements(clause->statements());
    __ b(&fall_through);
  }

  __ bind(&next);
  // Reached the end of the case statements without matching any of the cases.
  if (default_case.is_bound()) {
    // A default case exists -> execute its statements.
    __ b(&default_case);
  } else {
    // Remove the switch value from the stack.
    frame_->Pop();
  }

  __ bind(&fall_through);
  __ bind(node->break_target());
}


void CodeGenerator::VisitLoopStatement(LoopStatement* node) {
  Comment cmnt(masm_, "[ LoopStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
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
    __ b(&entry);
  }

  // body
  __ bind(&loop);
  Visit(node->body());

  // next
  __ bind(node->continue_target());
  if (node->next() != NULL) {
    // Record source position of the statement as this code which is after the
    // code for the body actually belongs to the loop statement and not the
    // body.
    if (FLAG_debug_info) __ RecordPosition(node->statement_pos());
    ASSERT(node->type() == LoopStatement::FOR_LOOP);
    Visit(node->next());
  }

  // cond
  __ bind(&entry);
  switch (info) {
    case ALWAYS_TRUE:
      CheckStack();  // TODO(1222600): ignore if body contains calls.
      __ b(&loop);
      break;
    case ALWAYS_FALSE:
      break;
    case DONT_KNOW:
      CheckStack();  // TODO(1222600): ignore if body contains calls.
      LoadCondition(node->cond(),
                    NOT_INSIDE_TYPEOF,
                    &loop,
                    node->break_target(),
                    true);
      Branch(true, &loop);
      break;
  }

  // exit
  __ bind(node->break_target());
}


void CodeGenerator::VisitForInStatement(ForInStatement* node) {
  Comment cmnt(masm_, "[ ForInStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);

  // We keep stuff on the stack while the body is executing.
  // Record it, so that a break/continue crossing this statement
  // can restore the stack.
  const int kForInStackSize = 5 * kPointerSize;
  break_stack_height_ += kForInStackSize;
  node->set_break_stack_height(break_stack_height_);

  Label loop, next, entry, cleanup, exit, primitive, jsobject;
  Label filter_key, end_del_check, fixed_array, non_string;

  // Get the object to enumerate over (converted to JSObject).
  Load(node->enumerable());
  frame_->Pop(r0);

  // Both SpiderMonkey and kjs ignore null and undefined in contrast
  // to the specification.  12.6.4 mandates a call to ToObject.
  __ cmp(r0, Operand(Factory::undefined_value()));
  __ b(eq, &exit);
  __ cmp(r0, Operand(Factory::null_value()));
  __ b(eq, &exit);

  // Stack layout in body:
  // [iteration counter (Smi)]
  // [length of array]
  // [FixedArray]
  // [Map or 0]
  // [Object]

  // Check if enumerable is already a JSObject
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &primitive);
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  __ cmp(r1, Operand(FIRST_JS_OBJECT_TYPE));
  __ b(hs, &jsobject);

  __ bind(&primitive);
  frame_->Push(r0);
  __ mov(r0, Operand(0));
  __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_JS);


  __ bind(&jsobject);

  // Get the set of properties (as a FixedArray or Map).
  frame_->Push(r0);  // duplicate the object being enumerated
  frame_->Push(r0);
  __ CallRuntime(Runtime::kGetPropertyNamesFast, 1);

  // If we got a Map, we can do a fast modification check.
  // Otherwise, we got a FixedArray, and we have to do a slow check.
  __ mov(r2, Operand(r0));
  __ ldr(r1, FieldMemOperand(r2, HeapObject::kMapOffset));
  __ cmp(r1, Operand(Factory::meta_map()));
  __ b(ne, &fixed_array);

  // Get enum cache
  __ mov(r1, Operand(r0));
  __ ldr(r1, FieldMemOperand(r1, Map::kInstanceDescriptorsOffset));
  __ ldr(r1, FieldMemOperand(r1, DescriptorArray::kEnumerationIndexOffset));
  __ ldr(r2,
         FieldMemOperand(r1, DescriptorArray::kEnumCacheBridgeCacheOffset));

  frame_->Push(r0);  // map
  frame_->Push(r2);  // enum cache bridge cache
  __ ldr(r0, FieldMemOperand(r2, FixedArray::kLengthOffset));
  __ mov(r0, Operand(r0, LSL, kSmiTagSize));
  frame_->Push(r0);
  __ mov(r0, Operand(Smi::FromInt(0)));
  frame_->Push(r0);
  __ b(&entry);


  __ bind(&fixed_array);

  __ mov(r1, Operand(Smi::FromInt(0)));
  frame_->Push(r1);  // insert 0 in place of Map
  frame_->Push(r0);

  // Push the length of the array and the initial index onto the stack.
  __ ldr(r0, FieldMemOperand(r0, FixedArray::kLengthOffset));
  __ mov(r0, Operand(r0, LSL, kSmiTagSize));
  frame_->Push(r0);
  __ mov(r0, Operand(Smi::FromInt(0)));  // init index
  frame_->Push(r0);

  __ b(&entry);

  // Body.
  __ bind(&loop);
  Visit(node->body());

  // Next.
  __ bind(node->continue_target());
  __ bind(&next);
  frame_->Pop(r0);
  __ add(r0, r0, Operand(Smi::FromInt(1)));
  frame_->Push(r0);

  // Condition.
  __ bind(&entry);

  // sp[0] : index
  // sp[1] : array/enum cache length
  // sp[2] : array or enum cache
  // sp[3] : 0 or map
  // sp[4] : enumerable
  __ ldr(r0, frame_->Element(0));  // load the current count
  __ ldr(r1, frame_->Element(1));  // load the length
  __ cmp(r0, Operand(r1));  // compare to the array length
  __ b(hs, &cleanup);

  __ ldr(r0, frame_->Element(0));

  // Get the i'th entry of the array.
  __ ldr(r2, frame_->Element(2));
  __ add(r2, r2, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ ldr(r3, MemOperand(r2, r0, LSL, kPointerSizeLog2 - kSmiTagSize));

  // Get Map or 0.
  __ ldr(r2, frame_->Element(3));
  // Check if this (still) matches the map of the enumerable.
  // If not, we have to filter the key.
  __ ldr(r1, frame_->Element(4));
  __ ldr(r1, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ cmp(r1, Operand(r2));
  __ b(eq, &end_del_check);

  // Convert the entry to a string (or null if it isn't a property anymore).
  __ ldr(r0, frame_->Element(4));  // push enumerable
  frame_->Push(r0);
  frame_->Push(r3);  // push entry
  __ mov(r0, Operand(1));
  __ InvokeBuiltin(Builtins::FILTER_KEY, CALL_JS);
  __ mov(r3, Operand(r0));

  // If the property has been removed while iterating, we just skip it.
  __ cmp(r3, Operand(Factory::null_value()));
  __ b(eq, &next);


  __ bind(&end_del_check);

  // Store the entry in the 'each' expression and take another spin in the loop.
  // r3: i'th entry of the enum cache (or string there of)
  frame_->Push(r3);  // push entry
  { Reference each(this, node->each());
    if (!each.is_illegal()) {
      if (each.size() > 0) {
        __ ldr(r0, frame_->Element(each.size()));
        frame_->Push(r0);
      }
      // If the reference was to a slot we rely on the convenient property
      // that it doesn't matter whether a value (eg, r3 pushed above) is
      // right on top of or right underneath a zero-sized reference.
      each.SetValue(NOT_CONST_INIT);
      if (each.size() > 0) {
        // It's safe to pop the value lying on top of the reference before
        // unloading the reference itself (which preserves the top of stack,
        // ie, now the topmost value of the non-zero sized reference), since
        // we will discard the top of stack after unloading the reference
        // anyway.
        frame_->Pop(r0);
      }
    }
  }
  // Discard the i'th entry pushed above or else the remainder of the
  // reference, whichever is currently on top of the stack.
  frame_->Pop();
  CheckStack();  // TODO(1222600): ignore if body contains calls.
  __ jmp(&loop);

  // Cleanup.
  __ bind(&cleanup);
  __ bind(node->break_target());
  frame_->Drop(5);

  // Exit.
  __ bind(&exit);

  break_stack_height_ -= kForInStackSize;
}


void CodeGenerator::VisitTryCatch(TryCatch* node) {
  Comment cmnt(masm_, "[ TryCatch");

  Label try_block, exit;

  __ bl(&try_block);
  // --- Catch block ---
  frame_->Push(r0);

  // Store the caught exception in the catch variable.
  { Reference ref(this, node->catch_var());
    ASSERT(ref.is_slot());
    // Here we make use of the convenient property that it doesn't matter
    // whether a value is immediately on top of or underneath a zero-sized
    // reference.
    ref.SetValue(NOT_CONST_INIT);
  }

  // Remove the exception from the stack.
  frame_->Pop();

  VisitStatements(node->catch_block()->statements());
  __ b(&exit);


  // --- Try block ---
  __ bind(&try_block);

  __ PushTryHandler(IN_JAVASCRIPT, TRY_CATCH_HANDLER);

  // Shadow the labels for all escapes from the try block, including
  // returns. During shadowing, the original label is hidden as the
  // LabelShadow and operations on the original actually affect the
  // shadowing label.
  //
  // We should probably try to unify the escaping labels and the return
  // label.
  int nof_escapes = node->escaping_labels()->length();
  List<LabelShadow*> shadows(1 + nof_escapes);
  shadows.Add(new LabelShadow(&function_return_));
  for (int i = 0; i < nof_escapes; i++) {
    shadows.Add(new LabelShadow(node->escaping_labels()->at(i)));
  }

  // Generate code for the statements in the try block.
  VisitStatements(node->try_block()->statements());
  frame_->Pop();  // Discard the result.

  // Stop the introduced shadowing and count the number of required unlinks.
  // After shadowing stops, the original labels are unshadowed and the
  // LabelShadows represent the formerly shadowing labels.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }

  // Unlink from try chain.
  // TOS contains code slot
  const int kNextIndex = (StackHandlerConstants::kNextOffset
                          + StackHandlerConstants::kAddressDisplacement)
                       / kPointerSize;
  __ ldr(r1, frame_->Element(kNextIndex));  // read next_sp
  __ mov(r3, Operand(ExternalReference(Top::k_handler_address)));
  __ str(r1, MemOperand(r3));
  ASSERT(StackHandlerConstants::kCodeOffset == 0);  // first field is code
  frame_->Drop(StackHandlerConstants::kSize / kPointerSize - 1);
  // Code slot popped.
  if (nof_unlinks > 0) __ b(&exit);

  // Generate unlink code for the (formerly) shadowing labels that have been
  // jumped to.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      // Unlink from try chain;
      __ bind(shadows[i]);

      // Reload sp from the top handler, because some statements that we
      // break from (eg, for...in) may have left stuff on the stack.
      __ mov(r3, Operand(ExternalReference(Top::k_handler_address)));
      __ ldr(sp, MemOperand(r3));

      __ ldr(r1, frame_->Element(kNextIndex));
      __ str(r1, MemOperand(r3));
      ASSERT(StackHandlerConstants::kCodeOffset == 0);  // first field is code
      frame_->Drop(StackHandlerConstants::kSize / kPointerSize - 1);
      // Code slot popped.

      __ b(shadows[i]->original_label());
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

  __ bl(&try_block);

  frame_->Push(r0);  // save exception object on the stack
  // In case of thrown exceptions, this is where we continue.
  __ mov(r2, Operand(Smi::FromInt(THROWING)));
  __ b(&finally_block);


  // --- Try block ---
  __ bind(&try_block);

  __ PushTryHandler(IN_JAVASCRIPT, TRY_FINALLY_HANDLER);

  // Shadow the labels for all escapes from the try block, including
  // returns.  Shadowing hides the original label as the LabelShadow and
  // operations on the original actually affect the shadowing label.
  //
  // We should probably try to unify the escaping labels and the return
  // label.
  int nof_escapes = node->escaping_labels()->length();
  List<LabelShadow*> shadows(1 + nof_escapes);
  shadows.Add(new LabelShadow(&function_return_));
  for (int i = 0; i < nof_escapes; i++) {
    shadows.Add(new LabelShadow(node->escaping_labels()->at(i)));
  }

  // Generate code for the statements in the try block.
  VisitStatements(node->try_block()->statements());

  // Stop the introduced shadowing and count the number of required unlinks.
  // After shadowing stops, the original labels are unshadowed and the
  // LabelShadows represent the formerly shadowing labels.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }

  // Set the state on the stack to FALLING.
  __ mov(r0, Operand(Factory::undefined_value()));  // fake TOS
  frame_->Push(r0);
  __ mov(r2, Operand(Smi::FromInt(FALLING)));
  if (nof_unlinks > 0) __ b(&unlink);

  // Generate code to set the state for the (formerly) shadowing labels that
  // have been jumped to.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      __ bind(shadows[i]);
      if (shadows[i]->original_label() == &function_return_) {
        // If this label shadowed the function return, materialize the
        // return value on the stack.
        frame_->Push(r0);
      } else {
        // Fake TOS for labels that shadowed breaks and continues.
        __ mov(r0, Operand(Factory::undefined_value()));
        frame_->Push(r0);
      }
      __ mov(r2, Operand(Smi::FromInt(JUMPING + i)));
      __ b(&unlink);
    }
  }

  // Unlink from try chain;
  __ bind(&unlink);

  frame_->Pop(r0);  // Store TOS in r0 across stack manipulation
  // Reload sp from the top handler, because some statements that we
  // break from (eg, for...in) may have left stuff on the stack.
  __ mov(r3, Operand(ExternalReference(Top::k_handler_address)));
  __ ldr(sp, MemOperand(r3));
  const int kNextIndex = (StackHandlerConstants::kNextOffset
                          + StackHandlerConstants::kAddressDisplacement)
                       / kPointerSize;
  __ ldr(r1, frame_->Element(kNextIndex));
  __ str(r1, MemOperand(r3));
  ASSERT(StackHandlerConstants::kCodeOffset == 0);  // first field is code
  frame_->Drop(StackHandlerConstants::kSize / kPointerSize - 1);
  // Code slot popped.
  frame_->Push(r0);

  // --- Finally block ---
  __ bind(&finally_block);

  // Push the state on the stack.
  frame_->Push(r2);

  // We keep two elements on the stack - the (possibly faked) result
  // and the state - while evaluating the finally block. Record it, so
  // that a break/continue crossing this statement can restore the
  // stack.
  const int kFinallyStackSize = 2 * kPointerSize;
  break_stack_height_ += kFinallyStackSize;

  // Generate code for the statements in the finally block.
  VisitStatements(node->finally_block()->statements());

  // Restore state and return value or faked TOS.
  frame_->Pop(r2);
  frame_->Pop(r0);
  break_stack_height_ -= kFinallyStackSize;

  // Generate code to jump to the right destination for all used (formerly)
  // shadowing labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_bound()) {
      __ cmp(r2, Operand(Smi::FromInt(JUMPING + i)));
      if (shadows[i]->original_label() != &function_return_) {
        Label next;
        __ b(ne, &next);
        __ b(shadows[i]->original_label());
        __ bind(&next);
      } else {
        __ b(eq, shadows[i]->original_label());
      }
    }
  }

  // Check if we need to rethrow the exception.
  __ cmp(r2, Operand(Smi::FromInt(THROWING)));
  __ b(ne, &exit);

  // Rethrow exception.
  frame_->Push(r0);
  __ CallRuntime(Runtime::kReThrow, 1);

  // Done.
  __ bind(&exit);
}


void CodeGenerator::VisitDebuggerStatement(DebuggerStatement* node) {
  Comment cmnt(masm_, "[ DebuggerStatament");
  if (FLAG_debug_info) RecordStatementPosition(node);
  __ CallRuntime(Runtime::kDebugBreak, 0);
  // Ignore the return value.
}


void CodeGenerator::InstantiateBoilerplate(Handle<JSFunction> boilerplate) {
  ASSERT(boilerplate->IsBoilerplate());

  // Push the boilerplate on the stack.
  __ mov(r0, Operand(boilerplate));
  frame_->Push(r0);

  // Create a new closure.
  frame_->Push(cp);
  __ CallRuntime(Runtime::kNewClosure, 2);
  frame_->Push(r0);
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
  __ b(&exit);
  __ bind(&else_);
  Load(node->else_expression(), typeof_state());
  __ bind(&exit);
}


void CodeGenerator::LoadFromSlot(Slot* slot, TypeofState typeof_state) {
  if (slot->type() == Slot::LOOKUP) {
    ASSERT(slot->var()->mode() == Variable::DYNAMIC);

    // For now, just do a runtime call.
    frame_->Push(cp);
    __ mov(r0, Operand(slot->var()->name()));
    frame_->Push(r0);

    if (typeof_state == INSIDE_TYPEOF) {
      __ CallRuntime(Runtime::kLoadContextSlotNoReferenceError, 2);
    } else {
      __ CallRuntime(Runtime::kLoadContextSlot, 2);
    }
    frame_->Push(r0);

  } else {
    // Note: We would like to keep the assert below, but it fires because of
    // some nasty code in LoadTypeofExpression() which should be removed...
    // ASSERT(slot->var()->mode() != Variable::DYNAMIC);

    // Special handling for locals allocated in registers.
    __ ldr(r0, SlotOperand(slot, r2));
    frame_->Push(r0);
    if (slot->var()->mode() == Variable::CONST) {
      // Const slots may contain 'the hole' value (the constant hasn't been
      // initialized yet) which needs to be converted into the 'undefined'
      // value.
      Comment cmnt(masm_, "[ Unhole const");
      frame_->Pop(r0);
      __ cmp(r0, Operand(Factory::the_hole_value()));
      __ mov(r0, Operand(Factory::undefined_value()), LeaveCC, eq);
      frame_->Push(r0);
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
  __ mov(r0, Operand(node->handle()));
  frame_->Push(r0);
}


void CodeGenerator::VisitRegExpLiteral(RegExpLiteral* node) {
  Comment cmnt(masm_, "[ RexExp Literal");

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ ldr(r1, frame_->Function());

  // Load the literals array of the function.
  __ ldr(r1, FieldMemOperand(r1, JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ ldr(r2, FieldMemOperand(r1, literal_offset));

  Label done;
  __ cmp(r2, Operand(Factory::undefined_value()));
  __ b(ne, &done);

  // If the entry is undefined we call the runtime system to computed
  // the literal.
  frame_->Push(r1);  // literal array  (0)
  __ mov(r0, Operand(Smi::FromInt(node->literal_index())));
  frame_->Push(r0);  // literal index  (1)
  __ mov(r0, Operand(node->pattern()));  // RegExp pattern (2)
  frame_->Push(r0);
  __ mov(r0, Operand(node->flags()));  // RegExp flags   (3)
  frame_->Push(r0);
  __ CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  __ mov(r2, Operand(r0));

  __ bind(&done);
  // Push the literal.
  frame_->Push(r2);
}


// This deferred code stub will be used for creating the boilerplate
// by calling Runtime_CreateObjectLiteral.
// Each created boilerplate is stored in the JSFunction and they are
// therefore context dependent.
class ObjectLiteralDeferred: public DeferredCode {
 public:
  ObjectLiteralDeferred(CodeGenerator* generator, ObjectLiteral* node)
      : DeferredCode(generator), node_(node) {
    set_comment("[ ObjectLiteralDeferred");
  }
  virtual void Generate();
 private:
  ObjectLiteral* node_;
};


void ObjectLiteralDeferred::Generate() {
  // If the entry is undefined we call the runtime system to computed
  // the literal.

  // Literal array (0).
  __ push(r1);
  // Literal index (1).
  __ mov(r0, Operand(Smi::FromInt(node_->literal_index())));
  __ push(r0);
  // Constant properties (2).
  __ mov(r0, Operand(node_->constant_properties()));
  __ push(r0);
  __ CallRuntime(Runtime::kCreateObjectLiteralBoilerplate, 3);
  __ mov(r2, Operand(r0));
}


void CodeGenerator::VisitObjectLiteral(ObjectLiteral* node) {
  Comment cmnt(masm_, "[ ObjectLiteral");

  ObjectLiteralDeferred* deferred = new ObjectLiteralDeferred(this, node);

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ ldr(r1, frame_->Function());

  // Load the literals array of the function.
  __ ldr(r1, FieldMemOperand(r1, JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ ldr(r2, FieldMemOperand(r1, literal_offset));

  // Check whether we need to materialize the object literal boilerplate.
  // If so, jump to the deferred code.
  __ cmp(r2, Operand(Factory::undefined_value()));
  __ b(eq, deferred->enter());
  __ bind(deferred->exit());

  // Push the object literal boilerplate.
  frame_->Push(r2);

  // Clone the boilerplate object.
  __ CallRuntime(Runtime::kCloneObjectLiteralBoilerplate, 1);
  frame_->Push(r0);  // save the result
  // r0: cloned object literal

  for (int i = 0; i < node->properties()->length(); i++) {
    ObjectLiteral::Property* property = node->properties()->at(i);
    Literal* key = property->key();
    Expression* value = property->value();
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT: break;
      case ObjectLiteral::Property::COMPUTED:  // fall through
      case ObjectLiteral::Property::PROTOTYPE: {
        frame_->Push(r0);  // dup the result
        Load(key);
        Load(value);
        __ CallRuntime(Runtime::kSetProperty, 3);
        // restore r0
        __ ldr(r0, frame_->Top());
        break;
      }
      case ObjectLiteral::Property::SETTER: {
        frame_->Push(r0);
        Load(key);
        __ mov(r0, Operand(Smi::FromInt(1)));
        frame_->Push(r0);
        Load(value);
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        __ ldr(r0, frame_->Top());
        break;
      }
      case ObjectLiteral::Property::GETTER: {
        frame_->Push(r0);
        Load(key);
        __ mov(r0, Operand(Smi::FromInt(0)));
        frame_->Push(r0);
        Load(value);
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        __ ldr(r0, frame_->Top());
        break;
      }
    }
  }
}


void CodeGenerator::VisitArrayLiteral(ArrayLiteral* node) {
  Comment cmnt(masm_, "[ ArrayLiteral");

  // Call runtime to create the array literal.
  __ mov(r0, Operand(node->literals()));
  frame_->Push(r0);
  // Load the function of this frame.
  __ ldr(r0, frame_->Function());
  __ ldr(r0, FieldMemOperand(r0, JSFunction::kLiteralsOffset));
  frame_->Push(r0);
  __ CallRuntime(Runtime::kCreateArrayLiteral, 2);

  // Push the resulting array literal on the stack.
  frame_->Push(r0);

  // Generate code to set the elements in the array that are not
  // literals.
  for (int i = 0; i < node->values()->length(); i++) {
    Expression* value = node->values()->at(i);

    // If value is literal the property value is already
    // set in the boilerplate object.
    if (value->AsLiteral() == NULL) {
      // The property must be set by generated code.
      Load(value);
      frame_->Pop(r0);

      // Fetch the object literal
      __ ldr(r1, frame_->Top());
        // Get the elements array.
      __ ldr(r1, FieldMemOperand(r1, JSObject::kElementsOffset));

      // Write to the indexed properties array.
      int offset = i * kPointerSize + Array::kHeaderSize;
      __ str(r0, FieldMemOperand(r1, offset));

      // Update the write barrier for the array address.
      __ mov(r3, Operand(offset));
      __ RecordWrite(r1, r3, r2);
    }
  }
}


void CodeGenerator::VisitAssignment(Assignment* node) {
  Comment cmnt(masm_, "[ Assignment");
  if (FLAG_debug_info) RecordStatementPosition(node);

  Reference target(this, node->target());
  if (target.is_illegal()) return;

  if (node->op() == Token::ASSIGN ||
      node->op() == Token::INIT_VAR ||
      node->op() == Token::INIT_CONST) {
    Load(node->value());

  } else {
    target.GetValue(NOT_INSIDE_TYPEOF);
    Literal* literal = node->value()->AsLiteral();
    if (literal != NULL && literal->handle()->IsSmi()) {
      SmiOperation(node->binary_op(), literal->handle(), false);
      frame_->Push(r0);

    } else {
      Load(node->value());
      GenericBinaryOperation(node->binary_op());
      frame_->Push(r0);
    }
  }

  Variable* var = node->target()->AsVariableProxy()->AsVariable();
  if (var != NULL &&
      (var->mode() == Variable::CONST) &&
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
  frame_->Push(r0);
}


void CodeGenerator::VisitProperty(Property* node) {
  Comment cmnt(masm_, "[ Property");

  Reference property(this, node);
  property.GetValue(typeof_state());
}


void CodeGenerator::VisitCall(Call* node) {
  Comment cmnt(masm_, "[ Call");

  ZoneList<Expression*>* args = node->arguments();

  if (FLAG_debug_info) RecordStatementPosition(node);
  // Standard function call.

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
    __ mov(r0, Operand(var->name()));
    frame_->Push(r0);

    // Pass the global object as the receiver and let the IC stub
    // patch the stack to use the global proxy as 'this' in the
    // invoked function.
    LoadGlobal();

    // Load the arguments.
    for (int i = 0; i < args->length(); i++) Load(args->at(i));

    // Setup the receiver register and call the IC initialization code.
    Handle<Code> stub = ComputeCallInitialize(args->length());
    __ RecordPosition(node->position());
    __ Call(stub, RelocInfo::CODE_TARGET_CONTEXT);
    __ ldr(cp, frame_->Context());
    // Remove the function from the stack.
    frame_->Pop();
    frame_->Push(r0);

  } else if (var != NULL && var->slot() != NULL &&
             var->slot()->type() == Slot::LOOKUP) {
    // ----------------------------------
    // JavaScript example: 'with (obj) foo(1, 2, 3)'  // foo is in obj
    // ----------------------------------

    // Load the function
    frame_->Push(cp);
    __ mov(r0, Operand(var->name()));
    frame_->Push(r0);
    __ CallRuntime(Runtime::kLoadContextSlot, 2);
    // r0: slot value; r1: receiver

    // Load the receiver.
    frame_->Push(r0);  // function
    frame_->Push(r1);  // receiver

    // Call the function.
    CallWithArguments(args, node->position());
    frame_->Push(r0);

  } else if (property != NULL) {
    // Check if the key is a literal string.
    Literal* literal = property->key()->AsLiteral();

    if (literal != NULL && literal->handle()->IsSymbol()) {
      // ------------------------------------------------------------------
      // JavaScript example: 'object.foo(1, 2, 3)' or 'map["key"](1, 2, 3)'
      // ------------------------------------------------------------------

      // Push the name of the function and the receiver onto the stack.
      __ mov(r0, Operand(literal->handle()));
      frame_->Push(r0);
      Load(property->obj());

      // Load the arguments.
      for (int i = 0; i < args->length(); i++) Load(args->at(i));

      // Set the receiver register and call the IC initialization code.
      Handle<Code> stub = ComputeCallInitialize(args->length());
      __ RecordPosition(node->position());
      __ Call(stub, RelocInfo::CODE_TARGET);
      __ ldr(cp, frame_->Context());

      // Remove the function from the stack.
      frame_->Pop();

      frame_->Push(r0);  // push after get rid of function from the stack

    } else {
      // -------------------------------------------
      // JavaScript example: 'array[index](1, 2, 3)'
      // -------------------------------------------

      // Load the function to call from the property through a reference.
      Reference ref(this, property);
      ref.GetValue(NOT_INSIDE_TYPEOF);  // receiver

      // Pass receiver to called function.
      __ ldr(r0, frame_->Element(ref.size()));
      frame_->Push(r0);
      // Call the function.
      CallWithArguments(args, node->position());
      frame_->Push(r0);
    }

  } else {
    // ----------------------------------
    // JavaScript example: 'foo(1, 2, 3)'  // foo is not global
    // ----------------------------------

    // Load the function.
    Load(function);

    // Pass the global proxy as the receiver.
    LoadGlobalReceiver(r0);

    // Call the function.
    CallWithArguments(args, node->position());
    frame_->Push(r0);
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
  // receiver. There is no need to use the global proxy here because
  // it will always be replaced with a newly allocated object.
  Load(node->expression());
  LoadGlobal();

  // Push the arguments ("left-to-right") on the stack.
  ZoneList<Expression*>* args = node->arguments();
  for (int i = 0; i < args->length(); i++) Load(args->at(i));

  // r0: the number of arguments.
  __ mov(r0, Operand(args->length()));

  // Load the function into r1 as per calling convention.
  __ ldr(r1, frame_->Element(args->length() + 1));

  // Call the construct call builtin that handles allocation and
  // constructor invocation.
  __ RecordPosition(RelocInfo::POSITION);
  __ Call(Handle<Code>(Builtins::builtin(Builtins::JSConstructCall)),
          RelocInfo::CONSTRUCT_CALL);

  // Discard old TOS value and push r0 on the stack (same as Pop(), push(r0)).
  __ str(r0, frame_->Top());
}


void CodeGenerator::GenerateValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Label leave;
  Load(args->at(0));
  frame_->Pop(r0);  // r0 contains object.
  // if (object->IsSmi()) return the object.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &leave);
  // It is a heap object - get map.
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return the object.
  __ cmp(r1, Operand(JS_VALUE_TYPE));
  __ b(ne, &leave);
  // Load the value.
  __ ldr(r0, FieldMemOperand(r0, JSValue::kValueOffset));
  __ bind(&leave);
  frame_->Push(r0);
}


void CodeGenerator::GenerateSetValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);
  Label leave;
  Load(args->at(0));  // Load the object.
  Load(args->at(1));  // Load the value.
  frame_->Pop(r0);  // r0 contains value
  frame_->Pop(r1);  // r1 contains object
  // if (object->IsSmi()) return object.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &leave);
  // It is a heap object - get map.
  __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return object.
  __ cmp(r2, Operand(JS_VALUE_TYPE));
  __ b(ne, &leave);
  // Store the value.
  __ str(r0, FieldMemOperand(r1, JSValue::kValueOffset));
  // Update the write barrier.
  __ mov(r2, Operand(JSValue::kValueOffset - kHeapObjectTag));
  __ RecordWrite(r1, r2, r3);
  // Leave.
  __ bind(&leave);
  frame_->Push(r0);
}


void CodeGenerator::GenerateIsSmi(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  frame_->Pop(r0);
  __ tst(r0, Operand(kSmiTagMask));
  cc_reg_ = eq;
}


void CodeGenerator::GenerateIsNonNegativeSmi(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  frame_->Pop(r0);
  __ tst(r0, Operand(kSmiTagMask | 0x80000000));
  cc_reg_ = eq;
}


// This should generate code that performs a charCodeAt() call or returns
// undefined in order to trigger the slow case, Runtime_StringCharCodeAt.
// It is not yet implemented on ARM, so it always goes to the slow case.
void CodeGenerator::GenerateFastCharCodeAt(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);
  __ mov(r0, Operand(Factory::undefined_value()));
  frame_->Push(r0);
}


void CodeGenerator::GenerateIsArray(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  Label answer;
  // We need the CC bits to come out as not_equal in the case where the
  // object is a smi.  This can't be done with the usual test opcode so
  // we use XOR to get the right CC bits.
  frame_->Pop(r0);
  __ and_(r1, r0, Operand(kSmiTagMask));
  __ eor(r1, r1, Operand(kSmiTagMask), SetCC);
  __ b(ne, &answer);
  // It is a heap object - get the map.
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  // Check if the object is a JS array or not.
  __ cmp(r1, Operand(JS_ARRAY_TYPE));
  __ bind(&answer);
  cc_reg_ = eq;
}


void CodeGenerator::GenerateArgumentsLength(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 0);

  // Seed the result with the formal parameters count, which will be used
  // in case no arguments adaptor frame is found below the current frame.
  __ mov(r0, Operand(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to the arguments.length.
  ArgumentsAccessStub stub(ArgumentsAccessStub::READ_LENGTH);
  __ CallStub(&stub);
  frame_->Push(r0);
}


void CodeGenerator::GenerateArgumentsAccess(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);

  // Satisfy contract with ArgumentsAccessStub:
  // Load the key into r1 and the formal parameters count into r0.
  Load(args->at(0));
  frame_->Pop(r1);
  __ mov(r0, Operand(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to arguments[key].
  ArgumentsAccessStub stub(ArgumentsAccessStub::READ_ELEMENT);
  __ CallStub(&stub);
  frame_->Push(r0);
}


void CodeGenerator::GenerateObjectEquals(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);

  // Load the two objects into registers and perform the comparison.
  Load(args->at(0));
  Load(args->at(1));
  frame_->Pop(r0);
  frame_->Pop(r1);
  __ cmp(r0, Operand(r1));
  cc_reg_ = eq;
}


void CodeGenerator::VisitCallRuntime(CallRuntime* node) {
  if (CheckForInlineRuntimeCall(node)) return;

  ZoneList<Expression*>* args = node->arguments();
  Comment cmnt(masm_, "[ CallRuntime");
  Runtime::Function* function = node->function();

  if (function != NULL) {
    // Push the arguments ("left-to-right").
    for (int i = 0; i < args->length(); i++) Load(args->at(i));

    // Call the C runtime function.
    __ CallRuntime(function, args->length());
    frame_->Push(r0);

  } else {
    // Prepare stack for calling JS runtime function.
    __ mov(r0, Operand(node->name()));
    frame_->Push(r0);
    // Push the builtins object found in the current global object.
    __ ldr(r1, GlobalObject());
    __ ldr(r0, FieldMemOperand(r1, GlobalObject::kBuiltinsOffset));
    frame_->Push(r0);

    for (int i = 0; i < args->length(); i++) Load(args->at(i));

    // Call the JS runtime function.
    Handle<Code> stub = ComputeCallInitialize(args->length());
    __ Call(stub, RelocInfo::CODE_TARGET);
    __ ldr(cp, frame_->Context());
    frame_->Pop();
    frame_->Push(r0);
  }
}


void CodeGenerator::VisitUnaryOperation(UnaryOperation* node) {
  Comment cmnt(masm_, "[ UnaryOperation");

  Token::Value op = node->op();

  if (op == Token::NOT) {
    LoadCondition(node->expression(),
                  NOT_INSIDE_TYPEOF,
                  false_target(),
                  true_target(),
                  true);
    cc_reg_ = NegateCondition(cc_reg_);

  } else if (op == Token::DELETE) {
    Property* property = node->expression()->AsProperty();
    Variable* variable = node->expression()->AsVariableProxy()->AsVariable();
    if (property != NULL) {
      Load(property->obj());
      Load(property->key());
      __ mov(r0, Operand(1));  // not counting receiver
      __ InvokeBuiltin(Builtins::DELETE, CALL_JS);

    } else if (variable != NULL) {
      Slot* slot = variable->slot();
      if (variable->is_global()) {
        LoadGlobal();
        __ mov(r0, Operand(variable->name()));
        frame_->Push(r0);
        __ mov(r0, Operand(1));  // not counting receiver
        __ InvokeBuiltin(Builtins::DELETE, CALL_JS);

      } else if (slot != NULL && slot->type() == Slot::LOOKUP) {
        // lookup the context holding the named variable
        frame_->Push(cp);
        __ mov(r0, Operand(variable->name()));
        frame_->Push(r0);
        __ CallRuntime(Runtime::kLookupContext, 2);
        // r0: context
        frame_->Push(r0);
        __ mov(r0, Operand(variable->name()));
        frame_->Push(r0);
        __ mov(r0, Operand(1));  // not counting receiver
        __ InvokeBuiltin(Builtins::DELETE, CALL_JS);

      } else {
        // Default: Result of deleting non-global, not dynamically
        // introduced variables is false.
        __ mov(r0, Operand(Factory::false_value()));
      }

    } else {
      // Default: Result of deleting expressions is true.
      Load(node->expression());  // may have side-effects
      frame_->Pop();
      __ mov(r0, Operand(Factory::true_value()));
    }
    frame_->Push(r0);

  } else if (op == Token::TYPEOF) {
    // Special case for loading the typeof expression; see comment on
    // LoadTypeofExpression().
    LoadTypeofExpression(node->expression());
    __ CallRuntime(Runtime::kTypeof, 1);
    frame_->Push(r0);  // r0 has result

  } else {
    Load(node->expression());
    frame_->Pop(r0);
    switch (op) {
      case Token::NOT:
      case Token::DELETE:
      case Token::TYPEOF:
        UNREACHABLE();  // handled above
        break;

      case Token::SUB: {
        UnarySubStub stub;
        __ CallStub(&stub);
        break;
      }

      case Token::BIT_NOT: {
        // smi check
        Label smi_label;
        Label continue_label;
        __ tst(r0, Operand(kSmiTagMask));
        __ b(eq, &smi_label);

        frame_->Push(r0);
        __ mov(r0, Operand(0));  // not counting receiver
        __ InvokeBuiltin(Builtins::BIT_NOT, CALL_JS);

        __ b(&continue_label);
        __ bind(&smi_label);
        __ mvn(r0, Operand(r0));
        __ bic(r0, r0, Operand(kSmiTagMask));  // bit-clear inverted smi-tag
        __ bind(&continue_label);
        break;
      }

      case Token::VOID:
        // since the stack top is cached in r0, popping and then
        // pushing a value can be done by just writing to r0.
        __ mov(r0, Operand(Factory::undefined_value()));
        break;

      case Token::ADD: {
        // Smi check.
        Label continue_label;
        __ tst(r0, Operand(kSmiTagMask));
        __ b(eq, &continue_label);
        frame_->Push(r0);
        __ mov(r0, Operand(0));  // not counting receiver
        __ InvokeBuiltin(Builtins::TO_NUMBER, CALL_JS);
        __ bind(&continue_label);
        break;
      }
      default:
        UNREACHABLE();
    }
    frame_->Push(r0);  // r0 has result
  }
}


void CodeGenerator::VisitCountOperation(CountOperation* node) {
  Comment cmnt(masm_, "[ CountOperation");

  bool is_postfix = node->is_postfix();
  bool is_increment = node->op() == Token::INC;

  Variable* var = node->expression()->AsVariableProxy()->AsVariable();
  bool is_const = (var != NULL && var->mode() == Variable::CONST);

  // Postfix: Make room for the result.
  if (is_postfix) {
     __ mov(r0, Operand(0));
     frame_->Push(r0);
  }

  { Reference target(this, node->expression());
    if (target.is_illegal()) return;
    target.GetValue(NOT_INSIDE_TYPEOF);
    frame_->Pop(r0);

    Label slow, exit;

    // Load the value (1) into register r1.
    __ mov(r1, Operand(Smi::FromInt(1)));

    // Check for smi operand.
    __ tst(r0, Operand(kSmiTagMask));
    __ b(ne, &slow);

    // Postfix: Store the old value as the result.
    if (is_postfix) {
      __ str(r0, frame_->Element(target.size()));
    }

    // Perform optimistic increment/decrement.
    if (is_increment) {
      __ add(r0, r0, Operand(r1), SetCC);
    } else {
      __ sub(r0, r0, Operand(r1), SetCC);
    }

    // If the increment/decrement didn't overflow, we're done.
    __ b(vc, &exit);

    // Revert optimistic increment/decrement.
    if (is_increment) {
      __ sub(r0, r0, Operand(r1));
    } else {
      __ add(r0, r0, Operand(r1));
    }

    // Slow case: Convert to number.
    __ bind(&slow);

    // Postfix: Convert the operand to a number and store it as the result.
    if (is_postfix) {
      InvokeBuiltinStub stub(InvokeBuiltinStub::ToNumber, 2);
      __ CallStub(&stub);
      // Store to result (on the stack).
      __ str(r0, frame_->Element(target.size()));
    }

    // Compute the new value by calling the right JavaScript native.
    if (is_increment) {
      InvokeBuiltinStub stub(InvokeBuiltinStub::Inc, 1);
      __ CallStub(&stub);
    } else {
      InvokeBuiltinStub stub(InvokeBuiltinStub::Dec, 1);
      __ CallStub(&stub);
    }

    // Store the new value in the target if not const.
    __ bind(&exit);
    frame_->Push(r0);
    if (!is_const) target.SetValue(NOT_CONST_INIT);
  }

  // Postfix: Discard the new value and use the old.
  if (is_postfix) frame_->Pop(r0);
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
    LoadCondition(node->left(),
                  NOT_INSIDE_TYPEOF,
                  &is_true,
                  false_target(),
                  false);
    if (has_cc()) {
      Branch(false, false_target());

      // Evaluate right side expression.
      __ bind(&is_true);
      LoadCondition(node->right(),
                    NOT_INSIDE_TYPEOF,
                    true_target(),
                    false_target(),
                    false);

    } else {
      Label pop_and_continue, exit;

      __ ldr(r0, frame_->Top());  // dup the stack top
      frame_->Push(r0);
      // Avoid popping the result if it converts to 'false' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      ToBoolean(&pop_and_continue, &exit);
      Branch(false, &exit);

      // Pop the result of evaluating the first part.
      __ bind(&pop_and_continue);
      frame_->Pop(r0);

      // Evaluate right side expression.
      __ bind(&is_true);
      Load(node->right());

      // Exit (always with a materialized value).
      __ bind(&exit);
    }

  } else if (op == Token::OR) {
    Label is_false;
    LoadCondition(node->left(),
                  NOT_INSIDE_TYPEOF,
                  true_target(),
                  &is_false,
                  false);
    if (has_cc()) {
      Branch(true, true_target());

      // Evaluate right side expression.
      __ bind(&is_false);
      LoadCondition(node->right(),
                    NOT_INSIDE_TYPEOF,
                    true_target(),
                    false_target(),
                    false);

    } else {
      Label pop_and_continue, exit;

      __ ldr(r0, frame_->Top());
      frame_->Push(r0);
      // Avoid popping the result if it converts to 'true' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      ToBoolean(&exit, &pop_and_continue);
      Branch(true, &exit);

      // Pop the result of evaluating the first part.
      __ bind(&pop_and_continue);
      frame_->Pop(r0);

      // Evaluate right side expression.
      __ bind(&is_false);
      Load(node->right());

      // Exit (always with a materialized value).
      __ bind(&exit);
    }

  } else {
    // Optimize for the case where (at least) one of the expressions
    // is a literal small integer.
    Literal* lliteral = node->left()->AsLiteral();
    Literal* rliteral = node->right()->AsLiteral();

    if (rliteral != NULL && rliteral->handle()->IsSmi()) {
      Load(node->left());
      SmiOperation(node->op(), rliteral->handle(), false);

    } else if (lliteral != NULL && lliteral->handle()->IsSmi()) {
      Load(node->right());
      SmiOperation(node->op(), lliteral->handle(), true);

    } else {
      Load(node->left());
      Load(node->right());
      GenericBinaryOperation(node->op());
    }
    frame_->Push(r0);
  }
}


void CodeGenerator::VisitThisFunction(ThisFunction* node) {
  __ ldr(r0, frame_->Function());
  frame_->Push(r0);
}


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

  if (op == Token::EQ || op == Token::EQ_STRICT) {
    bool left_is_null =
      left->AsLiteral() != NULL && left->AsLiteral()->IsNull();
    bool right_is_null =
      right->AsLiteral() != NULL && right->AsLiteral()->IsNull();
    // The 'null' value is only equal to 'null' or 'undefined'.
    if (left_is_null || right_is_null) {
      Load(left_is_null ? right : left);
      Label exit, undetectable;
      frame_->Pop(r0);
      __ cmp(r0, Operand(Factory::null_value()));

      // The 'null' value is only equal to 'undefined' if using
      // non-strict comparisons.
      if (op != Token::EQ_STRICT) {
        __ b(eq, &exit);
        __ cmp(r0, Operand(Factory::undefined_value()));

        // NOTE: it can be undetectable object.
        __ b(eq, &exit);
        __ tst(r0, Operand(kSmiTagMask));

        __ b(ne, &undetectable);
        __ b(false_target());

        __ bind(&undetectable);
        __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
        __ ldrb(r2, FieldMemOperand(r1, Map::kBitFieldOffset));
        __ and_(r2, r2, Operand(1 << Map::kIsUndetectable));
        __ cmp(r2, Operand(1 << Map::kIsUndetectable));
      }

      __ bind(&exit);

      cc_reg_ = eq;
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

    // Load the operand, move it to register r1.
    LoadTypeofExpression(operation->expression());
    frame_->Pop(r1);

    if (check->Equals(Heap::number_symbol())) {
      __ tst(r1, Operand(kSmiTagMask));
      __ b(eq, true_target());
      __ ldr(r1, FieldMemOperand(r1, HeapObject::kMapOffset));
      __ cmp(r1, Operand(Factory::heap_number_map()));
      cc_reg_ = eq;

    } else if (check->Equals(Heap::string_symbol())) {
      __ tst(r1, Operand(kSmiTagMask));
      __ b(eq, false_target());

      __ ldr(r1, FieldMemOperand(r1, HeapObject::kMapOffset));

      // NOTE: it might be an undetectable string object
      __ ldrb(r2, FieldMemOperand(r1, Map::kBitFieldOffset));
      __ and_(r2, r2, Operand(1 << Map::kIsUndetectable));
      __ cmp(r2, Operand(1 << Map::kIsUndetectable));
      __ b(eq, false_target());

      __ ldrb(r2, FieldMemOperand(r1, Map::kInstanceTypeOffset));
      __ cmp(r2, Operand(FIRST_NONSTRING_TYPE));
      cc_reg_ = lt;

    } else if (check->Equals(Heap::boolean_symbol())) {
      __ cmp(r1, Operand(Factory::true_value()));
      __ b(eq, true_target());
      __ cmp(r1, Operand(Factory::false_value()));
      cc_reg_ = eq;

    } else if (check->Equals(Heap::undefined_symbol())) {
      __ cmp(r1, Operand(Factory::undefined_value()));
      __ b(eq, true_target());

      __ tst(r1, Operand(kSmiTagMask));
      __ b(eq, false_target());

      // NOTE: it can be undetectable object.
      __ ldr(r1, FieldMemOperand(r1, HeapObject::kMapOffset));
      __ ldrb(r2, FieldMemOperand(r1, Map::kBitFieldOffset));
      __ and_(r2, r2, Operand(1 << Map::kIsUndetectable));
      __ cmp(r2, Operand(1 << Map::kIsUndetectable));

      cc_reg_ = eq;

    } else if (check->Equals(Heap::function_symbol())) {
      __ tst(r1, Operand(kSmiTagMask));
      __ b(eq, false_target());
      __ ldr(r1, FieldMemOperand(r1, HeapObject::kMapOffset));
      __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
      __ cmp(r1, Operand(JS_FUNCTION_TYPE));
      cc_reg_ = eq;

    } else if (check->Equals(Heap::object_symbol())) {
      __ tst(r1, Operand(kSmiTagMask));
      __ b(eq, false_target());

      __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
      __ cmp(r1, Operand(Factory::null_value()));
      __ b(eq, true_target());

      // NOTE: it might be an undetectable object.
      __ ldrb(r1, FieldMemOperand(r2, Map::kBitFieldOffset));
      __ and_(r1, r1, Operand(1 << Map::kIsUndetectable));
      __ cmp(r1, Operand(1 << Map::kIsUndetectable));
      __ b(eq, false_target());

      __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
      __ cmp(r2, Operand(FIRST_JS_OBJECT_TYPE));
      __ b(lt, false_target());
      __ cmp(r2, Operand(LAST_JS_OBJECT_TYPE));
      cc_reg_ = le;

    } else {
      // Uncommon case: Typeof testing against a string literal that
      // is never returned from the typeof operator.
      __ b(false_target());
    }
    return;
  }

  Load(left);
  Load(right);
  switch (op) {
    case Token::EQ:
      Comparison(eq, false);
      break;

    case Token::LT:
      Comparison(lt);
      break;

    case Token::GT:
      Comparison(gt);
      break;

    case Token::LTE:
      Comparison(le);
      break;

    case Token::GTE:
      Comparison(ge);
      break;

    case Token::EQ_STRICT:
      Comparison(eq, true);
      break;

    case Token::IN:
      __ mov(r0, Operand(1));  // not counting receiver
      __ InvokeBuiltin(Builtins::IN, CALL_JS);
      frame_->Push(r0);
      break;

    case Token::INSTANCEOF:
      __ mov(r0, Operand(1));  // not counting receiver
      __ InvokeBuiltin(Builtins::INSTANCE_OF, CALL_JS);
      __ tst(r0, Operand(r0));
      cc_reg_ = eq;
      break;

    default:
      UNREACHABLE();
  }
}


void CodeGenerator::RecordStatementPosition(Node* node) {
  if (FLAG_debug_info) {
    int statement_pos = node->statement_pos();
    if (statement_pos == RelocInfo::kNoPosition) return;
    __ RecordStatementPosition(statement_pos);
  }
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
  ASSERT(!is_illegal());
  ASSERT(!cgen_->has_cc());
  MacroAssembler* masm = cgen_->masm();
  VirtualFrame* frame = cgen_->frame();
  Property* property = expression_->AsProperty();
  if (property != NULL) {
    __ RecordPosition(property->position());
  }

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
      // Setup the name register.
      Handle<String> name(GetName());
      __ mov(r2, Operand(name));
      Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));

      Variable* var = expression_->AsVariableProxy()->AsVariable();
      if (var != NULL) {
        ASSERT(var->is_global());
        __ Call(ic, RelocInfo::CODE_TARGET_CONTEXT);
      } else {
        __ Call(ic, RelocInfo::CODE_TARGET);
      }
      frame->Push(r0);
      break;
    }

    case KEYED: {
      // TODO(1241834): Make sure that this it is safe to ignore the
      // distinction between expressions in a typeof and not in a typeof.
      Comment cmnt(masm, "[ Load from keyed Property");
      ASSERT(property != NULL);
      // TODO(1224671): Implement inline caching for keyed loads as on ia32.
      GetPropertyStub stub;
      __ CallStub(&stub);
      frame->Push(r0);
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
  VirtualFrame* frame = cgen_->frame();
  Property* property = expression_->AsProperty();
  if (property != NULL) {
    __ RecordPosition(property->position());
  }

  switch (type_) {
    case SLOT: {
      Comment cmnt(masm, "[ Store to Slot");
      Slot* slot = expression_->AsVariableProxy()->AsVariable()->slot();
      ASSERT(slot != NULL);
      if (slot->type() == Slot::LOOKUP) {
        ASSERT(slot->var()->mode() == Variable::DYNAMIC);

        // For now, just do a runtime call.
        frame->Push(cp);
        __ mov(r0, Operand(slot->var()->name()));
        frame->Push(r0);

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
        // stack. This is necessary for compiling assignment expressions.
        frame->Push(r0);

      } else {
        ASSERT(slot->var()->mode() != Variable::DYNAMIC);

        Label exit;
        if (init_state == CONST_INIT) {
          ASSERT(slot->var()->mode() == Variable::CONST);
          // Only the first const initialization must be executed (the slot
          // still contains 'the hole' value). When the assignment is
          // executed, the code is identical to a normal store (see below).
          Comment cmnt(masm, "[ Init const");
          __ ldr(r2, cgen_->SlotOperand(slot, r2));
          __ cmp(r2, Operand(Factory::the_hole_value()));
          __ b(ne, &exit);
        }

        // We must execute the store.  Storing a variable must keep the
        // (new) value on the stack. This is necessary for compiling
        // assignment expressions.
        //
        // Note: We will reach here even with slot->var()->mode() ==
        // Variable::CONST because of const declarations which will
        // initialize consts to 'the hole' value and by doing so, end up
        // calling this code.  r2 may be loaded with context; used below in
        // RecordWrite.
        frame->Pop(r0);
        __ str(r0, cgen_->SlotOperand(slot, r2));
        frame->Push(r0);
        if (slot->type() == Slot::CONTEXT) {
          // Skip write barrier if the written value is a smi.
          __ tst(r0, Operand(kSmiTagMask));
          __ b(eq, &exit);
          // r2 is loaded with context when calling SlotOperand above.
          int offset = FixedArray::kHeaderSize + slot->index() * kPointerSize;
          __ mov(r3, Operand(offset));
          __ RecordWrite(r2, r3, r1);
        }
        // If we definitely did not jump over the assignment, we do not need
        // to bind the exit label.  Doing so can defeat peephole
        // optimization.
        if (init_state == CONST_INIT || slot->type() == Slot::CONTEXT) {
          __ bind(&exit);
        }
      }
      break;
    }

    case NAMED: {
      Comment cmnt(masm, "[ Store to named Property");
      // Call the appropriate IC code.
      frame->Pop(r0);  // value
      // Setup the name register.
      Handle<String> name(GetName());
      __ mov(r2, Operand(name));
      Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
      __ Call(ic, RelocInfo::CODE_TARGET);
      frame->Push(r0);
      break;
    }

    case KEYED: {
      Comment cmnt(masm, "[ Store to keyed Property");
      Property* property = expression_->AsProperty();
      ASSERT(property != NULL);
      __ RecordPosition(property->position());
      frame->Pop(r0);  // value
      SetPropertyStub stub;
      __ CallStub(&stub);
      frame->Push(r0);
      break;
    }

    default:
      UNREACHABLE();
  }
}


void GetPropertyStub::Generate(MacroAssembler* masm) {
  // sp[0]: key
  // sp[1]: receiver
  Label slow, fast;
  // Get the key and receiver object from the stack.
  __ ldm(ia, sp, r0.bit() | r1.bit());
  // Check that the key is a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(ne, &slow);
  __ mov(r0, Operand(r0, ASR, kSmiTagSize));
  // Check that the object isn't a smi.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &slow);

  // Check that the object is some kind of JS object EXCEPT JS Value type.
  // In the case that the object is a value-wrapper object,
  // we enter the runtime system to make sure that indexing into string
  // objects work as intended.
  ASSERT(JS_OBJECT_TYPE > JS_VALUE_TYPE);
  __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  __ cmp(r2, Operand(JS_OBJECT_TYPE));
  __ b(lt, &slow);

  // Get the elements array of the object.
  __ ldr(r1, FieldMemOperand(r1, JSObject::kElementsOffset));
  // Check that the object is in fast mode (not dictionary).
  __ ldr(r3, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ cmp(r3, Operand(Factory::hash_table_map()));
  __ b(eq, &slow);
  // Check that the key (index) is within bounds.
  __ ldr(r3, FieldMemOperand(r1, Array::kLengthOffset));
  __ cmp(r0, Operand(r3));
  __ b(lo, &fast);

  // Slow case: Push extra copies of the arguments (2).
  __ bind(&slow);
  __ ldm(ia, sp, r0.bit() | r1.bit());
  __ stm(db_w, sp, r0.bit() | r1.bit());
  // Do tail-call to runtime routine.
  __ TailCallRuntime(ExternalReference(Runtime::kGetProperty), 2);

  // Fast case: Do the load.
  __ bind(&fast);
  __ add(r3, r1, Operand(Array::kHeaderSize - kHeapObjectTag));
  __ ldr(r0, MemOperand(r3, r0, LSL, kPointerSizeLog2));
  __ cmp(r0, Operand(Factory::the_hole_value()));
  // In case the loaded value is the_hole we have to consult GetProperty
  // to ensure the prototype chain is searched.
  __ b(eq, &slow);

  __ StubReturn(1);
}


void SetPropertyStub::Generate(MacroAssembler* masm) {
  // r0 : value
  // sp[0] : key
  // sp[1] : receiver

  Label slow, fast, array, extra, exit;
  // Get the key and the object from the stack.
  __ ldm(ia, sp, r1.bit() | r3.bit());  // r1 = key, r3 = receiver
  // Check that the key is a smi.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(ne, &slow);
  // Check that the object isn't a smi.
  __ tst(r3, Operand(kSmiTagMask));
  __ b(eq, &slow);
  // Get the type of the object from its map.
  __ ldr(r2, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  // Check if the object is a JS array or not.
  __ cmp(r2, Operand(JS_ARRAY_TYPE));
  __ b(eq, &array);
  // Check that the object is some kind of JS object.
  __ cmp(r2, Operand(FIRST_JS_OBJECT_TYPE));
  __ b(lt, &slow);


  // Object case: Check key against length in the elements array.
  __ ldr(r3, FieldMemOperand(r3, JSObject::kElementsOffset));
  // Check that the object is in fast mode (not dictionary).
  __ ldr(r2, FieldMemOperand(r3, HeapObject::kMapOffset));
  __ cmp(r2, Operand(Factory::hash_table_map()));
  __ b(eq, &slow);
  // Untag the key (for checking against untagged length in the fixed array).
  __ mov(r1, Operand(r1, ASR, kSmiTagSize));
  // Compute address to store into and check array bounds.
  __ add(r2, r3, Operand(Array::kHeaderSize - kHeapObjectTag));
  __ add(r2, r2, Operand(r1, LSL, kPointerSizeLog2));
  __ ldr(ip, FieldMemOperand(r3, Array::kLengthOffset));
  __ cmp(r1, Operand(ip));
  __ b(lo, &fast);


  // Slow case: Push extra copies of the arguments (3).
  __ bind(&slow);
  __ ldm(ia, sp, r1.bit() | r3.bit());  // r0 == value, r1 == key, r3 == object
  __ stm(db_w, sp, r0.bit() | r1.bit() | r3.bit());
  // Do tail-call to runtime routine.
  __ TailCallRuntime(ExternalReference(Runtime::kSetProperty), 3);


  // Extra capacity case: Check if there is extra capacity to
  // perform the store and update the length. Used for adding one
  // element to the array by writing to array[array.length].
  // r0 == value, r1 == key, r2 == elements, r3 == object
  __ bind(&extra);
  __ b(ne, &slow);  // do not leave holes in the array
  __ mov(r1, Operand(r1, ASR, kSmiTagSize));  // untag
  __ ldr(ip, FieldMemOperand(r2, Array::kLengthOffset));
  __ cmp(r1, Operand(ip));
  __ b(hs, &slow);
  __ mov(r1, Operand(r1, LSL, kSmiTagSize));  // restore tag
  __ add(r1, r1, Operand(1 << kSmiTagSize));  // and increment
  __ str(r1, FieldMemOperand(r3, JSArray::kLengthOffset));
  __ mov(r3, Operand(r2));
  // NOTE: Computing the address to store into must take the fact
  // that the key has been incremented into account.
  int displacement = Array::kHeaderSize - kHeapObjectTag -
      ((1 << kSmiTagSize) * 2);
  __ add(r2, r2, Operand(displacement));
  __ add(r2, r2, Operand(r1, LSL, kPointerSizeLog2 - kSmiTagSize));
  __ b(&fast);


  // Array case: Get the length and the elements array from the JS
  // array. Check that the array is in fast mode; if it is the
  // length is always a smi.
  // r0 == value, r3 == object
  __ bind(&array);
  __ ldr(r2, FieldMemOperand(r3, JSObject::kElementsOffset));
  __ ldr(r1, FieldMemOperand(r2, HeapObject::kMapOffset));
  __ cmp(r1, Operand(Factory::hash_table_map()));
  __ b(eq, &slow);

  // Check the key against the length in the array, compute the
  // address to store into and fall through to fast case.
  __ ldr(r1, MemOperand(sp));
  // r0 == value, r1 == key, r2 == elements, r3 == object.
  __ ldr(ip, FieldMemOperand(r3, JSArray::kLengthOffset));
  __ cmp(r1, Operand(ip));
  __ b(hs, &extra);
  __ mov(r3, Operand(r2));
  __ add(r2, r2, Operand(Array::kHeaderSize - kHeapObjectTag));
  __ add(r2, r2, Operand(r1, LSL, kPointerSizeLog2 - kSmiTagSize));


  // Fast case: Do the store.
  // r0 == value, r2 == address to store into, r3 == elements
  __ bind(&fast);
  __ str(r0, MemOperand(r2));
  // Skip write barrier if the written value is a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &exit);
  // Update write barrier for the elements array address.
  __ sub(r1, r2, Operand(r3));
  __ RecordWrite(r3, r1, r2);
  __ bind(&exit);
  __ StubReturn(1);
}


void GenericBinaryOpStub::Generate(MacroAssembler* masm) {
  // r1 : x
  // r0 : y
  // result : r0

  switch (op_) {
    case Token::ADD: {
      Label slow, exit;
      // fast path
      __ orr(r2, r1, Operand(r0));  // r2 = x | y;
      __ add(r0, r1, Operand(r0), SetCC);  // add y optimistically
      // go slow-path in case of overflow
      __ b(vs, &slow);
      // go slow-path in case of non-smi operands
      ASSERT(kSmiTag == 0);  // adjust code below
      __ tst(r2, Operand(kSmiTagMask));
      __ b(eq, &exit);
      // slow path
      __ bind(&slow);
      __ sub(r0, r0, Operand(r1));  // revert optimistic add
      __ push(r1);
      __ push(r0);
      __ mov(r0, Operand(1));  // set number of arguments
      __ InvokeBuiltin(Builtins::ADD, JUMP_JS);
      // done
      __ bind(&exit);
      break;
    }

    case Token::SUB: {
      Label slow, exit;
      // fast path
      __ orr(r2, r1, Operand(r0));  // r2 = x | y;
      __ sub(r3, r1, Operand(r0), SetCC);  // subtract y optimistically
      // go slow-path in case of overflow
      __ b(vs, &slow);
      // go slow-path in case of non-smi operands
      ASSERT(kSmiTag == 0);  // adjust code below
      __ tst(r2, Operand(kSmiTagMask));
      __ mov(r0, Operand(r3), LeaveCC, eq);  // conditionally set r0 to result
      __ b(eq, &exit);
      // slow path
      __ bind(&slow);
      __ push(r1);
      __ push(r0);
      __ mov(r0, Operand(1));  // set number of arguments
      __ InvokeBuiltin(Builtins::SUB, JUMP_JS);
      // done
      __ bind(&exit);
      break;
    }

    case Token::MUL: {
      Label slow, exit;
      // tag check
      __ orr(r2, r1, Operand(r0));  // r2 = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ tst(r2, Operand(kSmiTagMask));
      __ b(ne, &slow);
      // remove tag from one operand (but keep sign), so that result is smi
      __ mov(ip, Operand(r0, ASR, kSmiTagSize));
      // do multiplication
      __ smull(r3, r2, r1, ip);  // r3 = lower 32 bits of ip*r1
      // go slow on overflows (overflow bit is not set)
      __ mov(ip, Operand(r3, ASR, 31));
      __ cmp(ip, Operand(r2));  // no overflow if higher 33 bits are identical
      __ b(ne, &slow);
      // go slow on zero result to handle -0
      __ tst(r3, Operand(r3));
      __ mov(r0, Operand(r3), LeaveCC, ne);
      __ b(ne, &exit);
      // slow case
      __ bind(&slow);
      __ push(r1);
      __ push(r0);
      __ mov(r0, Operand(1));  // set number of arguments
      __ InvokeBuiltin(Builtins::MUL, JUMP_JS);
      // done
      __ bind(&exit);
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR: {
      Label slow, exit;
      // tag check
      __ orr(r2, r1, Operand(r0));  // r2 = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ tst(r2, Operand(kSmiTagMask));
      __ b(ne, &slow);
      switch (op_) {
        case Token::BIT_OR:  __ orr(r0, r0, Operand(r1)); break;
        case Token::BIT_AND: __ and_(r0, r0, Operand(r1)); break;
        case Token::BIT_XOR: __ eor(r0, r0, Operand(r1)); break;
        default: UNREACHABLE();
      }
      __ b(&exit);
      __ bind(&slow);
      __ push(r1);  // restore stack
      __ push(r0);
      __ mov(r0, Operand(1));  // 1 argument (not counting receiver).
      switch (op_) {
        case Token::BIT_OR:
          __ InvokeBuiltin(Builtins::BIT_OR, JUMP_JS);
          break;
        case Token::BIT_AND:
          __ InvokeBuiltin(Builtins::BIT_AND, JUMP_JS);
          break;
        case Token::BIT_XOR:
          __ InvokeBuiltin(Builtins::BIT_XOR, JUMP_JS);
          break;
        default:
          UNREACHABLE();
      }
      __ bind(&exit);
      break;
    }

    case Token::SHL:
    case Token::SHR:
    case Token::SAR: {
      Label slow, exit;
      // tag check
      __ orr(r2, r1, Operand(r0));  // r2 = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ tst(r2, Operand(kSmiTagMask));
      __ b(ne, &slow);
      // remove tags from operands (but keep sign)
      __ mov(r3, Operand(r1, ASR, kSmiTagSize));  // x
      __ mov(r2, Operand(r0, ASR, kSmiTagSize));  // y
      // use only the 5 least significant bits of the shift count
      __ and_(r2, r2, Operand(0x1f));
      // perform operation
      switch (op_) {
        case Token::SAR:
          __ mov(r3, Operand(r3, ASR, r2));
          // no checks of result necessary
          break;

        case Token::SHR:
          __ mov(r3, Operand(r3, LSR, r2));
          // check that the *unsigned* result fits in a smi
          // neither of the two high-order bits can be set:
          // - 0x80000000: high bit would be lost when smi tagging
          // - 0x40000000: this number would convert to negative when
          // smi tagging these two cases can only happen with shifts
          // by 0 or 1 when handed a valid smi
          __ and_(r2, r3, Operand(0xc0000000), SetCC);
          __ b(ne, &slow);
          break;

        case Token::SHL:
          __ mov(r3, Operand(r3, LSL, r2));
          // check that the *signed* result fits in a smi
          __ add(r2, r3, Operand(0x40000000), SetCC);
          __ b(mi, &slow);
          break;

        default: UNREACHABLE();
      }
      // tag result and store it in r0
      ASSERT(kSmiTag == 0);  // adjust code below
      __ mov(r0, Operand(r3, LSL, kSmiTagSize));
      __ b(&exit);
      // slow case
      __ bind(&slow);
      __ push(r1);  // restore stack
      __ push(r0);
      __ mov(r0, Operand(1));  // 1 argument (not counting receiver).
      switch (op_) {
        case Token::SAR: __ InvokeBuiltin(Builtins::SAR, JUMP_JS); break;
        case Token::SHR: __ InvokeBuiltin(Builtins::SHR, JUMP_JS); break;
        case Token::SHL: __ InvokeBuiltin(Builtins::SHL, JUMP_JS); break;
        default: UNREACHABLE();
      }
      __ bind(&exit);
      break;
    }

    default: UNREACHABLE();
  }
  __ Ret();
}


void StackCheckStub::Generate(MacroAssembler* masm) {
  Label within_limit;
  __ mov(ip, Operand(ExternalReference::address_of_stack_guard_limit()));
  __ ldr(ip, MemOperand(ip));
  __ cmp(sp, Operand(ip));
  __ b(hs, &within_limit);
  // Do tail-call to runtime routine.
  __ push(r0);
  __ TailCallRuntime(ExternalReference(Runtime::kStackGuard), 1);
  __ bind(&within_limit);

  __ StubReturn(1);
}


void UnarySubStub::Generate(MacroAssembler* masm) {
  Label undo;
  Label slow;
  Label done;

  // Enter runtime system if the value is not a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(ne, &slow);

  // Enter runtime system if the value of the expression is zero
  // to make sure that we switch between 0 and -0.
  __ cmp(r0, Operand(0));
  __ b(eq, &slow);

  // The value of the expression is a smi that is not zero.  Try
  // optimistic subtraction '0 - value'.
  __ rsb(r1, r0, Operand(0), SetCC);
  __ b(vs, &slow);

  // If result is a smi we are done.
  __ tst(r1, Operand(kSmiTagMask));
  __ mov(r0, Operand(r1), LeaveCC, eq);  // conditionally set r0 to result
  __ b(eq, &done);

  // Enter runtime system.
  __ bind(&slow);
  __ push(r0);
  __ mov(r0, Operand(0));  // set number of arguments
  __ InvokeBuiltin(Builtins::UNARY_MINUS, JUMP_JS);

  __ bind(&done);
  __ StubReturn(1);
}


void InvokeBuiltinStub::Generate(MacroAssembler* masm) {
  __ push(r0);
  __ mov(r0, Operand(0));  // set number of arguments
  switch (kind_) {
    case ToNumber: __ InvokeBuiltin(Builtins::TO_NUMBER, JUMP_JS); break;
    case Inc:      __ InvokeBuiltin(Builtins::INC, JUMP_JS);       break;
    case Dec:      __ InvokeBuiltin(Builtins::DEC, JUMP_JS);       break;
    default: UNREACHABLE();
  }
  __ StubReturn(argc_);
}


void CEntryStub::GenerateThrowTOS(MacroAssembler* masm) {
  // r0 holds exception
  ASSERT(StackHandlerConstants::kSize == 6 * kPointerSize);  // adjust this code
  __ mov(r3, Operand(ExternalReference(Top::k_handler_address)));
  __ ldr(sp, MemOperand(r3));
  __ pop(r2);  // pop next in chain
  __ str(r2, MemOperand(r3));
  // restore parameter- and frame-pointer and pop state.
  __ ldm(ia_w, sp, r3.bit() | pp.bit() | fp.bit());
  // Before returning we restore the context from the frame pointer if not NULL.
  // The frame pointer is NULL in the exception handler of a JS entry frame.
  __ cmp(fp, Operand(0));
  // Set cp to NULL if fp is NULL.
  __ mov(cp, Operand(0), LeaveCC, eq);
  // Restore cp otherwise.
  __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset), ne);
  if (kDebug && FLAG_debug_code) __ mov(lr, Operand(pc));
  __ pop(pc);
}


void CEntryStub::GenerateThrowOutOfMemory(MacroAssembler* masm) {
  // Fetch top stack handler.
  __ mov(r3, Operand(ExternalReference(Top::k_handler_address)));
  __ ldr(r3, MemOperand(r3));

  // Unwind the handlers until the ENTRY handler is found.
  Label loop, done;
  __ bind(&loop);
  // Load the type of the current stack handler.
  const int kStateOffset = StackHandlerConstants::kAddressDisplacement +
      StackHandlerConstants::kStateOffset;
  __ ldr(r2, MemOperand(r3, kStateOffset));
  __ cmp(r2, Operand(StackHandler::ENTRY));
  __ b(eq, &done);
  // Fetch the next handler in the list.
  const int kNextOffset =  StackHandlerConstants::kAddressDisplacement +
      StackHandlerConstants::kNextOffset;
  __ ldr(r3, MemOperand(r3, kNextOffset));
  __ jmp(&loop);
  __ bind(&done);

  // Set the top handler address to next handler past the current ENTRY handler.
  __ ldr(r0, MemOperand(r3, kNextOffset));
  __ mov(r2, Operand(ExternalReference(Top::k_handler_address)));
  __ str(r0, MemOperand(r2));

  // Set external caught exception to false.
  __ mov(r0, Operand(false));
  ExternalReference external_caught(Top::k_external_caught_exception_address);
  __ mov(r2, Operand(external_caught));
  __ str(r0, MemOperand(r2));

  // Set pending exception and r0 to out of memory exception.
  Failure* out_of_memory = Failure::OutOfMemoryException();
  __ mov(r0, Operand(reinterpret_cast<int32_t>(out_of_memory)));
  __ mov(r2, Operand(ExternalReference(Top::k_pending_exception_address)));
  __ str(r0, MemOperand(r2));

  // Restore the stack to the address of the ENTRY handler
  __ mov(sp, Operand(r3));

  // Stack layout at this point. See also PushTryHandler
  // r3, sp ->   next handler
  //             state (ENTRY)
  //             pp 
  //             fp
  //             lr 

  // Discard ENTRY state (r2 is not used), and restore parameter-
  // and frame-pointer and pop state.
  __ ldm(ia_w, sp, r2.bit() | r3.bit() | pp.bit() | fp.bit());
  // Before returning we restore the context from the frame pointer if not NULL.
  // The frame pointer is NULL in the exception handler of a JS entry frame.
  __ cmp(fp, Operand(0));
  // Set cp to NULL if fp is NULL.
  __ mov(cp, Operand(0), LeaveCC, eq);
  // Restore cp otherwise.
  __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset), ne);
  if (kDebug && FLAG_debug_code) __ mov(lr, Operand(pc));
  __ pop(pc);
}


void CEntryStub::GenerateCore(MacroAssembler* masm,
                              Label* throw_normal_exception,
                              Label* throw_out_of_memory_exception,
                              StackFrame::Type frame_type,
                              bool do_gc,
                              bool always_allocate) {
  // r0: result parameter for PerformGC, if any
  // r4: number of arguments including receiver  (C callee-saved)
  // r5: pointer to builtin function  (C callee-saved)
  // r6: pointer to the first argument (C callee-saved)

  if (do_gc) {
    // Passing r0.
    __ Call(FUNCTION_ADDR(Runtime::PerformGC), RelocInfo::RUNTIME_ENTRY);
  }

  ExternalReference scope_depth =
      ExternalReference::heap_always_allocate_scope_depth();
  if (always_allocate) {
    __ mov(r0, Operand(scope_depth));
    __ ldr(r1, MemOperand(r0));
    __ add(r1, r1, Operand(1));
    __ str(r1, MemOperand(r0));
  }

  // Call C built-in.
  // r0 = argc, r1 = argv
  __ mov(r0, Operand(r4));
  __ mov(r1, Operand(r6));

  // TODO(1242173): To let the GC traverse the return address of the exit
  // frames, we need to know where the return address is. Right now,
  // we push it on the stack to be able to find it again, but we never
  // restore from it in case of changes, which makes it impossible to
  // support moving the C entry code stub. This should be fixed, but currently
  // this is OK because the CEntryStub gets generated so early in the V8 boot
  // sequence that it is not moving ever.
  __ add(lr, pc, Operand(4));  // compute return address: (pc + 8) + 4
  __ push(lr);
#if !defined(__arm__)
  // Notify the simulator of the transition to C code.
  __ swi(assembler::arm::call_rt_r5);
#else /* !defined(__arm__) */
  __ mov(pc, Operand(r5));
#endif /* !defined(__arm__) */

  if (always_allocate) {
    // It's okay to clobber r2 and r3 here. Don't mess with r0 and r1
    // though (contain the result).
    __ mov(r2, Operand(scope_depth));
    __ ldr(r3, MemOperand(r2));
    __ sub(r3, r3, Operand(1));
    __ str(r3, MemOperand(r2));
  }

  // check for failure result
  Label failure_returned;
  ASSERT(((kFailureTag + 1) & kFailureTagMask) == 0);
  // Lower 2 bits of r2 are 0 iff r0 has failure tag.
  __ add(r2, r0, Operand(1));
  __ tst(r2, Operand(kFailureTagMask));
  __ b(eq, &failure_returned);

  // Exit C frame and return.
  // r0:r1: result
  // sp: stack pointer
  // fp: frame pointer
  // pp: caller's parameter pointer pp  (restored as C callee-saved)
  __ LeaveExitFrame(frame_type);

  // check if we should retry or throw exception
  Label retry;
  __ bind(&failure_returned);
  ASSERT(Failure::RETRY_AFTER_GC == 0);
  __ tst(r0, Operand(((1 << kFailureTypeTagSize) - 1) << kFailureTagSize));
  __ b(eq, &retry);

  Label continue_exception;
  // If the returned failure is EXCEPTION then promote Top::pending_exception().
  __ cmp(r0, Operand(reinterpret_cast<int32_t>(Failure::Exception())));
  __ b(ne, &continue_exception);

  // Retrieve the pending exception and clear the variable.
  __ mov(ip, Operand(Factory::the_hole_value().location()));
  __ ldr(r3, MemOperand(ip));
  __ mov(ip, Operand(Top::pending_exception_address()));
  __ ldr(r0, MemOperand(ip));
  __ str(r3, MemOperand(ip));

  __ bind(&continue_exception);
  // Special handling of out of memory exception.
  Failure* out_of_memory = Failure::OutOfMemoryException();
  __ cmp(r0, Operand(reinterpret_cast<int32_t>(out_of_memory)));
  __ b(eq, throw_out_of_memory_exception);

  // Handle normal exception.
  __ jmp(throw_normal_exception);

  __ bind(&retry);  // pass last failure (r0) as parameter (r0) when retrying
}


void CEntryStub::GenerateBody(MacroAssembler* masm, bool is_debug_break) {
  // Called from JavaScript; parameters are on stack as if calling JS function
  // r0: number of arguments including receiver
  // r1: pointer to builtin function
  // fp: frame pointer  (restored after C call)
  // sp: stack pointer  (restored as callee's pp after C call)
  // cp: current context  (C callee-saved)
  // pp: caller's parameter pointer pp  (C callee-saved)

  // NOTE: Invocations of builtins may return failure objects
  // instead of a proper result. The builtin entry handles
  // this by performing a garbage collection and retrying the
  // builtin once.

  StackFrame::Type frame_type = is_debug_break
      ? StackFrame::EXIT_DEBUG
      : StackFrame::EXIT;

  // Enter the exit frame that transitions from JavaScript to C++.
  __ EnterExitFrame(frame_type);

  // r4: number of arguments (C callee-saved)
  // r5: pointer to builtin function (C callee-saved)
  // r6: pointer to first argument (C callee-saved)

  Label throw_out_of_memory_exception;
  Label throw_normal_exception;

  // Call into the runtime system. Collect garbage before the call if
  // running with --gc-greedy set.
  if (FLAG_gc_greedy) {
    Failure* failure = Failure::RetryAfterGC(0);
    __ mov(r0, Operand(reinterpret_cast<intptr_t>(failure)));
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
  __ mov(r0, Operand(reinterpret_cast<int32_t>(failure)));
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
  // r0: code entry
  // r1: function
  // r2: receiver
  // r3: argc
  // [sp+0]: argv

  Label invoke, exit;

  // Called from C, so do not pop argc and args on exit (preserve sp)
  // No need to save register-passed args
  // Save callee-saved registers (incl. cp, pp, and fp), sp, and lr
  __ stm(db_w, sp, kCalleeSaved | lr.bit());

  // Get address of argv, see stm above.
  // r0: code entry
  // r1: function
  // r2: receiver
  // r3: argc
  __ add(r4, sp, Operand((kNumCalleeSaved + 1)*kPointerSize));
  __ ldr(r4, MemOperand(r4));  // argv

  // Push a frame with special values setup to mark it as an entry frame.
  // r0: code entry
  // r1: function
  // r2: receiver
  // r3: argc
  // r4: argv
  int marker = is_construct ? StackFrame::ENTRY_CONSTRUCT : StackFrame::ENTRY;
  __ mov(r8, Operand(-1));  // Push a bad frame pointer to fail if it is used.
  __ mov(r7, Operand(~ArgumentsAdaptorFrame::SENTINEL));
  __ mov(r6, Operand(Smi::FromInt(marker)));
  __ mov(r5, Operand(ExternalReference(Top::k_c_entry_fp_address)));
  __ ldr(r5, MemOperand(r5));
  __ stm(db_w, sp, r5.bit() | r6.bit() | r7.bit() | r8.bit());

  // Setup frame pointer for the frame to be pushed.
  __ add(fp, sp, Operand(-EntryFrameConstants::kCallerFPOffset));

  // Call a faked try-block that does the invoke.
  __ bl(&invoke);

  // Caught exception: Store result (exception) in the pending
  // exception field in the JSEnv and return a failure sentinel.
  // Coming in here the fp will be invalid because the PushTryHandler below
  // sets it to 0 to signal the existence of the JSEntry frame.
  __ mov(ip, Operand(Top::pending_exception_address()));
  __ str(r0, MemOperand(ip));
  __ mov(r0, Operand(reinterpret_cast<int32_t>(Failure::Exception())));
  __ b(&exit);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  // Must preserve r0-r4, r5-r7 are available.
  __ PushTryHandler(IN_JS_ENTRY, JS_ENTRY_HANDLER);
  // If an exception not caught by another handler occurs, this handler returns
  // control to the code after the bl(&invoke) above, which restores all
  // kCalleeSaved registers (including cp, pp and fp) to their saved values
  // before returning a failure to C.

  // Clear any pending exceptions.
  __ mov(ip, Operand(ExternalReference::the_hole_value_location()));
  __ ldr(r5, MemOperand(ip));
  __ mov(ip, Operand(Top::pending_exception_address()));
  __ str(r5, MemOperand(ip));

  // Invoke the function by calling through JS entry trampoline builtin.
  // Notice that we cannot store a reference to the trampoline code directly in
  // this stub, because runtime stubs are not traversed when doing GC.

  // Expected registers by Builtins::JSEntryTrampoline
  // r0: code entry
  // r1: function
  // r2: receiver
  // r3: argc
  // r4: argv
  if (is_construct) {
    ExternalReference construct_entry(Builtins::JSConstructEntryTrampoline);
    __ mov(ip, Operand(construct_entry));
  } else {
    ExternalReference entry(Builtins::JSEntryTrampoline);
    __ mov(ip, Operand(entry));
  }
  __ ldr(ip, MemOperand(ip));  // deref address

  // Branch and link to JSEntryTrampoline
  __ mov(lr, Operand(pc));
  __ add(pc, ip, Operand(Code::kHeaderSize - kHeapObjectTag));

  // Unlink this frame from the handler chain. When reading the
  // address of the next handler, there is no need to use the address
  // displacement since the current stack pointer (sp) points directly
  // to the stack handler.
  __ ldr(r3, MemOperand(sp, StackHandlerConstants::kNextOffset));
  __ mov(ip, Operand(ExternalReference(Top::k_handler_address)));
  __ str(r3, MemOperand(ip));
  // No need to restore registers
  __ add(sp, sp, Operand(StackHandlerConstants::kSize));

  __ bind(&exit);  // r0 holds result
  // Restore the top frame descriptors from the stack.
  __ pop(r3);
  __ mov(ip, Operand(ExternalReference(Top::k_c_entry_fp_address)));
  __ str(r3, MemOperand(ip));

  // Reset the stack to the callee saved registers.
  __ add(sp, sp, Operand(-EntryFrameConstants::kCallerFPOffset));

  // Restore callee-saved registers and return.
#ifdef DEBUG
  if (FLAG_debug_code) __ mov(lr, Operand(pc));
#endif
  __ ldm(ia_w, sp, kCalleeSaved | pc.bit());
}


void ArgumentsAccessStub::GenerateReadLength(MacroAssembler* masm) {
  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor;
  __ ldr(r2, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ ldr(r3, MemOperand(r2, StandardFrameConstants::kContextOffset));
  __ cmp(r3, Operand(ArgumentsAdaptorFrame::SENTINEL));
  __ b(eq, &adaptor);

  // Nothing to do: The formal number of parameters has already been
  // passed in register r0 by calling function. Just return it.
  __ mov(pc, lr);

  // Arguments adaptor case: Read the arguments length from the
  // adaptor frame and return it.
  __ bind(&adaptor);
  __ ldr(r0, MemOperand(r2, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ mov(pc, lr);
}


void ArgumentsAccessStub::GenerateReadElement(MacroAssembler* masm) {
  // The displacement is the offset of the last parameter (if any)
  // relative to the frame pointer.
  static const int kDisplacement =
      StandardFrameConstants::kCallerSPOffset - kPointerSize;

  // Check that the key is a smi.
  Label slow;
  __ tst(r1, Operand(kSmiTagMask));
  __ b(ne, &slow);

  // Check if the calling frame is an arguments adaptor frame.
  Label adaptor;
  __ ldr(r2, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ ldr(r3, MemOperand(r2, StandardFrameConstants::kContextOffset));
  __ cmp(r3, Operand(ArgumentsAdaptorFrame::SENTINEL));
  __ b(eq, &adaptor);

  // Check index against formal parameters count limit passed in
  // through register eax. Use unsigned comparison to get negative
  // check for free.
  __ cmp(r1, r0);
  __ b(cs, &slow);

  // Read the argument from the stack and return it.
  __ sub(r3, r0, r1);
  __ add(r3, fp, Operand(r3, LSL, kPointerSizeLog2 - kSmiTagSize));
  __ ldr(r0, MemOperand(r3, kDisplacement));
  __ mov(pc, lr);

  // Arguments adaptor case: Check index against actual arguments
  // limit found in the arguments adaptor frame. Use unsigned
  // comparison to get negative check for free.
  __ bind(&adaptor);
  __ ldr(r0, MemOperand(r2, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ cmp(r1, r0);
  __ b(cs, &slow);

  // Read the argument from the adaptor frame and return it.
  __ sub(r3, r0, r1);
  __ add(r3, r2, Operand(r3, LSL, kPointerSizeLog2 - kSmiTagSize));
  __ ldr(r0, MemOperand(r3, kDisplacement));
  __ mov(pc, lr);

  // Slow-case: Handle non-smi or out-of-bounds access to arguments
  // by calling the runtime system.
  __ bind(&slow);
  __ push(r1);
  __ TailCallRuntime(ExternalReference(Runtime::kGetArgumentsProperty), 1);
}


void ArgumentsAccessStub::GenerateNewObject(MacroAssembler* masm) {
  // Check if the calling frame is an arguments adaptor frame.
  Label runtime;
  __ ldr(r2, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ ldr(r3, MemOperand(r2, StandardFrameConstants::kContextOffset));
  __ cmp(r3, Operand(ArgumentsAdaptorFrame::SENTINEL));
  __ b(ne, &runtime);

  // Patch the arguments.length and the parameters pointer.
  __ ldr(r0, MemOperand(r2, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ str(r0, MemOperand(sp, 0 * kPointerSize));
  __ add(r3, r2, Operand(r0, LSL, kPointerSizeLog2 - kSmiTagSize));
  __ add(r3, r3, Operand(StandardFrameConstants::kCallerSPOffset));
  __ str(r3, MemOperand(sp, 1 * kPointerSize));

  // Do the runtime call to allocate the arguments object.
  __ bind(&runtime);
  __ TailCallRuntime(ExternalReference(Runtime::kNewArgumentsFast), 3);
}


void CallFunctionStub::Generate(MacroAssembler* masm) {
  Label slow;
  // Get the function to call from the stack.
  // function, receiver [, arguments]
  __ ldr(r1, MemOperand(sp, (argc_ + 1) * kPointerSize));

  // Check that the function is really a JavaScript function.
  // r1: pushed function (to be verified)
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &slow);
  // Get the map of the function object.
  __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  __ cmp(r2, Operand(JS_FUNCTION_TYPE));
  __ b(ne, &slow);

  // Fast-case: Invoke the function now.
  // r1: pushed function
  ParameterCount actual(argc_);
  __ InvokeFunction(r1, actual, JUMP_FUNCTION);

  // Slow-case: Non-function called.
  __ bind(&slow);
  __ mov(r0, Operand(argc_));  // Setup the number of arguments.
  __ InvokeBuiltin(Builtins::CALL_NON_FUNCTION, JUMP_JS);
}


#undef __

} }  // namespace v8::internal
