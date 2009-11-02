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
#include "fast-codegen.h"
#include "parser.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm_)

// Generate code for a JS function.  On entry to the function the receiver
// and arguments have been pushed on the stack left to right.  The actual
// argument count matches the formal parameter count expected by the
// function.
//
// The live registers are:
//   o r1: the JS function object being called (ie, ourselves)
//   o cp: our context
//   o fp: our caller's frame pointer
//   o sp: stack pointer
//   o lr: return address
//
// The function builds a JS frame.  Please see JavaScriptFrameConstants in
// frames-arm.h for its layout.
void FastCodeGenerator::Generate(FunctionLiteral* fun) {
  function_ = fun;
  // ARM does NOT call SetFunctionPosition.

  __ stm(db_w, sp, r1.bit() | cp.bit() | fp.bit() | lr.bit());
  // Adjust fp to point to caller's fp.
  __ add(fp, sp, Operand(2 * kPointerSize));

  { Comment cmnt(masm_, "[ Allocate locals");
    int locals_count = fun->scope()->num_stack_slots();
    if (locals_count > 0) {
      __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
    }
    __ LoadRoot(r2, Heap::kStackLimitRootIndex);
    for (int i = 0; i < locals_count; i++) {
      __ push(ip);
    }
  }

  // Check the stack for overflow or break request.
  // Put the lr setup instruction in the delay slot.  The kInstrSize is
  // added to the implicit 8 byte offset that always applies to operations
  // with pc and gives a return address 12 bytes down.
  Comment cmnt(masm_, "[ Stack check");
  __ add(lr, pc, Operand(Assembler::kInstrSize));
  __ cmp(sp, Operand(r2));
  StackCheckStub stub;
  __ mov(pc,
         Operand(reinterpret_cast<intptr_t>(stub.GetCode().location()),
                 RelocInfo::CODE_TARGET),
         LeaveCC,
         lo);

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
    __ LoadRoot(r0, Heap::kUndefinedValueRootIndex);
  }
  { Comment cmnt(masm_, "Return sequence");
    if (return_label_.is_bound()) {
      __ b(&return_label_);
    } else {
      __ bind(&return_label_);
      SetReturnPosition(fun);
      if (FLAG_trace) {
        // Push the return value on the stack as the parameter.
        // Runtime::TraceExit returns its parameter in r0.
        __ push(r0);
        __ CallRuntime(Runtime::kTraceExit, 1);
      }
      __ RecordJSReturn();
      __ mov(sp, fp);
      __ ldm(ia_w, sp, fp.bit() | lr.bit());
      int num_parameters = function_->scope()->num_parameters();
      __ add(sp, sp, Operand((num_parameters + 1) * kPointerSize));
      __ Jump(lr);
    }
  }
}


void FastCodeGenerator::Move(Expression::Context context, Register source) {
  switch (context) {
    case Expression::kUninitialized:
      UNREACHABLE();
    case Expression::kEffect:
      break;
    case Expression::kValue:
      __ push(source);
      break;
    case Expression::kTest:
      TestAndBranch(source, true_label_, false_label_);
      break;
    case Expression::kValueTest: {
      Label discard;
      __ push(source);
      TestAndBranch(source, true_label_, &discard);
      __ bind(&discard);
      __ pop();
      __ jmp(false_label_);
      break;
    }
    case Expression::kTestValue: {
      Label discard;
      __ push(source);
      TestAndBranch(source, &discard, false_label_);
      __ bind(&discard);
      __ pop();
      __ jmp(true_label_);
    }
  }
}


void FastCodeGenerator::Move(Expression::Context context, Slot* source) {
  switch (context) {
    case Expression::kUninitialized:
      UNREACHABLE();
    case Expression::kEffect:
      break;
    case Expression::kValue:  // Fall through.
    case Expression::kTest:  // Fall through.
    case Expression::kValueTest:  // Fall through.
    case Expression::kTestValue:
      __ ldr(ip, MemOperand(fp, SlotOffset(source)));
      Move(context, ip);
      break;
  }
}


void FastCodeGenerator::Move(Expression::Context context, Literal* expr) {
  switch (context) {
    case Expression::kUninitialized:
      UNREACHABLE();
    case Expression::kEffect:
      break;
    case Expression::kValue:  // Fall through.
    case Expression::kTest:  // Fall through.
    case Expression::kValueTest:  // Fall through.
    case Expression::kTestValue:
      __ mov(ip, Operand(expr->handle()));
      Move(context, ip);
      break;
  }
}


