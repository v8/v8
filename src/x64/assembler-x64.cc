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

// -----------------------------------------------------------------------------
// Implementation of Register

Register rax = { 0 };
Register rcx = { 1 };
Register rdx = { 2 };
Register rbx = { 3 };
Register rsp = { 4 };
Register rbp = { 5 };
Register rsi = { 6 };
Register rdi = { 7 };
Register r8 = { 8 };
Register r9 = { 9 };
Register r10 = { 10 };
Register r11 = { 11 };
Register r12 = { 12 };
Register r13 = { 13 };
Register r14 = { 14 };
Register r15 = { 15 };

Register no_reg = { -1 };

XMMRegister xmm0 = { 0 };
XMMRegister xmm1 = { 1 };
XMMRegister xmm2 = { 2 };
XMMRegister xmm3 = { 3 };
XMMRegister xmm4 = { 4 };
XMMRegister xmm5 = { 5 };
XMMRegister xmm6 = { 6 };
XMMRegister xmm7 = { 7 };
XMMRegister xmm8 = { 8 };
XMMRegister xmm9 = { 9 };
XMMRegister xmm10 = { 10 };
XMMRegister xmm11 = { 11 };
XMMRegister xmm12 = { 12 };
XMMRegister xmm13 = { 13 };
XMMRegister xmm14 = { 14 };
XMMRegister xmm15 = { 15 };

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


void Assembler::bind_to(Label* L, int pos) {
  ASSERT(!L->is_bound());  // Label may only be bound once.
  last_pc_ = NULL;
  ASSERT(0 <= pos && pos <= pc_offset());  // Position must be valid.
  if (L->is_linked()) {
    int current = L->pos();
    int next = long_at(current);
    while (next != current) {
      // relative address, relative to point after address
      int imm32 = pos - (current + sizeof(int32_t));
      long_at_put(current, imm32);
      current = next;
      next = long_at(next);
    }
    // Fix up last fixup on linked list.
    int last_imm32 = pos - (current + sizeof(int32_t));
    long_at_put(current, last_imm32);
  }
  L->bind_to(pos);
}


void Assembler::bind(Label* L) {
  bind_to(L, pc_offset());
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


// Assembler Instruction implementations

void Assembler::arithmetic_op(byte opcode, Register reg, const Operand& op) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(reg, op);
  EMIT(opcode);
  emit_operand(reg, op);
}


void Assembler::arithmetic_op(byte opcode, Register dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst, src);
  EMIT(opcode);
  EMIT(0xC0 | (dst.code() & 0x7) << 3 | (src.code() & 0x7));
}

void Assembler::immediate_arithmetic_op(byte subcode,
                                        Register dst,
                                        Immediate src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rax, dst);
  if (is_int8(src.value_)) {
    EMIT(0x83);
    EMIT(0xC0 | (subcode << 3) | (dst.code() & 0x7));
    EMIT(src.value_);
  } else {
    EMIT(0x81);
    EMIT(0xC0 | (subcode << 3) | (dst.code() & 0x7));
    emitl(src.value_);
  }
}

void Assembler::immediate_arithmetic_op(byte subcode,
                                        const Operand& dst,
                                        Immediate src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rax, dst);
  if (is_int8(src.value_)) {
    EMIT(0x83);
    emit_operand(Register::toRegister(subcode), dst);
    EMIT(src.value_);
  } else {
    EMIT(0x81);
    emit_operand(Register::toRegister(subcode), dst);
    emitl(src.value_);
  }
}


void Assembler::call(Label* L) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  // 1110 1000 #32-bit disp
  EMIT(0xE8);
  if (L->is_bound()) {
    int offset = L->pos() - pc_offset() - sizeof(int32_t);
    ASSERT(offset <= 0);
    emitl(offset);
  } else if (L->is_linked()) {
    emitl(L->pos());
    L->link_to(pc_offset() - sizeof(int32_t));
  } else {
    ASSERT(L->is_unused());
    int32_t current = pc_offset();
    emitl(current);
    L->link_to(current);
  }
}


void Assembler::dec(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rcx, dst);
  EMIT(0xFF);
  EMIT(0xC8 | (dst.code() & 0x7));
}


