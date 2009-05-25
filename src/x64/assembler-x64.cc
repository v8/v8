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

namespace v8 {
namespace internal {

Register no_reg = { -1 };


// Safe default is no features.
uint64_t CpuFeatures::supported_ = 0;
uint64_t CpuFeatures::enabled_ = 0;

void CpuFeatures::Probe()  {
  // TODO(X64): UNIMPLEMENTED
}

// -----------------------------------------------------------------------------
// Implementation of Assembler

// Emit a single byte. Must always be inlined.
#define EMIT(x)                                 \
  *pc_++ = (x)

#ifdef GENERATED_CODE_COVERAGE
static void InitCoverageLog();
#endif

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

void Assembler::GrowBuffer() {
  ASSERT(overflow());  // should not call this otherwise
  if (!own_buffer_) FATAL("external code buffer is too small");

  // compute new buffer size
  CodeDesc desc;  // the new buffer
  if (buffer_size_ < 4*KB) {
    desc.buffer_size = 4*KB;
  } else {
    desc.buffer_size = 2*buffer_size_;
  }
  // Some internal data structures overflow for very large buffers,
  // they must ensure that kMaximalBufferSize is not too large.
  if ((desc.buffer_size > kMaximalBufferSize) ||
      (desc.buffer_size > Heap::OldGenerationSize())) {
    V8::FatalProcessOutOfMemory("Assembler::GrowBuffer");
  }

  // setup new buffer
  desc.buffer = NewArray<byte>(desc.buffer_size);
  desc.instr_size = pc_offset();
  desc.reloc_size = (buffer_ + buffer_size_) - (reloc_info_writer.pos());

  // Clear the buffer in debug mode. Use 'int3' instructions to make
  // sure to get into problems if we ever run uninitialized code.
#ifdef DEBUG
  memset(desc.buffer, 0xCC, desc.buffer_size);
#endif

  // copy the data
  int pc_delta = desc.buffer - buffer_;
  int rc_delta = (desc.buffer + desc.buffer_size) - (buffer_ + buffer_size_);
  memmove(desc.buffer, buffer_, desc.instr_size);
  memmove(rc_delta + reloc_info_writer.pos(),
          reloc_info_writer.pos(), desc.reloc_size);

  // switch buffers
  if (spare_buffer_ == NULL && buffer_size_ == kMinimalBufferSize) {
    spare_buffer_ = buffer_;
  } else {
    DeleteArray(buffer_);
  }
  buffer_ = desc.buffer;
  buffer_size_ = desc.buffer_size;
  pc_ += pc_delta;
  if (last_pc_ != NULL) {
    last_pc_ += pc_delta;
  }
  reloc_info_writer.Reposition(reloc_info_writer.pos() + rc_delta,
                               reloc_info_writer.last_pc() + pc_delta);

  // relocate runtime entries
  for (RelocIterator it(desc); !it.done(); it.next()) {
    RelocInfo::Mode rmode = it.rinfo()->rmode();
    if (rmode == RelocInfo::RUNTIME_ENTRY) {
      int32_t* p = reinterpret_cast<int32_t*>(it.rinfo()->pc());
      *p -= pc_delta;  // relocate entry
    } else if (rmode == RelocInfo::INTERNAL_REFERENCE) {
      int32_t* p = reinterpret_cast<int32_t*>(it.rinfo()->pc());
      if (*p != 0) {  // 0 means uninitialized.
        *p += pc_delta;
      }
    }
  }

  ASSERT(!overflow());
}


void Assembler::emit_operand(Register reg, const Operand& adr) {
  const unsigned length = adr.len_;
  ASSERT(length > 0);

  // Emit updated ModRM byte containing the given register.
  pc_[0] = (adr.buf_[0] & ~0x38) | ((reg.code() && 0x7) << 3);

  // Emit the rest of the encoded operand.
  for (unsigned i = 1; i < length; i++) pc_[i] = adr.buf_[i];
  pc_ += length;
}


void Assembler::int3() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xCC);
}


void Assembler::hlt() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF4);
}


void Assembler::nop() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x90);
}


void Assembler::ret(int imm16) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_uint16(imm16));
  if (imm16 == 0) {
    EMIT(0xC3);
  } else {
    EMIT(0xC2);
    EMIT(imm16 & 0xFF);
    EMIT((imm16 >> 8) & 0xFF);
  }
}


void Assembler::mov(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst, src);
  EMIT(0x8B);
  emit_operand(dst, src);
}


void Assembler::mov(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst, src);
  EMIT(0x89);
  EMIT(0xC0 | (src.code() & 0x7) << 3 | (dst.code() & 0x7));
}

} }  // namespace v8::internal


// TODO(x64): Implement and move these to their correct cc-files:
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

namespace v8 {
namespace internal {

void ArgumentsAccessStub::GenerateNewObject(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void ArgumentsAccessStub::GenerateReadElement(MacroAssembler* a) {
  UNIMPLEMENTED();
}

void ArgumentsAccessStub::GenerateReadLength(MacroAssembler* a) {
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


StackFrame::Type ExitFrame::GetStateForFramePointer(unsigned char* a,
                                                    StackFrame::State* b) {
  // TODO(X64): UNIMPLEMENTED
  return NONE;
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
