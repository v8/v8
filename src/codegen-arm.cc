// Copyright 2006-2008 Google Inc. All Rights Reserved.
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
#include "prettyprinter.h"
#include "scopeinfo.h"
#include "scopes.h"
#include "runtime.h"

namespace v8 { namespace internal {

DEFINE_bool(optimize_locals, true,
            "optimize locals by allocating them in registers");
DEFINE_bool(trace, false, "trace function calls");
DECLARE_bool(debug_info);
DECLARE_bool(debug_code);
DECLARE_bool(optimize_locals);

#ifdef DEBUG
DECLARE_bool(gc_greedy);
DEFINE_bool(trace_codegen, false,
            "print name of functions for which code is generated");
DEFINE_bool(print_code, false, "print generated code");
DEFINE_bool(print_builtin_code, false, "print generated code for builtins");
DEFINE_bool(print_source, false, "pretty print source code");
DEFINE_bool(print_builtin_source, false,
            "pretty print source code for builtins");
DEFINE_bool(print_ast, false, "print source AST");
DEFINE_bool(print_builtin_ast, false, "print source AST for builtins");
DEFINE_bool(trace_calls, false, "trace calls");
DEFINE_bool(trace_builtin_calls, false, "trace builtins calls");
DEFINE_string(stop_at, "", "function name where to insert a breakpoint");
#endif  // DEBUG


DEFINE_bool(check_stack, true,
            "check stack for overflow, interrupt, breakpoint");


class ArmCodeGenerator;


// -----------------------------------------------------------------------------
// Reference support

// A reference is a C++ stack-allocated object that keeps an ECMA
// reference on the execution stack while in scope. For variables
// the reference is empty, indicating that it isn't necessary to
// store state on the stack for keeping track of references to those.
// For properties, we keep either one (named) or two (indexed) values
// on the execution stack to represent the reference.

class Reference BASE_EMBEDDED {
 public:
  enum Type { ILLEGAL = -1, EMPTY = 0, NAMED = 1, KEYED = 2 };
  Reference(ArmCodeGenerator* cgen, Expression* expression);
  ~Reference();

  Expression* expression() const  { return expression_; }
  Type type() const  { return type_; }
  void set_type(Type value)  {
    ASSERT(type_ == ILLEGAL);
    type_ = value;
  }
  int size() const  { return type_; }

  bool is_illegal() const  { return type_ == ILLEGAL; }

 private:
  ArmCodeGenerator* cgen_;
  Expression* expression_;
  Type type_;
};


// -----------------------------------------------------------------------------
// Code generation state

class CodeGenState BASE_EMBEDDED {
 public:
  enum AccessType {
    UNDEFINED,
    LOAD,
    LOAD_TYPEOF_EXPR,
    STORE,
    INIT_CONST
  };

  CodeGenState()
      : access_(UNDEFINED),
        ref_(NULL),
        true_target_(NULL),
        false_target_(NULL) {
  }

  CodeGenState(AccessType access,
               Reference* ref,
               Label* true_target,
               Label* false_target)
      : access_(access),
        ref_(ref),
        true_target_(true_target),
        false_target_(false_target) {
  }

  AccessType access() const { return access_; }
  Reference* ref() const { return ref_; }
  Label* true_target() const { return true_target_; }
  Label* false_target() const { return false_target_; }

 private:
  AccessType access_;
  Reference* ref_;
  Label* true_target_;
  Label* false_target_;
};


// -----------------------------------------------------------------------------
// ArmCodeGenerator

class ArmCodeGenerator: public CodeGenerator {
 public:
  static Handle<Code> MakeCode(FunctionLiteral* fun,
                               Handle<Script> script,
                               bool is_eval);

  MacroAssembler* masm()  { return masm_; }

 private:
  // Assembler
  MacroAssembler* masm_;  // to generate code

  // Code generation state
  Scope* scope_;
  Condition cc_reg_;
  CodeGenState* state_;
  RegList reg_locals_;  // the list of registers used to hold locals
  int num_reg_locals_;  // the number of registers holding locals
  int break_stack_height_;

  // Labels
  Label function_return_;

  // Construction/destruction
  ArmCodeGenerator(int buffer_size,
                   Handle<Script> script,
                   bool is_eval);

  virtual ~ArmCodeGenerator()  { delete masm_; }

  // Main code generation function
  void GenCode(FunctionLiteral* fun);

  // The following are used by class Reference.
  void LoadReference(Reference* ref);
  void UnloadReference(Reference* ref);
  friend class Reference;

  // State
  bool has_cc() const  { return cc_reg_ != al; }
  CodeGenState::AccessType access() const  { return state_->access(); }
  Reference* ref() const  { return state_->ref(); }
  bool is_referenced() const { return state_->ref() != NULL; }
  Label* true_target() const  { return state_->true_target(); }
  Label* false_target() const  { return state_->false_target(); }


  // Expressions
  MemOperand GlobalObject() const  {
    return ContextOperand(cp, Context::GLOBAL_INDEX);
  }

  MemOperand ContextOperand(Register context, int index) const {
    return MemOperand(context, Context::SlotOffset(index));
  }

  MemOperand ParameterOperand(int index) const {
    // index -2 corresponds to the activated closure, -1 corresponds
    // to the receiver
    ASSERT(-2 <= index && index < scope_->num_parameters());
    int offset = JavaScriptFrameConstants::kParam0Offset - index * kPointerSize;
    return MemOperand(pp, offset);
  }

  MemOperand FunctionOperand() const { return ParameterOperand(-2); }

  Register SlotRegister(int slot_index);
  MemOperand SlotOperand(Slot* slot, Register tmp);

  void LoadCondition(Expression* x, CodeGenState::AccessType access,
                     Label* true_target, Label* false_target, bool force_cc);
  void Load(Expression* x,
            CodeGenState::AccessType access = CodeGenState::LOAD);
  void LoadGlobal();

  // Special code for typeof expressions: Unfortunately, we must
  // be careful when loading the expression in 'typeof'
  // expressions. We are not allowed to throw reference errors for
  // non-existing properties of the global object, so we must make it
  // look like an explicit property access, instead of an access
  // through the context chain.
  void LoadTypeofExpression(Expression* x);

  // References
  void AccessReference(Reference* ref, CodeGenState::AccessType access);

  void GetValue(Reference* ref)  { AccessReference(ref, CodeGenState::LOAD); }
  void SetValue(Reference* ref)  { AccessReference(ref, CodeGenState::STORE); }
  void InitConst(Reference* ref)  {
    AccessReference(ref, CodeGenState::INIT_CONST);
  }

  void ToBoolean(Register reg, Label* true_target, Label* false_target);


  // Access property from the reference (must be at the TOS).
  void AccessReferenceProperty(Expression* key,
                               CodeGenState::AccessType access);

  void GenericOperation(Token::Value op);
  void Comparison(Condition cc, bool strict = false);

  void SmiOperation(Token::Value op, Handle<Object> value, bool reversed);

  void CallWithArguments(ZoneList<Expression*>* arguments, int position);

  // Declare global variables and functions in the given array of
  // name/value pairs.
  virtual void DeclareGlobals(Handle<FixedArray> pairs);

  // Instantiate the function boilerplate.
  void InstantiateBoilerplate(Handle<JSFunction> boilerplate);

  // Control flow
  void Branch(bool if_true, Label* L);
  void CheckStack();
  void CleanStack(int num_bytes);

  // Node visitors
#define DEF_VISIT(type)                         \
  virtual void Visit##type(type* node);
  NODE_LIST(DEF_VISIT)
#undef DEF_VISIT

  void RecordStatementPosition(Node* node);

  // Activation frames
  void EnterJSFrame(int argc, RegList callee_saved);  // preserves r1
  void ExitJSFrame(RegList callee_saved,
                   ExitJSFlag flag = RETURN);  // preserves r0-r2

  virtual void GenerateShiftDownAndTailCall(ZoneList<Expression*>* args);
  virtual void GenerateSetThisFunction(ZoneList<Expression*>* args);
  virtual void GenerateGetThisFunction(ZoneList<Expression*>* args);
  virtual void GenerateSetThis(ZoneList<Expression*>* args);
  virtual void GenerateGetArgumentsLength(ZoneList<Expression*>* args);
  virtual void GenerateSetArgumentsLength(ZoneList<Expression*>* args);
  virtual void GenerateTailCallWithArguments(ZoneList<Expression*>* args);
  virtual void GenerateSetArgument(ZoneList<Expression*>* args);
  virtual void GenerateSquashFrame(ZoneList<Expression*>* args);
  virtual void GenerateExpandFrame(ZoneList<Expression*>* args);
  virtual void GenerateIsSmi(ZoneList<Expression*>* args);
  virtual void GenerateIsArray(ZoneList<Expression*>* args);

  virtual void GenerateArgumentsLength(ZoneList<Expression*>* args);
  virtual void GenerateArgumentsAccess(ZoneList<Expression*>* args);

  virtual void GenerateValueOf(ZoneList<Expression*>* args);
  virtual void GenerateSetValueOf(ZoneList<Expression*>* args);
};


// -----------------------------------------------------------------------------
// ArmCodeGenerator implementation

#define __  masm_->


Handle<Code> ArmCodeGenerator::MakeCode(FunctionLiteral* flit,
                                        Handle<Script> script,
                                        bool is_eval) {
#ifdef DEBUG
  bool print_source = false;
  bool print_ast = false;
  bool print_code = false;
  const char* ftype;

  if (Bootstrapper::IsActive()) {
    print_source = FLAG_print_builtin_source;
    print_ast = FLAG_print_builtin_ast;
    print_code = FLAG_print_builtin_code;
    ftype = "builtin";
  } else {
    print_source = FLAG_print_source;
    print_ast = FLAG_print_ast;
    print_code = FLAG_print_code;
    ftype = "user-defined";
  }

  if (FLAG_trace_codegen || print_source || print_ast) {
    PrintF("*** Generate code for %s function: ", ftype);
    flit->name()->ShortPrint();
    PrintF(" ***\n");
  }

  if (print_source) {
    PrintF("--- Source from AST ---\n%s\n", PrettyPrinter().PrintProgram(flit));
  }

  if (print_ast) {
    PrintF("--- AST ---\n%s\n", AstPrinter().PrintProgram(flit));
  }
#endif  // DEBUG

  // Generate code.
  const int initial_buffer_size = 4 * KB;
  ArmCodeGenerator cgen(initial_buffer_size, script, is_eval);
  cgen.GenCode(flit);
  if (cgen.HasStackOverflow()) {
    Top::StackOverflow();
    return Handle<Code>::null();
  }

  // Process any deferred code.
  cgen.ProcessDeferred();

  // Allocate and install the code.
  CodeDesc desc;
  cgen.masm()->GetCode(&desc);
  ScopeInfo<> sinfo(flit->scope());
  Code::Flags flags = Code::ComputeFlags(Code::FUNCTION);
  Handle<Code> code = Factory::NewCode(desc, &sinfo, flags);

  // Add unresolved entries in the code to the fixup list.
  Bootstrapper::AddFixup(*code, cgen.masm());

#ifdef DEBUG
  if (print_code) {
    // Print the source code if available.
    if (!script->IsUndefined() && !script->source()->IsUndefined()) {
      PrintF("--- Raw source ---\n");
      StringInputBuffer stream(String::cast(script->source()));
      stream.Seek(flit->start_position());
      // flit->end_position() points to the last character in the stream. We
      // need to compensate by adding one to calculate the length.
      int source_len = flit->end_position() - flit->start_position() + 1;
      for (int i = 0; i < source_len; i++) {
        if (stream.has_more()) PrintF("%c", stream.GetNext());
      }
      PrintF("\n\n");
    }
    PrintF("--- Code ---\n");
    code->Print();
  }
#endif  // DEBUG

  return code;
}


ArmCodeGenerator::ArmCodeGenerator(int buffer_size,
                                   Handle<Script> script,
                                   bool is_eval)
    : CodeGenerator(is_eval, script),
      masm_(new MacroAssembler(NULL, buffer_size)),
      scope_(NULL),
      cc_reg_(al),
      state_(NULL),
      break_stack_height_(0) {
}


// Calling conventions:

// r0: always contains top-of-stack (TOS), but in case of a call it's
//     the number of arguments
// fp: frame pointer
// sp: stack pointer
// pp: caller's parameter pointer
// cp: callee's context

void ArmCodeGenerator::GenCode(FunctionLiteral* fun) {
  Scope* scope = fun->scope();
  ZoneList<Statement*>* body = fun->body();

  // Initialize state.
  { CodeGenState state;
    state_ = &state;
    scope_ = scope;
    cc_reg_ = al;
    if (FLAG_optimize_locals) {
      num_reg_locals_ = scope->num_stack_slots() < kNumJSCalleeSaved
          ? scope->num_stack_slots()
          : kNumJSCalleeSaved;
      reg_locals_ = JSCalleeSavedList(num_reg_locals_);
    } else {
      num_reg_locals_ = 0;
      reg_locals_ = 0;
    }

    // Entry
    // stack: function, receiver, arguments, return address
    // r0: number of arguments
    // sp: stack pointer
    // fp: frame pointer
    // pp: caller's parameter pointer
    // cp: callee's context

    { Comment cmnt(masm_, "[ enter JS frame");
      EnterJSFrame(scope->num_parameters(), reg_locals_);
    }
    // tos: code slot
#ifdef DEBUG
    if (strlen(FLAG_stop_at) > 0 &&
        fun->name()->IsEqualTo(CStrVector(FLAG_stop_at))) {
      __ bkpt(0);  // not supported before v5, but illegal instruction works too
    }
#endif

    // Allocate space for locals and initialize them.
    if (scope->num_stack_slots() > num_reg_locals_) {
      Comment cmnt(masm_, "[ allocate space for locals");
      // Pushing the first local materializes the code slot on the stack
      // (formerly stored in tos register r0).
      __ Push(Operand(Factory::undefined_value()));
      // The remaining locals are pushed using the fact that r0 (tos)
      // already contains the undefined value.
      for (int i = scope->num_stack_slots(); i-- > num_reg_locals_ + 1;) {
        __ push(r0);
      }
    }
    // Initialize locals allocated in registers
    if (num_reg_locals_ > 0) {
      if (scope->num_stack_slots() > num_reg_locals_) {
        // r0 contains 'undefined'
        __ mov(SlotRegister(0), Operand(r0));
      } else {
        __ mov(SlotRegister(0), Operand(Factory::undefined_value()));
      }
      for (int i = num_reg_locals_ - 1; i > 0; i--) {
        __ mov(SlotRegister(i), Operand(SlotRegister(0)));
      }
    }

    if (scope->num_heap_slots() > 0) {
      // Allocate local context.
      // Get outer context and create a new context based on it.
      __ Push(FunctionOperand());
      __ CallRuntime(Runtime::kNewContext, 2);
      // Update context local.
      __ str(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
    }

    // TODO(1241774): Improve this code!!!
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
          ASSERT(!scope->is_global_scope());  // no parameters in global scope
          int parameter_offset =
              JavaScriptFrameConstants::kParam0Offset - i * kPointerSize;
          __ ldr(r1, MemOperand(pp, parameter_offset));
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

    // Store the arguments object.
    // This must happen after context initialization because
    // the arguments array may be stored in the context!
    if (scope->arguments() != NULL) {
      ASSERT(scope->arguments_shadow() != NULL);
      Comment cmnt(masm_, "[ allocate arguments object");
      {
        Reference target(this, scope->arguments());
        __ Push(FunctionOperand());
        __ CallRuntime(Runtime::kNewArguments, 1);
        SetValue(&target);
      }
      // The value of arguments must also be stored in .arguments.
      // TODO(1241813): This code can probably be improved by fusing it with
      // the code that stores the arguments object above.
      {
        Reference target(this, scope->arguments_shadow());
        Load(scope->arguments());
        SetValue(&target);
      }
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
    }

    if (FLAG_trace) __ CallRuntime(Runtime::kTraceEnter, 1);
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
      if (should_trace) __ CallRuntime(Runtime::kDebugTrace, 1);
#endif
      VisitStatements(body);
    }

    state_ = NULL;
  }

  // exit
  // r0: result
  // sp: stack pointer
  // fp: frame pointer
  // pp: parameter pointer
  // cp: callee's context
  __ Push(Operand(Factory::undefined_value()));
  __ bind(&function_return_);
  if (FLAG_trace) __ CallRuntime(Runtime::kTraceExit, 1);
  ExitJSFrame(reg_locals_);

  // Code generation state must be reset.
  scope_ = NULL;
  ASSERT(!has_cc());
  ASSERT(state_ == NULL);
}


Register ArmCodeGenerator::SlotRegister(int slot_index) {
  Register reg;
  reg.code_ = JSCalleeSavedCode(slot_index);
  return reg;
}


MemOperand ArmCodeGenerator::SlotOperand(Slot* slot, Register tmp) {
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
      ASSERT(0 <= index &&
             index < scope_->num_stack_slots() &&
             index >= num_reg_locals_);
      int local_offset = JavaScriptFrameConstants::kLocal0Offset -
          (index - num_reg_locals_) * kPointerSize;
      return MemOperand(fp, local_offset);
    }

