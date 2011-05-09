// Copyright 2011 the V8 project authors. All rights reserved.
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

#if defined(V8_TARGET_ARCH_MIPS)

// Note on Mips implementation:
//
// The result_register() for mips is the 'v0' register, which is defined
// by the ABI to contain function return values. However, the first
// parameter to a function is defined to be 'a0'. So there are many
// places where we have to move a previous result in v0 to a0 for the
// next call: mov(a0, v0). This is not needed on the other architectures.

#include "code-stubs.h"
#include "codegen-inl.h"
#include "compiler.h"
#include "debug.h"
#include "full-codegen.h"
#include "parser.h"
#include "scopes.h"
#include "stub-cache.h"

#include "mips/code-stubs-mips.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm_)

// Generate code for a JS function.  On entry to the function the receiver
// and arguments have been pushed on the stack left to right.  The actual
// argument count matches the formal parameter count expected by the
// function.
//
// The live registers are:
//   o a1: the JS function object being called (ie, ourselves)
//   o cp: our context
//   o fp: our caller's frame pointer
//   o sp: stack pointer
//   o ra: return address
//
// The function builds a JS frame.  Please see JavaScriptFrameConstants in
// frames-mips.h for its layout.
void FullCodeGenerator::Generate(CompilationInfo* info) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::ClearAccumulator() {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitStackCheck(IterationStatement* stmt) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitReturnSequence() {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EffectContext::Plug(Slot* slot) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::AccumulatorValueContext::Plug(Slot* slot) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::StackValueContext::Plug(Slot* slot) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::TestContext::Plug(Slot* slot) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EffectContext::Plug(Heap::RootListIndex index) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::AccumulatorValueContext::Plug(
    Heap::RootListIndex index) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::StackValueContext::Plug(
    Heap::RootListIndex index) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::TestContext::Plug(Heap::RootListIndex index) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EffectContext::Plug(Handle<Object> lit) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::AccumulatorValueContext::Plug(
    Handle<Object> lit) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::StackValueContext::Plug(Handle<Object> lit) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::TestContext::Plug(Handle<Object> lit) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EffectContext::DropAndPlug(int count,
                                                   Register reg) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::AccumulatorValueContext::DropAndPlug(
    int count,
    Register reg) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::StackValueContext::DropAndPlug(int count,
                                                       Register reg) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::TestContext::DropAndPlug(int count,
                                                 Register reg) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EffectContext::Plug(Label* materialize_true,
                                            Label* materialize_false) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::AccumulatorValueContext::Plug(
    Label* materialize_true,
    Label* materialize_false) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::StackValueContext::Plug(
    Label* materialize_true,
    Label* materialize_false) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::TestContext::Plug(Label* materialize_true,
                                          Label* materialize_false) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EffectContext::Plug(bool flag) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::AccumulatorValueContext::Plug(bool flag) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::StackValueContext::Plug(bool flag) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::TestContext::Plug(bool flag) const {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::DoTest(Label* if_true,
                               Label* if_false,
                               Label* fall_through) {
  UNIMPLEMENTED_MIPS();
}


// Original prototype for mips, needs arch-indep change. Leave out for now.
// void FullCodeGenerator::Split(Condition cc,
//                               Register lhs,
//                               const Operand&  rhs,
//                               Label* if_true,
//                               Label* if_false,
//                               Label* fall_through) {
void FullCodeGenerator::Split(Condition cc,
                              Label* if_true,
                              Label* if_false,
                              Label* fall_through) {
  UNIMPLEMENTED_MIPS();
}


MemOperand FullCodeGenerator::EmitSlotSearch(Slot* slot, Register scratch) {
  UNIMPLEMENTED_MIPS();
  return MemOperand(zero_reg, 0);
}


void FullCodeGenerator::Move(Register destination, Slot* source) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::PrepareForBailoutBeforeSplit(State state,
                                                     bool should_normalize,
                                                     Label* if_true,
                                                     Label* if_false) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::Move(Slot* dst,
                             Register src,
                             Register scratch1,
                             Register scratch2) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitDeclaration(Variable* variable,
                                        Variable::Mode mode,
                                        FunctionLiteral* function) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitDeclaration(Declaration* decl) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::DeclareGlobals(Handle<FixedArray> pairs) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitSwitchStatement(SwitchStatement* stmt) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitForInStatement(ForInStatement* stmt) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitNewClosure(Handle<SharedFunctionInfo> info,
                                       bool pretenure) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitVariableProxy(VariableProxy* expr) {
  UNIMPLEMENTED_MIPS();
}


MemOperand FullCodeGenerator::ContextSlotOperandCheckExtensions(
    Slot* slot,
    Label* slow) {
  UNIMPLEMENTED_MIPS();
  return MemOperand(zero_reg, 0);
}


void FullCodeGenerator::EmitDynamicLoadFromSlotFastCase(
    Slot* slot,
    TypeofState typeof_state,
    Label* slow,
    Label* done) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitLoadGlobalSlotCheckExtensions(
    Slot* slot,
    TypeofState typeof_state,
    Label* slow) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitVariableLoad(Variable* var) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitRegExpLiteral(RegExpLiteral* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitObjectLiteral(ObjectLiteral* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitArrayLiteral(ArrayLiteral* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitAssignment(Assignment* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitNamedPropertyLoad(Property* prop) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitKeyedPropertyLoad(Property* prop) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitInlineSmiBinaryOp(Expression* expr,
                                              Token::Value op,
                                              OverwriteMode mode,
                                              Expression* left,
                                              Expression* right) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitBinaryOp(Token::Value op,
                                     OverwriteMode mode) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitAssignment(Expression* expr, int bailout_ast_id) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitVariableAssignment(Variable* var,
                                               Token::Value op) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitNamedPropertyAssignment(Assignment* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitKeyedPropertyAssignment(Assignment* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitProperty(Property* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitCallWithIC(Call* expr,
                                       Handle<Object> name,
                                       RelocInfo::Mode mode) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitKeyedCallWithIC(Call* expr,
                                            Expression* key,
                                            RelocInfo::Mode mode) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitCallWithStub(Call* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitCall(Call* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitCallNew(CallNew* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsSmi(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsNonNegativeSmi(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsObject(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsSpecObject(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsUndetectableObject(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsStringWrapperSafeForDefaultValueOf(
    ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsFunction(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsArray(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsRegExp(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsConstructCall(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitObjectEquals(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitArguments(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitArgumentsLength(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitClassOf(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitLog(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitRandomHeapNumber(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitSubString(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitRegExpExec(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitValueOf(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitMathPow(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitSetValueOf(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitNumberToString(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitStringCharFromCode(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitStringCharCodeAt(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitStringCharAt(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitStringAdd(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitStringCompare(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitMathSin(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitMathCos(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitMathSqrt(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitMathLog(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitCallFunction(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitRegExpConstructResult(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitSwapElements(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitGetFromCache(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitIsRegExpEquivalent(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitHasCachedArrayIndex(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitGetCachedArrayIndex(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::EmitFastAsciiArrayJoin(ZoneList<Expression*>* args) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitCallRuntime(CallRuntime* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitUnaryOperation(UnaryOperation* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitCountOperation(CountOperation* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitForTypeofValue(Expression* expr) {
  UNIMPLEMENTED_MIPS();
}


bool FullCodeGenerator::TryLiteralCompare(Token::Value op,
                                          Expression* left,
                                          Expression* right,
                                          Label* if_true,
                                          Label* if_false,
                                          Label* fall_through) {
  UNIMPLEMENTED_MIPS();
  return false;
}


void FullCodeGenerator::VisitCompareOperation(CompareOperation* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitCompareToNull(CompareToNull* expr) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::VisitThisFunction(ThisFunction* expr) {
  UNIMPLEMENTED_MIPS();
}


Register FullCodeGenerator::result_register() {
  UNIMPLEMENTED_MIPS();
  return v0;
}


Register FullCodeGenerator::context_register() {
  UNIMPLEMENTED_MIPS();
  return cp;
}


void FullCodeGenerator::EmitCallIC(Handle<Code> ic, RelocInfo::Mode mode) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::StoreToFrameField(int frame_offset, Register value) {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::LoadContextField(Register dst, int context_index) {
  UNIMPLEMENTED_MIPS();
}


// ----------------------------------------------------------------------------
// Non-local control flow support.

void FullCodeGenerator::EnterFinallyBlock() {
  UNIMPLEMENTED_MIPS();
}


void FullCodeGenerator::ExitFinallyBlock() {
  UNIMPLEMENTED_MIPS();
}


#undef __

} }  // namespace v8::internal

#endif  // V8_TARGET_ARCH_MIPS