void FastCodeGenerator::DropAndMove(Expression::Context context,
                                    Register source) {
  switch (context) {
    case Expression::kUninitialized:
      UNREACHABLE();
    case Expression::kEffect:
      __ pop();
      break;
    case Expression::kValue:
      __ str(source, MemOperand(sp));
      break;
    case Expression::kTest:
      ASSERT(!source.is(sp));
      __ pop();
      TestAndBranch(source, true_label_, false_label_);
      break;
    case Expression::kValueTest: {
      Label discard;
      __ str(source, MemOperand(sp));
      TestAndBranch(source, true_label_, &discard);
      __ bind(&discard);
      __ pop();
      __ jmp(false_label_);
      break;
    }
    case Expression::kTestValue: {
      Label discard;
      __ str(source, MemOperand(sp));
      TestAndBranch(source, &discard, false_label_);
      __ bind(&discard);
      __ pop();
      __ jmp(true_label_);
      break;
    }
  }
}


void FastCodeGenerator::TestAndBranch(Register source,
                                      Label* true_label,
                                      Label* false_label) {
  ASSERT_NE(NULL, true_label);
  ASSERT_NE(NULL, false_label);
  // Call the runtime to find the boolean value of the source and then
  // translate it into control flow to the pair of labels.
  __ push(source);
  __ CallRuntime(Runtime::kToBool, 1);
  __ LoadRoot(ip, Heap::kTrueValueRootIndex);
  __ cmp(r0, ip);
  __ b(eq, true_label);
  __ jmp(false_label);
}


void FastCodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  // Call the runtime to declare the globals.
  // The context is the first argument.
  __ mov(r1, Operand(pairs));
  __ mov(r0, Operand(Smi::FromInt(is_eval_ ? 1 : 0)));
  __ stm(db_w, sp, cp.bit() | r1.bit() | r0.bit());
  __ CallRuntime(Runtime::kDeclareGlobals, 3);
  // Return value is ignored.
}


void FastCodeGenerator::VisitReturnStatement(ReturnStatement* stmt) {
  Comment cmnt(masm_, "[ ReturnStatement");
  SetStatementPosition(stmt);
  Expression* expr = stmt->expression();
  // Complete the statement based on the type of the subexpression.
  if (expr->AsLiteral() != NULL) {
    __ mov(r0, Operand(expr->AsLiteral()->handle()));
  } else {
    ASSERT_EQ(Expression::kValue, expr->context());
    Visit(expr);
    __ pop(r0);
  }
  if (return_label_.is_bound()) {
    __ b(&return_label_);
  } else {
    __ bind(&return_label_);
    if (FLAG_trace) {
      __ push(r0);
      __ CallRuntime(Runtime::kTraceExit, 1);
    }
    __ RecordJSReturn();
    __ mov(sp, fp);
    __ ldm(ia_w, sp, fp.bit() | lr.bit());
    int num_parameters = function_->scope()->num_parameters();
    __ add(sp, sp, Operand((num_parameters + 1) * kPointerSize));
    __ Jump(lr);
  }
}


void FastCodeGenerator::VisitFunctionLiteral(FunctionLiteral* expr) {
  Comment cmnt(masm_, "[ FunctionLiteral");

  // Build the function boilerplate and instantiate it.
  Handle<JSFunction> boilerplate = BuildBoilerplate(expr);
  if (HasStackOverflow()) return;

  ASSERT(boilerplate->IsBoilerplate());

  // Create a new closure.
  __ mov(r0, Operand(boilerplate));
  __ stm(db_w, sp, cp.bit() | r0.bit());
  __ CallRuntime(Runtime::kNewClosure, 2);
  Move(expr->context(), r0);
}


void FastCodeGenerator::VisitVariableProxy(VariableProxy* expr) {
  Comment cmnt(masm_, "[ VariableProxy");
  Expression* rewrite = expr->var()->rewrite();
  if (rewrite == NULL) {
    Comment cmnt(masm_, "Global variable");
    // Use inline caching. Variable name is passed in r2 and the global
    // object on the stack.
    __ ldr(ip, CodeGenerator::GlobalObject());
    __ push(ip);
    __ mov(r2, Operand(expr->name()));
    Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
    __ Call(ic, RelocInfo::CODE_TARGET_CONTEXT);
    DropAndMove(expr->context(), r0);
  } else {
    Comment cmnt(masm_, "Stack slot");
    Move(expr->context(), rewrite->AsSlot());
  }
}


void FastCodeGenerator::VisitRegExpLiteral(RegExpLiteral* expr) {
  Comment cmnt(masm_, "[ RegExp Literal");
  Label done;
  // Registers will be used as follows:
  // r4 = JS function, literals array
  // r3 = literal index
  // r2 = RegExp pattern
  // r1 = RegExp flags
  // r0 = temp + return value (RegExp literal)
  __ ldr(r0, MemOperand(fp,  JavaScriptFrameConstants::kFunctionOffset));
  __ ldr(r4,  FieldMemOperand(r0, JSFunction::kLiteralsOffset));
  int literal_offset =
    FixedArray::kHeaderSize + expr->literal_index() * kPointerSize;
  __ ldr(r0, FieldMemOperand(r4, literal_offset));
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ cmp(r0, ip);
  __ b(ne, &done);
  __ mov(r3, Operand(Smi::FromInt(expr->literal_index())));
  __ mov(r2, Operand(expr->pattern()));
  __ mov(r1, Operand(expr->flags()));
  __ stm(db_w, sp, r4.bit() | r3.bit() | r2.bit() | r1.bit());
  __ CallRuntime(Runtime::kMaterializeRegExpLiteral, 4);
  __ bind(&done);
  Move(expr->context(), r0);
}