    case Slot::CONTEXT: {
      // Follow the context chain if necessary.
      ASSERT(!tmp.is(cp));  // do not overwrite context register
      Register context = cp;
      int chain_length = scope_->ContextChainLength(slot->var()->scope());
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
// (partially) translated into branches, or it may have set the condition code
// register. If force_cc is set, the value is forced to set the condition code
// register and no value is pushed. If the condition code register was set,
// has_cc() is true and cc_reg_ contains the condition to test for 'true'.
void ArmCodeGenerator::LoadCondition(Expression* x,
                                     CodeGenState::AccessType access,
                                     Label* true_target,
                                     Label* false_target,
                                     bool force_cc) {
  ASSERT(access == CodeGenState::LOAD ||
         access == CodeGenState::LOAD_TYPEOF_EXPR);
  ASSERT(!has_cc() && !is_referenced());

  CodeGenState* old_state = state_;
  CodeGenState new_state(access, NULL, true_target, false_target);
  state_ = &new_state;
  Visit(x);
  state_ = old_state;
  if (force_cc && !has_cc()) {
    // Pop the TOS from the stack and convert it to a boolean in the
    // condition code register.
    __ mov(r1, Operand(r0));
    __ pop(r0);
    ToBoolean(r1, true_target, false_target);
  }
  ASSERT(has_cc() || !force_cc);
}


void ArmCodeGenerator::Load(Expression* x, CodeGenState::AccessType access) {
  ASSERT(access == CodeGenState::LOAD ||
         access == CodeGenState::LOAD_TYPEOF_EXPR);

  Label true_target;
  Label false_target;
  LoadCondition(x, access, &true_target, &false_target, false);

  if (has_cc()) {
    // convert cc_reg_ into a bool
    Label loaded, materialize_true;
    __ b(cc_reg_, &materialize_true);
    __ Push(Operand(Factory::false_value()));
    __ b(&loaded);
    __ bind(&materialize_true);
    __ Push(Operand(Factory::true_value()));
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
      __ Push(Operand(Factory::true_value()));
    }
    // if both "true" and "false" need to be reincarnated,
    // jump across code for "false"
    if (both)
      __ b(&loaded);
    // reincarnate "false", if necessary
    if (false_target.is_linked()) {
      __ bind(&false_target);
      __ Push(Operand(Factory::false_value()));
    }
    // everything is loaded at this point
    __ bind(&loaded);
  }
  ASSERT(!has_cc());
}


void ArmCodeGenerator::LoadGlobal() {
  __ Push(GlobalObject());
}


// TODO(1241834): Get rid of this function in favor of just using Load, now
// that we have the LOAD_TYPEOF_EXPR access type. => Need to handle
// global variables w/o reference errors elsewhere.
void ArmCodeGenerator::LoadTypeofExpression(Expression* x) {
  Variable* variable = x->AsVariableProxy()->AsVariable();
  if (variable != NULL && !variable->is_this() && variable->is_global()) {
    // NOTE: This is somewhat nasty. We force the compiler to load
    // the variable as if through '<global>.<variable>' to make sure we
    // do not get reference errors.
    Slot global(variable, Slot::CONTEXT, Context::GLOBAL_INDEX);
    Literal key(variable->name());
    // TODO(1241834): Fetch the position from the variable instead of using
    // no position.
    Property property(&global, &key, kNoPosition);
    Load(&property);
  } else {
    Load(x, CodeGenState::LOAD_TYPEOF_EXPR);
  }
}


Reference::Reference(ArmCodeGenerator* cgen, Expression* expression)
    : cgen_(cgen), expression_(expression), type_(ILLEGAL) {
  cgen->LoadReference(this);
}


Reference::~Reference() {
  cgen_->UnloadReference(this);
}


void ArmCodeGenerator::LoadReference(Reference* ref) {
  Expression* e = ref->expression();
  Property* property = e->AsProperty();
  Variable* var = e->AsVariableProxy()->AsVariable();

  if (property != NULL) {
    Load(property->obj());
    // Used a named reference if the key is a literal symbol.
    // We don't use a named reference if they key is a string that can be
    // legally parsed as an integer.  This is because, otherwise we don't
    // get into the slow case code that handles [] on String objects.
    Literal* literal = property->key()->AsLiteral();
    uint32_t dummy;
    if (literal != NULL && literal->handle()->IsSymbol() &&
      !String::cast(*(literal->handle()))->AsArrayIndex(&dummy)) {
      ref->set_type(Reference::NAMED);
    } else {
      Load(property->key());
      ref->set_type(Reference::KEYED);
    }
  } else if (var != NULL) {
    if (var->is_global()) {
      // global variable
      LoadGlobal();
      ref->set_type(Reference::NAMED);
    } else {
      // local variable
      ref->set_type(Reference::EMPTY);
    }
  } else {
    Load(e);
    __ CallRuntime(Runtime::kThrowReferenceError, 1);
  }
}


void ArmCodeGenerator::UnloadReference(Reference* ref) {
  int size = ref->size();
  if (size <= 0) {
    // Do nothing. No popping is necessary.
  } else {
    __ add(sp, sp, Operand(size * kPointerSize));
  }
}


void ArmCodeGenerator::AccessReference(Reference* ref,
                                       CodeGenState::AccessType access) {
  ASSERT(!has_cc());
  ASSERT(ref->type() != Reference::ILLEGAL);
  CodeGenState* old_state = state_;
  CodeGenState new_state(access, ref, true_target(), false_target());
  state_ = &new_state;
  Visit(ref->expression());
  state_ = old_state;
}


// ECMA-262, section 9.2, page 30: ToBoolean(). Convert the given
// register to a boolean in the condition code register. The code
// may jump to 'false_target' in case the register converts to 'false'.
void ArmCodeGenerator::ToBoolean(Register reg,
                                 Label* true_target,
                                 Label* false_target) {
  // Note: The generated code snippet cannot change 'reg'.
  //       Only the condition code should be set.

  // Fast case checks

  // Check if reg is 'false'.
  __ cmp(reg, Operand(Factory::false_value()));
  __ b(eq, false_target);

  // Check if reg is 'true'.
  __ cmp(reg, Operand(Factory::true_value()));
  __ b(eq, true_target);

  // Check if reg is 'undefined'.
  __ cmp(reg, Operand(Factory::undefined_value()));
  __ b(eq, false_target);

  // Check if reg is a smi.
  __ cmp(reg, Operand(Smi::FromInt(0)));
  __ b(eq, false_target);
  __ tst(reg, Operand(kSmiTagMask));
  __ b(eq, true_target);

  // Slow case: call the runtime.
  __ push(r0);
  if (r0.is(reg)) {
    __ CallRuntime(Runtime::kToBool, 1);
  } else {
    __ mov(r0, Operand(reg));
    __ CallRuntime(Runtime::kToBool, 1);
  }
  // Convert result (r0) to condition code
  __ cmp(r0, Operand(Factory::false_value()));
  __ pop(r0);

  cc_reg_ = ne;
}


#undef __
#define __  masm->


class GetPropertyStub : public CodeStub {
 public:
  GetPropertyStub() { }

 private:
  Major MajorKey() { return GetProperty; }
  int MinorKey() { return 0; }
  void Generate(MacroAssembler* masm);

  const char* GetName() { return "GetPropertyStub"; }
};


void GetPropertyStub::Generate(MacroAssembler* masm) {
  Label slow, fast;
  // Get the object from the stack.
  __ ldr(r1, MemOperand(sp, 1 * kPointerSize));  // 1 ~ key
  // Check that the key is a smi.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(ne, &slow);
  __ mov(r0, Operand(r0, ASR, kSmiTagSize));
  // Check that the object isn't a smi.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &slow);
  // Check that the object is some kind of JS object.
  __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  __ cmp(r2, Operand(JS_OBJECT_TYPE));
  __ b(lt, &slow);

  // Check if the object is a value-wrapper object. In that case we
  // enter the runtime system to make sure that indexing into string
  // objects work as intended.
  __ cmp(r2, Operand(JS_VALUE_TYPE));
  __ b(eq, &slow);

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
  __ mov(r0, Operand(1));  // not counting receiver
  __ JumpToBuiltin(ExternalReference(Runtime::kGetProperty));

  // Fast case: Do the load.
  __ bind(&fast);
  __ add(r3, r1, Operand(Array::kHeaderSize - kHeapObjectTag));
  __ ldr(r0, MemOperand(r3, r0, LSL, kPointerSizeLog2));
  __ cmp(r0, Operand(Factory::the_hole_value()));
  // In case the loaded value is the_hole we have to consult GetProperty
  // to ensure the prototype chain is searched.
  __ b(eq, &slow);

  masm->StubReturn(1);
}


class SetPropertyStub : public CodeStub {
 public:
  SetPropertyStub() { }

 private:
  Major MajorKey() { return SetProperty; }
  int MinorKey() { return 0; }
  void Generate(MacroAssembler* masm);

