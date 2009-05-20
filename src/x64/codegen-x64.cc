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

namespace v8 { namespace internal {

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

void CodeGenerator::DeclareGlobals(Handle<FixedArray> a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenCode(FunctionLiteral* a) {
  masm_->int3();  // UNIMPLEMENTED
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


void CEntryStub::GenerateBody(MacroAssembler* masm, bool is_debug_break) {
  masm->int3();  // TODO(X64): UNIMPLEMENTED.
}

void JSEntryStub::GenerateBody(MacroAssembler* masm, bool is_construct) {
  masm->int3();  // TODO(X64): UNIMPLEMENTED.
}



} }  // namespace v8::internal