void FastCodeGenerator::VisitObjectLiteral(ObjectLiteral* expr) {
  Comment cmnt(masm_, "[ ObjectLiteral");
  Label boilerplate_exists;
  __ ldr(r2, MemOperand(fp,  JavaScriptFrameConstants::kFunctionOffset));
  // r2 = literal array (0).
  __ ldr(r2, FieldMemOperand(r2, JSFunction::kLiteralsOffset));
  int literal_offset =
      FixedArray::kHeaderSize + expr->literal_index() * kPointerSize;
  __ ldr(r0, FieldMemOperand(r2, literal_offset));
  // Check whether we need to materialize the object literal boilerplate.
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ cmp(r0, Operand(ip));
  __ b(ne, &boilerplate_exists);
  // Create boilerplate if it does not exist.
  // r1 = literal index (1).
  __ mov(r1, Operand(Smi::FromInt(expr->literal_index())));
  // r0 = constant properties (2).
  __ mov(r0, Operand(expr->constant_properties()));
  __ stm(db_w, sp, r2.bit() | r1.bit() | r0.bit());
  __ CallRuntime(Runtime::kCreateObjectLiteralBoilerplate, 3);
  __ bind(&boilerplate_exists);
  // r0 contains boilerplate.
  // Clone boilerplate.
  __ push(r0);
  if (expr->depth() > 1) {
    __ CallRuntime(Runtime::kCloneLiteralBoilerplate, 1);
  } else {
    __ CallRuntime(Runtime::kCloneShallowLiteralBoilerplate, 1);
  }

  // If result_saved == true: the result is saved on top of the stack.
  // If result_saved == false: the result is in r0.
  bool result_saved = false;

  for (int i = 0; i < expr->properties()->length(); i++) {
    ObjectLiteral::Property* property = expr->properties()->at(i);
    if (property->IsCompileTimeValue()) continue;

    Literal* key = property->key();
    Expression* value = property->value();
    if (!result_saved) {
      __ push(r0);  // Save result on stack
      result_saved = true;
    }
    switch (property->kind()) {
      case ObjectLiteral::Property::CONSTANT:
        UNREACHABLE();

      case ObjectLiteral::Property::MATERIALIZED_LITERAL:   // Fall through.
        ASSERT(!CompileTimeValue::IsCompileTimeValue(property->value()));
      case ObjectLiteral::Property::COMPUTED:
        if (key->handle()->IsSymbol()) {
          Visit(value);
          ASSERT_EQ(Expression::kValue, value->context());
          __ pop(r0);
          __ mov(r2, Operand(key->handle()));
          Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
          __ Call(ic, RelocInfo::CODE_TARGET);
          // StoreIC leaves the receiver on the stack.
          break;
        }
        // Fall through.

      case ObjectLiteral::Property::PROTOTYPE:
        __ push(r0);
        Visit(key);
        ASSERT_EQ(Expression::kValue, key->context());
        Visit(value);
        ASSERT_EQ(Expression::kValue, value->context());
        __ CallRuntime(Runtime::kSetProperty, 3);
        __ ldr(r0, MemOperand(sp));  // Restore result into r0
        break;

      case ObjectLiteral::Property::GETTER:  // Fall through.
      case ObjectLiteral::Property::SETTER:
        __ push(r0);
        Visit(key);
        ASSERT_EQ(Expression::kValue, key->context());
        __ mov(r1, Operand(property->kind() == ObjectLiteral::Property::SETTER ?
                           Smi::FromInt(1) :
                           Smi::FromInt(0)));
        __ push(r1);
        Visit(value);
        ASSERT_EQ(Expression::kValue, value->context());
        __ CallRuntime(Runtime::kDefineAccessor, 4);
        __ ldr(r0, MemOperand(sp));  // Restore result into r0
        break;
    }
  }
  switch (expr->context()) {
    case Expression::kUninitialized:
      UNREACHABLE();
    case Expression::kEffect:
      if (result_saved) __ pop();
      break;
    case Expression::kValue:
      if (!result_saved) __ push(r0);
      break;
    case Expression::kTest:
      if (result_saved) __ pop(r0);
      TestAndBranch(r0, true_label_, false_label_);
      break;
    case Expression::kValueTest: {
      Label discard;
      if (!result_saved) __ push(r0);
      TestAndBranch(r0, true_label_, &discard);
      __ bind(&discard);
      __ pop();
      __ jmp(false_label_);
      break;
    }
    case Expression::kTestValue: {
      Label discard;
      if (!result_saved) __ push(r0);
      TestAndBranch(r0, &discard, false_label_);
      __ bind(&discard);
      __ pop();
      __ jmp(true_label_);
      break;
    }
  }
}