  const char* GetName() { return "GetPropertyStub"; }
};


void SetPropertyStub::Generate(MacroAssembler* masm) {
  Label slow, fast, array, extra, exit;
  // Get the key and the object from the stack.
  __ ldm(ia, sp, r1.bit() | r3.bit());  // r0 == value, r1 == key, r3 == object
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
  __ cmp(r2, Operand(JS_OBJECT_TYPE));
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
  // r0 == value
  __ bind(&slow);
  __ ldm(ia, sp, r1.bit() | r3.bit());  // r0 == value, r1 == key, r3 == object
  __ stm(db_w, sp, r0.bit() | r1.bit() | r3.bit());
  // Do tail-call to runtime routine.
  __ mov(r0, Operand(2));  // not counting receiver
  __ JumpToBuiltin(ExternalReference(Runtime::kSetProperty));


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
  masm->StubReturn(1);
}


void GenericOpStub::Generate(MacroAssembler* masm) {
  switch (op_) {
    case Token::ADD: {
      Label slow, exit;
      // fast path
      // Get x (y is on TOS, i.e., r0).
      __ ldr(r1, MemOperand(sp, 0 * kPointerSize));
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
      __ push(r0);
      __ mov(r0, Operand(1));  // set number of arguments
      __ InvokeBuiltin("ADD", 1, JUMP_JS);
      // done
      __ bind(&exit);
      break;
    }

    case Token::SUB: {
      Label slow, exit;
      // fast path
      __ ldr(r1, MemOperand(sp, 0 * kPointerSize));  // get x
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
      __ push(r0);
      __ mov(r0, Operand(1));  // set number of arguments
      __ InvokeBuiltin("SUB", 1, JUMP_JS);
      // done
      __ bind(&exit);
      break;
    }

    case Token::MUL: {
      Label slow, exit;
      __ ldr(r1, MemOperand(sp, 0 * kPointerSize));  // get x
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
      __ push(r0);
      __ mov(r0, Operand(1));  // set number of arguments
      __ InvokeBuiltin("MUL", 1, JUMP_JS);
      // done
      __ bind(&exit);
      break;
    }
    default: UNREACHABLE();
  }
  masm->StubReturn(2);
}


class SmiOpStub : public CodeStub {
 public:
  SmiOpStub(Token::Value op, bool reversed)
      : op_(op), reversed_(reversed) {}

 private:
  Token::Value op_;
  bool reversed_;

  Major MajorKey() { return SmiOp; }
  int MinorKey() {
    return (op_ == Token::ADD ? 2 : 0) | (reversed_ ? 1 : 0);
  }
  void Generate(MacroAssembler* masm);
  void GenerateShared(MacroAssembler* masm);

  const char* GetName() { return "SmiOpStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("SmiOpStub (token %s), (reversed %s)\n",
           Token::String(op_), reversed_ ? "true" : "false");
  }
#endif
};


void SmiOpStub::Generate(MacroAssembler* masm) {
  switch (op_) {
    case Token::ADD: {
      if (!reversed_) {
        __ sub(r0, r0, Operand(r1));  // revert optimistic add
        __ push(r0);
        __ push(r1);
        __ mov(r0, Operand(1));  // set number of arguments
        __ InvokeBuiltin("ADD", 1, JUMP_JS);
      } else {
        __ sub(r0, r0, Operand(r1));  // revert optimistic add
        __ push(r1);  // reversed
        __ push(r0);
        __ mov(r0, Operand(1));  // set number of arguments
        __ InvokeBuiltin("ADD", 1, JUMP_JS);
      }
      break;
    }
    case Token::SUB: {
      if (!reversed_) {
        __ push(r0);
        __ push(r1);
        __ mov(r0, Operand(1));  // set number of arguments
        __ InvokeBuiltin("SUB", 1, JUMP_JS);
      } else {
        __ push(r1);
        __ push(r0);
        __ mov(r0, Operand(1));  // set number of arguments
        __ InvokeBuiltin("SUB", 1, JUMP_JS);
      }
      break;
    }
    default: UNREACHABLE();
  }
}

void StackCheckStub::Generate(MacroAssembler* masm) {
  Label within_limit;
  __ mov(ip, Operand(ExternalReference::address_of_stack_guard_limit()));
  __ ldr(ip, MemOperand(ip));
  __ cmp(sp, Operand(ip));
  __ b(hs, &within_limit);
  // Do tail-call to runtime routine.
  __ push(r0);
  __ mov(r0, Operand(0));  // not counting receiver (i.e. flushed TOS)
  __ JumpToBuiltin(ExternalReference(Runtime::kStackGuard));
  __ bind(&within_limit);

  masm->StubReturn(1);
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
  __ InvokeBuiltin("UNARY_MINUS", 0, JUMP_JS);

  __ bind(&done);
  masm->StubReturn(1);
}


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

  const char* GetName() { return "InvokeBuiltinStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("InvokeBuiltinStub (kind %d, argc, %d)\n",
           static_cast<int>(kind_),
           argc_);
  }
#endif
};


void InvokeBuiltinStub::Generate(MacroAssembler* masm) {
  __ push(r0);
  __ mov(r0, Operand(0));  // set number of arguments
  switch (kind_) {
    case ToNumber: __ InvokeBuiltin("TO_NUMBER", 0, JUMP_JS); break;
    case Inc:      __ InvokeBuiltin("INC", 0, JUMP_JS);       break;
    case Dec:      __ InvokeBuiltin("DEC", 0, JUMP_JS);       break;
    default: UNREACHABLE();
  }
  masm->StubReturn(argc_);
}


class JSExitStub : public CodeStub {
 public:
  enum Kind { Inc, Dec, ToNumber };

  JSExitStub(int num_callee_saved, RegList callee_saved, ExitJSFlag flag)
      : num_callee_saved_(num_callee_saved),
        callee_saved_(callee_saved),
        flag_(flag) { }

 private:
  int num_callee_saved_;
  RegList callee_saved_;
  ExitJSFlag flag_;

  Major MajorKey() { return JSExit; }
  int MinorKey() { return (num_callee_saved_ << 3) | static_cast<int>(flag_); }
  void Generate(MacroAssembler* masm);

  const char* GetName() { return "JSExitStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("JSExitStub (num_callee_saved %d, flag %d)\n",
           num_callee_saved_,
           static_cast<int>(flag_));
  }
#endif
};


void JSExitStub::Generate(MacroAssembler* masm) {
  __ ExitJSFrame(flag_, callee_saved_);
  masm->StubReturn(1);
}



void CEntryStub::GenerateThrowTOS(MacroAssembler* masm) {
  // r0 holds exception
  ASSERT(StackHandlerConstants::kSize == 6 * kPointerSize);  // adjust this code
  if (FLAG_optimize_locals) {
    // Locals are allocated in callee-saved registers, so we need to restore
    // saved callee-saved registers by unwinding the stack
    static JSCalleeSavedBuffer regs;
    intptr_t arg0 = reinterpret_cast<intptr_t>(&regs);
    __ push(r0);
    __ mov(r0, Operand(arg0));  // exception in r0 (TOS) is pushed, r0 == arg0
    // Do not push a second C entry frame, but call directly
    __ Call(FUNCTION_ADDR(StackFrameIterator::RestoreCalleeSavedForTopHandler),
            runtime_entry);  // passing r0
    // Frame::RestoreJSCalleeSaved returns arg0 (TOS)
    __ mov(r1, Operand(r0));
    __ pop(r0);  // r1 holds arg0, r0 holds exception
    __ ldm(ia, r1, kJSCalleeSaved);  // restore callee-saved registers
  }
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

  // Set pending exception and TOS to out of memory exception.
  Failure* out_of_memory = Failure::OutOfMemoryException();
  __ mov(r0, Operand(reinterpret_cast<int32_t>(out_of_memory)));
  __ mov(r2, Operand(ExternalReference(Top::k_pending_exception_address)));
  __ str(r0, MemOperand(r2));

  // Restore the stack to the address of the ENTRY handler
  __ mov(sp, Operand(r3));

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


void CEntryStub::GenerateCore(MacroAssembler* masm,
                              Label* throw_normal_exception,
                              Label* throw_out_of_memory_exception,
                              bool do_gc,
                              bool do_restore) {
  // r0: result parameter for PerformGC, if any
  // r4: number of arguments  (C callee-saved)
  // r5: pointer to builtin function  (C callee-saved)

  if (do_gc) {
    __ Call(FUNCTION_ADDR(Runtime::PerformGC), runtime_entry);  // passing r0
  }

  // call C built-in
  __ mov(r0, Operand(r4));  // a0 = argc
  __ add(r1, fp, Operand(r4, LSL, kPointerSizeLog2));
  __ add(r1, r1, Operand(ExitFrameConstants::kPPDisplacement));  // a1 = argv

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
  // result is in r0 or r0:r1 - do not destroy these registers!

  // check for failure result
  Label failure_returned;
  ASSERT(((kFailureTag + 1) & kFailureTagMask) == 0);
  // Lower 2 bits of r2 are 0 iff r0 has failure tag.
  __ add(r2, r0, Operand(1));
  __ tst(r2, Operand(kFailureTagMask));
  __ b(eq, &failure_returned);

  // clear top frame
  __ mov(r3, Operand(0));
  __ mov(ip, Operand(ExternalReference(Top::k_c_entry_fp_address)));
  __ str(r3, MemOperand(ip));

  // Restore the memory copy of the registers by digging them out from
  // the stack.
  if (do_restore) {
    // Ok to clobber r2 and r3.
    const int kCallerSavedSize = kNumJSCallerSaved * kPointerSize;
    const int kOffset = ExitFrameConstants::kDebugMarkOffset - kCallerSavedSize;
    __ add(r3, fp, Operand(kOffset));
    __ CopyRegistersFromStackToMemory(r3, r2, kJSCallerSaved);
  }

  // Exit C frame and return
  // r0:r1: result
  // sp: stack pointer
  // fp: frame pointer
  // pp: caller's parameter pointer pp  (restored as C callee-saved)

  // Restore current context from top and clear it in debug mode.
  __ mov(r3, Operand(Top::context_address()));
  __ ldr(cp, MemOperand(r3));
  __ mov(sp, Operand(fp));  // respect ABI stack constraint
  __ ldm(ia, sp, kJSCalleeSaved | pp.bit() | fp.bit() | sp.bit() | pc.bit());

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
  // r0: number of arguments
  // r1: pointer to builtin function
  // fp: frame pointer  (restored after C call)
  // sp: stack pointer  (restored as callee's pp after C call)
  // cp: current context  (C callee-saved)
  // pp: caller's parameter pointer pp  (C callee-saved)

  // NOTE: Invocations of builtins may return failure objects
  // instead of a proper result. The builtin entry handles
  // this by performing a garbage collection and retrying the
  // builtin once.

  // Enter C frame
  // Compute parameter pointer before making changes and save it as ip register
  // so that it is restored as sp register on exit, thereby popping the args.
  // ip = sp + kPointerSize*(args_len+1);  // +1 for receiver
  __ add(ip, sp, Operand(r0, LSL, kPointerSizeLog2));
  __ add(ip, ip, Operand(kPointerSize));

  // all JS callee-saved are saved and traversed by GC; push in reverse order:
  // JS callee-saved, caller_pp, caller_fp, sp_on_exit (ip==pp), caller_pc
  __ stm(db_w, sp, kJSCalleeSaved | pp.bit() | fp.bit() | ip.bit() | lr.bit());
  __ mov(fp, Operand(sp));  // setup new frame pointer

  // Store the current context in top.
  __ mov(ip, Operand(Top::context_address()));
  __ str(cp, MemOperand(ip));

  // remember top frame
  __ mov(ip, Operand(ExternalReference(Top::k_c_entry_fp_address)));
  __ str(fp, MemOperand(ip));

  // Push debug marker.
  __ mov(ip, Operand(is_debug_break ? 1 : 0));
  __ push(ip);

  if (is_debug_break) {
    // Save the state of all registers to the stack from the memory location.
    // Use sp as base to push.
    __ CopyRegistersFromMemoryToStack(sp, kJSCallerSaved);
  }

  // move number of arguments (argc) into callee-saved register
  __ mov(r4, Operand(r0));

  // move pointer to builtin function into callee-saved register
  __ mov(r5, Operand(r1));

  // r0: result parameter for PerformGC, if any (setup below)
  // r4: number of arguments
  // r5: pointer to builtin function  (C callee-saved)

  Label entry;
  __ bind(&entry);

  Label throw_out_of_memory_exception;
  Label throw_normal_exception;

#ifdef DEBUG
  if (FLAG_gc_greedy) {
    Failure* failure = Failure::RetryAfterGC(0, NEW_SPACE);
    __ mov(r0, Operand(reinterpret_cast<intptr_t>(failure)));
  }
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               FLAG_gc_greedy,
               is_debug_break);
#else
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               false,
               is_debug_break);
