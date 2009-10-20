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
    if (FLAG_check_stack) {
      __ LoadRoot(r2, Heap::kStackLimitRootIndex);
    }
    for (int i = 0; i < locals_count; i++) {
      __ push(ip);
    }
  }

  if (FLAG_check_stack) {
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
  }

  { Comment cmnt(masm_, "[ Body");
    VisitStatements(fun->body());
  }

  { Comment cmnt(masm_, "[ return <undefined>;");
    // Emit a 'return undefined' in case control fell off the end of the
    // body.
    __ LoadRoot(r0, Heap::kUndefinedValueRootIndex);
    SetReturnPosition(fun);
    __ RecordJSReturn();
    __ mov(sp, fp);
    __ ldm(ia_w, sp, fp.bit() | lr.bit());
    int num_parameters = function_->scope()->num_parameters();
    __ add(sp, sp, Operand((num_parameters + 1) * kPointerSize));
    __ Jump(lr);
  }
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
    __ pop(r0);
  } else {
    ASSERT(source.is_constant());
    ASSERT(expr->AsLiteral() != NULL);
    __ mov(r0, Operand(expr->AsLiteral()->handle()));
  }
  __ RecordJSReturn();
  __ mov(sp, fp);
  __ ldm(ia_w, sp, fp.bit() | lr.bit());
    int num_parameters = function_->scope()->num_parameters();
  __ add(sp, sp, Operand((num_parameters + 1) * kPointerSize));
  __ Jump(lr);
}


void FastCodeGenerator::VisitVariableProxy(VariableProxy* expr) {
  Comment cmnt(masm_, "[ VariableProxy");
  Expression* rewrite = expr->var()->rewrite();
  ASSERT(rewrite != NULL);

  Slot* slot = rewrite->AsSlot();
  ASSERT(slot != NULL);
  { Comment cmnt(masm_, "[ Slot");
    if (expr->location().is_temporary()) {
      __ ldr(ip, MemOperand(fp, SlotOffset(slot)));
      __ push(ip);
    } else {
      ASSERT(expr->location().is_nowhere());
    }
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
    // value is passed in r0, variable name in r2, and the global object on
    // the stack.
    if (source.is_temporary()) {
      __ pop(r0);
    } else {
      ASSERT(source.is_constant());
      ASSERT(rhs->AsLiteral() != NULL);
      __ mov(r0, Operand(rhs->AsLiteral()->handle()));
    }
    __ mov(r2, Operand(var->name()));
    __ ldr(ip, CodeGenerator::GlobalObject());
    __ push(ip);
    Handle<Code> ic(Builtins::builtin(Builtins::StoreIC_Initialize));
    __ Call(ic, RelocInfo::CODE_TARGET);
    // Overwrite the global object on the stack with the result if needed.
    if (destination.is_temporary()) {
      __ str(r0, MemOperand(sp));
    } else {
      ASSERT(destination.is_nowhere());
      __ pop();
    }

  } else {
    if (source.is_temporary()) {
      if (destination.is_temporary()) {
        // Case 'temp1 <- (var = temp0)'.  Preserve right-hand-side
        // temporary on the stack.
        __ ldr(ip, MemOperand(sp));
      } else {
        ASSERT(destination.is_nowhere());
        // Case 'var = temp'.  Discard right-hand-side temporary.
        __ pop(ip);
      }
      __ str(ip, MemOperand(fp, SlotOffset(var->slot())));
    } else {
      ASSERT(source.is_constant());
      ASSERT(rhs->AsLiteral() != NULL);
      // Two cases: 'temp <- (var = constant)', or 'var = constant' with a
      // discarded result.  Always perform the assignment.
      __ mov(ip, Operand(rhs->AsLiteral()->handle()));
      __ str(ip, MemOperand(fp, SlotOffset(var->slot())));
      if (destination.is_temporary()) {
        // Case 'temp <- (var = constant)'.  Save result.
        __ push(ip);
      }
    }
  }
}


} }  // namespace v8::internal