void FastCodeGenerator::VisitArrayLiteral(ArrayLiteral* expr) {
  Comment cmnt(masm_, "[ ArrayLiteral");
  Label make_clone;

  // Fetch the function's literals array.
  __ ldr(r3, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ ldr(r3, FieldMemOperand(r3, JSFunction::kLiteralsOffset));
  // Check if the literal's boilerplate has been instantiated.
  int offset =
      FixedArray::kHeaderSize + (expr->literal_index() * kPointerSize);
  __ ldr(r0, FieldMemOperand(r3, offset));
  __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
  __ cmp(r0, ip);
  __ b(&make_clone, ne);

  // Instantiate the boilerplate.
  __ mov(r2, Operand(Smi::FromInt(expr->literal_index())));
  __ mov(r1, Operand(expr->literals()));
  __ stm(db_w, sp, r3.bit() | r2.bit() | r1.bit());
  __ CallRuntime(Runtime::kCreateArrayLiteralBoilerplate, 3);

  __ bind(&make_clone);
  // Clone the boilerplate.
  __ push(r0);
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
      __ push(r0);
      result_saved = true;
    }
    Visit(subexpr);
    ASSERT_EQ(Expression::kValue, subexpr->context());

    // Store the subexpression value in the array's elements.
    __ pop(r0);  // Subexpression value.
    __ ldr(r1, MemOperand(sp));  // Copy of array literal.
    __ ldr(r1, FieldMemOperand(r1, JSObject::kElementsOffset));
    int offset = FixedArray::kHeaderSize + (i * kPointerSize);
    __ str(r0, FieldMemOperand(r1, offset));

    // Update the write barrier for the array store with r0 as the scratch
    // register.
    __ mov(r2, Operand(offset));
    __ RecordWrite(r1, r2, r0);
  }

  switch (expr->context()) {
    case Expression::kUninitialized:
      UNREACHABLE();
    case Expression::kEffect:
      if (result_saved) __ pop();
      break;
    case Expression::kValue:
      if (!result_saved) __ push(r0);
      break;
    case Expression::kTest:
      if (result_saved) __ pop(r0);
      TestAndBranch(r0, true_label_, false_label_);
      break;
    case Expression::kValueTest: {
      Label discard;
      if (!result_saved) __ push(r0);
      TestAndBranch(r0, true_label_, &discard);
      __ bind(&discard);
      __ pop();
      __ jmp(false_label_);
      break;
    }
    case Expression::kTestValue: {
      Label discard;
      if (!result_saved) __ push(r0);
      TestAndBranch(r0, &discard, false_label_);
      __ bind(&discard);
      __ pop();
      __ jmp(true_label_);
      break;
    }
  }
}


void FastCodeGenerator::EmitVariableAssignment(Assignment* expr) {
  Variable* var = expr->target()->AsVariableProxy()->AsVariable();
  ASSERT(var != NULL);

  if (var->is_global()) {
    // Assignment to a global variable.  Use inline caching for the
    // assignment.  Right-hand-side value is passed in r0, variable name in
    // r2, and the global object on the stack.
    __ pop(r0);
    __ mov(r2, Operand(var->name()));
    __ ldr(ip, CodeGenerator::GlobalObject());
    __ push(ip);
    Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
    __ Call(ic, RelocInfo::CODE_TARGET);
    // Overwrite the global object on the stack with the result if needed.
    DropAndMove(expr->context(), r0);

  } else {
    switch (expr->context()) {
      case Expression::kUninitialized:
        UNREACHABLE();
      case Expression::kEffect:
        // Perform assignment and discard value.
        __ pop(r0);
        __ str(r0, MemOperand(fp, SlotOffset(var->slot())));
        break;
      case Expression::kValue:
        // Perform assignment and preserve value.
        __ ldr(r0, MemOperand(sp));
        __ str(r0, MemOperand(fp, SlotOffset(var->slot())));
        break;
      case Expression::kTest:
        // Perform assignment and test (and discard) value.
        __ pop(r0);
        __ str(r0, MemOperand(fp, SlotOffset(var->slot())));
        TestAndBranch(r0, true_label_, false_label_);
        break;
      case Expression::kValueTest: {
        Label discard;
        __ ldr(r0, MemOperand(sp));
        __ str(r0, MemOperand(fp, SlotOffset(var->slot())));
        TestAndBranch(r0, true_label_, &discard);
        __ bind(&discard);
        __ pop();
        __ jmp(false_label_);
        break;
      }
      case Expression::kTestValue: {
        Label discard;
        __ ldr(r0, MemOperand(sp));
        __ str(r0, MemOperand(fp, SlotOffset(var->slot())));
        TestAndBranch(r0, &discard, false_label_);
        __ bind(&discard);
        __ pop();
        __ jmp(true_label_);
        break;
      }
    }
  }
}


