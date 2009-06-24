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

// TODO(X64): Remove stdio.h when compiler test is removed.
#include <stdio.h>

#include "v8.h"

#include "bootstrapper.h"
#include "codegen-inl.h"
#include "debug.h"
#include "ic-inl.h"
#include "parser.h"
#include "register-allocator-inl.h"
#include "scopes.h"

// TODO(X64): Remove compiler.h when compiler test is removed.
#include "compiler.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm_)

// -------------------------------------------------------------------------
// Platform-specific DeferredCode functions.

void DeferredCode::SaveRegisters() {
  for (int i = 0; i < RegisterAllocator::kNumRegisters; i++) {
    int action = registers_[i];
    if (action == kPush) {
      __ push(RegisterAllocator::ToRegister(i));
    } else if (action != kIgnore && (action & kSyncedFlag) == 0) {
      __ movq(Operand(rbp, action), RegisterAllocator::ToRegister(i));
    }
  }
}

void DeferredCode::RestoreRegisters() {
  // Restore registers in reverse order due to the stack.
  for (int i = RegisterAllocator::kNumRegisters - 1; i >= 0; i--) {
    int action = registers_[i];
    if (action == kPush) {
      __ pop(RegisterAllocator::ToRegister(i));
    } else if (action != kIgnore) {
      action &= ~kSyncedFlag;
      __ movq(RegisterAllocator::ToRegister(i), Operand(rbp, action));
    }
  }
}


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


void CodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  // Call the runtime to declare the globals.  The inevitable call
  // will sync frame elements to memory anyway, so we do it eagerly to
  // allow us to push the arguments directly into place.
  frame_->SyncRange(0, frame_->element_count() - 1);

  __ movq(kScratchRegister, pairs, RelocInfo::EMBEDDED_OBJECT);
  frame_->EmitPush(kScratchRegister);
  frame_->EmitPush(rsi);  // The context is the second argument.
  frame_->EmitPush(Immediate(Smi::FromInt(is_eval() ? 1 : 0)));
  Result ignored = frame_->CallRuntime(Runtime::kDeclareGlobals, 3);
  // Return value is ignored.
}


void CodeGenerator::TestCodeGenerator() {
  // Compile a function from a string, and run it.

  // Set flags appropriately for this stage of implementation.
  // TODO(X64): Make ic work, and stop disabling them.
  // These settings stick - remove them when we don't want them anymore.
#ifdef DEBUG
  FLAG_print_builtin_source = true;
  FLAG_print_builtin_ast = true;
#endif
  FLAG_use_ic = false;

  // Read the file "test.js" from the current directory, compile, and run it.
  // If the file is not there, use a simple script embedded here instead.
  Handle<String> test_script;
  FILE* file = fopen("test.js", "rb");
  if (file == NULL) {
    test_script = Factory::NewStringFromAscii(CStrVector(
          "// Put all code in anonymous function to avoid global scope.\n"
          "(function(){"
          "  var x = true ? 47 : 32;"
          "  return x;"
          "})()"));
  } else {
    fseek(file, 0, SEEK_END);
    int size = ftell(file);
    rewind(file);

    char* chars = new char[size + 1];
    chars[size] = '\0';
    for (int i = 0; i < size;) {
      int read = fread(&chars[i], 1, size - i, file);
      i += read;
    }
    fclose(file);
    test_script = Factory::NewStringFromAscii(CStrVector(chars));
    delete[] chars;
  }

  Handle<JSFunction> test_function = Compiler::Compile(
      test_script,
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
  // Function compiles and runs, but returns a JSFunction object.
#ifdef DEBUG
  PrintF("Result of test function: ");
  result->Print();
#endif
}


void CodeGenerator::GenCode(FunctionLiteral* function) {
  // Record the position for debugging purposes.
  CodeForFunctionPosition(function);
  ZoneList<Statement*>* body = function->body();

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

    // Allocate space for locals and initialize them.
    frame_->AllocateStackSlots();
    // Initialize the function return target after the locals are set
    // up, because it needs the expected frame height from the frame.
    function_return_.set_direction(JumpTarget::BIDIRECTIONAL);
    function_return_is_shadowed_ = false;

    // TODO(X64): Add code to handle arguments object and context object.

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
    // CheckStack();

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
        ASSERT(!function_return_is_shadowed_);
        CodeForReturnPosition(function);
        frame_->PrepareForReturn();
        Result undefined(Factory::undefined_value());
        if (function_return_.is_bound()) {
          function_return_.Jump(&undefined);
        } else {
          function_return_.Bind(&undefined);
          GenerateReturnSequence(&undefined);
        }
      } else if (function_return_.is_linked()) {
        // If the return target has dangling jumps to it, then we have not
        // yet generated the return sequence.  This can happen when (a)
        // control does not flow off the end of the body so we did not
        // compile an artificial return statement just above, and (b) there
        // are return statements in the body but (c) they are all shadowed.
        Result return_value;
        function_return_.Bind(&return_value);
        GenerateReturnSequence(&return_value);
      }
    }
  }

  // Adjust for function-level loop nesting.
  loop_nesting_ -= function->loop_nesting();

  // Code generation state must be reset.
  ASSERT(state_ == NULL);
  ASSERT(loop_nesting() == 0);
  ASSERT(!function_return_is_shadowed_);
  function_return_.Unuse();
  DeleteFrame();

  // Process any deferred code using the register allocator.
  if (!HasStackOverflow()) {
    HistogramTimerScope deferred_timer(&Counters::deferred_code_generation);
    JumpTarget::set_compiling_deferred_code(true);
    ProcessDeferred();
    JumpTarget::set_compiling_deferred_code(false);
  }

  // There is no need to delete the register allocator, it is a
  // stack-allocated local.
  allocator_ = NULL;
  scope_ = NULL;
}

void CodeGenerator::GenerateReturnSequence(Result* return_value) {
  // The return value is a live (but not currently reference counted)
  // reference to rax.  This is safe because the current frame does not
  // contain a reference to rax (it is prepared for the return by spilling
  // all registers).
  if (FLAG_trace) {
    frame_->Push(return_value);
    *return_value = frame_->CallRuntime(Runtime::kTraceExit, 1);
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

  // TODO(x64): introduce kX64JSReturnSequenceLength and enable assert.

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

#ifdef DEBUG
bool CodeGenerator::HasValidEntryRegisters() {
  return (allocator()->count(rax) == (frame()->is_used(rax) ? 1 : 0))
      && (allocator()->count(rbx) == (frame()->is_used(rbx) ? 1 : 0))
      && (allocator()->count(rcx) == (frame()->is_used(rcx) ? 1 : 0))
      && (allocator()->count(rdx) == (frame()->is_used(rdx) ? 1 : 0))
      && (allocator()->count(rdi) == (frame()->is_used(rdi) ? 1 : 0))
      && (allocator()->count(r8) == (frame()->is_used(r8) ? 1 : 0))
      && (allocator()->count(r9) == (frame()->is_used(r9) ? 1 : 0))
      && (allocator()->count(r11) == (frame()->is_used(r11) ? 1 : 0))
      && (allocator()->count(r14) == (frame()->is_used(r14) ? 1 : 0))
      && (allocator()->count(r15) == (frame()->is_used(r15) ? 1 : 0))
      && (allocator()->count(r13) == (frame()->is_used(r13) ? 1 : 0))
      && (allocator()->count(r12) == (frame()->is_used(r12) ? 1 : 0));
}
#endif


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
  node->break_target()->set_direction(JumpTarget::FORWARD_ONLY);
  VisitStatements(node->statements());
  if (node->break_target()->is_linked()) {
    node->break_target()->Bind();
  }
  node->break_target()->Unuse();
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
    ASSERT(var->is_dynamic());
    // For now, just do a runtime call.  Sync the virtual frame eagerly
    // so we can simply push the arguments into place.
    frame_->SyncRange(0, frame_->element_count() - 1);
    frame_->EmitPush(rsi);
    __ movq(kScratchRegister, var->name(), RelocInfo::EMBEDDED_OBJECT);
    frame_->EmitPush(kScratchRegister);
    // Declaration nodes are always introduced in one of two modes.
    ASSERT(node->mode() == Variable::VAR || node->mode() == Variable::CONST);
    PropertyAttributes attr = node->mode() == Variable::VAR ? NONE : READ_ONLY;
    frame_->EmitPush(Immediate(Smi::FromInt(attr)));
    // Push initial value, if any.
    // Note: For variables we must not push an initial value (such as
    // 'undefined') because we may have a (legal) redeclaration and we
    // must not destroy the current value.
    if (node->mode() == Variable::CONST) {
      __ movq(kScratchRegister, Factory::the_hole_value(),
              RelocInfo::EMBEDDED_OBJECT);
      frame_->EmitPush(kScratchRegister);
    } else if (node->fun() != NULL) {
      Load(node->fun());
    } else {
      frame_->EmitPush(Immediate(Smi::FromInt(0)));  // no initial value!
    }
    Result ignored = frame_->CallRuntime(Runtime::kDeclareContextSlot, 4);
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
    {
      // Set the initial value.
      Reference target(this, node->proxy());
      Load(val);
      target.SetValue(NOT_CONST_INIT);
      // The reference is removed from the stack (preserving TOS) when
      // it goes out of scope.
    }
    // Get rid of the assigned value (declarations are statements).
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
  JumpTarget exit;
  if (has_then_stm && has_else_stm) {
    JumpTarget then;
    JumpTarget else_;
    ControlDestination dest(&then, &else_, true);
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &dest, true);

    if (dest.false_was_fall_through()) {
      // The else target was bound, so we compile the else part first.
      Visit(node->else_statement());

      // We may have dangling jumps to the then part.
      if (then.is_linked()) {
        if (has_valid_frame()) exit.Jump();
        then.Bind();
        Visit(node->then_statement());
      }
    } else {
      // The then target was bound, so we compile the then part first.
      Visit(node->then_statement());

      if (else_.is_linked()) {
        if (has_valid_frame()) exit.Jump();
        else_.Bind();
        Visit(node->else_statement());
      }
    }

  } else if (has_then_stm) {
    ASSERT(!has_else_stm);
    JumpTarget then;
    ControlDestination dest(&then, &exit, true);
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &dest, true);

    if (dest.false_was_fall_through()) {
      // The exit label was bound.  We may have dangling jumps to the
      // then part.
      if (then.is_linked()) {
        exit.Unuse();
        exit.Jump();
        then.Bind();
        Visit(node->then_statement());
      }
    } else {
      // The then label was bound.
      Visit(node->then_statement());
    }

  } else if (has_else_stm) {
    ASSERT(!has_then_stm);
    JumpTarget else_;
    ControlDestination dest(&exit, &else_, false);
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &dest, true);

    if (dest.true_was_fall_through()) {
      // The exit label was bound.  We may have dangling jumps to the
      // else part.
      if (else_.is_linked()) {
        exit.Unuse();
        exit.Jump();
        else_.Bind();
        Visit(node->else_statement());
      }
    } else {
      // The else label was bound.
      Visit(node->else_statement());
    }

  } else {
    ASSERT(!has_then_stm && !has_else_stm);
    // We only care about the condition's side effects (not its value
    // or control flow effect).  LoadCondition is called without
    // forcing control flow.
    ControlDestination dest(&exit, &exit, true);
    LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &dest, false);
    if (!dest.is_used()) {
      // We got a value on the frame rather than (or in addition to)
      // control flow.
      frame_->Drop();
    }
  }

  if (exit.is_linked()) {
    exit.Bind();
  }
}