void Assembler::dec(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rax, dst);
  EMIT(0xFF);
  emit_operand(rcx, dst);
}


void Assembler::hlt() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xF4);
}


void Assembler::inc(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rax, dst);
  EMIT(0xFF);
  EMIT(0xC0 | (dst.code() & 0x7));
}


void Assembler::inc(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rax, dst);
  EMIT(0xFF);
  emit_operand(rax, dst);
}


void Assembler::int3() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0xCC);
}


void Assembler::j(Condition cc, Label* L) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(0 <= cc && cc < 16);
  if (L->is_bound()) {
    const int short_size = 2;
    const int long_size  = 6;
    int offs = L->pos() - pc_offset();
    ASSERT(offs <= 0);
    if (is_int8(offs - short_size)) {
      // 0111 tttn #8-bit disp
      EMIT(0x70 | cc);
      EMIT((offs - short_size) & 0xFF);
    } else {
      // 0000 1111 1000 tttn #32-bit disp
      EMIT(0x0F);
      EMIT(0x80 | cc);
      emitl(offs - long_size);
    }
  } else if (L->is_linked()) {
    // 0000 1111 1000 tttn #32-bit disp
    EMIT(0x0F);
    EMIT(0x80 | cc);
    emitl(L->pos());
    L->link_to(pc_offset() - sizeof(int32_t));
  } else {
    ASSERT(L->is_unused());
    EMIT(0x0F);
    EMIT(0x80 | cc);
    int32_t current = pc_offset();
    emitl(current);
    L->link_to(current);
  }
}


void Assembler::jmp(Label* L) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (L->is_bound()) {
    int offs = L->pos() - pc_offset() - 1;
    ASSERT(offs <= 0);
    if (is_int8(offs - sizeof(int8_t))) {
      // 1110 1011 #8-bit disp
      EMIT(0xEB);
      EMIT((offs - sizeof(int8_t)) & 0xFF);
    } else {
      // 1110 1001 #32-bit disp
      EMIT(0xE9);
      emitl(offs - sizeof(int32_t));
    }
  } else  if (L->is_linked()) {
    // 1110 1001 #32-bit disp
    EMIT(0xE9);
    emitl(L->pos());
    L->link_to(pc_offset() - sizeof(int32_t));
  } else {
    // 1110 1001 #32-bit disp
    ASSERT(L->is_unused());
    EMIT(0xE9);
    int32_t current = pc_offset();
    emitl(current);
    L->link_to(current);
  }
}


void Assembler::movq(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst, src);
  EMIT(0x8B);
  emit_operand(dst, src);
}


void Assembler::movq(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst, src);
  EMIT(0x8B);
  EMIT(0xC0 | (dst.code() & 0x7) << 3 | (src.code() & 0x7));
}


void Assembler::movq(Register dst, Immediate value) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rax, dst);
  EMIT(0xC7);
  EMIT(0xC0 | (dst.code() & 0x7));
  emit(value);  // Only 32-bit immediates are possible, not 8-bit immediates.
}


void Assembler::movq(Register dst, int64_t value, RelocInfo::Mode rmode) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rax, dst);
  EMIT(0xB8 | (dst.code() & 0x7));
  emitq(value, rmode);
}


void Assembler::nop() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  EMIT(0x90);
}


void Assembler::pop(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (dst.code() & 0x8) {
    emit_rex_64(rax, dst);
  }
  EMIT(0x58 | (dst.code() & 0x7));
}


void Assembler::pop(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rax, dst);  // Could be omitted in some cases.
  EMIT(0x8F);
  emit_operand(rax, dst);
}


void Assembler::push(Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (src.code() & 0x8) {
    emit_rex_64(rax, src);
  }
  EMIT(0x50 | (src.code() & 0x7));
}


void Assembler::push(const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(rsi, src);  // Could be omitted in some cases.
  EMIT(0xFF);
  emit_operand(rsi, src);
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
                                              StubCompiler::CheckType d,
                                              Code::Flags flags) {
  UNIMPLEMENTED();
  return NULL;
}

Object* CallStubCompiler::CompileCallField(Object* a,
                                           JSObject* b,
                                           int c,
                                           String* d,
                                           Code::Flags flags) {
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