void FastCodeGenerator::EmitNamedPropertyAssignment(Assignment* expr) {
  // Assignment to a property, using a named store IC.
  Property* prop = expr->target()->AsProperty();
  ASSERT(prop != NULL);
  ASSERT(prop->key()->AsLiteral() != NULL);

  // If the assignment starts a block of assignments to the same object,
  // change to slow case to avoid the quadratic behavior of repeatedly
  // adding fast properties.
  if (expr->starts_initialization_block()) {
    __ ldr(ip, MemOperand(sp, kPointerSize));  // Receiver is under value.
    __ push(ip);
    __ CallRuntime(Runtime::kToSlowProperties, 1);
  }

  __ pop(r0);
  __ mov(r2, Operand(prop->key()->AsLiteral()->handle()));
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
  __ Call(ic, RelocInfo::CODE_TARGET);

  // If the assignment ends an initialization block, revert to fast case.
  if (expr->ends_initialization_block()) {
    __ push(r0);  // Result of assignment, saved even if not needed.
    __ ldr(ip, MemOperand(sp, kPointerSize));  // Receiver is under value.
    __ push(ip);
    __ CallRuntime(Runtime::kToFastProperties, 1);
    __ pop(r0);
  }

  DropAndMove(expr->context(), r0);
}


void FastCodeGenerator::EmitKeyedPropertyAssignment(Assignment* expr) {
  // Assignment to a property, using a keyed store IC.

  // If the assignment starts a block of assignments to the same object,
  // change to slow case to avoid the quadratic behavior of repeatedly
  // adding fast properties.
  if (expr->starts_initialization_block()) {
    // Reciever is under the key and value.
    __ ldr(ip, MemOperand(sp, 2 * kPointerSize));
    __ push(ip);
    __ CallRuntime(Runtime::kToSlowProperties, 1);
  }

  __ pop(r0);
  Handle<Code> ic(Builtins::builtin(Builtins::KeyedStoreIC_Initialize));
  __ Call(ic, RelocInfo::CODE_TARGET);

  // If the assignment ends an initialization block, revert to fast case.
  if (expr->ends_initialization_block()) {
    __ push(r0);  // Result of assignment, saved even if not needed.
    // Reciever is under the key and value.
    __ ldr(ip, MemOperand(sp, 2 * kPointerSize));
    __ push(ip);
    __ CallRuntime(Runtime::kToFastProperties, 1);
    __ pop(r0);
  }

  // Receiver and key are still on stack.
  __ add(sp, sp, Operand(2 * kPointerSize));
  Move(expr->context(), r0);
}


void FastCodeGenerator::VisitProperty(Property* expr) {
  Comment cmnt(masm_, "[ Property");
  Expression* key = expr->key();
  uint32_t dummy;

  // Record the source position for the property load.
  SetSourcePosition(expr->position());

  // Evaluate receiver.
  Visit(expr->obj());

  if (key->AsLiteral() != NULL && key->AsLiteral()->handle()->IsSymbol() &&
      !String::cast(*(key->AsLiteral()->handle()))->AsArrayIndex(&dummy)) {
    // Do a NAMED property load.
    // The IC expects the property name in ecx and the receiver on the stack.
    __ mov(r2, Operand(key->AsLiteral()->handle()));
    Handle<Code> ic(Builtins::builtin(Builtins::LoadIC_Initialize));
    __ Call(ic, RelocInfo::CODE_TARGET);
  } else {
    // Do a KEYED property load.
    Visit(expr->key());
    Handle<Code> ic(Builtins::builtin(Builtins::KeyedLoadIC_Initialize));
    __ Call(ic, RelocInfo::CODE_TARGET);
    // Drop key and receiver left on the stack by IC.
    __ pop();
  }
  DropAndMove(expr->context(), r0);
}

void FastCodeGenerator::EmitCallWithIC(Call* expr, RelocInfo::Mode reloc_info) {
  // Code common for calls using the IC.
  ZoneList<Expression*>* args = expr->arguments();
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    Visit(args->at(i));
    ASSERT_EQ(Expression::kValue, args->at(i)->context());
  }
  // Record source position for debugger.
  SetSourcePosition(expr->position());
  // Call the IC initialization code.
  Handle<Code> ic = CodeGenerator::ComputeCallInitialize(arg_count,
                                                         NOT_IN_LOOP);
  __ Call(ic, reloc_info);
  // Restore context register.
  __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  // Discard the function left on TOS.
  DropAndMove(expr->context(), r0);
}


void FastCodeGenerator::EmitCallWithStub(Call* expr) {
  // Code common for calls using the call stub.
  ZoneList<Expression*>* args = expr->arguments();
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    Visit(args->at(i));
  }
  // Record source position for debugger.
  SetSourcePosition(expr->position());
  CallFunctionStub stub(arg_count, NOT_IN_LOOP);
  __ CallStub(&stub);
  // Restore context register.
  __ ldr(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  // Discard the function left on TOS.
  DropAndMove(expr->context(), r0);
}


