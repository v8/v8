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
#include "serialize.h"

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


void Assembler::emit_operand(int rm, const Operand& adr) {
  ASSERT_EQ(rm & 0x07, rm);
  const unsigned length = adr.len_;
  ASSERT(length > 0);

  // Emit updated ModRM byte containing the given register.
  pc_[0] = (adr.buf_[0] & ~0x38) | (rm << 3);

  // Emit the rest of the encoded operand.
  for (unsigned i = 1; i < length; i++) pc_[i] = adr.buf_[i];
  pc_ += length;
}


// Assembler Instruction implementations

void Assembler::arithmetic_op(byte opcode, Register reg, const Operand& op) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(reg, op);
  emit(opcode);
  emit_operand(reg, op);
}


void Assembler::arithmetic_op(byte opcode, Register dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst, src);
  emit(opcode);
  emit(0xC0 | (dst.code() & 0x7) << 3 | (src.code() & 0x7));
}

void Assembler::immediate_arithmetic_op(byte subcode,
                                        Register dst,
                                        Immediate src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  if (is_int8(src.value_)) {
    emit(0x83);
    emit(0xC0 | (subcode << 3) | (dst.code() & 0x7));
    emit(src.value_);
  } else if (dst.is(rax)) {
    emit(0x05 | (subcode << 3));
    emitl(src.value_);
  } else {
    emit(0x81);
    emit(0xC0 | (subcode << 3) | (dst.code() & 0x7));
    emitl(src.value_);
  }
}

void Assembler::immediate_arithmetic_op(byte subcode,
                                        const Operand& dst,
                                        Immediate src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  if (is_int8(src.value_)) {
    emit(0x83);
    emit_operand(Register::toRegister(subcode), dst);
    emit(src.value_);
  } else {
    emit(0x81);
    emit_operand(Register::toRegister(subcode), dst);
    emitl(src.value_);
  }
}


void Assembler::shift(Register dst, Immediate shift_amount, int subcode) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_uint6(shift_amount.value_));  // illegal shift count
  if (shift_amount.value_ == 1) {
    emit_rex_64(dst);
    emit(0xD1);
    emit(0xC0 | (subcode << 3) | (dst.code() & 0x7));
  } else {
    emit_rex_64(dst);
    emit(0xC1);
    emit(0xC0 | (subcode << 3) | (dst.code() & 0x7));
    emit(shift_amount.value_);
  }
}


void Assembler::shift(Register dst, int subcode) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xD3);
  emit(0xC0 | (subcode << 3) | (dst.code() & 0x7));
}


void Assembler::call(Label* L) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  // 1110 1000 #32-bit disp
  emit(0xE8);
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


void Assembler::call(Register adr) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  // Opcode: FF /2 r64
  if (!is_uint3(adr.code())) {
    emit_rex_64(adr);
  }
  emit(0xFF);
  emit(0xD0 | (adr.code() & 0x07));
}


void Assembler::dec(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xFF);
  emit(0xC8 | (dst.code() & 0x7));
}


void Assembler::dec(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xFF);
  emit_operand(1, dst);
}


void Assembler::hlt() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit(0xF4);
}


void Assembler::inc(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xFF);
  emit(0xC0 | (dst.code() & 0x7));
}


void Assembler::inc(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xFF);
  emit_operand(0, dst);
}


void Assembler::int3() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit(0xCC);
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
      emit(0x70 | cc);
      emit((offs - short_size) & 0xFF);
    } else {
      // 0000 1111 1000 tttn #32-bit disp
      emit(0x0F);
      emit(0x80 | cc);
      emitl(offs - long_size);
    }
  } else if (L->is_linked()) {
    // 0000 1111 1000 tttn #32-bit disp
    emit(0x0F);
    emit(0x80 | cc);
    emitl(L->pos());
    L->link_to(pc_offset() - sizeof(int32_t));
  } else {
    ASSERT(L->is_unused());
    emit(0x0F);
    emit(0x80 | cc);
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
      emit(0xEB);
      emit((offs - sizeof(int8_t)) & 0xFF);
    } else {
      // 1110 1001 #32-bit disp
      emit(0xE9);
      emitl(offs - sizeof(int32_t));
    }
  } else  if (L->is_linked()) {
    // 1110 1001 #32-bit disp
    emit(0xE9);
    emitl(L->pos());
    L->link_to(pc_offset() - sizeof(int32_t));
  } else {
    // 1110 1001 #32-bit disp
    ASSERT(L->is_unused());
    emit(0xE9);
    int32_t current = pc_offset();
    emitl(current);
    L->link_to(current);
  }
}