void CodeGenerator::VisitContinueStatement(ContinueStatement* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitBreakStatement(BreakStatement* a) {
  UNIMPLEMENTED();
}


void CodeGenerator::VisitReturnStatement(ReturnStatement* node) {
  ASSERT(!in_spilled_code());
  Comment cmnt(masm_, "[ ReturnStatement");

  CodeForStatementPosition(node);
  Load(node->expression());
  Result return_value = frame_->Pop();
  if (function_return_is_shadowed_) {
    function_return_.Jump(&return_value);
  } else {
    frame_->PrepareForReturn();
    if (function_return_.is_bound()) {
      // If the function return label is already bound we reuse the
      // code by jumping to the return site.
      function_return_.Jump(&return_value);
    } else {
      function_return_.Bind(&return_value);
      GenerateReturnSequence(&return_value);
    }
  }
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


void CodeGenerator::InstantiateBoilerplate(Handle<JSFunction> boilerplate) {
  // Call the runtime to instantiate the function boilerplate object.
  // The inevitable call will sync frame elements to memory anyway, so
  // we do it eagerly to allow us to push the arguments directly into
  // place.
  ASSERT(boilerplate->IsBoilerplate());
  frame_->SyncRange(0, frame_->element_count() - 1);

  // Push the boilerplate on the stack.
  __ movq(kScratchRegister, boilerplate, RelocInfo::EMBEDDED_OBJECT);
  frame_->EmitPush(kScratchRegister);

  // Create a new closure.
  frame_->EmitPush(rsi);
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
  JumpTarget then;
  JumpTarget else_;
  JumpTarget exit;
  ControlDestination dest(&then, &else_, true);
  LoadCondition(node->condition(), NOT_INSIDE_TYPEOF, &dest, true);

  if (dest.false_was_fall_through()) {
    // The else target was bound, so we compile the else part first.
    Load(node->else_expression(), typeof_state());

    if (then.is_linked()) {
      exit.Jump();
      then.Bind();
      Load(node->then_expression(), typeof_state());
    }
  } else {
    // The then target was bound, so we compile the then part first.
    Load(node->then_expression(), typeof_state());

    if (else_.is_linked()) {
      exit.Jump();
      else_.Bind();
      Load(node->else_expression(), typeof_state());
    }
  }

  exit.Bind();
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
  frame_->Push(node->handle());
}


// Materialize the regexp literal 'node' in the literals array
// 'literals' of the function.  Leave the regexp boilerplate in
// 'boilerplate'.
class DeferredRegExpLiteral: public DeferredCode {
 public:
  DeferredRegExpLiteral(Register boilerplate,
                        Register literals,
                        RegExpLiteral* node)
      : boilerplate_(boilerplate), literals_(literals), node_(node) {
    set_comment("[ DeferredRegExpLiteral");
  }

  void Generate();

 private:
  Register boilerplate_;
  Register literals_;
  RegExpLiteral* node_;
};


void DeferredRegExpLiteral::Generate() {
  // Since the entry is undefined we call the runtime system to
  // compute the literal.
  // Literal array (0).
  __ push(literals_);
  // Literal index (1).
  __ push(Immediate(Smi::FromInt(node_->literal_index())));
  // RegExp pattern (2).
  __ Push(node_->pattern());
  // RegExp flags (3).
  __ Push(node_->flags());
  __ CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  if (!boilerplate_.is(rax)) __ movq(boilerplate_, rax);
}


void CodeGenerator::VisitRegExpLiteral(RegExpLiteral* node) {
  Comment cmnt(masm_, "[ RegExp Literal");

  // Retrieve the literals array and check the allocated entry.  Begin
  // with a writable copy of the function of this activation in a
  // register.
  frame_->PushFunction();
  Result literals = frame_->Pop();
  literals.ToRegister();
  frame_->Spill(literals.reg());

  // Load the literals array of the function.
  __ movq(literals.reg(),
          FieldOperand(literals.reg(), JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  Result boilerplate = allocator_->Allocate();
  ASSERT(boilerplate.is_valid());
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ movq(boilerplate.reg(), FieldOperand(literals.reg(), literal_offset));

  // Check whether we need to materialize the RegExp object.  If so,
  // jump to the deferred code passing the literals array.
  DeferredRegExpLiteral* deferred =
      new DeferredRegExpLiteral(boilerplate.reg(), literals.reg(), node);
  __ Cmp(boilerplate.reg(), Factory::undefined_value());
  deferred->Branch(equal);
  deferred->BindExit();
  literals.Unuse();

  // Push the boilerplate object.
  frame_->Push(&boilerplate);
}


// Materialize the object literal 'node' in the literals array
// 'literals' of the function.  Leave the object boilerplate in
// 'boilerplate'.
class DeferredObjectLiteral: public DeferredCode {
 public:
  DeferredObjectLiteral(Register boilerplate,
                        Register literals,
                        ObjectLiteral* node)
      : boilerplate_(boilerplate), literals_(literals), node_(node) {
    set_comment("[ DeferredObjectLiteral");
  }

  void Generate();

 private:
  Register boilerplate_;
  Register literals_;
  ObjectLiteral* node_;
};


void DeferredObjectLiteral::Generate() {
  // Since the entry is undefined we call the runtime system to
  // compute the literal.
  // Literal array (0).
  __ push(literals_);
  // Literal index (1).
  __ push(Immediate(Smi::FromInt(node_->literal_index())));
  // Constant properties (2).
  __ Push(node_->constant_properties());
  __ CallRuntime(Runtime::kCreateObjectLiteralBoilerplate, 3);
  if (!boilerplate_.is(rax)) __ movq(boilerplate_, rax);
}


void CodeGenerator::VisitObjectLiteral(ObjectLiteral* node) {
  Comment cmnt(masm_, "[ ObjectLiteral");

  // Retrieve the literals array and check the allocated entry.  Begin
  // with a writable copy of the function of this activation in a
  // register.
  frame_->PushFunction();
  Result literals = frame_->Pop();
  literals.ToRegister();
  frame_->Spill(literals.reg());

  // Load the literals array of the function.
  __ movq(literals.reg(),
          FieldOperand(literals.reg(), JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  Result boilerplate = allocator_->Allocate();
  ASSERT(boilerplate.is_valid());
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ movq(boilerplate.reg(), FieldOperand(literals.reg(), literal_offset));

  // Check whether we need to materialize the object literal boilerplate.
  // If so, jump to the deferred code passing the literals array.
  DeferredObjectLiteral* deferred =
      new DeferredObjectLiteral(boilerplate.reg(), literals.reg(), node);
  __ Cmp(boilerplate.reg(), Factory::undefined_value());
  deferred->Branch(equal);
  deferred->BindExit();
  literals.Unuse();

  // Push the boilerplate object.
  frame_->Push(&boilerplate);
  // Clone the boilerplate object.
  Runtime::FunctionId clone_function_id = Runtime::kCloneLiteralBoilerplate;
  if (node->depth() == 1) {
    clone_function_id = Runtime::kCloneShallowLiteralBoilerplate;
  }
  Result clone = frame_->CallRuntime(clone_function_id, 1);
  // Push the newly cloned literal object as the result.
  frame_->Push(&clone);

  for (int i = 0; i < node->properties()->length(); i++) {
    ObjectLiteral::Property* property = node->properties()->at(i);
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT:
        break;
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:
        if (CompileTimeValue::IsCompileTimeValue(property->value())) break;
        // else fall through.
      case ObjectLiteral::Property::COMPUTED: {
        Handle<Object> key(property->key()->handle());
        if (key->IsSymbol()) {
          // Duplicate the object as the IC receiver.
          frame_->Dup();
          Load(property->value());
          frame_->Push(key);
          Result ignored = frame_->CallStoreIC();
          // Drop the duplicated receiver and ignore the result.
          frame_->Drop();
          break;
        }
        // Fall through
      }
      case ObjectLiteral::Property::PROTOTYPE: {
        // Duplicate the object as an argument to the runtime call.
        frame_->Dup();
        Load(property->key());
        Load(property->value());
        Result ignored = frame_->CallRuntime(Runtime::kSetProperty, 3);
        // Ignore the result.
        break;
      }
      case ObjectLiteral::Property::SETTER: {
        // Duplicate the object as an argument to the runtime call.
        frame_->Dup();
        Load(property->key());
        frame_->Push(Smi::FromInt(1));
        Load(property->value());
        Result ignored = frame_->CallRuntime(Runtime::kDefineAccessor, 4);
        // Ignore the result.
        break;
      }
      case ObjectLiteral::Property::GETTER: {
        // Duplicate the object as an argument to the runtime call.
        frame_->Dup();
        Load(property->key());
        frame_->Push(Smi::FromInt(0));
        Load(property->value());
        Result ignored = frame_->CallRuntime(Runtime::kDefineAccessor, 4);
        // Ignore the result.
        break;
      }
      default: UNREACHABLE();
    }
  }
}


// Materialize the array literal 'node' in the literals array 'literals'
// of the function.  Leave the array boilerplate in 'boilerplate'.
class DeferredArrayLiteral: public DeferredCode {
 public:
  DeferredArrayLiteral(Register boilerplate,
                       Register literals,
                       ArrayLiteral* node)
      : boilerplate_(boilerplate), literals_(literals), node_(node) {
    set_comment("[ DeferredArrayLiteral");
  }

  void Generate();

 private:
  Register boilerplate_;
  Register literals_;
  ArrayLiteral* node_;
};


void DeferredArrayLiteral::Generate() {
  // Since the entry is undefined we call the runtime system to
  // compute the literal.
  // Literal array (0).
  __ push(literals_);
  // Literal index (1).
  __ push(Immediate(Smi::FromInt(node_->literal_index())));
  // Constant properties (2).
  __ Push(node_->literals());
  __ CallRuntime(Runtime::kCreateArrayLiteralBoilerplate, 3);
  if (!boilerplate_.is(rax)) __ movq(boilerplate_, rax);
}


void CodeGenerator::VisitArrayLiteral(ArrayLiteral* node) {
  Comment cmnt(masm_, "[ ArrayLiteral");

  // Retrieve the literals array and check the allocated entry.  Begin
  // with a writable copy of the function of this activation in a
  // register.
  frame_->PushFunction();
  Result literals = frame_->Pop();
  literals.ToRegister();
  frame_->Spill(literals.reg());

  // Load the literals array of the function.
  __ movq(literals.reg(),
          FieldOperand(literals.reg(), JSFunction::kLiteralsOffset));

  // Load the literal at the ast saved index.
  Result boilerplate = allocator_->Allocate();
  ASSERT(boilerplate.is_valid());
  int literal_offset =
      FixedArray::kHeaderSize + node->literal_index() * kPointerSize;
  __ movq(boilerplate.reg(), FieldOperand(literals.reg(), literal_offset));

  // Check whether we need to materialize the object literal boilerplate.
  // If so, jump to the deferred code passing the literals array.
  DeferredArrayLiteral* deferred =
      new DeferredArrayLiteral(boilerplate.reg(), literals.reg(), node);
  __ Cmp(boilerplate.reg(), Factory::undefined_value());
  deferred->Branch(equal);
  deferred->BindExit();
  literals.Unuse();

  // Push the resulting array literal boilerplate on the stack.
  frame_->Push(&boilerplate);
  // Clone the boilerplate object.
  Runtime::FunctionId clone_function_id = Runtime::kCloneLiteralBoilerplate;
  if (node->depth() == 1) {
    clone_function_id = Runtime::kCloneShallowLiteralBoilerplate;
  }
  Result clone = frame_->CallRuntime(clone_function_id, 1);
  // Push the newly cloned literal object as the result.
  frame_->Push(&clone);

  // Generate code to set the elements in the array that are not
  // literals.
  for (int i = 0; i < node->values()->length(); i++) {
    Expression* value = node->values()->at(i);

    // If value is a literal the property value is already set in the
    // boilerplate object.
    if (value->AsLiteral() != NULL) continue;
    // If value is a materialized literal the property value is already set
    // in the boilerplate object if it is simple.
    if (CompileTimeValue::IsCompileTimeValue(value)) continue;

    // The property must be set by generated code.
    Load(value);

    // Get the property value off the stack.
    Result prop_value = frame_->Pop();
    prop_value.ToRegister();

    // Fetch the array literal while leaving a copy on the stack and
    // use it to get the elements array.
    frame_->Dup();
    Result elements = frame_->Pop();
    elements.ToRegister();
    frame_->Spill(elements.reg());
    // Get the elements array.
    __ movq(elements.reg(),
            FieldOperand(elements.reg(), JSObject::kElementsOffset));

    // Write to the indexed properties array.
    int offset = i * kPointerSize + Array::kHeaderSize;
    __ movq(FieldOperand(elements.reg(), offset), prop_value.reg());

    // Update the write barrier for the array address.
    frame_->Spill(prop_value.reg());  // Overwritten by the write barrier.
    Result scratch = allocator_->Allocate();
    ASSERT(scratch.is_valid());
    __ RecordWrite(elements.reg(), offset, prop_value.reg(), scratch.reg());
  }
}


void CodeGenerator::VisitCatchExtensionObject(CatchExtensionObject* a) {
  UNIMPLEMENTED();
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
    Variable* var = node->target()->AsVariableProxy()->AsVariable();

    if (node->starts_initialization_block()) {
      ASSERT(target.type() == Reference::NAMED ||
             target.type() == Reference::KEYED);
      // Change to slow case in the beginning of an initialization
      // block to avoid the quadratic behavior of repeatedly adding
      // fast properties.

      // The receiver is the argument to the runtime call.  It is the
      // first value pushed when the reference was loaded to the
      // frame.
      frame_->PushElementAt(target.size() - 1);
      // Result ignored = frame_->CallRuntime(Runtime::kToSlowProperties, 1);
    }
    if (node->op() == Token::ASSIGN ||
        node->op() == Token::INIT_VAR ||
        node->op() == Token::INIT_CONST) {
      Load(node->value());

    } else {
      // TODO(X64): Make compound assignments work.
      /*
      Literal* literal = node->value()->AsLiteral();
      bool overwrite_value =
          (node->value()->AsBinaryOperation() != NULL &&
           node->value()->AsBinaryOperation()->ResultOverwriteAllowed());
      Variable* right_var = node->value()->AsVariableProxy()->AsVariable();
      // There are two cases where the target is not read in the right hand
      // side, that are easy to test for: the right hand side is a literal,
      // or the right hand side is a different variable.  TakeValue invalidates
      // the target, with an implicit promise that it will be written to again
      // before it is read.
      if (literal != NULL || (right_var != NULL && right_var != var)) {
        target.TakeValue(NOT_INSIDE_TYPEOF);
      } else {
        target.GetValue(NOT_INSIDE_TYPEOF);
      }
      */
      Load(node->value());
      /*
      GenericBinaryOperation(node->binary_op(),
                             node->type(),
                             overwrite_value ? OVERWRITE_RIGHT : NO_OVERWRITE);
      */
    }

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
      if (node->ends_initialization_block()) {
        ASSERT(target.type() == Reference::NAMED ||
               target.type() == Reference::KEYED);
        // End of initialization block. Revert to fast case.  The
        // argument to the runtime call is the receiver, which is the
        // first value pushed as part of the reference, which is below
        // the lhs value.
        frame_->PushElementAt(target.size());
        // Result ignored = frame_->CallRuntime(Runtime::kToFastProperties, 1);
      }
    }
  }
}


void CodeGenerator::VisitThrow(Throw* node) {
  Comment cmnt(masm_, "[ Throw");
  CodeForStatementPosition(node);

  Load(node->exception());
  Result result = frame_->CallRuntime(Runtime::kThrow, 1);
  frame_->Push(&result);
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

    // Call the IC initialization code.
    CodeForSourcePosition(node->position());
    Result result = frame_->CallCallIC(RelocInfo::CODE_TARGET_CONTEXT,
                                       arg_count,
                                       loop_nesting());
    frame_->RestoreContextRegister();
    // Replace the function on the stack with the result.
    frame_->SetElementAt(0, &result);
  } else if (var != NULL && var->slot() != NULL &&
             var->slot()->type() == Slot::LOOKUP) {
    // TODO(X64): Enable calls of non-global functions.
    UNIMPLEMENTED();
    /*
    // ----------------------------------
    // JavaScript example: 'with (obj) foo(1, 2, 3)'  // foo is in obj
    // ----------------------------------

    // Load the function from the context.  Sync the frame so we can
    // push the arguments directly into place.
    frame_->SyncRange(0, frame_->element_count() - 1);
    frame_->EmitPush(esi);
    frame_->EmitPush(Immediate(var->name()));
    frame_->CallRuntime(Runtime::kLoadContextSlot, 2);
    // The runtime call returns a pair of values in eax and edx.  The
    // looked-up function is in eax and the receiver is in edx.  These
    // register references are not ref counted here.  We spill them
    // eagerly since they are arguments to an inevitable call (and are
    // not sharable by the arguments).
    ASSERT(!allocator()->is_used(eax));
    frame_->EmitPush(eax);

    // Load the receiver.
    ASSERT(!allocator()->is_used(edx));
    frame_->EmitPush(edx);

    // Call the function.
    CallWithArguments(args, node->position());
    */
  } else if (property != NULL) {
    // Check if the key is a literal string.
    Literal* literal = property->key()->AsLiteral();

    if (literal != NULL && literal->handle()->IsSymbol()) {
      // ------------------------------------------------------------------
      // JavaScript example: 'object.foo(1, 2, 3)' or 'map["key"](1, 2, 3)'
      // ------------------------------------------------------------------

      // TODO(X64): Consider optimizing Function.prototype.apply calls
      // with arguments object. Requires lazy arguments allocation;
      // see http://codereview.chromium.org/147075.

      // Push the name of the function and the receiver onto the stack.
      frame_->Push(literal->handle());
      Load(property->obj());

      // Load the arguments.
      int arg_count = args->length();
      for (int i = 0; i < arg_count; i++) {
        Load(args->at(i));
      }

      // Call the IC initialization code.
      CodeForSourcePosition(node->position());
      Result result =
          frame_->CallCallIC(RelocInfo::CODE_TARGET, arg_count, loop_nesting());
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
      if (property->is_synthetic()) {
        // Use global object as receiver.
        LoadGlobalReceiver();
      } else {
        // The reference's size is non-negative.
        frame_->PushElementAt(ref.size());
      }

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


void CodeGenerator::VisitCallEval(CallEval* a) {
  UNIMPLEMENTED();
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

  // Call the construct call builtin that handles allocation and
  // constructor invocation.
  CodeForSourcePosition(node->position());
  Result result = frame_->CallConstructor(arg_count);
  // Replace the function on the stack with the result.
  frame_->SetElementAt(0, &result);
}


void CodeGenerator::VisitCallRuntime(CallRuntime* node) {
  if (CheckForInlineRuntimeCall(node)) {
    return;
  }

  ZoneList<Expression*>* args = node->arguments();
  Comment cmnt(masm_, "[ CallRuntime");
  Runtime::Function* function = node->function();

  if (function == NULL) {
    // Prepare stack for calling JS runtime function.
    frame_->Push(node->name());
    // Push the builtins object found in the current global object.
    Result temp = allocator()->Allocate();
    ASSERT(temp.is_valid());
    __ movq(temp.reg(), GlobalObject());
    __ movq(temp.reg(),
            FieldOperand(temp.reg(), GlobalObject::kBuiltinsOffset));
    frame_->Push(&temp);
  }

  // Push the arguments ("left-to-right").
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    Load(args->at(i));
  }

  if (function == NULL) {
    // Call the JS runtime function.
    Result answer = frame_->CallCallIC(RelocInfo::CODE_TARGET,
                                       arg_count,
                                       loop_nesting_);
    frame_->RestoreContextRegister();
    frame_->SetElementAt(0, &answer);
  } else {
    // Call the C runtime function.
    Result answer = frame_->CallRuntime(function, arg_count);
    frame_->Push(&answer);
  }
}


void CodeGenerator::VisitUnaryOperation(UnaryOperation* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitCountOperation(CountOperation* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::VisitBinaryOperation(BinaryOperation* node) {
  // TODO(X64): This code was copied verbatim from codegen-ia32.
  //     Either find a reason to change it or move it to a shared location.

  // Note that due to an optimization in comparison operations (typeof
  // compared to a string literal), we can evaluate a binary expression such
  // as AND or OR and not leave a value on the frame or in the cc register.
  Comment cmnt(masm_, "[ BinaryOperation");
  Token::Value op = node->op();

  // According to ECMA-262 section 11.11, page 58, the binary logical
  // operators must yield the result of one of the two expressions
  // before any ToBoolean() conversions. This means that the value
  // produced by a && or || operator is not necessarily a boolean.

  // NOTE: If the left hand side produces a materialized value (not
  // control flow), we force the right hand side to do the same. This
  // is necessary because we assume that if we get control flow on the
  // last path out of an expression we got it on all paths.
  if (op == Token::AND) {
    JumpTarget is_true;
    ControlDestination dest(&is_true, destination()->false_target(), true);
    LoadCondition(node->left(), NOT_INSIDE_TYPEOF, &dest, false);

    if (dest.false_was_fall_through()) {
      // The current false target was used as the fall-through.  If
      // there are no dangling jumps to is_true then the left
      // subexpression was unconditionally false.  Otherwise we have
      // paths where we do have to evaluate the right subexpression.
      if (is_true.is_linked()) {
        // We need to compile the right subexpression.  If the jump to
        // the current false target was a forward jump then we have a
        // valid frame, we have just bound the false target, and we
        // have to jump around the code for the right subexpression.
        if (has_valid_frame()) {
          destination()->false_target()->Unuse();
          destination()->false_target()->Jump();
        }
        is_true.Bind();
        // The left subexpression compiled to control flow, so the
        // right one is free to do so as well.
        LoadCondition(node->right(), NOT_INSIDE_TYPEOF, destination(), false);
      } else {
        // We have actually just jumped to or bound the current false
        // target but the current control destination is not marked as
        // used.
        destination()->Use(false);
      }

    } else if (dest.is_used()) {
      // The left subexpression compiled to control flow (and is_true
      // was just bound), so the right is free to do so as well.
      LoadCondition(node->right(), NOT_INSIDE_TYPEOF, destination(), false);

    } else {
      // We have a materialized value on the frame, so we exit with
      // one on all paths.  There are possibly also jumps to is_true
      // from nested subexpressions.
      JumpTarget pop_and_continue;
      JumpTarget exit;

      // Avoid popping the result if it converts to 'false' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      //
      // Duplicate the TOS value. The duplicate will be popped by
      // ToBoolean.
      frame_->Dup();
      ControlDestination dest(&pop_and_continue, &exit, true);
      ToBoolean(&dest);

      // Pop the result of evaluating the first part.
      frame_->Drop();

      // Compile right side expression.
      is_true.Bind();
      Load(node->right());

      // Exit (always with a materialized value).
      exit.Bind();
    }

  } else if (op == Token::OR) {
    JumpTarget is_false;
    ControlDestination dest(destination()->true_target(), &is_false, false);
    LoadCondition(node->left(), NOT_INSIDE_TYPEOF, &dest, false);

    if (dest.true_was_fall_through()) {
      // The current true target was used as the fall-through.  If
      // there are no dangling jumps to is_false then the left
      // subexpression was unconditionally true.  Otherwise we have
      // paths where we do have to evaluate the right subexpression.
      if (is_false.is_linked()) {
        // We need to compile the right subexpression.  If the jump to
        // the current true target was a forward jump then we have a
        // valid frame, we have just bound the true target, and we
        // have to jump around the code for the right subexpression.
        if (has_valid_frame()) {
          destination()->true_target()->Unuse();
          destination()->true_target()->Jump();
        }
        is_false.Bind();
        // The left subexpression compiled to control flow, so the
        // right one is free to do so as well.
        LoadCondition(node->right(), NOT_INSIDE_TYPEOF, destination(), false);
      } else {
        // We have just jumped to or bound the current true target but
        // the current control destination is not marked as used.
        destination()->Use(true);
      }

    } else if (dest.is_used()) {
      // The left subexpression compiled to control flow (and is_false
      // was just bound), so the right is free to do so as well.
      LoadCondition(node->right(), NOT_INSIDE_TYPEOF, destination(), false);

    } else {
      // We have a materialized value on the frame, so we exit with
      // one on all paths.  There are possibly also jumps to is_false
      // from nested subexpressions.
      JumpTarget pop_and_continue;
      JumpTarget exit;

      // Avoid popping the result if it converts to 'true' using the
      // standard ToBoolean() conversion as described in ECMA-262,
      // section 9.2, page 30.
      //
      // Duplicate the TOS value. The duplicate will be popped by
      // ToBoolean.
      frame_->Dup();
      ControlDestination dest(&exit, &pop_and_continue, false);
      ToBoolean(&dest);

      // Pop the result of evaluating the first part.
      frame_->Drop();

      // Compile right side expression.
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

    Load(node->left());
    Load(node->right());
    GenericBinaryOperation(node->op(), node->type(), overwrite_mode);
  }
}



void CodeGenerator::VisitCompareOperation(CompareOperation* node) {
  Comment cmnt(masm_, "[ CompareOperation");

  // Get the expressions from the node.
  Expression* left = node->left();
  Expression* right = node->right();
  Token::Value op = node->op();
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
      __ testl(answer.reg(), Immediate(kSmiTagMask));
      destination()->true_target()->Branch(zero);
      frame_->Spill(answer.reg());
      __ movq(answer.reg(), FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ Cmp(answer.reg(), Factory::heap_number_map());
      answer.Unuse();
      destination()->Split(equal);

    } else if (check->Equals(Heap::string_symbol())) {
      __ testl(answer.reg(), Immediate(kSmiTagMask));
      destination()->false_target()->Branch(zero);

      // It can be an undetectable string object.
      __ movq(kScratchRegister,
              FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ testb(FieldOperand(kScratchRegister, Map::kBitFieldOffset),
               Immediate(1 << Map::kIsUndetectable));
      destination()->false_target()->Branch(not_zero);
      __ CmpInstanceType(kScratchRegister, FIRST_NONSTRING_TYPE);
      answer.Unuse();
      destination()->Split(below);  // Unsigned byte comparison needed.

    } else if (check->Equals(Heap::boolean_symbol())) {
      __ Cmp(answer.reg(), Factory::true_value());
      destination()->true_target()->Branch(equal);
      __ Cmp(answer.reg(), Factory::false_value());
      answer.Unuse();
      destination()->Split(equal);

    } else if (check->Equals(Heap::undefined_symbol())) {
      __ Cmp(answer.reg(), Factory::undefined_value());
      destination()->true_target()->Branch(equal);

      __ testl(answer.reg(), Immediate(kSmiTagMask));
      destination()->false_target()->Branch(zero);

      // It can be an undetectable object.
      __ movq(kScratchRegister,
              FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ testb(FieldOperand(kScratchRegister, Map::kBitFieldOffset),
               Immediate(1 << Map::kIsUndetectable));
      answer.Unuse();
      destination()->Split(not_zero);

    } else if (check->Equals(Heap::function_symbol())) {
      __ testl(answer.reg(), Immediate(kSmiTagMask));
      destination()->false_target()->Branch(zero);
      frame_->Spill(answer.reg());
      __ CmpObjectType(answer.reg(), JS_FUNCTION_TYPE, answer.reg());
      answer.Unuse();
      destination()->Split(equal);

    } else if (check->Equals(Heap::object_symbol())) {
      __ testl(answer.reg(), Immediate(kSmiTagMask));
      destination()->false_target()->Branch(zero);
      __ Cmp(answer.reg(), Factory::null_value());
      destination()->true_target()->Branch(equal);

      // It can be an undetectable object.
      __ movq(kScratchRegister,
              FieldOperand(answer.reg(), HeapObject::kMapOffset));
      __ movb(kScratchRegister,
              FieldOperand(kScratchRegister, Map::kBitFieldOffset));
      __ testb(kScratchRegister, Immediate(1 << Map::kIsUndetectable));
      destination()->false_target()->Branch(not_zero);
      __ cmpb(kScratchRegister, Immediate(FIRST_JS_OBJECT_TYPE));
      destination()->false_target()->Branch(below);
      __ cmpb(kScratchRegister, Immediate(LAST_JS_OBJECT_TYPE));
      answer.Unuse();
      destination()->Split(below_equal);
    } else {
      // Uncommon case: typeof testing against a string literal that is
      // never returned from the typeof operator.
      answer.Unuse();
      destination()->Goto(false);
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
      __ testq(answer.reg(), answer.reg());
      answer.Unuse();
      destination()->Split(zero);
      return;
    }
    default:
      UNREACHABLE();
  }
  Load(left);
  Load(right);
  Comparison(cc, strict, destination());
}


void CodeGenerator::VisitThisFunction(ThisFunction* node) {
  frame_->PushFunction();
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

// -----------------------------------------------------------------------------
// CodeGenerator implementation of Expressions

void CodeGenerator::Load(Expression* x, TypeofState typeof_state) {
#ifdef DEBUG
  int original_height = frame_->height();
#endif
  ASSERT(!in_spilled_code());
  JumpTarget true_target;
  JumpTarget false_target;
  ControlDestination dest(&true_target, &false_target, true);
  LoadCondition(x, typeof_state, &dest, false);

  if (dest.false_was_fall_through()) {
    // The false target was just bound.
    JumpTarget loaded;
    frame_->Push(Factory::false_value());
    // There may be dangling jumps to the true target.
    if (true_target.is_linked()) {
      loaded.Jump();
      true_target.Bind();
      frame_->Push(Factory::true_value());
      loaded.Bind();
    }

  } else if (dest.is_used()) {
    // There is true, and possibly false, control flow (with true as
    // the fall through).
    JumpTarget loaded;
    frame_->Push(Factory::true_value());
    if (false_target.is_linked()) {
      loaded.Jump();
      false_target.Bind();
      frame_->Push(Factory::false_value());
      loaded.Bind();
    }

  } else {
    // We have a valid value on top of the frame, but we still may
    // have dangling jumps to the true and false targets from nested
    // subexpressions (eg, the left subexpressions of the
    // short-circuited boolean operators).
    ASSERT(has_valid_frame());
    if (true_target.is_linked() || false_target.is_linked()) {
      JumpTarget loaded;
      loaded.Jump();  // Don't lose the current TOS.
      if (true_target.is_linked()) {
        true_target.Bind();
        frame_->Push(Factory::true_value());
        if (false_target.is_linked()) {
          loaded.Jump();
        }
      }
      if (false_target.is_linked()) {
        false_target.Bind();
        frame_->Push(Factory::false_value());
      }
      loaded.Bind();
    }
  }

  ASSERT(has_valid_frame());
  ASSERT(frame_->height() == original_height + 1);
}


// Emit code to load the value of an expression to the top of the
// frame. If the expression is boolean-valued it may be compiled (or
// partially compiled) into control flow to the control destination.
// If force_control is true, control flow is forced.
void CodeGenerator::LoadCondition(Expression* x,
                                  TypeofState typeof_state,
                                  ControlDestination* dest,
                                  bool force_control) {
  ASSERT(!in_spilled_code());
  int original_height = frame_->height();

  { CodeGenState new_state(this, typeof_state, dest);
    Visit(x);

    // If we hit a stack overflow, we may not have actually visited
    // the expression.  In that case, we ensure that we have a
    // valid-looking frame state because we will continue to generate
    // code as we unwind the C++ stack.
    //
    // It's possible to have both a stack overflow and a valid frame
    // state (eg, a subexpression overflowed, visiting it returned
    // with a dummied frame state, and visiting this expression
    // returned with a normal-looking state).
    if (HasStackOverflow() &&
        !dest->is_used() &&
        frame_->height() == original_height) {
      dest->Goto(true);
    }
  }

  if (force_control && !dest->is_used()) {
    // Convert the TOS value into flow to the control destination.
    // TODO(X64): Make control flow to control destinations work.
    ToBoolean(dest);
  }

  ASSERT(!(force_control && !dest->is_used()));
  ASSERT(dest->is_used() || frame_->height() == original_height + 1);
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
void CodeGenerator::ToBoolean(ControlDestination* dest) {
  Comment cmnt(masm_, "[ ToBoolean");

  // The value to convert should be popped from the frame.
  Result value = frame_->Pop();
  value.ToRegister();
  // Fast case checks.

  // 'false' => false.
  __ Cmp(value.reg(), Factory::false_value());
  dest->false_target()->Branch(equal);

  // 'true' => true.
  __ Cmp(value.reg(), Factory::true_value());
  dest->true_target()->Branch(equal);

  // 'undefined' => false.
  __ Cmp(value.reg(), Factory::undefined_value());
  dest->false_target()->Branch(equal);

  // Smi => false iff zero.
  ASSERT(kSmiTag == 0);
  __ testq(value.reg(), value.reg());
  dest->false_target()->Branch(zero);
  __ testl(value.reg(), Immediate(kSmiTagMask));
  dest->true_target()->Branch(zero);

  // Call the stub for all other cases.
  frame_->Push(&value);  // Undo the Pop() from above.
  ToBooleanStub stub;
  Result temp = frame_->CallStub(&stub, 1);
  // Convert the result to a condition code.
  __ testq(temp.reg(), temp.reg());
  temp.Unuse();
  dest->Split(not_equal);
}


void CodeGenerator::LoadUnsafeSmi(Register target, Handle<Object> value) {
  UNIMPLEMENTED();
  // TODO(X64): Implement security policy for loads of smis.
}


bool CodeGenerator::IsUnsafeSmi(Handle<Object> value) {
  return false;
}

//------------------------------------------------------------------------------
// CodeGenerator implementation of variables, lookups, and stores.

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
      LoadGlobal();
      ref->set_type(Reference::NAMED);
    } else {
      ASSERT(var->slot() != NULL);
      ref->set_type(Reference::SLOT);
    }
  } else {
    // Anything else is a runtime error.
    Load(e);
    // frame_->CallRuntime(Runtime::kThrowReferenceError, 1);
  }

  in_spilled_code_ = was_in_spilled_code;
}


void CodeGenerator::UnloadReference(Reference* ref) {
  // Pop a reference from the stack while preserving TOS.
  Comment cmnt(masm_, "[ UnloadReference");
  frame_->Nip(ref->size());
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
      ASSERT(!tmp.is(rsi));  // do not overwrite context register
      Register context = rsi;
      int chain_length = scope()->ContextChainLength(slot->var()->scope());
      for (int i = 0; i < chain_length; i++) {
        // Load the closure.
        // (All contexts, even 'with' contexts, have a closure,
        // and it is the same for all contexts inside a function.
        // There is no need to go to the function context first.)
        __ movq(tmp, ContextOperand(context, Context::CLOSURE_INDEX));
        // Load the function context (which is the incoming, outer context).
        __ movq(tmp, FieldOperand(tmp, JSFunction::kContextOffset));
        context = tmp;
      }
      // We may have a 'with' context now. Get the function context.
      // (In fact this mov may never be the needed, since the scope analysis
      // may not permit a direct context access in this case and thus we are
      // always at a function context. However it is safe to dereference be-
      // cause the function context of a function context is itself. Before
      // deleting this mov we should try to create a counter-example first,
      // though...)
      __ movq(tmp, ContextOperand(context, Context::FCONTEXT_INDEX));
      return ContextOperand(tmp, index);
    }

    default:
      UNREACHABLE();
      return Operand(rsp, 0);
  }
}


Operand CodeGenerator::ContextSlotOperandCheckExtensions(Slot* slot,
                                                         Result tmp,
                                                         JumpTarget* slow) {
  UNIMPLEMENTED();
  return Operand(rsp, 0);
}


void CodeGenerator::LoadFromSlot(Slot* slot, TypeofState typeof_state) {
  if (slot->type() == Slot::LOOKUP) {
    ASSERT(slot->var()->is_dynamic());

    JumpTarget slow;
    JumpTarget done;
    Result value;

    // Generate fast-case code for variables that might be shadowed by
    // eval-introduced variables.  Eval is used a lot without
    // introducing variables.  In those cases, we do not want to
    // perform a runtime call for all variables in the scope
    // containing the eval.
    if (slot->var()->mode() == Variable::DYNAMIC_GLOBAL) {
      value = LoadFromGlobalSlotCheckExtensions(slot, typeof_state, &slow);
      // If there was no control flow to slow, we can exit early.
      if (!slow.is_linked()) {
        frame_->Push(&value);
        return;
      }

      done.Jump(&value);

    } else if (slot->var()->mode() == Variable::DYNAMIC_LOCAL) {
      Slot* potential_slot = slot->var()->local_if_not_shadowed()->slot();
      // Only generate the fast case for locals that rewrite to slots.
      // This rules out argument loads.
      if (potential_slot != NULL) {
        // Allocate a fresh register to use as a temp in
        // ContextSlotOperandCheckExtensions and to hold the result
        // value.
        value = allocator_->Allocate();
        ASSERT(value.is_valid());
        __ movq(value.reg(),
               ContextSlotOperandCheckExtensions(potential_slot,
                                                 value,
                                                 &slow));
        if (potential_slot->var()->mode() == Variable::CONST) {
          __ Cmp(value.reg(), Factory::the_hole_value());
          done.Branch(not_equal, &value);
          __ movq(value.reg(), Factory::undefined_value(),
                  RelocInfo::EMBEDDED_OBJECT);
        }
        // There is always control flow to slow from
        // ContextSlotOperandCheckExtensions so we have to jump around
        // it.
        done.Jump(&value);
      }
    }

    slow.Bind();
    // A runtime call is inevitable.  We eagerly sync frame elements
    // to memory so that we can push the arguments directly into place
    // on top of the frame.
    frame_->SyncRange(0, frame_->element_count() - 1);
    frame_->EmitPush(rsi);
    __ movq(kScratchRegister, slot->var()->name(), RelocInfo::EMBEDDED_OBJECT);
    frame_->EmitPush(kScratchRegister);
    if (typeof_state == INSIDE_TYPEOF) {
      // value =
      //    frame_->CallRuntime(Runtime::kLoadContextSlotNoReferenceError, 2);
    } else {
      // value = frame_->CallRuntime(Runtime::kLoadContextSlot, 2);
    }

    done.Bind(&value);
    frame_->Push(&value);

  } else if (slot->var()->mode() == Variable::CONST) {
    // Const slots may contain 'the hole' value (the constant hasn't been
    // initialized yet) which needs to be converted into the 'undefined'
    // value.
    //
    // We currently spill the virtual frame because constants use the
    // potentially unsafe direct-frame access of SlotOperand.
    VirtualFrame::SpilledScope spilled_scope;
    Comment cmnt(masm_, "[ Load const");
    JumpTarget exit;
    __ movq(rcx, SlotOperand(slot, rcx));
    __ Cmp(rcx, Factory::the_hole_value());
    exit.Branch(not_equal);
    __ movq(rcx, Factory::undefined_value(), RelocInfo::EMBEDDED_OBJECT);
    exit.Bind();
    frame_->EmitPush(rcx);

  } else if (slot->type() == Slot::PARAMETER) {
    frame_->PushParameterAt(slot->index());

  } else if (slot->type() == Slot::LOCAL) {
    frame_->PushLocalAt(slot->index());

  } else {
    // The other remaining slot types (LOOKUP and GLOBAL) cannot reach
    // here.
    //
    // The use of SlotOperand below is safe for an unspilled frame
    // because it will always be a context slot.
    ASSERT(slot->type() == Slot::CONTEXT);
    Result temp = allocator_->Allocate();
    ASSERT(temp.is_valid());
    __ movq(temp.reg(), SlotOperand(slot, temp.reg()));
    frame_->Push(&temp);
  }
}


void CodeGenerator::StoreToSlot(Slot* slot, InitState init_state) {
  // TODO(X64): Enable more types of slot.

  if (slot->type() == Slot::LOOKUP) {
    UNIMPLEMENTED();
    /*
    ASSERT(slot->var()->is_dynamic());

    // For now, just do a runtime call.  Since the call is inevitable,
    // we eagerly sync the virtual frame so we can directly push the
    // arguments into place.
    frame_->SyncRange(0, frame_->element_count() - 1);

    frame_->EmitPush(esi);
    frame_->EmitPush(Immediate(slot->var()->name()));

    Result value;
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
    */
  } else {
    ASSERT(!slot->var()->is_dynamic());

    JumpTarget exit;
    if (init_state == CONST_INIT) {
      ASSERT(slot->var()->mode() == Variable::CONST);
      // Only the first const initialization must be executed (the slot
      // still contains 'the hole' value). When the assignment is executed,
      // the code is identical to a normal store (see below).
      //
      // We spill the frame in the code below because the direct-frame
      // access of SlotOperand is potentially unsafe with an unspilled
      // frame.
      VirtualFrame::SpilledScope spilled_scope;
      Comment cmnt(masm_, "[ Init const");
      __ movq(rcx, SlotOperand(slot, rcx));
      __ Cmp(rcx, Factory::the_hole_value());
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
      //
      // The use of SlotOperand below is safe for an unspilled frame
      // because the slot is a context slot.
      ASSERT(slot->type() == Slot::CONTEXT);
      frame_->Dup();
      Result value = frame_->Pop();
      value.ToRegister();
      Result start = allocator_->Allocate();
      ASSERT(start.is_valid());
      __ movq(SlotOperand(slot, start.reg()), value.reg());
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

    exit.Bind();
  }
}


Result CodeGenerator::LoadFromGlobalSlotCheckExtensions(
    Slot* slot,
    TypeofState typeof_state,
    JumpTarget* slow) {
  UNIMPLEMENTED();
  return Result(rax);
}


void CodeGenerator::LoadGlobal() {
  if (in_spilled_code()) {
    frame_->EmitPush(GlobalObject());
  } else {
    Result temp = allocator_->Allocate();
    __ movq(temp.reg(), GlobalObject());
    frame_->Push(&temp);
  }
}


void CodeGenerator::LoadGlobalReceiver() {
  Result temp = allocator_->Allocate();
  Register reg = temp.reg();
  __ movq(reg, GlobalObject());
  __ movq(reg, FieldOperand(reg, GlobalObject::kGlobalReceiverOffset));
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

  // Branch to the label if the given object isn't a symbol.
  void BranchIfNonSymbol(MacroAssembler* masm,
                         Label* label,
                         Register object);

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
                               ControlDestination* dest) {
  // Strict only makes sense for equality comparisons.
  ASSERT(!strict || cc == equal);

  Result left_side;
  Result right_side;
  // Implement '>' and '<=' by reversal to obtain ECMA-262 conversion order.
  if (cc == greater || cc == less_equal) {
    cc = ReverseCondition(cc);
    left_side = frame_->Pop();
    right_side = frame_->Pop();
  } else {
    right_side = frame_->Pop();
    left_side = frame_->Pop();
  }
  ASSERT(cc == less || cc == equal || cc == greater_equal);

  // If either side is a constant smi, optimize the comparison.
  bool left_side_constant_smi =
      left_side.is_constant() && left_side.handle()->IsSmi();
  bool right_side_constant_smi =
      right_side.is_constant() && right_side.handle()->IsSmi();
  bool left_side_constant_null =
      left_side.is_constant() && left_side.handle()->IsNull();
  bool right_side_constant_null =
      right_side.is_constant() && right_side.handle()->IsNull();

  if (left_side_constant_smi || right_side_constant_smi) {
    if (left_side_constant_smi && right_side_constant_smi) {
      // Trivial case, comparing two constants.
      int left_value = Smi::cast(*left_side.handle())->value();
      int right_value = Smi::cast(*right_side.handle())->value();
      switch (cc) {
        case less:
          dest->Goto(left_value < right_value);
          break;
        case equal:
          dest->Goto(left_value == right_value);
          break;
        case greater_equal:
          dest->Goto(left_value >= right_value);
          break;
        default:
          UNREACHABLE();
      }
    } else {  // Only one side is a constant Smi.
      // If left side is a constant Smi, reverse the operands.
      // Since one side is a constant Smi, conversion order does not matter.
      if (left_side_constant_smi) {
        Result temp = left_side;
        left_side = right_side;
        right_side = temp;
        cc = ReverseCondition(cc);
        // This may reintroduce greater or less_equal as the value of cc.
        // CompareStub and the inline code both support all values of cc.
      }
      // Implement comparison against a constant Smi, inlining the case
      // where both sides are Smis.
      left_side.ToRegister();

      // Here we split control flow to the stub call and inlined cases
      // before finally splitting it to the control destination.  We use
      // a jump target and branching to duplicate the virtual frame at
      // the first split.  We manually handle the off-frame references
      // by reconstituting them on the non-fall-through path.
      JumpTarget is_smi;
      Register left_reg = left_side.reg();
      Handle<Object> right_val = right_side.handle();
      __ testl(left_side.reg(), Immediate(kSmiTagMask));
      is_smi.Branch(zero, taken);

      // Setup and call the compare stub.
      CompareStub stub(cc, strict);
      Result result = frame_->CallStub(&stub, &left_side, &right_side);
      result.ToRegister();
      __ cmpq(result.reg(), Immediate(0));
      result.Unuse();
      dest->true_target()->Branch(cc);
      dest->false_target()->Jump();

      is_smi.Bind();
      left_side = Result(left_reg);
      right_side = Result(right_val);
      // Test smi equality and comparison by signed int comparison.
      if (IsUnsafeSmi(right_side.handle())) {
        right_side.ToRegister();
        __ cmpq(left_side.reg(), right_side.reg());
      } else {
        __ Cmp(left_side.reg(), right_side.handle());
      }
      left_side.Unuse();
      right_side.Unuse();
      dest->Split(cc);
    }
  } else if (cc == equal &&
             (left_side_constant_null || right_side_constant_null)) {
    // To make null checks efficient, we check if either the left side or
    // the right side is the constant 'null'.
    // If so, we optimize the code by inlining a null check instead of
    // calling the (very) general runtime routine for checking equality.
    Result operand = left_side_constant_null ? right_side : left_side;
    right_side.Unuse();
    left_side.Unuse();
    operand.ToRegister();
    __ Cmp(operand.reg(), Factory::null_value());
    if (strict) {
      operand.Unuse();
      dest->Split(equal);
    } else {
      // The 'null' value is only equal to 'undefined' if using non-strict
      // comparisons.
      dest->true_target()->Branch(equal);
      __ Cmp(operand.reg(), Factory::undefined_value());
      dest->true_target()->Branch(equal);
      __ testl(operand.reg(), Immediate(kSmiTagMask));
      dest->false_target()->Branch(equal);

      // It can be an undetectable object.
      // Use a scratch register in preference to spilling operand.reg().
      Result temp = allocator()->Allocate();
      ASSERT(temp.is_valid());
      __ movq(temp.reg(),
             FieldOperand(operand.reg(), HeapObject::kMapOffset));
      __ testb(FieldOperand(temp.reg(), Map::kBitFieldOffset),
               Immediate(1 << Map::kIsUndetectable));
      temp.Unuse();
      operand.Unuse();
      dest->Split(not_zero);
    }
  } else {  // Neither side is a constant Smi or null.
    // If either side is a non-smi constant, skip the smi check.
    bool known_non_smi =
        (left_side.is_constant() && !left_side.handle()->IsSmi()) ||
        (right_side.is_constant() && !right_side.handle()->IsSmi());
    left_side.ToRegister();
    right_side.ToRegister();

    if (known_non_smi) {
      // When non-smi, call out to the compare stub.
      CompareStub stub(cc, strict);
      Result answer = frame_->CallStub(&stub, &left_side, &right_side);
      if (cc == equal) {
        __ testq(answer.reg(), answer.reg());
      } else {
        __ cmpq(answer.reg(), Immediate(0));
      }
      answer.Unuse();
      dest->Split(cc);
    } else {
      // Here we split control flow to the stub call and inlined cases
      // before finally splitting it to the control destination.  We use
      // a jump target and branching to duplicate the virtual frame at
      // the first split.  We manually handle the off-frame references
      // by reconstituting them on the non-fall-through path.
      JumpTarget is_smi;
      Register left_reg = left_side.reg();
      Register right_reg = right_side.reg();

      __ movq(kScratchRegister, left_side.reg());
      __ or_(kScratchRegister, right_side.reg());
      __ testl(kScratchRegister, Immediate(kSmiTagMask));
      is_smi.Branch(zero, taken);
      // When non-smi, call out to the compare stub.
      CompareStub stub(cc, strict);
      Result answer = frame_->CallStub(&stub, &left_side, &right_side);
      if (cc == equal) {
        __ testq(answer.reg(), answer.reg());
      } else {
        __ cmpq(answer.reg(), Immediate(0));
      }
      answer.Unuse();
      dest->true_target()->Branch(cc);
      dest->false_target()->Jump();

      is_smi.Bind();
      left_side = Result(left_reg);
      right_side = Result(right_reg);
      __ cmpq(left_side.reg(), right_side.reg());
      right_side.Unuse();
      left_side.Unuse();
      dest->Split(cc);
    }
  }
}


// Flag that indicates whether or not the code that handles smi arguments
// should be placed in the stub, inlined, or omitted entirely.
enum GenericBinaryFlags {
  SMI_CODE_IN_STUB,
  SMI_CODE_INLINED
};


class FloatingPointHelper : public AllStatic {
 public:
  // Code pattern for loading a floating point value. Input value must
  // be either a smi or a heap number object (fp value). Requirements:
  // operand in src register. Returns operand as floating point number
  // in XMM register
  static void LoadFloatOperand(MacroAssembler* masm,
                               Register src,
                               XMMRegister dst);
  // Code pattern for loading floating point values. Input values must
  // be either smi or heap number objects (fp values). Requirements:
  // operand_1 on TOS+1 , operand_2 on TOS+2; Returns operands as
  // floating point numbers in XMM registers.
  static void LoadFloatOperands(MacroAssembler* masm,
                                XMMRegister dst1,
                                XMMRegister dst2);

  // Code pattern for loading floating point values onto the fp stack.
  // Input values must be either smi or heap number objects (fp values).
  // Requirements:
  // operand_1 on TOS+1 , operand_2 on TOS+2; Returns operands as
  // floating point numbers on fp stack.
  static void LoadFloatOperands(MacroAssembler* masm);

  // Code pattern for loading a floating point value and converting it
  // to a 32 bit integer. Input value must be either a smi or a heap number
  // object.
  // Returns operands as 32-bit sign extended integers in a general purpose
  // registers.
  static void LoadInt32Operand(MacroAssembler* masm,
                               const Operand& src,
                               Register dst);

  // Test if operands are smi or number objects (fp). Requirements:
  // operand_1 in eax, operand_2 in edx; falls through on float
  // operands, jumps to the non_float label otherwise.
  static void CheckFloatOperands(MacroAssembler* masm,
                                 Label* non_float);
  // Allocate a heap number in new space with undefined value.
  // Returns tagged pointer in result, or jumps to need_gc if new space is full.
  static void AllocateHeapNumber(MacroAssembler* masm,
                                 Label* need_gc,
                                 Register scratch,
                                 Register result);
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


class DeferredInlineBinaryOperation: public DeferredCode {
 public:
  DeferredInlineBinaryOperation(Token::Value op,
                                Register dst,
                                Register left,
                                Register right,
                                OverwriteMode mode)
      : op_(op), dst_(dst), left_(left), right_(right), mode_(mode) {
    set_comment("[ DeferredInlineBinaryOperation");
  }

  virtual void Generate();

 private:
  Token::Value op_;
  Register dst_;
  Register left_;
  Register right_;
  OverwriteMode mode_;
};


void DeferredInlineBinaryOperation::Generate() {
  __ push(left_);
  __ push(right_);
  GenericBinaryOpStub stub(op_, mode_, SMI_CODE_INLINED);
  __ CallStub(&stub);
  if (!dst_.is(rax)) __ movq(dst_, rax);
}


void CodeGenerator::GenericBinaryOperation(Token::Value op,
                                           SmiAnalysis* type,
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

  Result right = frame_->Pop();
  Result left = frame_->Pop();

  if (op == Token::ADD) {
    bool left_is_string = left.is_constant() && left.handle()->IsString();
    bool right_is_string = right.is_constant() && right.handle()->IsString();
    if (left_is_string || right_is_string) {
      frame_->Push(&left);
      frame_->Push(&right);
      Result answer;
      if (left_is_string) {
        if (right_is_string) {
          // TODO(lrn): if both are constant strings
          // -- do a compile time cons, if allocation during codegen is allowed.
          answer = frame_->CallRuntime(Runtime::kStringAdd, 2);
        } else {
          answer =
            frame_->InvokeBuiltin(Builtins::STRING_ADD_LEFT, CALL_FUNCTION, 2);
        }
      } else if (right_is_string) {
        answer =
          frame_->InvokeBuiltin(Builtins::STRING_ADD_RIGHT, CALL_FUNCTION, 2);
      }
      frame_->Push(&answer);
      return;
    }
    // Neither operand is known to be a string.
  }

  bool left_is_smi = left.is_constant() && left.handle()->IsSmi();
  bool left_is_non_smi = left.is_constant() && !left.handle()->IsSmi();
  bool right_is_smi = right.is_constant() && right.handle()->IsSmi();
  bool right_is_non_smi = right.is_constant() && !right.handle()->IsSmi();
  bool generate_no_smi_code = false;  // No smi code at all, inline or in stub.

  if (left_is_smi && right_is_smi) {
    // Compute the constant result at compile time, and leave it on the frame.
    int left_int = Smi::cast(*left.handle())->value();
    int right_int = Smi::cast(*right.handle())->value();
    if (FoldConstantSmis(op, left_int, right_int)) return;
  }

  if (left_is_non_smi || right_is_non_smi) {
    // Set flag so that we go straight to the slow case, with no smi code.
    generate_no_smi_code = true;
  } else if (right_is_smi) {
    ConstantSmiBinaryOperation(op, &left, right.handle(),
                               type, false, overwrite_mode);
    return;
  } else if (left_is_smi) {
    ConstantSmiBinaryOperation(op, &right, left.handle(),
                               type, true, overwrite_mode);
    return;
  }

  if (flags == SMI_CODE_INLINED && !generate_no_smi_code) {
    LikelySmiBinaryOperation(op, &left, &right, overwrite_mode);
  } else {
    frame_->Push(&left);
    frame_->Push(&right);
    // If we know the arguments aren't smis, use the binary operation stub
    // that does not check for the fast smi case.
    // The same stub is used for NO_SMI_CODE and SMI_CODE_INLINED.
    if (generate_no_smi_code) {
      flags = SMI_CODE_INLINED;
    }
    GenericBinaryOpStub stub(op, overwrite_mode, flags);
    Result answer = frame_->CallStub(&stub, 2);
    frame_->Push(&answer);
  }
}


// Emit a LoadIC call to get the value from receiver and leave it in
// dst.  The receiver register is restored after the call.
class DeferredReferenceGetNamedValue: public DeferredCode {
 public:
  DeferredReferenceGetNamedValue(Register dst,
                                 Register receiver,
                                 Handle<String> name)
      : dst_(dst), receiver_(receiver),  name_(name) {
    set_comment("[ DeferredReferenceGetNamedValue");
  }

  virtual void Generate();

  Label* patch_site() { return &patch_site_; }

 private:
  Label patch_site_;
  Register dst_;
  Register receiver_;
  Handle<String> name_;
};


void DeferredReferenceGetNamedValue::Generate() {
  __ push(receiver_);
  __ Move(rcx, name_);
  Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
  __ Call(ic, RelocInfo::CODE_TARGET);
  // The call must be followed by a test eax instruction to indicate
  // that the inobject property case was inlined.
  //
  // Store the delta to the map check instruction here in the test
  // instruction.  Use masm_-> instead of the __ macro since the
  // latter can't return a value.
  int delta_to_patch_site = masm_->SizeOfCodeGeneratedSince(patch_site());
  // Here we use masm_-> instead of the __ macro because this is the
  // instruction that gets patched and coverage code gets in the way.
  masm_->testq(rax, Immediate(-delta_to_patch_site));
  __ IncrementCounter(&Counters::named_load_inline_miss, 1);

  if (!dst_.is(rax)) __ movq(dst_, rax);
  __ pop(receiver_);
}




// The result of src + value is in dst.  It either overflowed or was not
// smi tagged.  Undo the speculative addition and call the appropriate
// specialized stub for add.  The result is left in dst.
class DeferredInlineSmiAdd: public DeferredCode {
 public:
  DeferredInlineSmiAdd(Register dst,
                       Smi* value,
                       OverwriteMode overwrite_mode)
      : dst_(dst), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlineSmiAdd");
  }

  virtual void Generate();

 private:
  Register dst_;
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlineSmiAdd::Generate() {
  // Undo the optimistic add operation and call the shared stub.
  __ subq(dst_, Immediate(value_));
  __ push(dst_);
  __ push(Immediate(value_));
  GenericBinaryOpStub igostub(Token::ADD, overwrite_mode_, SMI_CODE_INLINED);
  __ CallStub(&igostub);
  if (!dst_.is(rax)) __ movq(dst_, rax);
}


// The result of value + src is in dst.  It either overflowed or was not
// smi tagged.  Undo the speculative addition and call the appropriate
// specialized stub for add.  The result is left in dst.
class DeferredInlineSmiAddReversed: public DeferredCode {
 public:
  DeferredInlineSmiAddReversed(Register dst,
                               Smi* value,
                               OverwriteMode overwrite_mode)
      : dst_(dst), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlineSmiAddReversed");
  }

  virtual void Generate();

 private:
  Register dst_;
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlineSmiAddReversed::Generate() {
  // Undo the optimistic add operation and call the shared stub.
  __ subq(dst_, Immediate(value_));
  __ push(Immediate(value_));
  __ push(dst_);
  GenericBinaryOpStub igostub(Token::ADD, overwrite_mode_, SMI_CODE_INLINED);
  __ CallStub(&igostub);
  if (!dst_.is(rax)) __ movq(dst_, rax);
}


// The result of src - value is in dst.  It either overflowed or was not
// smi tagged.  Undo the speculative subtraction and call the
// appropriate specialized stub for subtract.  The result is left in
// dst.
class DeferredInlineSmiSub: public DeferredCode {
 public:
  DeferredInlineSmiSub(Register dst,
                       Smi* value,
                       OverwriteMode overwrite_mode)
      : dst_(dst), value_(value), overwrite_mode_(overwrite_mode) {
    set_comment("[ DeferredInlineSmiSub");
  }

  virtual void Generate();

 private:
  Register dst_;
  Smi* value_;
  OverwriteMode overwrite_mode_;
};


void DeferredInlineSmiSub::Generate() {
  // Undo the optimistic sub operation and call the shared stub.
  __ addq(dst_, Immediate(value_));
  __ push(dst_);
  __ push(Immediate(value_));
  GenericBinaryOpStub igostub(Token::SUB, overwrite_mode_, SMI_CODE_INLINED);
  __ CallStub(&igostub);
  if (!dst_.is(rax)) __ movq(dst_, rax);
}


void CodeGenerator::ConstantSmiBinaryOperation(Token::Value op,
                                               Result* operand,
                                               Handle<Object> value,
                                               SmiAnalysis* type,
                                               bool reversed,
                                               OverwriteMode overwrite_mode) {
  // NOTE: This is an attempt to inline (a bit) more of the code for
  // some possible smi operations (like + and -) when (at least) one
  // of the operands is a constant smi.
  // Consumes the argument "operand".

  // TODO(199): Optimize some special cases of operations involving a
  // smi literal (multiply by 2, shift by 0, etc.).
  if (IsUnsafeSmi(value)) {
    Result unsafe_operand(value);
    if (reversed) {
      LikelySmiBinaryOperation(op, &unsafe_operand, operand,
                               overwrite_mode);
    } else {
      LikelySmiBinaryOperation(op, operand, &unsafe_operand,
                               overwrite_mode);
    }
    ASSERT(!operand->is_valid());
    return;
  }

  // Get the literal value.
  Smi* smi_value = Smi::cast(*value);

  switch (op) {
    case Token::ADD: {
      operand->ToRegister();
      frame_->Spill(operand->reg());

      // Optimistically add.  Call the specialized add stub if the
      // result is not a smi or overflows.
      DeferredCode* deferred = NULL;
      if (reversed) {
        deferred = new DeferredInlineSmiAddReversed(operand->reg(),
                                                    smi_value,
                                                    overwrite_mode);
      } else {
        deferred = new DeferredInlineSmiAdd(operand->reg(),
                                            smi_value,
                                            overwrite_mode);
      }
      __ movq(kScratchRegister, value, RelocInfo::NONE);
      __ addl(operand->reg(), kScratchRegister);
      deferred->Branch(overflow);
      __ testl(operand->reg(), Immediate(kSmiTagMask));
      deferred->Branch(not_zero);
      deferred->BindExit();
      frame_->Push(operand);
      break;
    }
    // TODO(X64): Move other implementations from ia32 to here.
    default: {
      Result constant_operand(value);
      if (reversed) {
        LikelySmiBinaryOperation(op, &constant_operand, operand,
                                 overwrite_mode);
      } else {
        LikelySmiBinaryOperation(op, operand, &constant_operand,
                                 overwrite_mode);
      }
      break;
    }
  }
  ASSERT(!operand->is_valid());
}

void CodeGenerator::LikelySmiBinaryOperation(Token::Value op,
                                             Result* left,
                                             Result* right,
                                             OverwriteMode overwrite_mode) {
  // Special handling of div and mod because they use fixed registers.
  if (op == Token::DIV || op == Token::MOD) {
    // We need rax as the quotient register, rdx as the remainder
    // register, neither left nor right in rax or rdx, and left copied
    // to rax.
    Result quotient;
    Result remainder;
    bool left_is_in_rax = false;
    // Step 1: get rax for quotient.
    if ((left->is_register() && left->reg().is(rax)) ||
        (right->is_register() && right->reg().is(rax))) {
      // One or both is in rax.  Use a fresh non-rdx register for
      // them.
      Result fresh = allocator_->Allocate();
      ASSERT(fresh.is_valid());
      if (fresh.reg().is(rdx)) {
        remainder = fresh;
        fresh = allocator_->Allocate();
        ASSERT(fresh.is_valid());
      }
      if (left->is_register() && left->reg().is(rax)) {
        quotient = *left;
        *left = fresh;
        left_is_in_rax = true;
      }
      if (right->is_register() && right->reg().is(rax)) {
        quotient = *right;
        *right = fresh;
      }
      __ movq(fresh.reg(), rax);
    } else {
      // Neither left nor right is in rax.
      quotient = allocator_->Allocate(rax);
    }
    ASSERT(quotient.is_register() && quotient.reg().is(rax));
    ASSERT(!(left->is_register() && left->reg().is(rax)));
    ASSERT(!(right->is_register() && right->reg().is(rax)));

    // Step 2: get rdx for remainder if necessary.
    if (!remainder.is_valid()) {
      if ((left->is_register() && left->reg().is(rdx)) ||
          (right->is_register() && right->reg().is(rdx))) {
        Result fresh = allocator_->Allocate();
        ASSERT(fresh.is_valid());
        if (left->is_register() && left->reg().is(rdx)) {
          remainder = *left;
          *left = fresh;
        }
        if (right->is_register() && right->reg().is(rdx)) {
          remainder = *right;
          *right = fresh;
        }
        __ movq(fresh.reg(), rdx);
      } else {
        // Neither left nor right is in rdx.
        remainder = allocator_->Allocate(rdx);
      }
    }
    ASSERT(remainder.is_register() && remainder.reg().is(rdx));
    ASSERT(!(left->is_register() && left->reg().is(rdx)));
    ASSERT(!(right->is_register() && right->reg().is(rdx)));

    left->ToRegister();
    right->ToRegister();
    frame_->Spill(rax);
    frame_->Spill(rdx);

    // Check that left and right are smi tagged.
    DeferredInlineBinaryOperation* deferred =
        new DeferredInlineBinaryOperation(op,
                                          (op == Token::DIV) ? rax : rdx,
                                          left->reg(),
                                          right->reg(),
                                          overwrite_mode);
    if (left->reg().is(right->reg())) {
      __ testl(left->reg(), Immediate(kSmiTagMask));
    } else {
      // Use the quotient register as a scratch for the tag check.
      if (!left_is_in_rax) __ movq(rax, left->reg());
      left_is_in_rax = false;  // About to destroy the value in rax.
      __ or_(rax, right->reg());
      ASSERT(kSmiTag == 0);  // Adjust test if not the case.
      __ testl(rax, Immediate(kSmiTagMask));
    }
    deferred->Branch(not_zero);

    if (!left_is_in_rax) __ movq(rax, left->reg());
    // Sign extend rax into rdx:rax.
    __ cqo();
    // Check for 0 divisor.
    __ testq(right->reg(), right->reg());
    deferred->Branch(zero);
    // Divide rdx:rax by the right operand.
    __ idiv(right->reg());

    // Complete the operation.
    if (op == Token::DIV) {
      // Check for negative zero result.  If result is zero, and divisor
      // is negative, return a floating point negative zero.  The
      // virtual frame is unchanged in this block, so local control flow
      // can use a Label rather than a JumpTarget.
      Label non_zero_result;
      __ testq(left->reg(), left->reg());
      __ j(not_zero, &non_zero_result);
      __ testq(right->reg(), right->reg());
      deferred->Branch(negative);
      __ bind(&non_zero_result);
      // Check for the corner case of dividing the most negative smi by
      // -1. We cannot use the overflow flag, since it is not set by
      // idiv instruction.
      ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
      __ cmpq(rax, Immediate(0x40000000));
      deferred->Branch(equal);
      // Check that the remainder is zero.
      __ testq(rdx, rdx);
      deferred->Branch(not_zero);
      // Tag the result and store it in the quotient register.
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(rax, Operand(rax, rax, times_1, kSmiTag));
      deferred->BindExit();
      left->Unuse();
      right->Unuse();
      frame_->Push(&quotient);
    } else {
      ASSERT(op == Token::MOD);
      // Check for a negative zero result.  If the result is zero, and
      // the dividend is negative, return a floating point negative
      // zero.  The frame is unchanged in this block, so local control
      // flow can use a Label rather than a JumpTarget.
      Label non_zero_result;
      __ testq(rdx, rdx);
      __ j(not_zero, &non_zero_result);
      __ testq(left->reg(), left->reg());
      deferred->Branch(negative);
      __ bind(&non_zero_result);
      deferred->BindExit();
      left->Unuse();
      right->Unuse();
      frame_->Push(&remainder);
    }
    return;
  }

  // Special handling of shift operations because they use fixed
  // registers.
  if (op == Token::SHL || op == Token::SHR || op == Token::SAR) {
    // Move left out of rcx if necessary.
    if (left->is_register() && left->reg().is(rcx)) {
      *left = allocator_->Allocate();
      ASSERT(left->is_valid());
      __ movq(left->reg(), rcx);
    }
    right->ToRegister(rcx);
    left->ToRegister();
    ASSERT(left->is_register() && !left->reg().is(rcx));
    ASSERT(right->is_register() && right->reg().is(rcx));

    // We will modify right, it must be spilled.
    frame_->Spill(rcx);

    // Use a fresh answer register to avoid spilling the left operand.
    Result answer = allocator_->Allocate();
    ASSERT(answer.is_valid());
    // Check that both operands are smis using the answer register as a
    // temporary.
    DeferredInlineBinaryOperation* deferred =
        new DeferredInlineBinaryOperation(op,
                                          answer.reg(),
                                          left->reg(),
                                          rcx,
                                          overwrite_mode);
    __ movq(answer.reg(), left->reg());
    __ or_(answer.reg(), rcx);
    __ testl(answer.reg(), Immediate(kSmiTagMask));
    deferred->Branch(not_zero);

    // Untag both operands.
    __ movq(answer.reg(), left->reg());
    __ sar(answer.reg(), Immediate(kSmiTagSize));
    __ sar(rcx, Immediate(kSmiTagSize));
    // Perform the operation.
    switch (op) {
      case Token::SAR:
        __ sar(answer.reg());
        // No checks of result necessary
        break;
      case Token::SHR: {
        Label result_ok;
        __ shr(answer.reg());
        // Check that the *unsigned* result fits in a smi.  Neither of
        // the two high-order bits can be set:
        //  * 0x80000000: high bit would be lost when smi tagging.
        //  * 0x40000000: this number would convert to negative when smi
        //    tagging.
        // These two cases can only happen with shifts by 0 or 1 when
        // handed a valid smi.  If the answer cannot be represented by a
        // smi, restore the left and right arguments, and jump to slow
        // case.  The low bit of the left argument may be lost, but only
        // in a case where it is dropped anyway.
        __ testl(answer.reg(), Immediate(0xc0000000));
        __ j(zero, &result_ok);
        ASSERT(kSmiTag == 0);
        __ shl(rcx, Immediate(kSmiTagSize));
        deferred->Jump();
        __ bind(&result_ok);
        break;
      }
      case Token::SHL: {
        Label result_ok;
        __ shl(answer.reg());
        // Check that the *signed* result fits in a smi.
        __ cmpq(answer.reg(), Immediate(0xc0000000));
        __ j(positive, &result_ok);
        ASSERT(kSmiTag == 0);
        __ shl(rcx, Immediate(kSmiTagSize));
        deferred->Jump();
        __ bind(&result_ok);
        break;
      }
      default:
        UNREACHABLE();
    }
    // Smi-tag the result in answer.
    ASSERT(kSmiTagSize == 1);  // Adjust code if not the case.
    __ lea(answer.reg(),
           Operand(answer.reg(), answer.reg(), times_1, kSmiTag));
    deferred->BindExit();
    left->Unuse();
    right->Unuse();
    frame_->Push(&answer);
    return;
  }

  // Handle the other binary operations.
  left->ToRegister();
  right->ToRegister();
  // A newly allocated register answer is used to hold the answer.  The
  // registers containing left and right are not modified so they don't
  // need to be spilled in the fast case.
  Result answer = allocator_->Allocate();
  ASSERT(answer.is_valid());

  // Perform the smi tag check.
  DeferredInlineBinaryOperation* deferred =
      new DeferredInlineBinaryOperation(op,
                                        answer.reg(),
                                        left->reg(),
                                        right->reg(),
                                        overwrite_mode);
  if (left->reg().is(right->reg())) {
    __ testl(left->reg(), Immediate(kSmiTagMask));
  } else {
    __ movq(answer.reg(), left->reg());
    __ or_(answer.reg(), right->reg());
    ASSERT(kSmiTag == 0);  // Adjust test if not the case.
    __ testl(answer.reg(), Immediate(kSmiTagMask));
  }
  deferred->Branch(not_zero);
  __ movq(answer.reg(), left->reg());
  switch (op) {
    case Token::ADD:
      __ addl(answer.reg(), right->reg());  // Add optimistically.
      deferred->Branch(overflow);
      break;

    case Token::SUB:
      __ subl(answer.reg(), right->reg());  // Subtract optimistically.
      deferred->Branch(overflow);
      break;

    case Token::MUL: {
      // If the smi tag is 0 we can just leave the tag on one operand.
      ASSERT(kSmiTag == 0);  // Adjust code below if not the case.
      // Remove smi tag from the left operand (but keep sign).
      // Left-hand operand has been copied into answer.
      __ sar(answer.reg(), Immediate(kSmiTagSize));
      // Do multiplication of smis, leaving result in answer.
      __ imull(answer.reg(), right->reg());
      // Go slow on overflows.
      deferred->Branch(overflow);
      // Check for negative zero result.  If product is zero, and one
      // argument is negative, go to slow case.  The frame is unchanged
      // in this block, so local control flow can use a Label rather
      // than a JumpTarget.
      Label non_zero_result;
      __ testq(answer.reg(), answer.reg());
      __ j(not_zero, &non_zero_result);
      __ movq(answer.reg(), left->reg());
      __ or_(answer.reg(), right->reg());
      deferred->Branch(negative);
      __ xor_(answer.reg(), answer.reg());  // Positive 0 is correct.
      __ bind(&non_zero_result);
      break;
    }

    case Token::BIT_OR:
      __ or_(answer.reg(), right->reg());
      break;

    case Token::BIT_AND:
      __ and_(answer.reg(), right->reg());
      break;

    case Token::BIT_XOR:
      __ xor_(answer.reg(), right->reg());
      break;

    default:
      UNREACHABLE();
      break;
  }
  deferred->BindExit();
  left->Unuse();
  right->Unuse();
  frame_->Push(&answer);
}


#undef __
#define __ ACCESS_MASM(masm)


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
  ASSERT(cgen_->HasValidEntryRegisters());
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
      Variable* var = expression_->AsVariableProxy()->AsVariable();
      bool is_global = var != NULL;
      ASSERT(!is_global || var->is_global());

      // Do not inline the inobject property case for loads from the global
      // object.  Also do not inline for unoptimized code.  This saves time
      // in the code generator.  Unoptimized code is toplevel code or code
      // that is not in a loop.
      if (is_global ||
          cgen_->scope()->is_global_scope() ||
          cgen_->loop_nesting() == 0) {
        Comment cmnt(masm, "[ Load from named Property");
        cgen_->frame()->Push(GetName());

        RelocInfo::Mode mode = is_global
                               ? RelocInfo::CODE_TARGET_CONTEXT
                               : RelocInfo::CODE_TARGET;
        Result answer = cgen_->frame()->CallLoadIC(mode);
        // A test rax instruction following the call signals that the
        // inobject property case was inlined.  Ensure that there is not
        // a test rax instruction here.
        __ nop();
        cgen_->frame()->Push(&answer);
      } else {
        // Inline the inobject property case.
        Comment cmnt(masm, "[ Inlined named property load");
        Result receiver = cgen_->frame()->Pop();
        receiver.ToRegister();

        Result value = cgen_->allocator()->Allocate();
        ASSERT(value.is_valid());
        DeferredReferenceGetNamedValue* deferred =
            new DeferredReferenceGetNamedValue(value.reg(),
                                               receiver.reg(),
                                               GetName());

        // Check that the receiver is a heap object.
        __ testl(receiver.reg(), Immediate(kSmiTagMask));
        deferred->Branch(zero);

        __ bind(deferred->patch_site());
        // This is the map check instruction that will be patched (so we can't
        // use the double underscore macro that may insert instructions).
        // Initially use an invalid map to force a failure.
        masm->Move(kScratchRegister, Factory::null_value());
        masm->cmpq(FieldOperand(receiver.reg(), HeapObject::kMapOffset),
                   kScratchRegister);
        // This branch is always a forwards branch so it's always a fixed
        // size which allows the assert below to succeed and patching to work.
        deferred->Branch(not_equal);

        // The delta from the patch label to the load offset must be
        // statically known.
        ASSERT(masm->SizeOfCodeGeneratedSince(deferred->patch_site()) ==
               LoadIC::kOffsetToLoadInstruction);
        // The initial (invalid) offset has to be large enough to force
        // a 32-bit instruction encoding to allow patching with an
        // arbitrary offset.  Use kMaxInt (minus kHeapObjectTag).
        int offset = kMaxInt;
        masm->movq(value.reg(), FieldOperand(receiver.reg(), offset));

        __ IncrementCounter(&Counters::named_load_inline, 1);
        deferred->BindExit();
        cgen_->frame()->Push(&receiver);
        cgen_->frame()->Push(&value);
      }
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

      // TODO(x64): Implement inlined loads for keyed properties.
      //      Comment cmnt(masm, "[ Load from keyed Property");

      RelocInfo::Mode mode = is_global
        ? RelocInfo::CODE_TARGET_CONTEXT
        : RelocInfo::CODE_TARGET;
      Result answer = cgen_->frame()->CallKeyedLoadIC(mode);
      // Make sure that we do not have a test instruction after the
      // call.  A test instruction after the call is used to
      // indicate that we have generated an inline version of the
      // keyed load.  The explicit nop instruction is here because
      // the push that follows might be peep-hole optimized away.
      __ nop();
      cgen_->frame()->Push(&answer);
      break;
    }

    default:
      UNREACHABLE();
  }
}


void Reference::SetValue(InitState init_state) {
  ASSERT(cgen_->HasValidEntryRegisters());
  ASSERT(!is_illegal());
  MacroAssembler* masm = cgen_->masm();
  switch (type_) {
    case SLOT: {
      Comment cmnt(masm, "[ Store to Slot");
      Slot* slot = expression_->AsVariableProxy()->AsVariable()->slot();
      ASSERT(slot != NULL);
      cgen_->StoreToSlot(slot, init_state);
      break;
    }

    case NAMED: {
      Comment cmnt(masm, "[ Store to named Property");
      cgen_->frame()->Push(GetName());
      Result answer = cgen_->frame()->CallStoreIC();
      cgen_->frame()->Push(&answer);
      break;
    }

    case KEYED: {
      Comment cmnt(masm, "[ Store to keyed Property");

      // TODO(x64): Implement inlined version of keyed stores.

      Result answer = cgen_->frame()->CallKeyedStoreIC();
      // Make sure that we do not have a test instruction after the
      // call.  A test instruction after the call is used to
      // indicate that we have generated an inline version of the
      // keyed store.
      __ nop();
      cgen_->frame()->Push(&answer);
      break;
    }

    default:
      UNREACHABLE();
  }
}


void ToBooleanStub::Generate(MacroAssembler* masm) {
  Label false_result, true_result, not_string;
  __ movq(rax, Operand(rsp, 1 * kPointerSize));

  // 'null' => false.
  __ Cmp(rax, Factory::null_value());
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
  __ Cmp(rdx, Factory::heap_number_map());
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


bool CodeGenerator::FoldConstantSmis(Token::Value op, int left, int right) {
  // TODO(X64): This method is identical to the ia32 version.
  // Either find a reason to change it, or move it somewhere where it can be
  // shared. (Notice: It assumes that a Smi can fit in an int).

  Object* answer_object = Heap::undefined_value();
  switch (op) {
    case Token::ADD:
      if (Smi::IsValid(left + right)) {
        answer_object = Smi::FromInt(left + right);
      }
      break;
    case Token::SUB:
      if (Smi::IsValid(left - right)) {
        answer_object = Smi::FromInt(left - right);
      }
      break;
    case Token::MUL: {
        double answer = static_cast<double>(left) * right;
        if (answer >= Smi::kMinValue && answer <= Smi::kMaxValue) {
          // If the product is zero and the non-zero factor is negative,
          // the spec requires us to return floating point negative zero.
          if (answer != 0 || (left >= 0 && right >= 0)) {
            answer_object = Smi::FromInt(static_cast<int>(answer));
          }
        }
      }
      break;
    case Token::DIV:
    case Token::MOD:
      break;
    case Token::BIT_OR:
      answer_object = Smi::FromInt(left | right);
      break;
    case Token::BIT_AND:
      answer_object = Smi::FromInt(left & right);
      break;
    case Token::BIT_XOR:
      answer_object = Smi::FromInt(left ^ right);
      break;

    case Token::SHL: {
        int shift_amount = right & 0x1F;
        if (Smi::IsValid(left << shift_amount)) {
          answer_object = Smi::FromInt(left << shift_amount);
        }
        break;
      }
    case Token::SHR: {
        int shift_amount = right & 0x1F;
        unsigned int unsigned_left = left;
        unsigned_left >>= shift_amount;
        if (unsigned_left <= static_cast<unsigned int>(Smi::kMaxValue)) {
          answer_object = Smi::FromInt(unsigned_left);
        }
        break;
      }
    case Token::SAR: {
        int shift_amount = right & 0x1F;
        unsigned int unsigned_left = left;
        if (left < 0) {
          // Perform arithmetic shift of a negative number by
          // complementing number, logical shifting, complementing again.
          unsigned_left = ~unsigned_left;
          unsigned_left >>= shift_amount;
          unsigned_left = ~unsigned_left;
        } else {
          unsigned_left >>= shift_amount;
        }
        ASSERT(Smi::IsValid(unsigned_left));  // Converted to signed.
        answer_object = Smi::FromInt(unsigned_left);  // Converted to signed.
        break;
      }
    default:
      UNREACHABLE();
      break;
  }
  if (answer_object == Heap::undefined_value()) {
    return false;
  }
  frame_->Push(Handle<Object>(answer_object));
  return true;
}




// End of CodeGenerator implementation.

void UnarySubStub::Generate(MacroAssembler* masm) {
  UNIMPLEMENTED();
}


void CompareStub::Generate(MacroAssembler* masm) {
  Label call_builtin, done;

  // NOTICE! This code is only reached after a smi-fast-case check, so
  // it is certain that at least one operand isn't a smi.

  if (cc_ == equal) {  // Both strict and non-strict.
    Label slow;  // Fallthrough label.
    // Equality is almost reflexive (everything but NaN), so start by testing
    // for "identity and not NaN".
    {
      Label not_identical;
      __ cmpq(rax, rdx);
      __ j(not_equal, &not_identical);
      // Test for NaN. Sadly, we can't just compare to Factory::nan_value(),
      // so we do the second best thing - test it ourselves.

      Label return_equal;
      Label heap_number;
      // If it's not a heap number, then return equal.
      __ Cmp(FieldOperand(rdx, HeapObject::kMapOffset),
             Factory::heap_number_map());
      __ j(equal, &heap_number);
      __ bind(&return_equal);
      __ xor_(rax, rax);
      __ ret(0);

      __ bind(&heap_number);
      // It is a heap number, so return non-equal if it's NaN and equal if it's
      // not NaN.
      // The representation of NaN values has all exponent bits (52..62) set,
      // and not all mantissa bits (0..51) clear.
      // Read double representation into rax.
      __ movq(rbx, 0x7ff0000000000000, RelocInfo::NONE);
      __ movq(rax, FieldOperand(rdx, HeapNumber::kValueOffset));
      // Test that exponent bits are all set.
      __ or_(rbx, rax);
      __ cmpq(rbx, rax);
      __ j(not_equal, &return_equal);
      // Shift out flag and all exponent bits, retaining only mantissa.
      __ shl(rax, Immediate(12));
      // If all bits in the mantissa are zero the number is Infinity, and
      // we return zero.  Otherwise it is a NaN, and we return non-zero.
      // So just return rax.
      __ ret(0);

      __ bind(&not_identical);
    }

    // If we're doing a strict equality comparison, we don't have to do
    // type conversion, so we generate code to do fast comparison for objects
    // and oddballs. Non-smi numbers and strings still go through the usual
    // slow-case code.
    if (strict_) {
      // If either is a Smi (we know that not both are), then they can only
      // be equal if the other is a HeapNumber. If so, use the slow case.
      {
        Label not_smis;
        ASSERT_EQ(0, kSmiTag);
        ASSERT_EQ(0, Smi::FromInt(0));
        __ movq(rcx, Immediate(kSmiTagMask));
        __ and_(rcx, rax);
        __ testq(rcx, rdx);
        __ j(not_zero, &not_smis);
        // One operand is a smi.

        // Check whether the non-smi is a heap number.
        ASSERT_EQ(1, kSmiTagMask);
        // rcx still holds rax & kSmiTag, which is either zero or one.
        __ decq(rcx);  // If rax is a smi, all 1s, else all 0s.
        __ movq(rbx, rdx);
        __ xor_(rbx, rax);
        __ and_(rbx, rcx);  // rbx holds either 0 or rax ^ rdx.
        __ xor_(rbx, rax);
        // if rax was smi, rbx is now rdx, else rax.

        // Check if the non-smi operand is a heap number.
        __ Cmp(FieldOperand(rbx, HeapObject::kMapOffset),
               Factory::heap_number_map());
        // If heap number, handle it in the slow case.
        __ j(equal, &slow);
        // Return non-equal (ebx is not zero)
        __ movq(rax, rbx);
        __ ret(0);

        __ bind(&not_smis);
      }

      // If either operand is a JSObject or an oddball value, then they are not
      // equal since their pointers are different
      // There is no test for undetectability in strict equality.

      // If the first object is a JS object, we have done pointer comparison.
      ASSERT(LAST_TYPE == JS_FUNCTION_TYPE);
      Label first_non_object;
      __ CmpObjectType(rax, FIRST_JS_OBJECT_TYPE, rcx);
      __ j(below, &first_non_object);
      // Return non-zero (rax is not zero)
      Label return_not_equal;
      ASSERT(kHeapObjectTag != 0);
      __ bind(&return_not_equal);
      __ ret(0);

      __ bind(&first_non_object);
      // Check for oddballs: true, false, null, undefined.
      __ CmpInstanceType(rcx, ODDBALL_TYPE);
      __ j(equal, &return_not_equal);

      __ CmpObjectType(rdx, FIRST_JS_OBJECT_TYPE, rcx);
      __ j(above_equal, &return_not_equal);

      // Check for oddballs: true, false, null, undefined.
      __ CmpInstanceType(rcx, ODDBALL_TYPE);
      __ j(equal, &return_not_equal);

      // Fall through to the general case.
    }
    __ bind(&slow);
  }

  // Push arguments below the return address.
  __ pop(rcx);
  __ push(rax);
  __ push(rdx);
  __ push(rcx);

  // Inlined floating point compare.
  // Call builtin if operands are not floating point or smi.
  Label check_for_symbols;
  // TODO(X64): Implement floating point comparisons;
  __ int3();

  // TODO(1243847): Use cmov below once CpuFeatures are properly hooked up.
  Label below_lbl, above_lbl;
  // use edx, eax to convert unsigned to signed comparison
  __ j(below, &below_lbl);
  __ j(above, &above_lbl);

  __ xor_(rax, rax);  // equal
  __ ret(2 * kPointerSize);

  __ bind(&below_lbl);
  __ movq(rax, Immediate(-1));
  __ ret(2 * kPointerSize);

  __ bind(&above_lbl);
  __ movq(rax, Immediate(1));
  __ ret(2 * kPointerSize);  // eax, edx were pushed

  // Fast negative check for symbol-to-symbol equality.
  __ bind(&check_for_symbols);
  if (cc_ == equal) {
    BranchIfNonSymbol(masm, &call_builtin, rax);
    BranchIfNonSymbol(masm, &call_builtin, rdx);

    // We've already checked for object identity, so if both operands
    // are symbols they aren't equal. Register eax already holds a
    // non-zero value, which indicates not equal, so just return.
    __ ret(2 * kPointerSize);
  }

  __ bind(&call_builtin);
  // must swap argument order
  __ pop(rcx);
  __ pop(rdx);
  __ pop(rax);
  __ push(rdx);
  __ push(rax);

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
  __ push(rcx);

  // Call the native; it returns -1 (less), 0 (equal), or 1 (greater)
  // tagged as a small integer.
  __ InvokeBuiltin(builtin, JUMP_FUNCTION);
}


void CompareStub::BranchIfNonSymbol(MacroAssembler* masm,
                                    Label* label,
                                    Register object) {
  __ testl(object, Immediate(kSmiTagMask));
  __ j(zero, label);
  __ movq(kScratchRegister, FieldOperand(object, HeapObject::kMapOffset));
  __ movzxbq(kScratchRegister,
             FieldOperand(kScratchRegister, Map::kInstanceTypeOffset));
  __ and_(kScratchRegister, Immediate(kIsSymbolMask | kIsNotStringMask));
  __ cmpb(kScratchRegister, Immediate(kSymbolTag | kStringTag));
  __ j(not_equal, label);
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
  Label slow;

  // Get the function to call from the stack.
  // +2 ~ receiver, return address
  __ movq(rdi, Operand(rsp, (argc_ + 2) * kPointerSize));

  // Check that the function really is a JavaScript function.
  __ testl(rdi, Immediate(kSmiTagMask));
  __ j(zero, &slow);
  // Goto slow case if we do not have a function.
  __ CmpObjectType(rdi, JS_FUNCTION_TYPE, rcx);
  __ j(not_equal, &slow);

  // Fast-case: Just invoke the function.
  ParameterCount actual(argc_);
  __ InvokeFunction(rdi, actual, JUMP_FUNCTION);

  // Slow-case: Non-function called.
  __ bind(&slow);
  __ Set(rax, argc_);
  __ Set(rbx, 0);
  __ GetBuiltinEntry(rdx, Builtins::CALL_NON_FUNCTION);
  Handle<Code> adaptor(Builtins::builtin(Builtins::ArgumentsAdaptorTrampoline));
  __ Jump(adaptor, RelocInfo::CODE_TARGET);
}


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
  InLoopFlag in_loop = loop_nesting() > 0 ? IN_LOOP : NOT_IN_LOOP;
  CallFunctionStub call_function(arg_count, in_loop);
  Result answer = frame_->CallStub(&call_function, arg_count + 1);
  // Restore context and replace function on the stack with the
  // result of the stub invocation.
  frame_->RestoreContextRegister();
  frame_->SetElementAt(0, &answer);
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
  __ lea(rdx, Operand(rdx, rcx, times_4, kDisplacement));
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
  __ lea(rbx, Operand(rbp, rax, times_4, 0));
  __ neg(rdx);
  __ movq(rax, Operand(rbx, rdx, times_4, kDisplacement));
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
  __ lea(rbx, Operand(rbx, rcx, times_4, 0));
  __ neg(rdx);
  __ movq(rax, Operand(rbx, rdx, times_4, kDisplacement));
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
  __ testl(rax, Immediate(((1 << kFailureTypeTagSize) - 1) << kFailureTagSize));
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


// -----------------------------------------------------------------------------
// Implementation of stubs.

//  Stub classes have public member named masm, not masm_.


void FloatingPointHelper::AllocateHeapNumber(MacroAssembler* masm,
                                             Label* need_gc,
                                             Register scratch,
                                             Register result) {
  ExternalReference allocation_top =
      ExternalReference::new_space_allocation_top_address();
  ExternalReference allocation_limit =
      ExternalReference::new_space_allocation_limit_address();
  __ movq(scratch, allocation_top);  // scratch: address of allocation top.
  __ movq(result, Operand(scratch, 0));
  __ addq(result, Immediate(HeapNumber::kSize));  // New top.
  __ movq(kScratchRegister, allocation_limit);
  __ cmpq(result, Operand(kScratchRegister, 0));
  __ j(above, need_gc);

  __ movq(Operand(scratch, 0), result);  // store new top
  __ addq(result, Immediate(kHeapObjectTag - HeapNumber::kSize));
  __ movq(kScratchRegister,
          Factory::heap_number_map(),
          RelocInfo::EMBEDDED_OBJECT);
  __ movq(FieldOperand(result, HeapObject::kMapOffset), kScratchRegister);
  // Tag old top and use as result.
}



void FloatingPointHelper::LoadFloatOperand(MacroAssembler* masm,
                                           Register src,
                                           XMMRegister dst) {
  Label load_smi, done;

  __ testl(src, Immediate(kSmiTagMask));
  __ j(zero, &load_smi);
  __ movsd(dst, FieldOperand(src, HeapNumber::kValueOffset));
  __ jmp(&done);

  __ bind(&load_smi);
  __ sar(src, Immediate(kSmiTagSize));
  __ cvtlsi2sd(dst, src);

  __ bind(&done);
}


void FloatingPointHelper::LoadFloatOperands(MacroAssembler* masm,
                                            XMMRegister dst1,
                                            XMMRegister dst2) {
  __ movq(kScratchRegister, Operand(rsp, 2 * kPointerSize));
  LoadFloatOperand(masm, kScratchRegister, dst1);
  __ movq(kScratchRegister, Operand(rsp, 1 * kPointerSize));
  LoadFloatOperand(masm, kScratchRegister, dst2);
}


void FloatingPointHelper::LoadInt32Operand(MacroAssembler* masm,
                                           const Operand& src,
                                           Register dst) {
  // TODO(X64): Convert number operands to int32 values.
  // Don't convert a Smi to a double first.
  UNIMPLEMENTED();
}


void FloatingPointHelper::LoadFloatOperands(MacroAssembler* masm) {
  Label load_smi_1, load_smi_2, done_load_1, done;
  __ movq(kScratchRegister, Operand(rsp, 2 * kPointerSize));
  __ testl(kScratchRegister, Immediate(kSmiTagMask));
  __ j(zero, &load_smi_1);
  __ fld_d(FieldOperand(kScratchRegister, HeapNumber::kValueOffset));
  __ bind(&done_load_1);

  __ movq(kScratchRegister, Operand(rsp, 1 * kPointerSize));
  __ testl(kScratchRegister, Immediate(kSmiTagMask));
  __ j(zero, &load_smi_2);
  __ fld_d(FieldOperand(kScratchRegister, HeapNumber::kValueOffset));
  __ jmp(&done);

  __ bind(&load_smi_1);
  __ sar(kScratchRegister, Immediate(kSmiTagSize));
  __ push(kScratchRegister);
  __ fild_s(Operand(rsp, 0));
  __ pop(kScratchRegister);
  __ jmp(&done_load_1);

  __ bind(&load_smi_2);
  __ sar(kScratchRegister, Immediate(kSmiTagSize));
  __ push(kScratchRegister);
  __ fild_s(Operand(rsp, 0));
  __ pop(kScratchRegister);

  __ bind(&done);
}


void FloatingPointHelper::CheckFloatOperands(MacroAssembler* masm,
                                             Label* non_float) {
  Label test_other, done;
  // Test if both operands are floats or smi -> scratch=k_is_float;
  // Otherwise scratch = k_not_float.
  __ testl(rdx, Immediate(kSmiTagMask));
  __ j(zero, &test_other);  // argument in rdx is OK
  __ movq(kScratchRegister,
          Factory::heap_number_map(),
          RelocInfo::EMBEDDED_OBJECT);
  __ cmpq(kScratchRegister, FieldOperand(rdx, HeapObject::kMapOffset));
  __ j(not_equal, non_float);  // argument in rdx is not a number -> NaN

  __ bind(&test_other);
  __ testl(rax, Immediate(kSmiTagMask));
  __ j(zero, &done);  // argument in rax is OK
  __ movq(kScratchRegister,
          Factory::heap_number_map(),
          RelocInfo::EMBEDDED_OBJECT);
  __ cmpq(kScratchRegister, FieldOperand(rax, HeapObject::kMapOffset));
  __ j(not_equal, non_float);  // argument in rax is not a number -> NaN

  // Fall-through: Both operands are numbers.
  __ bind(&done);
}


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
      // TODO(X64): TODO(Smi): Smi implementation dependent constant.
      // Value is Smi::fromInt(-(1<<31)) / Smi::fromInt(-1)
      __ cmpq(rax, Immediate(0x40000000));
      __ j(equal, slow);
      // Check for negative zero result.
      __ NegativeZeroTest(rax, rcx, slow);  // use ecx = x | y
      // Tag the result and store it in register rax.
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(rax, Operand(rax, rax, times_1, kSmiTag));
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
      ASSERT_EQ(0, kSmiTag);
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
          __ shrl(rax);  // rcx is implicit shift register
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
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(rax, Operand(rax, rax, times_1, kSmiTag));
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
    __ movq(rbx, Operand(rsp, 1 * kPointerSize));  // get y
    __ movq(rax, Operand(rsp, 2 * kPointerSize));  // get x
    GenerateSmiCode(masm, &slow);
    __ ret(2 * kPointerSize);  // remove both operands

    // Too bad. The fast case smi code didn't succeed.
    __ bind(&slow);
  }

  // Setup registers.
  __ movq(rax, Operand(rsp, 1 * kPointerSize));  // get y
  __ movq(rdx, Operand(rsp, 2 * kPointerSize));  // get x

  // Floating point case.
  switch (op_) {
    case Token::ADD:
    case Token::SUB:
    case Token::MUL:
    case Token::DIV: {
      // rax: y
      // rdx: x
      FloatingPointHelper::CheckFloatOperands(masm, &call_runtime);
      // Fast-case: Both operands are numbers.
      // Allocate a heap number, if needed.
      Label skip_allocation;
      switch (mode_) {
        case OVERWRITE_LEFT:
          __ movq(rax, rdx);
          // Fall through!
        case OVERWRITE_RIGHT:
          // If the argument in rax is already an object, we skip the
          // allocation of a heap number.
          __ testl(rax, Immediate(kSmiTagMask));
          __ j(not_zero, &skip_allocation);
          // Fall through!
        case NO_OVERWRITE:
          FloatingPointHelper::AllocateHeapNumber(masm,
                                                  &call_runtime,
                                                  rcx,
                                                  rax);
          __ bind(&skip_allocation);
          break;
        default: UNREACHABLE();
      }
      // xmm4 and xmm5 are volatile XMM registers.
      FloatingPointHelper::LoadFloatOperands(masm, xmm4, xmm5);

      switch (op_) {
        case Token::ADD: __ addsd(xmm4, xmm5); break;
        case Token::SUB: __ subsd(xmm4, xmm5); break;
        case Token::MUL: __ mulsd(xmm4, xmm5); break;
        case Token::DIV: __ divsd(xmm4, xmm5); break;
        default: UNREACHABLE();
      }
      __ movsd(FieldOperand(rax, HeapNumber::kValueOffset), xmm4);
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
      FloatingPointHelper::CheckFloatOperands(masm, &call_runtime);
      // TODO(X64): Don't convert a Smi to float and then back to int32
      // afterwards.
      FloatingPointHelper::LoadFloatOperands(masm);

      Label skip_allocation, non_smi_result, operand_conversion_failure;

      // Reserve space for converted numbers.
      __ subq(rsp, Immediate(2 * kPointerSize));

      bool use_sse3 = CpuFeatures::IsSupported(CpuFeatures::SSE3);
      if (use_sse3) {
        // Truncate the operands to 32-bit integers and check for
        // exceptions in doing so.
         CpuFeatures::Scope scope(CpuFeatures::SSE3);
        __ fisttp_s(Operand(rsp, 0 * kPointerSize));
        __ fisttp_s(Operand(rsp, 1 * kPointerSize));
        __ fnstsw_ax();
        __ testl(rax, Immediate(1));
        __ j(not_zero, &operand_conversion_failure);
      } else {
        // Check if right operand is int32.
        __ fist_s(Operand(rsp, 0 * kPointerSize));
        __ fild_s(Operand(rsp, 0 * kPointerSize));
        __ fucompp();
        __ fnstsw_ax();
        __ sahf();  // TODO(X64): Not available.
        __ j(not_zero, &operand_conversion_failure);
        __ j(parity_even, &operand_conversion_failure);

        // Check if left operand is int32.
        __ fist_s(Operand(rsp, 1 * kPointerSize));
        __ fild_s(Operand(rsp, 1 * kPointerSize));
        __ fucompp();
        __ fnstsw_ax();
        __ sahf();  // TODO(X64): Not available. Test bits in ax directly
        __ j(not_zero, &operand_conversion_failure);
        __ j(parity_even, &operand_conversion_failure);
      }

      // Get int32 operands and perform bitop.
      __ pop(rcx);
      __ pop(rax);
      switch (op_) {
        case Token::BIT_OR:  __ or_(rax, rcx); break;
        case Token::BIT_AND: __ and_(rax, rcx); break;
        case Token::BIT_XOR: __ xor_(rax, rcx); break;
        case Token::SAR: __ sar(rax); break;
        case Token::SHL: __ shl(rax); break;
        case Token::SHR: __ shr(rax); break;
        default: UNREACHABLE();
      }
      if (op_ == Token::SHR) {
        // Check if result is non-negative and fits in a smi.
        __ testl(rax, Immediate(0xc0000000));
        __ j(not_zero, &non_smi_result);
      } else {
        // Check if result fits in a smi.
        __ cmpl(rax, Immediate(0xc0000000));
        __ j(negative, &non_smi_result);
      }
      // Tag smi result and return.
      ASSERT(kSmiTagSize == times_2);  // adjust code if not the case
      __ lea(rax, Operand(rax, rax, times_1, kSmiTag));
      __ ret(2 * kPointerSize);

      // All ops except SHR return a signed int32 that we load in a HeapNumber.
      if (op_ != Token::SHR) {
        __ bind(&non_smi_result);
        // Allocate a heap number if needed.
        __ movsxlq(rbx, rax);  // rbx: sign extended 32-bit result
        switch (mode_) {
          case OVERWRITE_LEFT:
          case OVERWRITE_RIGHT:
            // If the operand was an object, we skip the
            // allocation of a heap number.
            __ movq(rax, Operand(rsp, mode_ == OVERWRITE_RIGHT ?
                                 1 * kPointerSize : 2 * kPointerSize));
            __ testl(rax, Immediate(kSmiTagMask));
            __ j(not_zero, &skip_allocation);
            // Fall through!
          case NO_OVERWRITE:
            FloatingPointHelper::AllocateHeapNumber(masm, &call_runtime,
                                                    rcx, rax);
            __ bind(&skip_allocation);
            break;
          default: UNREACHABLE();
        }
        // Store the result in the HeapNumber and return.
        __ movq(Operand(rsp, 1 * kPointerSize), rbx);
        __ fild_s(Operand(rsp, 1 * kPointerSize));
        __ fstp_d(FieldOperand(rax, HeapNumber::kValueOffset));
        __ ret(2 * kPointerSize);
      }

      // Clear the FPU exception flag and reset the stack before calling
      // the runtime system.
      __ bind(&operand_conversion_failure);
      __ addq(rsp, Immediate(2 * kPointerSize));
      if (use_sse3) {
        // If we've used the SSE3 instructions for truncating the
        // floating point values to integers and it failed, we have a
        // pending #IA exception. Clear it.
        __ fnclex();
      } else {
        // The non-SSE3 variant does early bailout if the right
        // operand isn't a 32-bit integer, so we may have a single
        // value on the FPU stack we need to get rid of.
        __ ffree(0);
      }

      // SHR should return uint32 - go to runtime for non-smi/negative result.
      if (op_ == Token::SHR) {
        __ bind(&non_smi_result);
      }
      __ movq(rax, Operand(rsp, 1 * kPointerSize));
      __ movq(rdx, Operand(rsp, 2 * kPointerSize));
      break;
    }
    default: UNREACHABLE(); break;
  }

  // If all else fails, use the runtime system to get the correct
  // result.
  __ bind(&call_runtime);
  // Disable builtin-calls until JS builtins can compile and run.
  __ Abort("Disabled until builtins compile and run.");
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


#undef __

} }  // namespace v8::internal