void FastCodeGenerator::VisitCall(Call* expr) {
  Expression* fun = expr->expression();
  Variable* var = fun->AsVariableProxy()->AsVariable();

  if (var != NULL &&
      var->is_possibly_eval()) {
    // Call to eval.
    UNREACHABLE();
  } else if (fun->AsProperty() != NULL) {
    // Call on a property.
    Property* prop = fun->AsProperty();
    Literal* key = prop->key()->AsLiteral();
    if (key != NULL && key->handle()->IsSymbol()) {
      // Call on a named property: foo.x(1,2,3)
      __ mov(r0, Operand(key->handle()));
      __ push(r0);
      Visit(prop->obj());
      // Use call IC.
      EmitCallWithIC(expr, RelocInfo::CODE_TARGET);
    } else {
      // Call on a keyed property : foo[key](1,2,3)
      // Use a keyed load IC followed by a call IC.
      Visit(prop->obj());
      Visit(prop->key());
      // Record source position of property.
      SetSourcePosition(prop->position());
      Handle<Code> ic(Builtins::builtin(Builtins::KeyedLoadIC_Initialize));
      __ Call(ic, RelocInfo::CODE_TARGET);
      // Load receiver object into r1.
      if (prop->is_synthetic()) {
        __ ldr(r1, CodeGenerator::GlobalObject());
      } else {
        __ ldr(r1, MemOperand(sp, kPointerSize));
      }
      // Overwrite (object, key) with (function, receiver).
      __ str(r0, MemOperand(sp, kPointerSize));
      __ str(r1, MemOperand(sp));
      EmitCallWithStub(expr);
    }
  } else if (var != NULL) {
    // Call on a global variable
    ASSERT(var != NULL && !var->is_this() && var->is_global());
    ASSERT(!var->is_possibly_eval());
    __ mov(r1, Operand(var->name()));
    // Push global object as receiver.
    __ ldr(r0, CodeGenerator::GlobalObject());
    __ stm(db_w, sp, r1.bit() | r0.bit());
    EmitCallWithIC(expr, RelocInfo::CODE_TARGET_CONTEXT);
  } else if (var != NULL && var->slot() != NULL &&
             var->slot()->type() == Slot::LOOKUP) {
    // Call inside a with-statement
    UNREACHABLE();
  } else {
    // Call with an arbitrary function expression.
    Visit(expr->expression());
    // Load global receiver object.
    __ ldr(r1, CodeGenerator::GlobalObject());
    __ ldr(r1, FieldMemOperand(r1, GlobalObject::kGlobalReceiverOffset));
    __ push(r1);
    // Emit function call.
    EmitCallWithStub(expr);
  }
}


void FastCodeGenerator::VisitCallNew(CallNew* expr) {
  Comment cmnt(masm_, "[ CallNew");
  // According to ECMA-262, section 11.2.2, page 44, the function
  // expression in new calls must be evaluated before the
  // arguments.
  // Push function on the stack.
  Visit(expr->expression());
  ASSERT_EQ(Expression::kValue, expr->expression()->context());

  // Push global object (receiver).
  __ ldr(r0, CodeGenerator::GlobalObject());
  __ push(r0);
  // Push the arguments ("left-to-right") on the stack.
  ZoneList<Expression*>* args = expr->arguments();
  int arg_count = args->length();
  for (int i = 0; i < arg_count; i++) {
    Visit(args->at(i));
    ASSERT_EQ(Expression::kValue, args->at(i)->context());
    // If location is value, it is already on the stack,
    // so nothing to do here.
  }

  // Call the construct call builtin that handles allocation and
  // constructor invocation.
  SetSourcePosition(expr->position());

  // Load function, arg_count into r1 and r0.
  __ mov(r0, Operand(arg_count));
  // Function is in esp[arg_count + 1].
  __ ldr(r1, MemOperand(sp, (arg_count + 1) * kPointerSize));

  Handle<Code> construct_builtin(Builtins::builtin(Builtins::JSConstructCall));
  __ Call(construct_builtin, RelocInfo::CONSTRUCT_CALL);

  // Replace function on TOS with result in r0, or pop it.
  DropAndMove(expr->context(), r0);
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
    ASSERT_EQ(Expression::kValue, args->at(i)->context());
  }

  __ CallRuntime(function, arg_count);
  Move(expr->context(), r0);
}


