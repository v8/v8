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

#include "codegen-inl.h"
#include "debug.h"
#include "fast-codegen.h"
#include "parser.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm_)

// Generate code for a JS function.  On entry to the function the receiver
// and arguments have been pushed on the stack left to right, with the
// return address on top of them.  The actual argument count matches the
// formal parameter count expected by the function.
//
// The live registers are:
//   o rdi: the JS function object being called (ie, ourselves)
//   o rsi: our context
//   o rbp: our caller's frame pointer
//   o rsp: stack pointer (pointing to return address)
//
// The function builds a JS frame.  Please see JavaScriptFrameConstants in
// frames-x64.h for its layout.
void FastCodeGenerator::Generate(FunctionLiteral* fun) {
  function_ = fun;
  SetFunctionPosition(fun);

  __ push(rbp);  // Caller's frame pointer.
  __ movq(rbp, rsp);
  __ push(rsi);  // Callee's context.
  __ push(rdi);  // Callee's JS Function.

  { Comment cmnt(masm_, "[ Allocate locals");
    int locals_count = fun->scope()->num_stack_slots();
    for (int i = 0; i < locals_count; i++) {
      __ PushRoot(Heap::kUndefinedValueRootIndex);
    }
  }

  { Comment cmnt(masm_, "[ Stack check");
    Label ok;
    __ CompareRoot(rsp, Heap::kStackLimitRootIndex);
    __ j(above_equal, &ok);
    StackCheckStub stub;
    __ CallStub(&stub);
    __ bind(&ok);
  }

  { Comment cmnt(masm_, "[ Declarations");
    VisitDeclarations(fun->scope()->declarations());
  }

  if (FLAG_trace) {
    __ CallRuntime(Runtime::kTraceEnter, 0);
  }

  { Comment cmnt(masm_, "[ Body");
    VisitStatements(fun->body());
  }

  { Comment cmnt(masm_, "[ return <undefined>;");
    // Emit a 'return undefined' in case control fell off the end of the
    // body.
    __ LoadRoot(rax, Heap::kUndefinedValueRootIndex);
    SetReturnPosition(fun);
    if (FLAG_trace) {
      __ push(rax);
      __ CallRuntime(Runtime::kTraceExit, 1);
    }
    __ RecordJSReturn();

    // Do not use the leave instruction here because it is too short to
    // patch with the code required by the debugger.
    __ movq(rsp, rbp);
    __ pop(rbp);
    __ ret((fun->scope()->num_parameters() + 1) * kPointerSize);
#ifdef ENABLE_DEBUGGER_SUPPORT
    // Add padding that will be overwritten by a debugger breakpoint.  We
    // have just generated "movq rsp, rbp; pop rbp; ret k" with length 7
    // (3 + 1 + 3).
    const int kPadding = Debug::kX64JSReturnSequenceLength - 7;
    for (int i = 0; i < kPadding; ++i) {
      masm_->int3();
    }
#endif
  }
}


void FastCodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  // Call the runtime to declare the globals.
  __ push(rsi);  // The context is the first argument.
  __ Push(pairs);
  __ Push(Smi::FromInt(is_eval_ ? 1 : 0));
  __ CallRuntime(Runtime::kDeclareGlobals, 3);
  // Return value is ignored.
}


void FastCodeGenerator::VisitBlock(Block* stmt) {
  Comment cmnt(masm_, "[ Block");
  SetStatementPosition(stmt);
  VisitStatements(stmt->statements());
}


void FastCodeGenerator::VisitExpressionStatement(ExpressionStatement* stmt) {
  Comment cmnt(masm_, "[ ExpressionStatement");
  SetStatementPosition(stmt);
  Visit(stmt->expression());
}


void FastCodeGenerator::VisitReturnStatement(ReturnStatement* stmt) {
  Comment cmnt(masm_, "[ ReturnStatement");
  SetStatementPosition(stmt);
  Expression* expr = stmt->expression();
  Visit(expr);

  // Complete the statement based on the location of the subexpression.
  Location source = expr->location();
  ASSERT(!source.is_nowhere());
  if (source.is_temporary()) {
    __ pop(rax);
  } else {
    ASSERT(source.is_constant());
    ASSERT(expr->AsLiteral() != NULL);
    __ Move(rax, expr->AsLiteral()->handle());
  }
  if (FLAG_trace) {
    __ push(rax);
    __ CallRuntime(Runtime::kTraceExit, 1);
  }

  __ RecordJSReturn();
  // Do not use the leave instruction here because it is too short to
  // patch with the code required by the debugger.
  __ movq(rsp, rbp);
  __ pop(rbp);
  __ ret((function_->scope()->num_parameters() + 1) * kPointerSize);
#ifdef ENABLE_DEBUGGER_SUPPORT
  // Add padding that will be overwritten by a debugger breakpoint.  We
  // have just generated "movq rsp, rbp; pop rbp; ret k" with length 7
  // (3 + 1 + 3).
  const int kPadding = Debug::kX64JSReturnSequenceLength - 7;
  for (int i = 0; i < kPadding; ++i) {
    masm_->int3();
  }
#endif
}