#endif
  GenerateCore(masm,
               &throw_normal_exception,
               &throw_out_of_memory_exception,
               true,
               is_debug_break);

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
  __ mov(ip, Operand(sp));
  __ stm(db_w, sp, kCalleeSaved | ip.bit() | lr.bit());

  // Setup frame pointer
  __ mov(fp, Operand(sp));

  // Add constructor mark.
  __ mov(ip, Operand(is_construct ? 1 : 0));
  __ push(ip);

  // Move arguments into registers expected by Builtins::JSEntryTrampoline
  // preserve r0-r3, set r4, r5-r7 may be clobbered

  // Get address of argv, see stm above.
  __ add(r4, sp, Operand((kNumCalleeSaved + 3)*kPointerSize));
  __ ldr(r4, MemOperand(r4));  // argv

  // Save copies of the top frame descriptors on the stack.
  __ mov(ip, Operand(ExternalReference(Top::k_c_entry_fp_address)));
  __ ldr(r6, MemOperand(ip));
  __ stm(db_w, sp, r6.bit());

  // Call a faked try-block that does the invoke.
  __ bl(&invoke);

  // Caught exception: Store result (exception) in the pending
  // exception field in the JSEnv and return a failure sentinel.
  __ mov(ip, Operand(Top::pending_exception_address()));
  __ str(r0, MemOperand(ip));
  __ mov(r0, Operand(Handle<Failure>(Failure::Exception())));
  __ b(&exit);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  // Must preserve r0-r3, r5-r7 are available.
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
  __ ldm(ia_w, sp, r3.bit());
  __ mov(ip, Operand(ExternalReference(Top::k_c_entry_fp_address)));
  __ str(r3, MemOperand(ip));

  // Remove constructor mark.
  __ add(sp, sp, Operand(kPointerSize));

  // Restore callee-saved registers, sp, and return.
#ifdef DEBUG
  if (FLAG_debug_code) __ mov(lr, Operand(pc));
#endif
  __ ldm(ia, sp, kCalleeSaved | sp.bit() | pc.bit());
}


class ArgumentsAccessStub: public CodeStub {
 public:
  explicit ArgumentsAccessStub(bool is_length) : is_length_(is_length) { }

 private:
  bool is_length_;

  Major MajorKey() { return ArgumentsAccess; }
  int MinorKey() { return is_length_ ? 1 : 0; }
  void Generate(MacroAssembler* masm);

  const char* GetName() { return "ArgumentsAccessStub"; }

#ifdef DEBUG
  void Print() {
    PrintF("ArgumentsAccessStub (is_length %s)\n",
           is_length_ ? "true" : "false");
  }
#endif
};


void ArgumentsAccessStub::Generate(MacroAssembler* masm) {
  if (is_length_) {
    __ ldr(r0, MemOperand(fp, JavaScriptFrameConstants::kArgsLengthOffset));
    __ mov(r0, Operand(r0, LSL, kSmiTagSize));
    __ Ret();
  } else {
    // Check that the key is a smi.
    Label slow;
    __ tst(r0, Operand(kSmiTagMask));
    __ b(ne, &slow);

    // Get the actual number of arguments passed and do bounds
    // check. Use unsigned comparison to get negative check for free.
    __ ldr(r1, MemOperand(fp, JavaScriptFrameConstants::kArgsLengthOffset));
    __ cmp(r0, Operand(r1, LSL, kSmiTagSize));
    __ b(hs, &slow);

    // Load the argument directly from the stack and return.
    __ sub(r1, pp, Operand(r0, LSL, kPointerSizeLog2 - kSmiTagSize));
    __ ldr(r0, MemOperand(r1, JavaScriptFrameConstants::kParam0Offset));
    __ Ret();

    // Slow-case: Handle non-smi or out-of-bounds access to arguments
    // by calling the runtime system.
    __ bind(&slow);
    __ push(r0);
    __ mov(r0, Operand(0));  // not counting receiver
    __ JumpToBuiltin(ExternalReference(Runtime::kGetArgumentsProperty));
  }
}


#undef __
#define __  masm_->


void ArmCodeGenerator::AccessReferenceProperty(
    Expression* key,
    CodeGenState::AccessType access) {
  Reference::Type type = ref()->type();
  ASSERT(type != Reference::ILLEGAL);

  // TODO(1241834): Make sure that this is sufficient. If there is a chance
  // that reference errors can be thrown below, we must distinguish
  // between the 2 kinds of loads (typeof expression loads must not
  // throw a reference errror).
  bool is_load = (access == CodeGenState::LOAD ||
                  access == CodeGenState::LOAD_TYPEOF_EXPR);

  if (type == Reference::NAMED) {
    // Compute the name of the property.
    Literal* literal = key->AsLiteral();
    Handle<String> name(String::cast(*literal->handle()));

    // Loading adds a value to the stack; push the TOS to prepare.
    if (is_load) __ push(r0);

    // Setup the name register.
    __ mov(r2, Operand(name));

    // Call the appropriate IC code.
    if (is_load) {
      Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
      Variable* var = ref()->expression()->AsVariableProxy()->AsVariable();
      if (var != NULL) {
        ASSERT(var->is_global());
        __ Call(ic, code_target_context);
      } else {
        __ Call(ic, code_target);
      }
    } else {
      Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
      __ Call(ic, code_target);
    }
    return;
  }

  // Access keyed property.
  ASSERT(type == Reference::KEYED);

  if (is_load) {
    __ push(r0);  // empty tos
    // TODO(1224671): Implement inline caching for keyed loads as on ia32.
    GetPropertyStub stub;
    __ CallStub(&stub);
  } else {
    SetPropertyStub stub;
    __ CallStub(&stub);
  }
}


void ArmCodeGenerator::GenericOperation(Token::Value op) {
  // Stub is entered with a call: 'return address' is in lr.
  switch (op) {
    case Token::ADD:  // fall through.
    case Token::SUB:  // fall through.
    case Token::MUL: {
      GenericOpStub stub(op);
      __ CallStub(&stub);
      break;
    }

    case Token::DIV: {
      __ push(r0);
      __ mov(r0, Operand(1));  // set number of arguments
      __ InvokeBuiltin("DIV", 1, CALL_JS);
      break;
    }

    case Token::MOD: {
      __ push(r0);
      __ mov(r0, Operand(1));  // set number of arguments
      __ InvokeBuiltin("MOD", 1, CALL_JS);
      break;
    }

    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR: {
      Label slow, exit;
      __ pop(r1);  // get x
      // tag check
      __ orr(r2, r1, Operand(r0));  // r2 = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ tst(r2, Operand(kSmiTagMask));
      __ b(ne, &slow);
      switch (op) {
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
      switch (op) {
        case Token::BIT_OR:  __ InvokeBuiltin("BIT_OR",  1, CALL_JS); break;
        case Token::BIT_AND: __ InvokeBuiltin("BIT_AND", 1, CALL_JS); break;
        case Token::BIT_XOR: __ InvokeBuiltin("BIT_XOR", 1, CALL_JS); break;
        default: UNREACHABLE();
      }
      __ bind(&exit);
      break;
    }

    case Token::SHL:
    case Token::SHR:
    case Token::SAR: {
      Label slow, exit;
      __ mov(r1, Operand(r0));  // get y
      __ pop(r0);  // get x
      // tag check
      __ orr(r2, r1, Operand(r0));  // r2 = x | y;
      ASSERT(kSmiTag == 0);  // adjust code below
      __ tst(r2, Operand(kSmiTagMask));
      __ b(ne, &slow);
       // get copies of operands
      __ mov(r3, Operand(r0));
      __ mov(r2, Operand(r1));
      // remove tags from operands (but keep sign)
      __ mov(r3, Operand(r3, ASR, kSmiTagSize));
      __ mov(r2, Operand(r2, ASR, kSmiTagSize));
      // use only the 5 least significant bits of the shift count
      __ and_(r2, r2, Operand(0x1f));
      // perform operation
      switch (op) {
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
      // tag result and store it in TOS (r0)
      ASSERT(kSmiTag == 0);  // adjust code below
      __ mov(r0, Operand(r3, LSL, kSmiTagSize));
      __ b(&exit);
      // slow case
      __ bind(&slow);
      __ push(r0);  // restore stack
      __ mov(r0, Operand(r1));
      __ Push(Operand(1));  // 1 argument (not counting receiver).
      switch (op) {
        case Token::SAR: __ InvokeBuiltin("SAR", 1, CALL_JS); break;
        case Token::SHR: __ InvokeBuiltin("SHR", 1, CALL_JS); break;
        case Token::SHL: __ InvokeBuiltin("SHL", 1, CALL_JS); break;
        default: UNREACHABLE();
      }
      __ bind(&exit);
      break;
    }

    case Token::COMMA:
      // simply discard left value
      __ add(sp, sp, Operand(kPointerSize));
      break;

    default:
      // Other cases should have been handled before this point.
      UNREACHABLE();
      break;
  }
}




void ArmCodeGenerator::SmiOperation(Token::Value op,
                                    Handle<Object> value,
                                    bool reversed) {
  // NOTE: This is an attempt to inline (a bit) more of the code for
  // some possible smi operations (like + and -) when (at least) one
  // of the operands is a literal smi. With this optimization, the
  // performance of the system is increased by ~15%, and the generated
  // code size is increased by ~1% (measured on a combination of
  // different benchmarks).

  ASSERT(value->IsSmi());

  Label exit;

  switch (op) {
    case Token::ADD: {
      Label slow;

      __ mov(r1, Operand(value));
      __ add(r0, r0, Operand(r1), SetCC);
      __ b(vs, &slow);
      __ tst(r0, Operand(kSmiTagMask));
      __ b(eq, &exit);
      __ bind(&slow);

      SmiOpStub stub(Token::ADD, reversed);
      __ CallStub(&stub);
      break;
    }

    case Token::SUB: {
      Label slow;

      __ mov(r1, Operand(value));
      if (!reversed) {
        __ sub(r2, r0, Operand(r1), SetCC);
      } else {
        __ rsb(r2, r0, Operand(r1), SetCC);
      }
      __ b(vs, &slow);
      __ tst(r2, Operand(kSmiTagMask));
      __ mov(r0, Operand(r2), LeaveCC, eq);  // conditionally set r0 to result
      __ b(eq, &exit);

      __ bind(&slow);

      SmiOpStub stub(Token::SUB, reversed);
      __ CallStub(&stub);
      break;
    }

    default:
      if (!reversed) {
        __ Push(Operand(value));
      } else {
        __ mov(ip, Operand(value));
        __ push(ip);
      }
      GenericOperation(op);
      break;
  }

  __ bind(&exit);
}


void ArmCodeGenerator::Comparison(Condition cc, bool strict) {
  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == eq);

  Label exit, smi;
  __ pop(r1);
  __ orr(r2, r0, Operand(r1));
  __ tst(r2, Operand(kSmiTagMask));
  __ b(eq, &smi);

  // Perform non-smi comparison by runtime call.
  __ push(r1);

  // Figure out which native to call and setup the arguments.
  const char* native;
  int argc;
  if (cc == eq) {
    native = strict ? "STRICT_EQUALS" : "EQUALS";
    argc = 1;
  } else {
    native = "COMPARE";
    int ncr;  // NaN compare result
    if (cc == lt || cc == le) {
      ncr = GREATER;
    } else {
      ASSERT(cc == gt || cc == ge);  // remaining cases
      ncr = LESS;
    }
    __ Push(Operand(Smi::FromInt(ncr)));
    argc = 2;
  }

  // Call the native; it returns -1 (less), 0 (equal), or 1 (greater)
  // tagged as a small integer.
  __ Push(Operand(argc));
  __ InvokeBuiltin(native, argc, CALL_JS);
  __ cmp(r0, Operand(0));
  __ b(&exit);

  // test smi equality by pointer comparison.
  __ bind(&smi);
  __ cmp(r1, Operand(r0));

  __ bind(&exit);
  __ pop(r0);  // be careful not to destroy the cc register
  cc_reg_ = cc;
}


// Call the function just below TOS on the stack with the given
// arguments. The receiver is the TOS.
void ArmCodeGenerator::CallWithArguments(ZoneList<Expression*>* args,
                                         int position) {
  Label fast, slow, exit;

  // Push the arguments ("left-to-right") on the stack.
  for (int i = 0; i < args->length(); i++) Load(args->at(i));

  // Push the number of arguments.
  __ Push(Operand(args->length()));

  // Get the function to call from the stack.
  // +1 ~ receiver.
  __ ldr(r1, MemOperand(sp, (args->length() + 1) * kPointerSize));

  // Check that the function really is a JavaScript function.
  __ tst(r1, Operand(kSmiTagMask));
  __ b(eq, &slow);
  __ ldr(r2, FieldMemOperand(r1, HeapObject::kMapOffset));  // get the map
  __ ldrb(r2, FieldMemOperand(r2, Map::kInstanceTypeOffset));
  __ cmp(r2, Operand(JS_FUNCTION_TYPE));
  __ b(eq, &fast);

  __ RecordPosition(position);

  // Slow-case: Non-function called.
  __ bind(&slow);
  __ InvokeBuiltin("CALL_NON_FUNCTION", 0, CALL_JS);
  __ b(&exit);

  // Fast-case: Get the code from the function, call the first
  // instruction in it, and pop function.
  __ bind(&fast);
  __ ldr(cp, FieldMemOperand(r1, JSFunction::kContextOffset));
  __ ldr(r1, FieldMemOperand(r1, JSFunction::kSharedFunctionInfoOffset));
  __ ldr(r1, MemOperand(r1, SharedFunctionInfo::kCodeOffset - kHeapObjectTag));
  __ add(r1, r1, Operand(Code::kHeaderSize - kHeapObjectTag));
  __ Call(r1);

  // Restore context and pop function from the stack.
  __ bind(&exit);
  __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ add(sp, sp, Operand(kPointerSize));  // discard
}


void ArmCodeGenerator::Branch(bool if_true, Label* L) {
  ASSERT(has_cc());
  Condition cc = if_true ? cc_reg_ : NegateCondition(cc_reg_);
  __ b(cc, L);
  cc_reg_ = al;
}


void ArmCodeGenerator::CheckStack() {
  if (FLAG_check_stack) {
    Comment cmnt(masm_, "[ check stack");
    StackCheckStub stub;
    __ CallStub(&stub);
  }
}


void ArmCodeGenerator::VisitBlock(Block* node) {
  Comment cmnt(masm_, "[ Block");
  if (FLAG_debug_info) RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);
  VisitStatements(node->statements());
  __ bind(node->break_target());
}


void ArmCodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  __ Push(Operand(pairs));
  __ Push(Operand(cp));
  __ Push(Operand(Smi::FromInt(is_eval() ? 1 : 0)));
  __ CallRuntime(Runtime::kDeclareGlobals, 3);

  // Get rid of return value.
  __ pop(r0);
}


void ArmCodeGenerator::VisitDeclaration(Declaration* node) {
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
    __ Push(Operand(cp));
    __ Push(Operand(var->name()));
    // Declaration nodes are always declared in only two modes.
    ASSERT(node->mode() == Variable::VAR || node->mode() == Variable::CONST);
    PropertyAttributes attr = node->mode() == Variable::VAR ? NONE : READ_ONLY;
    __ Push(Operand(Smi::FromInt(attr)));
    // Push initial value, if any.
    // Note: For variables we must not push an initial value (such as
    // 'undefined') because we may have a (legal) redeclaration and we
    // must not destroy the current value.
    if (node->mode() == Variable::CONST) {
      __ Push(Operand(Factory::the_hole_value()));
    } else if (node->fun() != NULL) {
      Load(node->fun());
    } else {
      __ Push(Operand(0));  // no initial value!
    }
    __ CallRuntime(Runtime::kDeclareContextSlot, 5);
    // DeclareContextSlot pops the assigned value by accepting an
    // extra argument and returning the TOS; no need to explicitly pop
    // here.
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
    Load(val);
    SetValue(&target);
    // Get rid of the assigned value (declarations are statements).
    __ pop(r0);  // Pop(no_reg);
  }
}