void FastCodeGenerator::VisitUnaryOperation(UnaryOperation* expr) {
  Comment cmnt(masm_, "[ UnaryOperation");

  switch (expr->op()) {
    case Token::VOID:
      Visit(expr->expression());
      ASSERT_EQ(Expression::kEffect, expr->expression()->context());
      switch (expr->context()) {
        case Expression::kUninitialized:
          UNREACHABLE();
          break;
        case Expression::kEffect:
          break;
        case Expression::kValue:
          __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
          __ push(ip);
          break;
        case Expression::kTestValue:
          // Value is false so it's needed.
          __ LoadRoot(ip, Heap::kUndefinedValueRootIndex);
          __ push(ip);
        case Expression::kTest:  // Fall through.
        case Expression::kValueTest:
          __ jmp(false_label_);
          break;
      }
      break;

    case Token::NOT: {
      ASSERT_EQ(Expression::kTest, expr->expression()->context());

      Label push_true;
      Label push_false;
      Label done;
      Label* saved_true = true_label_;
      Label* saved_false = false_label_;
      switch (expr->context()) {
        case Expression::kUninitialized:
          UNREACHABLE();
          break;

        case Expression::kValue:
          true_label_ = &push_false;
          false_label_ = &push_true;
          Visit(expr->expression());
          __ bind(&push_true);
          __ LoadRoot(ip, Heap::kTrueValueRootIndex);
          __ push(ip);
          __ jmp(&done);
          __ bind(&push_false);
          __ LoadRoot(ip, Heap::kFalseValueRootIndex);
          __ push(ip);
          __ bind(&done);
          break;

        case Expression::kEffect:
          true_label_ = &done;
          false_label_ = &done;
          Visit(expr->expression());
          __ bind(&done);
          break;

        case Expression::kTest:
          true_label_ = saved_false;
          false_label_ = saved_true;
          Visit(expr->expression());
          break;

        case Expression::kValueTest:
          true_label_ = saved_false;
          false_label_ = &push_true;
          Visit(expr->expression());
          __ bind(&push_true);
          __ LoadRoot(ip, Heap::kTrueValueRootIndex);
          __ push(ip);
          __ jmp(saved_true);
          break;

        case Expression::kTestValue:
          true_label_ = &push_false;
          false_label_ = saved_true;
          Visit(expr->expression());
          __ bind(&push_false);
          __ LoadRoot(ip, Heap::kFalseValueRootIndex);
          __ push(ip);
          __ jmp(saved_false);
          break;
      }
      true_label_ = saved_true;
      false_label_ = saved_false;
      break;
    }

    default:
      UNREACHABLE();
  }
}


void FastCodeGenerator::VisitCountOperation(CountOperation* expr) {
  VariableProxy* v = expr->expression()->AsVariableProxy();
  ASSERT(v->AsVariable() != NULL);
  ASSERT(v->AsVariable()->is_global());

  Visit(v);

  __ InvokeBuiltin(Builtins::TO_NUMBER, CALL_JS);

  switch (expr->context()) {
    case Expression::kUninitialized:
      UNREACHABLE();
    case Expression::kValue:  // Fall through
    case Expression::kTest:  // Fall through
    case Expression::kTestValue:  // Fall through
    case Expression::kValueTest:
      // Duplicate the result on the stack.
      __ push(r0);
      break;
    case Expression::kEffect:
      // Do not save result.
      break;
  }
  // Call runtime for +1/-1.
  __ push(r0);
  __ mov(ip, Operand(Smi::FromInt(1)));
  __ push(ip);
  if (expr->op() == Token::INC) {
    __ CallRuntime(Runtime::kNumberAdd, 2);
  } else {
    __ CallRuntime(Runtime::kNumberSub, 2);
  }
  // Call Store IC.
  __ mov(r2, Operand(v->AsVariable()->name()));
  __ ldr(ip, CodeGenerator::GlobalObject());
  __ push(ip);
  Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
  __ Call(ic, RelocInfo::CODE_TARGET);
  // Restore up stack after store IC.
  __ add(sp, sp, Operand(kPointerSize));

  switch (expr->context()) {
    case Expression::kUninitialized:
      UNREACHABLE();
    case Expression::kEffect:  // Fall through
    case Expression::kValue:
      // Do nothing. Result in either on the stack for value context
      // or discarded for effect context.
      break;
    case Expression::kTest:
      __ pop(r0);
      TestAndBranch(r0, true_label_, false_label_);
      break;
    case Expression::kValueTest: {
      Label discard;
      __ ldr(r0, MemOperand(sp));
      TestAndBranch(r0, true_label_, &discard);
      __ bind(&discard);
      __ add(sp, sp, Operand(kPointerSize));
      __ b(false_label_);
      break;
    }
    case Expression::kTestValue: {
      Label discard;
      __ ldr(r0, MemOperand(sp));
      TestAndBranch(r0, &discard, false_label_);
      __ bind(&discard);
      __ add(sp, sp, Operand(kPointerSize));
      __ b(true_label_);
      break;
    }
  }
}