void Assembler::jmp(Register target) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  // Opcode FF/4 r64
  if (!is_uint3(target.code())) {
    emit_rex_64(target);
  }
  emit(0xFF);
  emit(0xE0 | target.code() & 0x07);
}


void Assembler::movq(Register dst, const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst, src);
  emit(0x8B);
  emit_operand(dst, src);
}


void Assembler::movq(Register dst, Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst, src);
  emit(0x8B);
  emit(0xC0 | (dst.code() & 0x7) << 3 | (src.code() & 0x7));
}


void Assembler::movq(Register dst, Immediate value) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xC7);
  emit(0xC0 | (dst.code() & 0x7));
  emit(value);  // Only 32-bit immediates are possible, not 8-bit immediates.
}


void Assembler::movq(Register dst, int64_t value, RelocInfo::Mode rmode) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xB8 | (dst.code() & 0x7));
  emitq(value, rmode);
}


void Assembler::neg(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xF7);
  emit(0xC0 | (0x3 << 3) | (dst.code() & 0x7));
}


void Assembler::neg(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xF7);
  emit_operand(3, dst);
}


void Assembler::nop() {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit(0x90);
}


void Assembler::not_(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xF7);
  emit(0xC0 | (0x2 << 3) | (dst.code() & 0x7));
}


void Assembler::not_(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);
  emit(0xF7);
  emit_operand(2, dst);
}


void Assembler::nop(int n) {
  // The recommended muti-byte sequences of NOP instructions from the Intel 64
  // and IA-32 Architectures Software Developer's Manual.
  //
  // Length   Assembly                                Byte Sequence
  // 2 bytes  66 NOP                                  66 90H
  // 3 bytes  NOP DWORD ptr [EAX]                     0F 1F 00H
  // 4 bytes  NOP DWORD ptr [EAX + 00H]               0F 1F 40 00H
  // 5 bytes  NOP DWORD ptr [EAX + EAX*1 + 00H]       0F 1F 44 00 00H
  // 6 bytes  66 NOP DWORD ptr [EAX + EAX*1 + 00H]    66 0F 1F 44 00 00H
  // 7 bytes  NOP DWORD ptr [EAX + 00000000H]         0F 1F 80 00 00 00 00H
  // 8 bytes  NOP DWORD ptr [EAX + EAX*1 + 00000000H] 0F 1F 84 00 00 00 00 00H
  // 9 bytes  66 NOP DWORD ptr [EAX + EAX*1 +         66 0F 1F 84 00 00 00 00
  //          00000000H]                              00H

  ASSERT(1 <= n);
  ASSERT(n <= 9);
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  switch (n) {
  case 1:
    emit(0x90);
    return;
  case 2:
    emit(0x66);
    emit(0x90);
    return;
  case 3:
    emit(0x0f);
    emit(0x1f);
    emit(0x00);
    return;
  case 4:
    emit(0x0f);
    emit(0x1f);
    emit(0x40);
    emit(0x00);
    return;
  case 5:
    emit(0x0f);
    emit(0x1f);
    emit(0x44);
    emit(0x00);
    emit(0x00);
    return;
  case 6:
    emit(0x66);
    emit(0x0f);
    emit(0x1f);
    emit(0x44);
    emit(0x00);
    emit(0x00);
    return;
  case 7:
    emit(0x0f);
    emit(0x1f);
    emit(0x80);
    emit(0x00);
    emit(0x00);
    emit(0x00);
    emit(0x00);
    return;
  case 8:
    emit(0x0f);
    emit(0x1f);
    emit(0x84);
    emit(0x00);
    emit(0x00);
    emit(0x00);
    emit(0x00);
    emit(0x00);
    return;
  case 9:
    emit(0x66);
    emit(0x0f);
    emit(0x1f);
    emit(0x84);
    emit(0x00);
    emit(0x00);
    emit(0x00);
    emit(0x00);
    emit(0x00);
    return;
  }
}


void Assembler::pop(Register dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (dst.code() & 0x8) {
    emit_rex_64(dst);
  }
  emit(0x58 | (dst.code() & 0x7));
}