void ArmCodeGenerator::VisitExpressionStatement(ExpressionStatement* node) {
  Comment cmnt(masm_, "[ ExpressionStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Expression* expression = node->expression();
  expression->MarkAsStatement();
  Load(expression);
  __ pop(r0);  // __ Pop(no_reg)
}


void ArmCodeGenerator::VisitEmptyStatement(EmptyStatement* node) {
  Comment cmnt(masm_, "// EmptyStatement");
  // nothing to do
}


void ArmCodeGenerator::VisitIfStatement(IfStatement* node) {
  Comment cmnt(masm_, "[ IfStatement");
  // Generate different code depending on which
  // parts of the if statement are present or not.
  bool has_then_stm = node->HasThenStatement();
  bool has_else_stm = node->HasElseStatement();

  if (FLAG_debug_info) RecordStatementPosition(node);

  Label exit;
  if (has_then_stm && has_else_stm) {
    Label then;
    Label else_;
    // if (cond)
    LoadCondition(node->condition(), CodeGenState::LOAD, &then, &else_, true);
    Branch(false, &else_);
    // then
    __ bind(&then);
    Visit(node->then_statement());
    __ b(&exit);
    // else
    __ bind(&else_);
    Visit(node->else_statement());

  } else if (has_then_stm) {
    ASSERT(!has_else_stm);
    Label then;
    // if (cond)
    LoadCondition(node->condition(), CodeGenState::LOAD, &then, &exit, true);
    Branch(false, &exit);
    // then
    __ bind(&then);
    Visit(node->then_statement());

  } else if (has_else_stm) {
    ASSERT(!has_then_stm);
    Label else_;
    // if (!cond)
    LoadCondition(node->condition(), CodeGenState::LOAD, &exit, &else_, true);
    Branch(true, &exit);
    // else
    __ bind(&else_);
    Visit(node->else_statement());

  } else {
    ASSERT(!has_then_stm && !has_else_stm);
    // if (cond)
    LoadCondition(node->condition(), CodeGenState::LOAD, &exit, &exit, false);
    if (has_cc()) {
      cc_reg_ = al;
    } else {
      __ pop(r0);  // __ Pop(no_reg)
    }
  }

  // end
  __ bind(&exit);
}


void ArmCodeGenerator::CleanStack(int num_bytes) {
  ASSERT(num_bytes >= 0);
  if (num_bytes > 0) {
    __ add(sp, sp, Operand(num_bytes - kPointerSize));
    __ pop(r0);
  }
}


void ArmCodeGenerator::VisitContinueStatement(ContinueStatement* node) {
  Comment cmnt(masm_, "[ ContinueStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  __ b(node->target()->continue_target());
}


void ArmCodeGenerator::VisitBreakStatement(BreakStatement* node) {
  Comment cmnt(masm_, "[ BreakStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  CleanStack(break_stack_height_ - node->target()->break_stack_height());
  __ b(node->target()->break_target());
}


void ArmCodeGenerator::VisitReturnStatement(ReturnStatement* node) {
  Comment cmnt(masm_, "[ ReturnStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Load(node->expression());
  __ b(&function_return_);
}


void ArmCodeGenerator::VisitWithEnterStatement(WithEnterStatement* node) {
  Comment cmnt(masm_, "[ WithEnterStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  Load(node->expression());
  __ CallRuntime(Runtime::kPushContext, 2);
  // Update context local.
  __ str(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void ArmCodeGenerator::VisitWithExitStatement(WithExitStatement* node) {
  Comment cmnt(masm_, "[ WithExitStatement");
  // Pop context.
  __ ldr(cp, ContextOperand(cp, Context::PREVIOUS_INDEX));
  // Update context local.
  __ str(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void ArmCodeGenerator::VisitSwitchStatement(SwitchStatement* node) {
  Comment cmnt(masm_, "[ SwitchStatement");
  if (FLAG_debug_info) RecordStatementPosition(node);
  node->set_break_stack_height(break_stack_height_);

  Load(node->tag());

  Label next, fall_through, default_case;
  ZoneList<CaseClause*>* cases = node->cases();
  int length = cases->length();

  for (int i = 0; i < length; i++) {
    CaseClause* clause = cases->at(i);

    Comment cmnt(masm_, "[ case clause");

    if (clause->is_default()) {
      // Bind the default case label, so we can branch to it when we
      // have compared against all other cases.
      ASSERT(default_case.is_unused());  // at most one default clause

      // If the default case is the first (but not only) case, we have
      // to jump past it for now. Once we're done with the remaining
      // clauses, we'll branch back here. If it isn't the first case,
      // we jump past it by avoiding to chain it into the next chain.
      if (length > 1) {
        if (i == 0) __ b(&next);
        __ bind(&default_case);
      }

    } else {
      __ bind(&next);
      next.Unuse();
      __ push(r0);  // duplicate TOS
      Load(clause->label());
      Comparison(eq, true);
      Branch(false, &next);
      __ pop(r0);  // __ Pop(no_reg)
    }

    // Generate code for the body.
    __ bind(&fall_through);
    fall_through.Unuse();
    VisitStatements(clause->statements());
    __ b(&fall_through);
  }

  __ bind(&next);
  __ pop(r0);  // __ Pop(no_reg)
  if (default_case.is_bound()) __ b(&default_case);

  __ bind(&fall_through);
  __ bind(node->break_target());
}


void ArmCodeGenerator::VisitLoopStatement(LoopStatement* node) {
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
                    CodeGenState::LOAD,
                    &loop,
                    node->break_target(),
                    true);
      Branch(true, &loop);
      break;
  }

  // exit
  __ bind(node->break_target());
}


void ArmCodeGenerator::VisitForInStatement(ForInStatement* node) {
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
  __ cmp(r1, Operand(JS_OBJECT_TYPE));
  __ b(hs, &jsobject);

  __ bind(&primitive);
  __ Push(Operand(0));
  __ InvokeBuiltin("TO_OBJECT", 0, CALL_JS);


  __ bind(&jsobject);

  // Get the set of properties (as a FixedArray or Map).
  __ push(r0);  // duplicate the object being enumerated
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

  __ Push(Operand(r2));
  __ Push(FieldMemOperand(r2, FixedArray::kLengthOffset));
  __ mov(r0, Operand(r0, LSL, kSmiTagSize));
  __ Push(Operand(Smi::FromInt(0)));
  __ b(&entry);


  __ bind(&fixed_array);

  __ mov(r1, Operand(Smi::FromInt(0)));
  __ push(r1);  // insert 0 in place of Map

  // Push the length of the array and the initial index onto the stack.
  __ Push(FieldMemOperand(r0, FixedArray::kLengthOffset));
  __ mov(r0, Operand(r0, LSL, kSmiTagSize));
  __ Push(Operand(Smi::FromInt(0)));
  __ b(&entry);

  // Body.
  __ bind(&loop);
  Visit(node->body());

  // Next.
  __ bind(node->continue_target());
  __ bind(&next);
  __ add(r0, r0, Operand(Smi::FromInt(1)));

  // Condition.
  __ bind(&entry);

  __ ldr(ip, MemOperand(sp, 0));
  __ cmp(r0, Operand(ip));
  __ b(hs, &cleanup);

  // Get the i'th entry of the array.
  __ ldr(r2, MemOperand(sp, kPointerSize));
  __ add(r2, r2, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
  __ ldr(r3, MemOperand(r2, r0, LSL, kPointerSizeLog2 - kSmiTagSize));

  // Get Map or 0.
  __ ldr(r2, MemOperand(sp, 2 * kPointerSize));
  // Check if this (still) matches the map of the enumerable.
  // If not, we have to filter the key.
  __ ldr(r1, MemOperand(sp, 3 * kPointerSize));
  __ ldr(r1, FieldMemOperand(r1, HeapObject::kMapOffset));
  __ cmp(r1, Operand(r2));
  __ b(eq, &end_del_check);

  // Convert the entry to a string (or null if it isn't a property anymore).
  __ Push(MemOperand(sp, 4 * kPointerSize));  // push enumerable
  __ Push(Operand(r3));  // push entry
  __ Push(Operand(1));
  __ InvokeBuiltin("FILTER_KEY", 1, CALL_JS);
  __ mov(r3, Operand(r0));
  __ pop(r0);

  // If the property has been removed while iterating, we just skip it.
  __ cmp(r3, Operand(Factory::null_value()));
  __ b(eq, &next);


  __ bind(&end_del_check);

  // Store the entry in the 'each' expression and take another spin in the loop.
  __ Push(Operand(r3));
  { Reference each(this, node->each());
    if (!each.is_illegal()) {
      if (each.size() > 0) __ Push(MemOperand(sp, kPointerSize * each.size()));
      SetValue(&each);
      if (each.size() > 0) __ pop(r0);
    }
  }
  __ pop(r0);
  CheckStack();  // TODO(1222600): ignore if body contains calls.
  __ jmp(&loop);

  // Cleanup.
  __ bind(&cleanup);
  __ bind(node->break_target());
  __ add(sp, sp, Operand(4 * kPointerSize));

  // Exit.
  __ bind(&exit);
  __ pop(r0);

  break_stack_height_ -= kForInStackSize;
}


void ArmCodeGenerator::VisitTryCatch(TryCatch* node) {
  Comment cmnt(masm_, "[ TryCatch");

  Label try_block, exit;

  __ push(r0);
  __ bl(&try_block);


  // --- Catch block ---

  // Store the caught exception in the catch variable.
  { Reference ref(this, node->catch_var());
    // Load the exception to the top of the stack.
    __ Push(MemOperand(sp, ref.size() * kPointerSize));
    SetValue(&ref);
  }

  // Remove the exception from the stack.
  __ add(sp, sp, Operand(kPointerSize));

  // Restore TOS register caching.
  __ pop(r0);

  VisitStatements(node->catch_block()->statements());
  __ b(&exit);


  // --- Try block ---
  __ bind(&try_block);

  __ PushTryHandler(IN_JAVASCRIPT, TRY_CATCH_HANDLER);

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
  VisitStatements(node->try_block()->statements());

  // Stop the introduced shadowing and count the number of required unlinks.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }

  // Unlink from try chain.
  // TOS contains code slot
  const int kNextOffset = StackHandlerConstants::kNextOffset +
      StackHandlerConstants::kAddressDisplacement;
  __ ldr(r1, MemOperand(sp, kNextOffset));  // read next_sp
  __ mov(r3, Operand(ExternalReference(Top::k_handler_address)));
  __ str(r1, MemOperand(r3));
  ASSERT(StackHandlerConstants::kCodeOffset == 0);  // first field is code
  __ add(sp, sp, Operand(StackHandlerConstants::kSize - kPointerSize));
  // Code slot popped.
  __ pop(r0);  // restore TOS
  if (nof_unlinks > 0) __ b(&exit);

  // Generate unlink code for all used shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      // Unlink from try chain; be careful not to destroy the TOS.
      __ bind(shadows[i]);

      bool is_return = (shadows[i]->shadowed() == &function_return_);
      if (!is_return) {
        // Break/continue case. TOS is the code slot of the handler.
        __ push(r0);  // flush TOS
      }

      // Reload sp from the top handler, because some statements that we
      // break from (eg, for...in) may have left stuff on the stack.
      __ mov(r3, Operand(ExternalReference(Top::k_handler_address)));
      __ ldr(sp, MemOperand(r3));

      __ ldr(r1, MemOperand(sp, kNextOffset));
      __ str(r1, MemOperand(r3));
      ASSERT(StackHandlerConstants::kCodeOffset == 0);  // first field is code
      __ add(sp, sp, Operand(StackHandlerConstants::kSize - kPointerSize));
      // Code slot popped.

      if (!is_return) {
        __ pop(r0);  // restore TOS
      }

      __ b(shadows[i]->shadowed());
    }
  }

  __ bind(&exit);
}


void ArmCodeGenerator::VisitTryFinally(TryFinally* node) {
  Comment cmnt(masm_, "[ TryFinally");

  // State: Used to keep track of reason for entering the finally
  // block. Should probably be extended to hold information for
  // break/continue from within the try block.
  enum { FALLING, THROWING, JUMPING };

  Label exit, unlink, try_block, finally_block;

  __ push(r0);
  __ bl(&try_block);

  // In case of thrown exceptions, this is where we continue.
  __ mov(r2, Operand(Smi::FromInt(THROWING)));
  __ b(&finally_block);


  // --- Try block ---
  __ bind(&try_block);

  __ PushTryHandler(IN_JAVASCRIPT, TRY_FINALLY_HANDLER);

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
  VisitStatements(node->try_block()->statements());

  // Stop the introduced shadowing and count the number of required
  // unlinks.
  int nof_unlinks = 0;
  for (int i = 0; i <= nof_escapes; i++) {
    shadows[i]->StopShadowing();
    if (shadows[i]->is_linked()) nof_unlinks++;
  }

  // Set the state on the stack to FALLING.
  __ Push(Operand(Factory::undefined_value()));  // fake TOS
  __ mov(r2, Operand(Smi::FromInt(FALLING)));
  if (nof_unlinks > 0) __ b(&unlink);

  // Generate code that sets the state for all used shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_linked()) {
      __ bind(shadows[i]);
      if (shadows[i]->shadowed() != &function_return_) {
        // Fake TOS for break and continue (not return).
        __ Push(Operand(Factory::undefined_value()));
      }
      __ mov(r2, Operand(Smi::FromInt(JUMPING + i)));
      __ b(&unlink);
    }
  }

  // Unlink from try chain; be careful not to destroy the TOS.
  __ bind(&unlink);

  // Reload sp from the top handler, because some statements that we
  // break from (eg, for...in) may have left stuff on the stack.
  __ mov(r3, Operand(ExternalReference(Top::k_handler_address)));
  __ ldr(sp, MemOperand(r3));
  const int kNextOffset = StackHandlerConstants::kNextOffset +
      StackHandlerConstants::kAddressDisplacement;
  __ ldr(r1, MemOperand(sp, kNextOffset));
  __ str(r1, MemOperand(r3));
  ASSERT(StackHandlerConstants::kCodeOffset == 0);  // first field is code
  __ add(sp, sp, Operand(StackHandlerConstants::kSize - kPointerSize));
  // Code slot popped.


  // --- Finally block ---
  __ bind(&finally_block);

  // Push the state on the stack. If necessary move the state to a
  // local variable to avoid having extra values on the stack while
  // evaluating the finally block.
  __ Push(Operand(r2));
  if (node->finally_var() != NULL) {
    Reference target(this, node->finally_var());
    SetValue(&target);
    ASSERT(target.size() == 0);  // no extra stuff on the stack
    __ pop(r0);
  }

  // Generate code for the statements in the finally block.
  VisitStatements(node->finally_block()->statements());

  // Get the state from the stack - or the local variable - and
  // restore the TOS register.
  if (node->finally_var() != NULL) {
    Reference target(this, node->finally_var());
    GetValue(&target);
  }
  __ Pop(r2);

  // Generate code that jumps to the right destination for all used
  // shadow labels.
  for (int i = 0; i <= nof_escapes; i++) {
    if (shadows[i]->is_bound()) {
      __ cmp(r2, Operand(Smi::FromInt(JUMPING + i)));
      if (shadows[i]->shadowed() != &function_return_) {
        Label next;
        __ b(ne, &next);
        __ pop(r0);  // pop faked TOS
        __ b(shadows[i]->shadowed());
        __ bind(&next);
      } else {
        __ b(eq, shadows[i]->shadowed());
      }
    }
  }

  // Check if we need to rethrow the exception.
  __ cmp(r2, Operand(Smi::FromInt(THROWING)));
  __ b(ne, &exit);

  // Rethrow exception.
  __ CallRuntime(Runtime::kReThrow, 1);

  // Done.
  __ bind(&exit);
  __ pop(r0);  // restore TOS caching.
}


void ArmCodeGenerator::VisitDebuggerStatement(DebuggerStatement* node) {
  Comment cmnt(masm_, "[ DebuggerStatament");
  if (FLAG_debug_info) RecordStatementPosition(node);
  __ CallRuntime(Runtime::kDebugBreak, 1);
}


void ArmCodeGenerator::InstantiateBoilerplate(Handle<JSFunction> boilerplate) {
  ASSERT(boilerplate->IsBoilerplate());

  // Push the boilerplate on the stack.
  __ Push(Operand(boilerplate));

  // Create a new closure.
  __ Push(Operand(cp));
  __ CallRuntime(Runtime::kNewClosure, 2);
}


void ArmCodeGenerator::VisitFunctionLiteral(FunctionLiteral* node) {
  Comment cmnt(masm_, "[ FunctionLiteral");

  // Build the function boilerplate and instantiate it.
  Handle<JSFunction> boilerplate = BuildBoilerplate(node);
  InstantiateBoilerplate(boilerplate);
}


void ArmCodeGenerator::VisitFunctionBoilerplateLiteral(
    FunctionBoilerplateLiteral* node) {
  Comment cmnt(masm_, "[ FunctionBoilerplateLiteral");
  InstantiateBoilerplate(node->boilerplate());
}


void ArmCodeGenerator::VisitConditional(Conditional* node) {
  Comment cmnt(masm_, "[ Conditional");
  Label then, else_, exit;
  LoadCondition(node->condition(), CodeGenState::LOAD, &then, &else_, true);
  Branch(false, &else_);
  __ bind(&then);
  Load(node->then_expression(), access());
  __ b(&exit);
  __ bind(&else_);
  Load(node->else_expression(), access());
  __ bind(&exit);
}


void ArmCodeGenerator::VisitSlot(Slot* node) {
  Comment cmnt(masm_, "[ Slot");

  if (node->type() == Slot::LOOKUP) {
    ASSERT(node->var()->mode() == Variable::DYNAMIC);

    // For now, just do a runtime call.
    __ Push(Operand(cp));
    __ Push(Operand(node->var()->name()));

    switch (access()) {
      case CodeGenState::UNDEFINED:
        UNREACHABLE();
        break;

      case CodeGenState::LOAD:
        __ CallRuntime(Runtime::kLoadContextSlot, 2);
        // result (TOS) is the value that was loaded
        break;

      case CodeGenState::LOAD_TYPEOF_EXPR:
        __ CallRuntime(Runtime::kLoadContextSlotNoReferenceError, 2);
        // result (TOS) is the value that was loaded
        break;

      case CodeGenState::STORE:
        // Storing a variable must keep the (new) value on the stack. This
        // is necessary for compiling assignment expressions.
        __ CallRuntime(Runtime::kStoreContextSlot, 3);
        // result (TOS) is the value that was stored
        break;

      case CodeGenState::INIT_CONST:
        // Same as STORE but ignores attribute (e.g. READ_ONLY) of
        // context slot so that we can initialize const properties
        // (introduced via eval("const foo = (some expr);")). Also,
        // uses the current function context instead of the top
        // context.
        //
        // Note that we must declare the foo upon entry of eval(),
        // via a context slot declaration, but we cannot initialize
        // it at the same time, because the const declaration may
        // be at the end of the eval code (sigh...) and the const
        // variable may have been used before (where its value is
        // 'undefined'). Thus, we can only do the initialization
        // when we actually encounter the expression and when the
        // expression operands are defined and valid, and thus we
        // need the split into 2 operations: declaration of the
        // context slot followed by initialization.
        __ CallRuntime(Runtime::kInitializeConstContextSlot, 3);
        break;
    }

  } else {
    // Note: We would like to keep the assert below, but it fires because
    // of some nasty code in LoadTypeofExpression() which should be removed...
    // ASSERT(node->var()->mode() != Variable::DYNAMIC);

    switch (access()) {
      case CodeGenState::UNDEFINED:
        UNREACHABLE();
        break;

      case CodeGenState::LOAD:  // fall through
      case CodeGenState::LOAD_TYPEOF_EXPR:
        // Special handling for locals allocated in registers.
        if (FLAG_optimize_locals && node->type() == Slot::LOCAL &&
            node->index() < num_reg_locals_) {
          __ Push(Operand(SlotRegister(node->index())));
        } else {
          __ Push(SlotOperand(node, r2));
        }
        if (node->var()->mode() == Variable::CONST) {
          // Const slots may contain 'the hole' value (the constant hasn't
          // been initialized yet) which needs to be converted into the
          // 'undefined' value.
          Comment cmnt(masm_, "[ Unhole const");
          __ cmp(r0, Operand(Factory::the_hole_value()));
          __ mov(r0, Operand(Factory::undefined_value()), LeaveCC, eq);
        }
        break;

      case CodeGenState::INIT_CONST: {
        ASSERT(node->var()->mode() == Variable::CONST);
        // Only the first const initialization must be executed (the slot
        // still contains 'the hole' value). When the assignment is executed,
        // the code is identical to a normal store (see below).
        { Comment cmnt(masm_, "[ Init const");
          Label L;
          if (FLAG_optimize_locals && node->type() == Slot::LOCAL &&
              node->index() < num_reg_locals_) {
            __ mov(r2, Operand(SlotRegister(node->index())));
          } else {
            __ ldr(r2, SlotOperand(node, r2));
          }
          __ cmp(r2, Operand(Factory::the_hole_value()));
          __ b(ne, &L);
          // We must execute the store.
          if (FLAG_optimize_locals && node->type() == Slot::LOCAL &&
              node->index() < num_reg_locals_) {
            __ mov(SlotRegister(node->index()), Operand(r0));
          } else {
            // r2 may be loaded with context; used below in RecordWrite.
            __ str(r0, SlotOperand(node, r2));
          }
          if (node->type() == Slot::CONTEXT) {
            // Skip write barrier if the written value is a smi.
            Label exit;
            __ tst(r0, Operand(kSmiTagMask));
            __ b(eq, &exit);
            // r2 is loaded with context when calling SlotOperand above.
            int offset = FixedArray::kHeaderSize + node->index() * kPointerSize;
            __ mov(r3, Operand(offset));
            __ RecordWrite(r2, r3, r1);
            __ bind(&exit);
          }
          __ bind(&L);
        }
        break;
      }

      case CodeGenState::STORE: {
        // Storing a variable must keep the (new) value on the stack. This
        // is necessary for compiling assignment expressions.
        // Special handling for locals allocated in registers.
        //
        // Note: We will reach here even with node->var()->mode() ==
        // Variable::CONST because of const declarations which will
        // initialize consts to 'the hole' value and by doing so, end
        // up calling this code.
        if (FLAG_optimize_locals && node->type() == Slot::LOCAL &&
            node->index() < num_reg_locals_) {
          __ mov(SlotRegister(node->index()), Operand(r0));
        } else {
          // r2 may be loaded with context; used below in RecordWrite.
          __ str(r0, SlotOperand(node, r2));
        }
        if (node->type() == Slot::CONTEXT) {
          // Skip write barrier if the written value is a smi.
          Label exit;
          __ tst(r0, Operand(kSmiTagMask));
          __ b(eq, &exit);
          // r2 is loaded with context when calling SlotOperand above.
          int offset = FixedArray::kHeaderSize + node->index() * kPointerSize;
          __ mov(r3, Operand(offset));
          __ RecordWrite(r2, r3, r1);
          __ bind(&exit);
        }
        break;
      }
    }
  }
}


void ArmCodeGenerator::VisitVariableProxy(VariableProxy* proxy_node) {
  Comment cmnt(masm_, "[ VariableProxy");
  Variable* node = proxy_node->var();

  Expression* x = node->rewrite();
  if (x != NULL) {
    Visit(x);
    return;
  }

  ASSERT(node->is_global());
  if (is_referenced()) {
    if (node->AsProperty() != NULL) {
      __ RecordPosition(node->AsProperty()->position());
    }
    AccessReferenceProperty(new Literal(node->name()), access());

  } else {
    // All stores are through references.
    ASSERT(access() != CodeGenState::STORE);
    Reference property(this, proxy_node);
    GetValue(&property);
  }
}


void ArmCodeGenerator::VisitLiteral(Literal* node) {
  Comment cmnt(masm_, "[ Literal");
  __ Push(Operand(node->handle()));
}


void ArmCodeGenerator::VisitRegExpLiteral(RegExpLiteral* node) {
  Comment cmnt(masm_, "[ RexExp Literal");

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ ldr(r1, MemOperand(pp, 0));

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
  __ Push(Operand(r1));                                   // literal array  (0)
  __ Push(Operand(Smi::FromInt(node->literal_index())));  // literal index  (1)
  __ Push(Operand(node->pattern()));                      // RegExp pattern (2)
  __ Push(Operand(node->flags()));                        // RegExp flags   (3)
  __ CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  __ Pop(r2);
  __ bind(&done);

  // Push the literal.
  __ Push(Operand(r2));
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
  __ Push(Operand(r1));
  // Literal index (1).
  __ Push(Operand(Smi::FromInt(node_->literal_index())));
  // Constant properties (2).
  __ Push(Operand(node_->constant_properties()));
  __ CallRuntime(Runtime::kCreateObjectLiteralBoilerplate, 3);
  __ Pop(r2);
}


void ArmCodeGenerator::VisitObjectLiteral(ObjectLiteral* node) {
  Comment cmnt(masm_, "[ ObjectLiteral");

  ObjectLiteralDeferred* deferred = new ObjectLiteralDeferred(this, node);

  // Retrieve the literal array and check the allocated entry.

  // Load the function of this activation.
  __ ldr(r1, MemOperand(pp, 0));

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
  __ Push(Operand(r2));
  // Clone the boilerplate object.
  __ CallRuntime(Runtime::kCloneObjectLiteralBoilerplate, 1);

  for (int i = 0; i < node->properties()->length(); i++) {
    ObjectLiteral::Property* property = node->properties()->at(i);
    Literal* key = property->key();
    Expression* value = property->value();
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT: break;
      case ObjectLiteral::Property::COMPUTED:  // fall through
      case ObjectLiteral::Property::PROTOTYPE: {
        // Save a copy of the resulting object on the stack.
        __ push(r0);
        Load(key);
        Load(value);
        __ CallRuntime(Runtime::kSetProperty, 3);
        // Restore the result object from the stack.
        __ pop(r0);
        break;
      }
      case ObjectLiteral::Property::SETTER: {
        __ push(r0);
        Load(key);
        __ Push(Operand(Smi::FromInt(1)));
        Load(value);
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        __ pop(r0);
        break;
      }
      case ObjectLiteral::Property::GETTER: {
        __ push(r0);
        Load(key);
        __ Push(Operand(Smi::FromInt(0)));
        Load(value);
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        __ pop(r0);
        break;
      }
    }
  }
}


void ArmCodeGenerator::VisitArrayLiteral(ArrayLiteral* node) {
  Comment cmnt(masm_, "[ ArrayLiteral");
  // Load the resulting object.
  Load(node->result());
  for (int i = 0; i < node->values()->length(); i++) {
    Expression* value = node->values()->at(i);

    // If value is literal the property value is already
    // set in the boilerplate object.
    if (value->AsLiteral() == NULL) {
      // The property must be set by generated code.
      Load(value);

      // Fetch the object literal
      __ ldr(r1, MemOperand(sp, 0));
        // Get the elements array.
      __ ldr(r1, FieldMemOperand(r1, JSObject::kElementsOffset));

      // Write to the indexed properties array.
      int offset = i * kPointerSize + Array::kHeaderSize;
      __ str(r0, FieldMemOperand(r1, offset));

      // Update the write barrier for the array address.
      __ mov(r3, Operand(offset));
      __ RecordWrite(r1, r3, r2);

      __ pop(r0);
    }
  }
}


void ArmCodeGenerator::VisitAssignment(Assignment* node) {
  Comment cmnt(masm_, "[ Assignment");

  if (FLAG_debug_info) RecordStatementPosition(node);
  Reference target(this, node->target());
  if (target.is_illegal()) return;

  if (node->op() == Token::ASSIGN ||
      node->op() == Token::INIT_VAR ||
      node->op() == Token::INIT_CONST) {
    Load(node->value());

  } else {
    GetValue(&target);
    Literal* literal = node->value()->AsLiteral();
    if (literal != NULL && literal->handle()->IsSmi()) {
      SmiOperation(node->binary_op(), literal->handle(), false);
    } else {
      Load(node->value());
      GenericOperation(node->binary_op());
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
      InitConst(&target);
    } else {
      SetValue(&target);
    }
  }
}


void ArmCodeGenerator::VisitThrow(Throw* node) {
  Comment cmnt(masm_, "[ Throw");

  Load(node->exception());
  __ RecordPosition(node->position());
  __ CallRuntime(Runtime::kThrow, 1);
}


void ArmCodeGenerator::VisitProperty(Property* node) {
  Comment cmnt(masm_, "[ Property");
  if (is_referenced()) {
    __ RecordPosition(node->position());
    AccessReferenceProperty(node->key(), access());
  } else {
    // All stores are through references.
    ASSERT(access() != CodeGenState::STORE);
    Reference property(this, node);
    __ RecordPosition(node->position());
    GetValue(&property);
  }
}


void ArmCodeGenerator::VisitCall(Call* node) {
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
    __ Push(Operand(var->name()));
    LoadGlobal();

    // Load the arguments.
    for (int i = 0; i < args->length(); i++) Load(args->at(i));
    __ Push(Operand(args->length()));

    // Setup the receiver register and call the IC initialization code.
    Handle<Code> stub = ComputeCallInitialize(args->length());
    __ ldr(r1, GlobalObject());
    __ RecordPosition(node->position());
    __ Call(stub, code_target_context);
    __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));

    // Remove the function from the stack.
    __ add(sp, sp, Operand(kPointerSize));

  } else if (var != NULL && var->slot() != NULL &&
             var->slot()->type() == Slot::LOOKUP) {
    // ----------------------------------
    // JavaScript example: 'with (obj) foo(1, 2, 3)'  // foo is in obj
    // ----------------------------------

    // Load the function
    __ Push(Operand(cp));
    __ Push(Operand(var->name()));
    __ CallRuntime(Runtime::kLoadContextSlot, 2);
    // r0: slot value; r1: receiver

    // Load the receiver.
    __ push(r0);
    __ mov(r0, Operand(r1));

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
      __ Push(Operand(literal->handle()));
      Load(property->obj());

      // Load the arguments.
      for (int i = 0; i < args->length(); i++) Load(args->at(i));
      __ Push(Operand(args->length()));

      // Set the receiver register and call the IC initialization code.
      Handle<Code> stub = ComputeCallInitialize(args->length());
      __ ldr(r1, MemOperand(sp, args->length() * kPointerSize));
      __ RecordPosition(node->position());
      __ Call(stub, code_target);
      __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));

      // Remove the function from the stack.
      __ add(sp, sp, Operand(kPointerSize));

    } else {
      // -------------------------------------------
      // JavaScript example: 'array[index](1, 2, 3)'
      // -------------------------------------------

      // Load the function to call from the property through a reference.
      Reference ref(this, property);
      GetValue(&ref);

      // Pass receiver to called function.
      __ Push(MemOperand(sp, ref.size() * kPointerSize));

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


void ArmCodeGenerator::VisitCallNew(CallNew* node) {
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

  // Push the number of arguments.
  __ Push(Operand(args->length()));

  // Call the construct call builtin that handles allocation and
  // constructor invocation.
  __ RecordPosition(position);
  __ Call(Handle<Code>(Builtins::builtin(Builtins::JSConstructCall)),
          js_construct_call);
  __ add(sp, sp, Operand(kPointerSize));  // discard
}


void ArmCodeGenerator::GenerateSetThisFunction(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  __ str(r0, MemOperand(pp, JavaScriptFrameConstants::kFunctionOffset));
}


void ArmCodeGenerator::GenerateGetThisFunction(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 0);
  __ Push(MemOperand(pp, JavaScriptFrameConstants::kFunctionOffset));
}