void FastCodeGenerator::VisitBinaryOperation(BinaryOperation* expr) {
  switch (expr->op()) {
    case Token::COMMA:
      ASSERT_EQ(Expression::kEffect, expr->left()->context());
      ASSERT_EQ(expr->context(), expr->right()->context());
      Visit(expr->left());
      Visit(expr->right());
      break;

    case Token::OR:
    case Token::AND:
      EmitLogicalOperation(expr);
      break;

    case Token::ADD:
    case Token::SUB:
    case Token::DIV:
    case Token::MOD:
    case Token::MUL:
    case Token::BIT_OR:
    case Token::BIT_AND:
    case Token::BIT_XOR:
    case Token::SHL:
    case Token::SHR:
    case Token::SAR: {
      ASSERT_EQ(Expression::kValue, expr->left()->context());
      ASSERT_EQ(Expression::kValue, expr->right()->context());

      Visit(expr->left());
      Visit(expr->right());
      __ pop(r0);
      __ pop(r1);
      GenericBinaryOpStub stub(expr->op(),
                               NO_OVERWRITE);
      __ CallStub(&stub);
      Move(expr->context(), r0);

      break;
    }
    default:
      UNREACHABLE();
  }
}


void FastCodeGenerator::VisitCompareOperation(CompareOperation* expr) {
  ASSERT_EQ(Expression::kValue, expr->left()->context());
  ASSERT_EQ(Expression::kValue, expr->right()->context());
  Visit(expr->left());
  Visit(expr->right());

  // Convert current context to test context: Pre-test code.
  Label push_true;
  Label push_false;
  Label done;
  Label* saved_true = true_label_;
  Label* saved_false = false_label_;
  switch (expr->context()) {
    case Expression::kUninitialized:
      UNREACHABLE();
      break;

    case Expression::kValue:
      true_label_ = &push_true;
      false_label_ = &push_false;
      break;

    case Expression::kEffect:
      true_label_ = &done;
      false_label_ = &done;
      break;

    case Expression::kTest:
      break;

    case Expression::kValueTest:
      true_label_ = &push_true;
      break;

    case Expression::kTestValue:
      false_label_ = &push_false;
      break;
  }
  // Convert current context to test context: End pre-test code.

  switch (expr->op()) {
    case Token::IN: {
      __ InvokeBuiltin(Builtins::IN, CALL_JS);
      __ LoadRoot(ip, Heap::kTrueValueRootIndex);
      __ cmp(r0, ip);
      __ b(eq, true_label_);
      __ jmp(false_label_);
      break;
    }

    case Token::INSTANCEOF: {
      InstanceofStub stub;
      __ CallStub(&stub);
      __ tst(r0, r0);
      __ b(eq, true_label_);  // The stub returns 0 for true.
      __ jmp(false_label_);
      break;
    }

    default: {
      Condition cc = eq;
      bool strict = false;
      switch (expr->op()) {
        case Token::EQ_STRICT:
          strict = true;
          // Fall through
        case Token::EQ:
          cc = eq;
          __ pop(r0);
          __ pop(r1);
          break;
        case Token::LT:
          cc = lt;
          __ pop(r0);
          __ pop(r1);
          break;
        case Token::GT:
          // Reverse left and right sizes to obtain ECMA-262 conversion order.
          cc = lt;
          __ pop(r1);
          __ pop(r0);
         break;
        case Token::LTE:
          // Reverse left and right sizes to obtain ECMA-262 conversion order.
          cc = ge;
          __ pop(r1);
          __ pop(r0);
          break;
        case Token::GTE:
          cc = ge;
          __ pop(r0);
          __ pop(r1);
          break;
        case Token::IN:
        case Token::INSTANCEOF:
        default:
          UNREACHABLE();
      }

      // The comparison stub expects the smi vs. smi case to be handled
      // before it is called.
      Label slow_case;
      __ orr(r2, r0, Operand(r1));
      __ tst(r2, Operand(kSmiTagMask));
      __ b(ne, &slow_case);
      __ cmp(r1, r0);
      __ b(cc, true_label_);
      __ jmp(false_label_);

      __ bind(&slow_case);
      CompareStub stub(cc, strict);
      __ CallStub(&stub);
      __ tst(r0, r0);
      __ b(cc, true_label_);
      __ jmp(false_label_);
    }
  }

  // Convert current context to test context: Post-test code.
  switch (expr->context()) {
    case Expression::kUninitialized:
      UNREACHABLE();
      break;

    case Expression::kValue:
      __ bind(&push_true);
      __ LoadRoot(ip, Heap::kTrueValueRootIndex);
      __ push(ip);
      __ jmp(&done);
      __ bind(&push_false);
      __ LoadRoot(ip, Heap::kFalseValueRootIndex);
      __ push(ip);
      __ bind(&done);
      break;

    case Expression::kEffect:
      __ bind(&done);
      break;

    case Expression::kTest:
      break;

    case Expression::kValueTest:
      __ bind(&push_true);
      __ LoadRoot(ip, Heap::kTrueValueRootIndex);
      __ push(ip);
      __ jmp(saved_true);
      break;

    case Expression::kTestValue:
      __ bind(&push_false);
      __ LoadRoot(ip, Heap::kFalseValueRootIndex);
      __ push(ip);
      __ jmp(saved_false);
      break;
  }
  true_label_ = saved_true;
  false_label_ = saved_false;
  // Convert current context to test context: End post-test code.
}


#undef __


} }  // namespace v8::internal