void Assembler::pop(const Operand& dst) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(dst);  // Could be omitted in some cases.
  emit(0x8F);
  emit_operand(0, dst);
}


void Assembler::push(Register src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (src.code() & 0x8) {
    emit_rex_64(src);
  }
  emit(0x50 | (src.code() & 0x7));
}


void Assembler::push(const Operand& src) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_rex_64(src);  // Could be omitted in some cases.
  emit(0xFF);
  emit_operand(6, src);
}


void Assembler::ret(int imm16) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  ASSERT(is_uint16(imm16));
  if (imm16 == 0) {
    emit(0xC3);
  } else {
    emit(0xC2);
    emit(imm16 & 0xFF);
    emit((imm16 >> 8) & 0xFF);
  }
}


void Assembler::testb(Register reg, Immediate mask) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (reg.is(rax)) {
    emit(0xA8);
    emit(mask);
  } else {
    if (reg.code() & 0x8) {
      emit_rex_32(rax, reg);
    }
    emit(0xF6);
    emit(0xC0 | (reg.code() & 0x3));
    emit(mask.value_);  // Low byte emitted.
  }
}


void Assembler::testb(const Operand& op, Immediate mask) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_optional_rex_32(rax, op);
  emit(0xF6);
  emit_operand(rax, op);  // Operation code 0
  emit(mask.value_);  // Low byte emitted.
}


void Assembler::testl(Register reg, Immediate mask) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  if (reg.is(rax)) {
    emit(0xA9);
    emit(mask);
  } else {
    emit_optional_rex_32(rax, reg);
    emit(0xF7);
    emit(0xC0 | (reg.code() & 0x3));
    emit(mask);
  }
}


void Assembler::testl(const Operand& op, Immediate mask) {
  EnsureSpace ensure_space(this);
  last_pc_ = pc_;
  emit_optional_rex_32(rax, op);
  emit(0xF7);
  emit_operand(rax, op);  // Operation code 0
  emit(mask);
}


// Relocation information implementations

void Assembler::RecordRelocInfo(RelocInfo::Mode rmode, intptr_t data) {
  ASSERT(rmode != RelocInfo::NONE);
  // Don't record external references unless the heap will be serialized.
  if (rmode == RelocInfo::EXTERNAL_REFERENCE &&
      !Serializer::enabled() &&
      !FLAG_debug_code) {
    return;
  }
  RelocInfo rinfo(pc_, rmode, data);
  reloc_info_writer.Write(&rinfo);
}

void Assembler::RecordJSReturn() {
  WriteRecordedPositions();
  EnsureSpace ensure_space(this);
  RecordRelocInfo(RelocInfo::JS_RETURN);
}


void Assembler::RecordComment(const char* msg) {
  if (FLAG_debug_code) {
    EnsureSpace ensure_space(this);
    RecordRelocInfo(RelocInfo::COMMENT, reinterpret_cast<intptr_t>(msg));
  }
}


void Assembler::RecordPosition(int pos) {
  ASSERT(pos != RelocInfo::kNoPosition);
  ASSERT(pos >= 0);
  current_position_ = pos;
}


void Assembler::RecordStatementPosition(int pos) {
  ASSERT(pos != RelocInfo::kNoPosition);
  ASSERT(pos >= 0);
  current_statement_position_ = pos;
}


void Assembler::WriteRecordedPositions() {
  // Write the statement position if it is different from what was written last
  // time.
  if (current_statement_position_ != written_statement_position_) {
    EnsureSpace ensure_space(this);
    RecordRelocInfo(RelocInfo::STATEMENT_POSITION, current_statement_position_);
    written_statement_position_ = current_statement_position_;
  }

  // Write the position if it is different from what was written last time and
  // also different from the written statement position.
  if (current_position_ != written_position_ &&
      current_position_ != written_statement_position_) {
    EnsureSpace ensure_space(this);
    RecordRelocInfo(RelocInfo::POSITION, current_position_);
    written_position_ = current_position_;
  }
}


const int RelocInfo::kApplyMask =
  RelocInfo::kCodeTargetMask | 1 << RelocInfo::RUNTIME_ENTRY |
    1 << RelocInfo::JS_RETURN | 1 << RelocInfo::INTERNAL_REFERENCE;


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