void ArmCodeGenerator::GenerateSetThis(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  __ str(r0, MemOperand(pp, JavaScriptFrameConstants::kReceiverOffset));
}


void ArmCodeGenerator::GenerateSetArgumentsLength(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  __ mov(r0, Operand(r0, LSR, kSmiTagSize));
  __ str(r0, MemOperand(fp, JavaScriptFrameConstants::kArgsLengthOffset));
  __ mov(r0, Operand(Smi::FromInt(0)));
}


void ArmCodeGenerator::GenerateGetArgumentsLength(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  __ push(r0);
  __ ldr(r0, MemOperand(fp, JavaScriptFrameConstants::kArgsLengthOffset));
  __ mov(r0, Operand(r0, LSL, kSmiTagSize));
}


void ArmCodeGenerator::GenerateValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Label leave;
  Load(args->at(0));
  // r0 contains object.
  // if (object->IsSmi()) return TOS.
  __ tst(r0, Operand(kSmiTagMask));
  __ b(eq, &leave);
  // It is a heap object - get map.
  __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
  __ ldrb(r1, FieldMemOperand(r1, Map::kInstanceTypeOffset));
  // if (!object->IsJSValue()) return TOS.
  __ cmp(r1, Operand(JS_VALUE_TYPE));
  __ b(ne, &leave);
  // Load the value.
  __ ldr(r0, FieldMemOperand(r0, JSValue::kValueOffset));
  __ bind(&leave);
}


void ArmCodeGenerator::GenerateSetValueOf(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);
  Label leave;
  Load(args->at(0));  // Load the object.
  Load(args->at(1));  // Load the value.
  __ pop(r1);
  // r0 contains value.
  // r1 contains object.
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
}


void ArmCodeGenerator::GenerateTailCallWithArguments(
    ZoneList<Expression*>* args) {
  // r0 = number of arguments (smi)
  ASSERT(args->length() == 1);
  Load(args->at(0));
  __ mov(r0, Operand(r0, LSR, kSmiTagSize));

  // r1 = new function (previously written to stack)
  __ ldr(r1, MemOperand(pp, JavaScriptFrameConstants::kFunctionOffset));

  // Reset parameter pointer and frame pointer to previous frame
  ExitJSFrame(reg_locals_, DO_NOT_RETURN);

  // Jump (tail-call) to the function in register r1.
  __ ldr(cp, FieldMemOperand(r1, JSFunction::kContextOffset));
  __ ldr(r1, FieldMemOperand(r1, JSFunction::kSharedFunctionInfoOffset));
  __ ldr(r1, FieldMemOperand(r1, SharedFunctionInfo::kCodeOffset));
  __ add(pc, r1, Operand(Code::kHeaderSize - kHeapObjectTag));
  return;
}


void ArmCodeGenerator::GenerateSetArgument(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 3);
  // r1 = args[i]
  Comment cmnt(masm_, "[ GenerateSetArgument");
  Load(args->at(1));
  __ mov(r1, Operand(r0));
  // r0 = i
  Load(args->at(0));
#if defined(DEBUG)
  { Label L;
    __ tst(r0, Operand(kSmiTagMask));
    __ b(eq, &L);
    __ stop("SMI expected");
    __ bind(&L);
  }
#endif  // defined(DEBUG)
  __ add(r2, pp, Operand(JavaScriptFrameConstants::kParam0Offset));
  __ str(r1,
         MemOperand(r2, r0, LSL, kPointerSizeLog2 - kSmiTagSize, NegOffset));
  __ pop(r0);
}


void ArmCodeGenerator::GenerateSquashFrame(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);
  // Load r1 with old number of arguments, r0 with new number, r1 > r0.
  Load(args->at(0));
  __ mov(r1, Operand(r0, LSR, kSmiTagSize));
  Load(args->at(1));
  __ mov(r0, Operand(r0, LSR, kSmiTagSize));
  // r1 = number of words to move stack.
  __ sub(r1, r1, Operand(r0));
  // r2 is source.
  __ add(r2, fp, Operand(StandardFrameConstants::kCallerPCOffset));
  // Move down frame pointer fp.
  __ add(fp, fp, Operand(r1, LSL, kPointerSizeLog2));
  // r1 is destination.
  __ add(r1, fp, Operand(StandardFrameConstants::kCallerPCOffset));

  Label move;
  __ bind(&move);
  __ ldr(r3, MemOperand(r2, -kPointerSize, PostIndex));
  __ str(r3, MemOperand(r1, -kPointerSize, PostIndex));
  __ cmp(r2, Operand(sp));
  __ b(ne, &move);
  __ ldr(r3, MemOperand(r2));
  __ str(r3, MemOperand(r1));

  // Move down stack pointer esp.
  __ mov(sp, Operand(r1));
  // Balance stack and put something GC-able in r0.
  __ pop(r0);
}


void ArmCodeGenerator::GenerateExpandFrame(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 2);
  // Load r1 with new number of arguments, r0 with old number (as Smi), r1 > r0.
  Load(args->at(1));
  __ mov(r1, Operand(r0, LSR, kSmiTagSize));
  Load(args->at(0));
  // r1 = number of words to move stack.
  __ sub(r1, r1, Operand(r0, LSR, kSmiTagSize));
  Label end_of_expand_frame;
  if (FLAG_check_stack) {
    Label not_too_big;
    __ sub(r2, sp, Operand(r1, LSL, kPointerSizeLog2));
    __ mov(ip, Operand(ExternalReference::address_of_stack_guard_limit()));
    __ ldr(ip, MemOperand(ip));
    __ cmp(r2, Operand(ip));
    __ b(gt, &not_too_big);
    __ pop(r0);
    __ mov(r0, Operand(Factory::false_value()));
    __ b(&end_of_expand_frame);
    __ bind(&not_too_big);
  }
  // r3 is source.
  __ mov(r3, Operand(sp));
  // r0 is copy limit + 1 word
  __ add(r0, fp,
         Operand(StandardFrameConstants::kCallerPCOffset + kPointerSize));
  // Move up frame pointer fp.
  __ sub(fp, fp, Operand(r1, LSL, kPointerSizeLog2));
  // Move up stack pointer sp.
  __ sub(sp, sp, Operand(r1, LSL, kPointerSizeLog2));
  // r1 is destination (r1 = source - r1).
  __ mov(r2, Operand(0));
  __ sub(r2, r2, Operand(r1, LSL, kPointerSizeLog2));
  __ add(r1, r3, Operand(r2));

  Label move;
  __ bind(&move);
  __ ldr(r2, MemOperand(r3, kPointerSize, PostIndex));
  __ str(r2, MemOperand(r1, kPointerSize, PostIndex));
  __ cmp(r3, Operand(r0));
  __ b(ne, &move);

  // Balance stack and put success value in top of stack
  __ pop(r0);
  __ mov(r0, Operand(Factory::true_value()));
  __ bind(&end_of_expand_frame);
}