void FastCodeGenerator::VisitFunctionLiteral(FunctionLiteral* expr) {
  Comment cmnt(masm_, "[ FunctionLiteral");

  // Build the function boilerplate and instantiate it.
  Handle<JSFunction> boilerplate = BuildBoilerplate(expr);
  if (HasStackOverflow()) return;

  ASSERT(boilerplate->IsBoilerplate());

  // Create a new closure.
  __ push(rsi);
  __ Push(boilerplate);
  __ CallRuntime(Runtime::kNewClosure, 2);

  if (expr->location().is_temporary()) {
    __ push(rax);
  } else {
    ASSERT(expr->location().is_nowhere());
  }
}


void FastCodeGenerator::VisitVariableProxy(VariableProxy* expr) {
  Comment cmnt(masm_, "[ VariableProxy");
  Expression* rewrite = expr->var()->rewrite();
  if (rewrite == NULL) {
    Comment cmnt(masm_, "Global variable");
    // Use inline caching. Variable name is passed in rcx and the global
    // object on the stack.
    __ push(CodeGenerator::GlobalObject());
    __ Move(rcx, expr->name());
    Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
    __ Call(ic, RelocInfo::CODE_TARGET_CONTEXT);

    // A test rax instruction following the call is used by the IC to
    // indicate that the inobject property case was inlined.  Ensure there
    // is no test rax instruction here.
    if (expr->location().is_temporary()) {
      // Replace the global object with the result.
      __ movq(Operand(rsp, 0), rax);
    } else {
      ASSERT(expr->location().is_nowhere());
      __ addq(rsp, Immediate(kPointerSize));
    }

  } else {
    Comment cmnt(masm_, "Stack slot");
    Slot* slot = rewrite->AsSlot();
    ASSERT(slot != NULL);
    if (expr->location().is_temporary()) {
      __ push(Operand(rbp, SlotOffset(slot)));
    } else {
      ASSERT(expr->location().is_nowhere());
    }
  }
}


void FastCodeGenerator::VisitObjectLiteral(ObjectLiteral* expr) {
  Comment cmnt(masm_, "[ ObjectLiteral");
  Label boilerplate_exists;

  __ movq(rdi, Operand(rbp, JavaScriptFrameConstants::kFunctionOffset));
  __ movq(rbx, FieldOperand(rdi, JSFunction::kLiteralsOffset));
  int literal_offset =
    FixedArray::kHeaderSize + expr->literal_index() * kPointerSize;
  __ movq(rax, FieldOperand(rbx, literal_offset));
  __ CompareRoot(rax, Heap::kUndefinedValueRootIndex);
  __ j(not_equal, &boilerplate_exists);
  // Create boilerplate if it does not exist.
  // Literal array (0).
  __ push(rbx);
  // Literal index (1).
  __ Push(Smi::FromInt(expr->literal_index()));
  // Constant properties (2).
  __ Push(expr->constant_properties());
  __ CallRuntime(Runtime::kCreateObjectLiteralBoilerplate, 3);
  __ bind(&boilerplate_exists);
  // rax contains boilerplate.
  // Clone boilerplate.
  __ push(rax);
  if (expr->depth() == 1) {
    __ CallRuntime(Runtime::kCloneShallowLiteralBoilerplate, 1);
  } else {
    __ CallRuntime(Runtime::kCloneLiteralBoilerplate, 1);
  }

  // If result_saved == true: the result is saved on top of the stack.
  // If result_saved == false: the result is not on the stack, just in rax.
  bool result_saved = false;

  for (int i = 0; i < expr->properties()->length(); i++) {
    ObjectLiteral::Property* property = expr->properties()->at(i);
    Literal* key = property->key();
    Expression* value = property->value();
    if (property->kind() == ObjectLiteral::Property::CONSTANT) continue;
    if (property->kind() == ObjectLiteral::Property::MATERIALIZED_LITERAL &&
        CompileTimeValue::IsCompileTimeValue(value)) {
      continue;
    }
    if (!result_saved) {
      __ push(rax);  // Save result on the stack
      result_saved = true;
    }
    switch (property->kind()) {
      case ObjectLiteral::Property::MATERIALIZED_LITERAL:  // fall through
        ASSERT(!CompileTimeValue::IsCompileTimeValue(value));
      case ObjectLiteral::Property::COMPUTED:
        if (key->handle()->IsSymbol()) {
          Visit(value);
          ASSERT(value->location().is_temporary());
          __ pop(rax);
          __ Move(rcx, key->handle());
          Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
          __ call(ic, RelocInfo::CODE_TARGET);
          // StoreIC leaves the receiver on the stack.
          break;
        }
        // fall through
      case ObjectLiteral::Property::PROTOTYPE:
        __ push(rax);
        Visit(key);
        if (key->location().is_constant()) {
          __ Push(key->handle());
        }
        Visit(value);
        ASSERT(value->location().is_temporary());
        __ CallRuntime(Runtime::kSetProperty, 3);
        __ movq(rax, Operand(rsp, 0));  // Restore result into rax.
        break;
      case ObjectLiteral::Property::SETTER:  // fall through
      case ObjectLiteral::Property::GETTER:
        __ push(rax);
        Visit(key);
        if (key->location().is_constant()) {
          __ Push(key->handle());
        }
        __ Push(property->kind() == ObjectLiteral::Property::SETTER ?
                Smi::FromInt(1) :
                Smi::FromInt(0));
        Visit(value);
        ASSERT(value->location().is_temporary());
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        __ movq(rax, Operand(rsp, 0));  // Restore result into rax.
        break;
      default: UNREACHABLE();
    }
  }
  if (expr->location().is_nowhere() && result_saved) {
    __ addq(rsp, Immediate(kPointerSize));
  } else if (expr->location().is_temporary() && !result_saved) {
    __ push(rax);
  }
}


