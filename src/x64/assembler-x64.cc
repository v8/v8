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

namespace v8 { namespace internal {

Register no_reg = { -1 };


} }  // namespace v8::internal


// TODO(x64): Implement and move these to their correct cc-files:
#include "assembler.h"
#include "ast.h"
#include "bootstrapper.h"
#include "codegen-inl.h"
#include "cpu.h"
#include "debug.h"
#include "disasm.h"
#include "disassembler.h"
#include "frames-inl.h"
#include "x64/macro-assembler-x64.h"
#include "x64/regexp-macro-assembler-x64.h"
#include "ic-inl.h"
#include "log.h"
#include "macro-assembler.h"
#include "parser.h"
#include "regexp-macro-assembler.h"
#include "regexp-stack.h"
#include "register-allocator-inl.h"
#include "register-allocator.h"
#include "runtime.h"
#include "scopes.h"
#include "serialize.h"
#include "stub-cache.h"
#include "unicode.h"

namespace v8 { namespace internal {

void ArgumentsAccessStub::GenerateNewObject(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void ArgumentsAccessStub::GenerateReadElement(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void ArgumentsAccessStub::GenerateReadLength(MacroAssembler* a) {
  UNIMPLEMENTED();
}

// -----------------------------------------------------------------------------
// Implementation of Assembler

byte* Assembler::spare_buffer_ = NULL;

Assembler::Assembler(void* buffer, int buffer_size) {
  if (buffer == NULL) {
    // do our own buffer management
    if (buffer_size <= kMinimalBufferSize) {
      buffer_size = kMinimalBufferSize;

      if (spare_buffer_ != NULL) {
        buffer = spare_buffer_;
        spare_buffer_ = NULL;
      }
    }
    if (buffer == NULL) {
      buffer_ = NewArray<byte>(buffer_size);
    } else {
      buffer_ = static_cast<byte*>(buffer);
    }
    buffer_size_ = buffer_size;
    own_buffer_ = true;
  } else {
    // use externally provided buffer instead
    ASSERT(buffer_size > 0);
    buffer_ = static_cast<byte*>(buffer);
    buffer_size_ = buffer_size;
    own_buffer_ = false;
  }

  // Clear the buffer in debug mode unless it was provided by the
  // caller in which case we can't be sure it's okay to overwrite
  // existing code in it; see CodePatcher::CodePatcher(...).
#ifdef DEBUG
  if (own_buffer_) {
    memset(buffer_, 0xCC, buffer_size);  // int3
  }
#endif

  // setup buffer pointers
  ASSERT(buffer_ != NULL);
  pc_ = buffer_;
  reloc_info_writer.Reposition(buffer_ + buffer_size, pc_);

  last_pc_ = NULL;
  current_statement_position_ = RelocInfo::kNoPosition;
  current_position_ = RelocInfo::kNoPosition;
  written_statement_position_ = current_statement_position_;
  written_position_ = current_position_;
#ifdef GENERATED_CODE_COVERAGE
  InitCoverageLog();
#endif
}


Assembler::~Assembler() {
  if (own_buffer_) {
    if (spare_buffer_ == NULL && buffer_size_ == kMinimalBufferSize) {
      spare_buffer_ = buffer_;
    } else {
      DeleteArray(buffer_);
    }
  }
}


void Assembler::GetCode(CodeDesc* desc) {
  // finalize code
  // (at this point overflow() may be true, but the gap ensures that
  // we are still not overlapping instructions and relocation info)
  ASSERT(pc_ <= reloc_info_writer.pos());  // no overlap
  // setup desc
  desc->buffer = buffer_;
  desc->buffer_size = buffer_size_;
  desc->instr_size = pc_offset();
  desc->reloc_size = (buffer_ + buffer_size_) - reloc_info_writer.pos();
  desc->origin = this;

  Counters::reloc_info_size.Increment(desc->reloc_size);
}


void Assembler::Align(int m) {
  ASSERT(IsPowerOf2(m));
  while ((pc_offset() & (m - 1)) != 0) {
    nop();
  }
}


void Assembler::RecordComment(char const* a) {
  UNIMPLEMENTED();
}

void Assembler::RecordPosition(int a) {
  UNIMPLEMENTED();
}

void Assembler::RecordStatementPosition(int a) {
  UNIMPLEMENTED();
}

void Assembler::bind(Label* a) {
  UNIMPLEMENTED();
}


void Assembler::nop() {
  UNIMPLEMENTED();
}


void BreakLocationIterator::ClearDebugBreakAtReturn() {
  UNIMPLEMENTED();
}

bool BreakLocationIterator::IsDebugBreakAtReturn()  {
  UNIMPLEMENTED();
  return false;
}

void BreakLocationIterator::SetDebugBreakAtReturn()  {
  UNIMPLEMENTED();
}

void CEntryStub::GenerateBody(MacroAssembler* a, bool b) {
  UNIMPLEMENTED();
}

void CallIC::Generate(MacroAssembler* a, int b, ExternalReference const& c) {
  UNIMPLEMENTED();
}

void CallIC::GenerateMegamorphic(MacroAssembler* a, int b) {
  UNIMPLEMENTED();
}

void CallIC::GenerateNormal(MacroAssembler* a, int b) {
  UNIMPLEMENTED();
}

Object* CallStubCompiler::CompileCallConstant(Object* a,
                                              JSObject* b,
                                              JSFunction* c,
                                              StubCompiler::CheckType d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* CallStubCompiler::CompileCallField(Object* a,
                                           JSObject* b,
                                           int c,
                                           String* d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* CallStubCompiler::CompileCallInterceptor(Object* a,
                                                 JSObject* b,
                                                 String* c) {
  UNIMPLEMENTED();
  return NULL;
}

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
  UNIMPLEMENTED();
}

void CodeGenerator::DeclareGlobals(Handle<FixedArray> a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenCode(FunctionLiteral* a) {
  UNIMPLEMENTED();
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

void CpuFeatures::Probe()  {
  UNIMPLEMENTED();
}


bool Debug::IsDebugBreakAtReturn(v8::internal::RelocInfo*) {
  UNIMPLEMENTED();
  return false;
}

void Debug::GenerateCallICDebugBreak(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Debug::GenerateConstructCallDebugBreak(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Debug::GenerateKeyedLoadICDebugBreak(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Debug::GenerateKeyedStoreICDebugBreak(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Debug::GenerateLoadICDebugBreak(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Debug::GenerateReturnDebugBreak(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Debug::GenerateReturnDebugBreakEntry(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Debug::GenerateStoreICDebugBreak(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Debug::GenerateStubNoRegistersDebugBreak(MacroAssembler* a) {
  UNIMPLEMENTED();
}

StackFrame::Type ExitFrame::GetStateForFramePointer(unsigned char* a,
                                                    StackFrame::State* b) {
  UNIMPLEMENTED();
  return NONE;
}

void JSEntryStub::GenerateBody(MacroAssembler* a, bool b) {
  UNIMPLEMENTED();
}

int JavaScriptFrame::GetProvidedParametersCount() const {
  UNIMPLEMENTED();
  return 0;
}

void JumpTarget::DoBind(int a) {
  UNIMPLEMENTED();
}

void JumpTarget::DoBranch(Condition a, Hint b) {
  UNIMPLEMENTED();
}

void JumpTarget::DoJump() {
  UNIMPLEMENTED();
}

void KeyedLoadIC::ClearInlinedVersion(unsigned char* a) {
  UNIMPLEMENTED();
}

void KeyedLoadIC::Generate(MacroAssembler* a, ExternalReference const& b) {
  UNIMPLEMENTED();
}

void KeyedLoadIC::GenerateGeneric(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void KeyedLoadIC::GenerateMiss(MacroAssembler* a) {
  UNIMPLEMENTED();
}

bool KeyedLoadIC::PatchInlinedLoad(unsigned char* a, Object* b) {
  UNIMPLEMENTED();
  return false;
}

Object* KeyedLoadStubCompiler::CompileLoadArrayLength(String* a) {
  UNIMPLEMENTED();
  return NULL;
}

Object* KeyedLoadStubCompiler::CompileLoadCallback(String* a,
                                                   JSObject* b,
                                                   JSObject* c,
                                                   AccessorInfo* d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* KeyedLoadStubCompiler::CompileLoadConstant(String* a,
                                                   JSObject* b,
                                                   JSObject* c,
                                                   Object* d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* KeyedLoadStubCompiler::CompileLoadField(String* a,
                                                JSObject* b,
                                                JSObject* c,
                                                int d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* KeyedLoadStubCompiler::CompileLoadFunctionPrototype(String* a) {
  UNIMPLEMENTED();
  return NULL;
}

Object* KeyedLoadStubCompiler::CompileLoadInterceptor(JSObject* a,
                                                      JSObject* b,
                                                      String* c) {
  UNIMPLEMENTED();
  return NULL;
}

Object* KeyedLoadStubCompiler::CompileLoadStringLength(String* a) {
  UNIMPLEMENTED();
  return NULL;
}

void KeyedStoreIC::Generate(MacroAssembler* a, ExternalReference const& b) {
  UNIMPLEMENTED();
}

void KeyedStoreIC::GenerateExtendStorage(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void KeyedStoreIC::GenerateGeneric(MacroAssembler*a) {
  UNIMPLEMENTED();
}

Object* KeyedStoreStubCompiler::CompileStoreField(JSObject* a,
                                                  int b,
                                                  Map* c,
                                                  String* d) {
  UNIMPLEMENTED();
  return NULL;
}

void LoadIC::ClearInlinedVersion(unsigned char* a) {
  UNIMPLEMENTED();
}

void LoadIC::Generate(MacroAssembler* a, ExternalReference const& b) {
  UNIMPLEMENTED();
}

void LoadIC::GenerateArrayLength(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void LoadIC::GenerateFunctionPrototype(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void LoadIC::GenerateMegamorphic(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void LoadIC::GenerateMiss(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void LoadIC::GenerateNormal(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void LoadIC::GenerateStringLength(MacroAssembler* a) {
  UNIMPLEMENTED();
}

bool LoadIC::PatchInlinedLoad(unsigned char* a, Object* b, int c) {
  UNIMPLEMENTED();
  return false;
}

Object* LoadStubCompiler::CompileLoadCallback(JSObject* a,
                                              JSObject* b,
                                              AccessorInfo* c,
                                              String* d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* LoadStubCompiler::CompileLoadConstant(JSObject* a,
                                              JSObject* b,
                                              Object* c,
                                              String* d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* LoadStubCompiler::CompileLoadField(JSObject* a,
                                           JSObject* b,
                                           int c,
                                           String* d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* LoadStubCompiler::CompileLoadInterceptor(JSObject* a,
                                                 JSObject* b,
                                                 String* c) {
  UNIMPLEMENTED();
  return NULL;
}

MacroAssembler::MacroAssembler(void* buffer, int size)
  : Assembler(buffer, size),
    unresolved_(0),
    generating_stub_(false),
    allow_stub_calls_(true),
    code_object_(Heap::undefined_value()) {
  UNIMPLEMENTED();
}

void MacroAssembler::TailCallRuntime(ExternalReference const& a, int b) {
  UNIMPLEMENTED();
}

bool RegisterAllocator::IsReserved(int a) {
  UNIMPLEMENTED();
  return false;
}

RegisterFile RegisterAllocator::Reserved() {
  UNIMPLEMENTED();
  return RegisterFile();
}

const int RelocInfo::kApplyMask = -1;

StackFrame::Type StackFrame::ComputeType(StackFrame::State* a) {
  UNIMPLEMENTED();
  return NONE;
}

void StoreIC::Generate(MacroAssembler* a, ExternalReference const& b) {
  UNIMPLEMENTED();
}

void StoreIC::GenerateExtendStorage(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void StoreIC::GenerateMegamorphic(MacroAssembler* a) {
  UNIMPLEMENTED();
}

Object* StoreStubCompiler::CompileStoreCallback(JSObject* a,
                                                AccessorInfo* b,
                                                String* c) {
  UNIMPLEMENTED();
  return NULL;
}

Object* StoreStubCompiler::CompileStoreField(JSObject* a,
                                             int b,
                                             Map* c,
                                             String* d) {
  UNIMPLEMENTED();
  return NULL;
}

Object* StoreStubCompiler::CompileStoreInterceptor(JSObject* a, String* b) {
  UNIMPLEMENTED();
  return NULL;
}

Object* StubCompiler::CompileLazyCompile(Code::Flags a) {
  UNIMPLEMENTED();
  return NULL;
}

void VirtualFrame::Drop(int a) {
  UNIMPLEMENTED();
}

int VirtualFrame::InvalidateFrameSlotAt(int a) {
  UNIMPLEMENTED();
  return -1;
}

void VirtualFrame::MergeTo(VirtualFrame* a) {
  UNIMPLEMENTED();
}

Result VirtualFrame::Pop() {
  UNIMPLEMENTED();
  return Result(NULL);
}

Result VirtualFrame::RawCallStub(CodeStub* a) {
  UNIMPLEMENTED();
  return Result(NULL);
}

void VirtualFrame::SyncElementBelowStackPointer(int a) {
  UNIMPLEMENTED();
}

void VirtualFrame::SyncElementByPushing(int a) {
  UNIMPLEMENTED();
}

void VirtualFrame::SyncRange(int a, int b) {
  UNIMPLEMENTED();
}

VirtualFrame::VirtualFrame() : elements_(0) {
  UNIMPLEMENTED();
}

byte* ArgumentsAdaptorFrame::GetCallerStackPointer() const {
  UNIMPLEMENTED();
  return NULL;
}

void Builtins::Generate_Adaptor(MacroAssembler* a, Builtins::CFunctionId b) {
  UNIMPLEMENTED();
}

void Builtins::Generate_ArgumentsAdaptorTrampoline(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Builtins::Generate_FunctionApply(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Builtins::Generate_FunctionCall(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Builtins::Generate_JSConstructCall(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void Builtins::Generate_JSEntryTrampoline(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateArgumentsAccess(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateArgumentsLength(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateFastCharCodeAt(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateIsArray(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateIsNonNegativeSmi(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateIsSmi(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateLog(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateObjectEquals(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateSetValueOf(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void CodeGenerator::GenerateValueOf(ZoneList<Expression*>* a) {
  UNIMPLEMENTED();
}

void ExitFrame::Iterate(ObjectVisitor* a) const {
  UNIMPLEMENTED();
}

byte* InternalFrame::GetCallerStackPointer() const {
  UNIMPLEMENTED();
  return NULL;
}

byte* JavaScriptFrame::GetCallerStackPointer() const {
  UNIMPLEMENTED();
  return NULL;
}

} }  // namespace v8::internal