void ArmCodeGenerator::GenerateIsSmi(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);
  Load(args->at(0));
  __ tst(r0, Operand(kSmiTagMask));
  __ pop(r0);
  cc_reg_ = eq;
}


// This is used in the implementation of apply on ia32 but it is not
// used on ARM yet.
void ArmCodeGenerator::GenerateIsArray(ZoneList<Expression*>* args) {
  __ int3();
  cc_reg_ = eq;
}


void ArmCodeGenerator::GenerateArgumentsLength(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 0);

  // Flush the TOS cache and seed the result with the formal
  // parameters count, which will be used in case no arguments adaptor
  // frame is found below the current frame.
  __ push(r0);
  __ mov(r0, Operand(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to the arguments.length.
  ArgumentsAccessStub stub(true);
  __ CallStub(&stub);
}


void ArmCodeGenerator::GenerateArgumentsAccess(ZoneList<Expression*>* args) {
  ASSERT(args->length() == 1);

  // Load the key onto the stack and set register r1 to the formal
  // parameters count for the currently executing function.
  Load(args->at(0));
  __ mov(r1, Operand(Smi::FromInt(scope_->num_parameters())));

  // Call the shared stub to get to arguments[key].
  ArgumentsAccessStub stub(false);
  __ CallStub(&stub);
}


void ArmCodeGenerator::GenerateShiftDownAndTailCall(
    ZoneList<Expression*>* args) {
    // r0 = number of arguments
    ASSERT(args->length() == 1);
    Load(args->at(0));
    __ mov(r0, Operand(r0, LSR, kSmiTagSize));

    // Get the 'this' function and exit the frame without returning.
    __ ldr(r1, MemOperand(pp, JavaScriptFrameConstants::kFunctionOffset));
    ExitJSFrame(reg_locals_, DO_NOT_RETURN);
    // return address in lr

    // Move arguments one element down the stack.
    Label move;
    Label moved;
    __ sub(r2, r0, Operand(0), SetCC);
    __ b(eq, &moved);
    __ bind(&move);
    __ sub(ip, r2, Operand(1));
    __ ldr(r3, MemOperand(sp, ip, LSL, kPointerSizeLog2));
    __ str(r3, MemOperand(sp, r2, LSL, kPointerSizeLog2));
    __ sub(r2, r2, Operand(1), SetCC);
    __ b(ne, &move);
    __ bind(&moved);

    // Remove the TOS (copy of last argument)
    __ add(sp, sp, Operand(kPointerSize));

    // Jump (tail-call) to the function in register r1.
    __ ldr(cp, FieldMemOperand(r1, JSFunction::kContextOffset));
    __ ldr(r1, FieldMemOperand(r1, JSFunction::kSharedFunctionInfoOffset));
    __ ldr(r1, FieldMemOperand(r1, SharedFunctionInfo::kCodeOffset));
    __ add(pc, r1, Operand(Code::kHeaderSize - kHeapObjectTag));
    return;
}


void ArmCodeGenerator::VisitCallRuntime(CallRuntime* node) {
  if (CheckForInlineRuntimeCall(node))
    return;

  ZoneList<Expression*>* args = node->arguments();
  Comment cmnt(masm_, "[ CallRuntime");
  Runtime::Function* function = node->function();

  if (function == NULL) {
    // Prepare stack for calling JS runtime function.
    __ Push(Operand(node->name()));
    // Push the builtins object found in the current global object.
    __ ldr(r1, GlobalObject());
    __ Push(FieldMemOperand(r1, GlobalObject::kBuiltinsOffset));
  }

  // Push the arguments ("left-to-right").
  for (int i = 0; i < args->length(); i++) Load(args->at(i));

  if (function != NULL) {
    // Call the C runtime function.
    __ CallRuntime(function, args->length());
  } else {
    // Call the JS runtime function.
    __ Push(Operand(args->length()));
    __ ldr(r1, MemOperand(sp, args->length() * kPointerSize));
    Handle<Code> stub = ComputeCallInitialize(args->length());
    __ Call(stub, code_target);
    __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
    __ add(sp, sp, Operand(kPointerSize));
  }
}


void ArmCodeGenerator::VisitUnaryOperation(UnaryOperation* node) {
  Comment cmnt(masm_, "[ UnaryOperation");

  Token::Value op = node->op();

  if (op == Token::NOT) {
    LoadCondition(node->expression(),
                  CodeGenState::LOAD,
                  false_target(),
                  true_target(),
                  true);
    cc_reg_ = NegateCondition(cc_reg_);

  } else if (op == Token::DELETE) {
    Property* property = node->expression()->AsProperty();
    if (property != NULL) {
      Load(property->obj());
      Load(property->key());
      __ Push(Operand(1));  // not counting receiver
      __ InvokeBuiltin("DELETE", 1, CALL_JS);
      return;
    }

    Variable* variable = node->expression()->AsVariableProxy()->AsVariable();
    if (variable != NULL) {
      Slot* slot = variable->slot();
      if (variable->is_global()) {
        LoadGlobal();
        __ Push(Operand(variable->name()));
        __ Push(Operand(1));  // not counting receiver
        __ InvokeBuiltin("DELETE", 1, CALL_JS);
        return;

      } else if (slot != NULL && slot->type() == Slot::LOOKUP) {
        // lookup the context holding the named variable
        __ Push(Operand(cp));
        __ Push(Operand(variable->name()));
        __ CallRuntime(Runtime::kLookupContext, 2);
        // r0: context
        __ Push(Operand(variable->name()));
        __ Push(Operand(1));  // not counting receiver
        __ InvokeBuiltin("DELETE", 1, CALL_JS);
        return;
      }

      // Default: Result of deleting non-global, not dynamically
      // introduced variables is false.
      __ Push(Operand(Factory::false_value()));

    } else {
      // Default: Result of deleting expressions is true.
      Load(node->expression());  // may have side-effects
      __ mov(r0, Operand(Factory::true_value()));
    }

  } else if (op == Token::TYPEOF) {
    // Special case for loading the typeof expression; see comment on
    // LoadTypeofExpression().
    LoadTypeofExpression(node->expression());
    __ CallRuntime(Runtime::kTypeof, 1);

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
        __ CallStub(&stub);
        break;
      }

      case Token::BIT_NOT: {
        // smi check
        Label smi_label;
        Label continue_label;
        __ tst(r0, Operand(kSmiTagMask));
        __ b(eq, &smi_label);

        __ Push(Operand(0));  // not counting receiver
        __ InvokeBuiltin("BIT_NOT", 0, CALL_JS);

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

      case Token::ADD:
        __ Push(Operand(0));  // not counting receiver
        __ InvokeBuiltin("TO_NUMBER", 0, CALL_JS);
        break;

      default:
        UNREACHABLE();
    }
  }
}


void ArmCodeGenerator::VisitCountOperation(CountOperation* node) {
  Comment cmnt(masm_, "[ CountOperation");

  bool is_postfix = node->is_postfix();
  bool is_increment = node->op() == Token::INC;

  Variable* var = node->expression()->AsVariableProxy()->AsVariable();
  bool is_const = (var != NULL && var->mode() == Variable::CONST);

  // Postfix: Make room for the result.
  if (is_postfix) __ Push(Operand(0));

  { Reference target(this, node->expression());
    if (target.is_illegal()) return;
    GetValue(&target);

    Label slow, exit;

    // Load the value (1) into register r1.
    __ mov(r1, Operand(Smi::FromInt(1)));

    // Check for smi operand.
    __ tst(r0, Operand(kSmiTagMask));
    __ b(ne, &slow);

    // Postfix: Store the old value as the result.
    if (is_postfix) __ str(r0, MemOperand(sp, target.size() * kPointerSize));

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
      __ str(r0, MemOperand(sp, target.size() * kPointerSize));
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
    if (!is_const) SetValue(&target);
  }

  // Postfix: Discard the new value and use the old.
  if (is_postfix) __ pop(r0);
}


void ArmCodeGenerator::VisitBinaryOperation(BinaryOperation* node) {
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
                  CodeGenState::LOAD,
                  &is_true,
                  false_target(),
                  false);
    if (has_cc()) {
      Branch(false, false_target());

      // Evaluate right side expression.
      __ bind(&is_true);
      LoadCondition(node->right(),
                    CodeGenState::LOAD,
                    true_target(),
                    false_target(),
                    false);

    } else {
      Label pop_and_continue, exit;

      // Avoid popping the result if it converts to 'false' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      ToBoolean(r0, &pop_and_continue, &exit);
      Branch(false, &exit);

      // Pop the result of evaluating the first part.
      __ bind(&pop_and_continue);
      __ pop(r0);

      // Evaluate right side expression.
      __ bind(&is_true);
      Load(node->right());

      // Exit (always with a materialized value).
      __ bind(&exit);
    }

  } else if (op == Token::OR) {
    Label is_false;
    LoadCondition(node->left(),
                  CodeGenState::LOAD,
                  true_target(),
                  &is_false,
                  false);
    if (has_cc()) {
      Branch(true, true_target());

      // Evaluate right side expression.
      __ bind(&is_false);
      LoadCondition(node->right(),
                    CodeGenState::LOAD,
                    true_target(),
                    false_target(),
                    false);

    } else {
      Label pop_and_continue, exit;

      // Avoid popping the result if it converts to 'true' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      ToBoolean(r0, &exit, &pop_and_continue);
      Branch(true, &exit);

      // Pop the result of evaluating the first part.
      __ bind(&pop_and_continue);
      __ pop(r0);

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
      GenericOperation(node->op());
    }
  }
}


void ArmCodeGenerator::VisitThisFunction(ThisFunction* node) {
  __ Push(FunctionOperand());
}


void ArmCodeGenerator::VisitCompareOperation(CompareOperation* node) {
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
        __ pop(r0);
        __ b(false_target());

        __ bind(&undetectable);
        __ ldr(r1, FieldMemOperand(r0, HeapObject::kMapOffset));
        __ ldrb(r2, FieldMemOperand(r1, Map::kBitFieldOffset));
        __ and_(r2, r2, Operand(1 << Map::kIsUndetectable));
        __ cmp(r2, Operand(1 << Map::kIsUndetectable));
      }

      __ bind(&exit);
      __ pop(r0);

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

    // Load the operand, move it to register r1, and restore TOS.
    LoadTypeofExpression(operation->expression());
    __ mov(r1, Operand(r0));
    __ pop(r0);

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
      __ Push(Operand(1));  // not counting receiver
      __ InvokeBuiltin("IN", 1, CALL_JS);
      break;

    case Token::INSTANCEOF:
      __ Push(Operand(1));  // not counting receiver
      __ InvokeBuiltin("INSTANCE_OF", 1, CALL_JS);
      break;

    default:
      UNREACHABLE();
  }
}


void ArmCodeGenerator::RecordStatementPosition(Node* node) {
  if (FLAG_debug_info) {
    int statement_pos = node->statement_pos();
    if (statement_pos == kNoPosition) return;
    __ RecordStatementPosition(statement_pos);
  }
}


void ArmCodeGenerator::EnterJSFrame(int argc, RegList callee_saved) {
  __ EnterJSFrame(argc, callee_saved);
}


void ArmCodeGenerator::ExitJSFrame(RegList callee_saved, ExitJSFlag flag) {
  // The JavaScript debugger expects ExitJSFrame to be implemented as a stub,
  // so that a breakpoint can be inserted at the end of a function.
  int num_callee_saved = NumRegs(callee_saved);

  // We support a fixed number of register variable configurations
  ASSERT(num_callee_saved <= 5 &&
         JSCalleeSavedList(num_callee_saved) == callee_saved);

  JSExitStub stub(num_callee_saved, callee_saved, flag);
  __ CallJSExitStub(&stub);
}


#undef __


// -----------------------------------------------------------------------------
// CodeGenerator interface

// MakeCode() is just a wrapper for CodeGenerator::MakeCode()
// so we don't have to expose the entire CodeGenerator class in
// the .h file.
Handle<Code> CodeGenerator::MakeCode(FunctionLiteral* fun,
                                     Handle<Script> script,
                                     bool is_eval) {
  Handle<Code> code = ArmCodeGenerator::MakeCode(fun, script, is_eval);
  if (!code.is_null()) {
    Counters::total_compiled_code_size.Increment(code->instruction_size());
  }
  return code;
}


} }  // namespace v8::internal