void FastCodeGenerator::VisitRegExpLiteral(RegExpLiteral* expr) {
  Comment cmnt(masm_, "[ RegExp Literal");
  Label done;
  // Registers will be used as follows:
  // rdi = JS function.
  // rbx = literals array.
  // rax = regexp literal.
  __ movq(rdi, Operand(rbp, JavaScriptFrameConstants::kFunctionOffset));
  __ movq(rbx, FieldOperand(rdi, JSFunction::kLiteralsOffset));
  int literal_offset =
    FixedArray::kHeaderSize + expr->literal_index() * kPointerSize;
  __ movq(rax, FieldOperand(rbx, literal_offset));
  __ CompareRoot(rax, Heap::kUndefinedValueRootIndex);
  __ j(not_equal, &done);
  // Create regexp literal using runtime function
  // Result will be in rax.
  __ push(rbx);
  __ Push(Smi::FromInt(expr->literal_index()));
  __ Push(expr->pattern());
  __ Push(expr->flags());
  __ CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  // Label done:
  __ bind(&done);
  if (expr->location().is_temporary()) {
    __ push(rax);
  } else {
    ASSERT(expr->location().is_nowhere());
  }
}


void FastCodeGenerator::VisitArrayLiteral(ArrayLiteral* expr) {
  Comment cmnt(masm_, "[ ArrayLiteral");
  Label make_clone;

  // Fetch the function's literals array.
  __ movq(rbx, Operand(rbp, JavaScriptFrameConstants::kFunctionOffset));
  __ movq(rbx, FieldOperand(rbx, JSFunction::kLiteralsOffset));
  // Check if the literal's boilerplate has been instantiated.
  int offset =
      FixedArray::kHeaderSize + (expr->literal_index() * kPointerSize);
  __ movq(rax, FieldOperand(rbx, offset));
  __ CompareRoot(rax, Heap::kUndefinedValueRootIndex);
  __ j(not_equal, &make_clone);

  // Instantiate the boilerplate.
  __ push(rbx);
  __ Push(Smi::FromInt(expr->literal_index()));
  __ Push(expr->literals());
  __ CallRuntime(Runtime::kCreateArrayLiteralBoilerplate, 3);

  __ bind(&make_clone);
  // Clone the boilerplate.
  __ push(rax);
  if (expr->depth() > 1) {
    __ CallRuntime(Runtime::kCloneLiteralBoilerplate, 1);
  } else {
    __ CallRuntime(Runtime::kCloneShallowLiteralBoilerplate, 1);
  }

  bool result_saved = false;  // Is the result saved to the stack?

  // Emit code to evaluate all the non-constant subexpressions and to store
  // them into the newly cloned array.
  ZoneList<Expression*>* subexprs = expr->values();
  for (int i = 0, len = subexprs->length(); i < len; i++) {
    Expression* subexpr = subexprs->at(i);
    // If the subexpression is a literal or a simple materialized literal it
    // is already set in the cloned array.
    if (subexpr->AsLiteral() != NULL ||
        CompileTimeValue::IsCompileTimeValue(subexpr)) {
      continue;
    }

    if (!result_saved) {
      __ push(rax);
      result_saved = true;
    }
    Visit(subexpr);
    ASSERT(subexpr->location().is_temporary());

    // Store the subexpression value in the array's elements.
    __ pop(rax);  // Subexpression value.
    __ movq(rbx, Operand(rsp, 0));  // Copy of array literal.
    __ movq(rbx, FieldOperand(rbx, JSObject::kElementsOffset));
    int offset = FixedArray::kHeaderSize + (i * kPointerSize);
    __ movq(FieldOperand(rbx, offset), rax);

    // Update the write barrier for the array store.
    __ RecordWrite(rbx, offset, rax, rcx);
  }

  Location destination = expr->location();
  if (destination.is_nowhere() && result_saved) {
    __ addq(rsp, Immediate(kPointerSize));
  } else if (destination.is_temporary() && !result_saved) {
    __ push(rax);
  }
}


void FastCodeGenerator::VisitAssignment(Assignment* expr) {
  Comment cmnt(masm_, "[ Assignment");
  ASSERT(expr->op() == Token::ASSIGN || expr->op() == Token::INIT_VAR);
  Expression* rhs = expr->value();
  Visit(rhs);

  // Left-hand side can only be a global or a (parameter or local) slot.
  Variable* var = expr->target()->AsVariableProxy()->AsVariable();
  ASSERT(var != NULL);
  ASSERT(var->is_global() || var->slot() != NULL);

  // Complete the assignment based on the location of the right-hand-side
  // value and the desired location of the assignment value.
  Location destination = expr->location();
  Location source = rhs->location();
  ASSERT(!destination.is_constant());
  ASSERT(!source.is_nowhere());

  if (var->is_global()) {
    // Assignment to a global variable, use inline caching.  Right-hand-side
    // value is passed in rax, variable name in rcx, and the global object
    // on the stack.
    if (source.is_temporary()) {
      __ pop(rax);
    } else {
      ASSERT(source.is_constant());
      ASSERT(rhs->AsLiteral() != NULL);
      __ Move(rax, rhs->AsLiteral()->handle());
    }
    __ Move(rcx, var->name());
    __ push(CodeGenerator::GlobalObject());
    Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
    __ Call(ic, RelocInfo::CODE_TARGET);
    // Overwrite the global object on the stack with the result if needed.
    if (destination.is_temporary()) {
      __ movq(Operand(rsp, 0), rax);
    } else {
      __ addq(rsp, Immediate(kPointerSize));
    }
  } else {
    if (source.is_temporary()) {
      if (destination.is_temporary()) {
        // Case 'temp1 <- (var = temp0)'.  Preserve right-hand-side temporary
        // on the stack.
        __ movq(kScratchRegister, Operand(rsp, 0));
        __ movq(Operand(rbp, SlotOffset(var->slot())), kScratchRegister);
      } else {
        ASSERT(destination.is_nowhere());
        // Case 'var = temp'.  Discard right-hand-side temporary.
        __ pop(Operand(rbp, SlotOffset(var->slot())));
      }
    } else {
      ASSERT(source.is_constant());
      ASSERT(rhs->AsLiteral() != NULL);
      // Two cases: 'temp <- (var = constant)', or 'var = constant' with a
      // discarded result.  Always perform the assignment.
      __ Move(kScratchRegister, rhs->AsLiteral()->handle());
      __ movq(Operand(rbp, SlotOffset(var->slot())), kScratchRegister);
      if (destination.is_temporary()) {
        // Case 'temp <- (var = constant)'.  Save result.
        __ push(kScratchRegister);
      }
    }
  }
}


void FastCodeGenerator::VisitCall(Call* expr) {
  Expression* fun = expr->expression();
  ZoneList<Expression*>* args = expr->arguments();
  Variable* var = fun->AsVariableProxy()->AsVariable();
  ASSERT(var != NULL && !var->is_this() && var->is_global());
  ASSERT(!var->is_possibly_eval());

  __ Push(var->name());
  // Push global object (receiver).
  __ push(CodeGenerator::GlobalObject());
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    Visit(args->at(i));
    ASSERT(!args->at(i)->location().is_nowhere());
    if (args->at(i)->location().is_constant()) {
      ASSERT(args->at(i)->AsLiteral() != NULL);
      __ Push(args->at(i)->AsLiteral()->handle());
    }
  }
  // Record source position for debugger
  SetSourcePosition(expr->position());
  // Call the IC initialization code.
  Handle<Code> ic = CodeGenerator::ComputeCallInitialize(arg_count,
                                                         NOT_IN_LOOP);
  __ call(ic, RelocInfo::CODE_TARGET_CONTEXT);
  // Restore context register.
  __ movq(rsi, Operand(rbp, StandardFrameConstants::kContextOffset));
  // Discard the function left on TOS.
  if (expr->location().is_temporary()) {
    __ movq(Operand(rsp, 0), rax);
  } else {
    ASSERT(expr->location().is_nowhere());
    __ addq(rsp, Immediate(kPointerSize));
  }
}


void FastCodeGenerator::VisitCallRuntime(CallRuntime* expr) {
  Comment cmnt(masm_, "[ CallRuntime");
  ZoneList<Expression*>* args = expr->arguments();
  Runtime::Function* function = expr->function();

  ASSERT(function != NULL);

  // Push the arguments ("left-to-right").
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    Visit(args->at(i));
    ASSERT(!args->at(i)->location().is_nowhere());
    if (args->at(i)->location().is_constant()) {
      ASSERT(args->at(i)->AsLiteral() != NULL);
      __ Push(args->at(i)->AsLiteral()->handle());
    } else {
      ASSERT(args->at(i)->location().is_temporary());
      // If location is temporary, it is already on the stack,
      // so nothing to do here.
    }
  }

  __ CallRuntime(function, arg_count);
  if (expr->location().is_temporary()) {
    __ push(rax);
  } else {
    ASSERT(expr->location().is_nowhere());
  }
}


void FastCodeGenerator::VisitBinaryOperation(BinaryOperation* expr) {
  // Compile a short-circuited boolean or operation in a non-test
  // context.
  ASSERT(expr->op() == Token::OR);
  // Compile (e0 || e1) as if it were
  // (let (temp = e0) temp ? temp : e1).

  Label eval_right, done;
  Location destination = expr->location();
  ASSERT(!destination.is_constant());

  Expression* left = expr->left();
  Location left_source = left->location();
  ASSERT(!left_source.is_nowhere());

  Expression* right = expr->right();
  Location right_source = right->location();
  ASSERT(!right_source.is_nowhere());

  Visit(left);
  // Use the shared ToBoolean stub to find the boolean value of the
  // left-hand subexpression.  Load the value into rax to perform some
  // inlined checks assumed by the stub.
  if (left_source.is_temporary()) {
    if (destination.is_temporary()) {
      // Copy the left-hand value into rax because we may need it as the
      // final result.
      __ movq(rax, Operand(rsp, 0));
    } else {
      // Pop the left-hand value into rax because we will not need it as the
      // final result.
      __ pop(rax);
    }
  } else {
    // Load the left-hand value into rax.  Put it on the stack if we may
    // need it.
    ASSERT(left->AsLiteral() != NULL);
    __ Move(rax, left->AsLiteral()->handle());
    if (destination.is_temporary()) __ push(rax);
  }
  // The left-hand value is in rax.  It is also on the stack iff the
  // destination location is temporary.

  // Perform fast checks assumed by the stub.
  // The undefined value is false.
  __ CompareRoot(rax, Heap::kUndefinedValueRootIndex);
  __ j(equal, &eval_right);
  __ CompareRoot(rax, Heap::kTrueValueRootIndex);  // True is true.
  __ j(equal, &done);
  __ CompareRoot(rax, Heap::kFalseValueRootIndex);  // False is false.
  __ j(equal, &eval_right);
  ASSERT(kSmiTag == 0);
  __ SmiCompare(rax, Smi::FromInt(0));  // The smi zero is false.
  __ j(equal, &eval_right);
  Condition is_smi = masm_->CheckSmi(rax);  // All other smis are true.
  __ j(is_smi, &done);

  // Call the stub for all other cases.
  __ push(rax);
  ToBooleanStub stub;
  __ CallStub(&stub);
  __ testq(rax, rax);  // The stub returns nonzero for true.
  __ j(not_zero, &done);

  __ bind(&eval_right);
  // Discard the left-hand value if present on the stack.
  if (destination.is_temporary()) {
    __ addq(rsp, Immediate(kPointerSize));
  }
  Visit(right);

  // Save or discard the right-hand value as needed.
  if (destination.is_temporary() && right_source.is_constant()) {
    ASSERT(right->AsLiteral() != NULL);
    __ Push(right->AsLiteral()->handle());
  } else if (destination.is_nowhere() && right_source.is_temporary()) {
    __ addq(rsp, Immediate(kPointerSize));
  }

  __ bind(&done);
}


} }  // namespace v8::internal
