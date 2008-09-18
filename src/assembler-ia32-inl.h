// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2006-2008 the V8 project authors. All rights reserved.

// A light-weight IA32 Assembler.

#ifndef V8_ASSEMBLER_IA32_INL_H_
#define V8_ASSEMBLER_IA32_INL_H_

#include "cpu.h"

namespace v8 { namespace internal {

Condition NegateCondition(Condition cc) {
  return static_cast<Condition>(cc ^ 1);
}


// The modes possibly affected by apply must be in kApplyMask.
void RelocInfo::apply(int delta) {
  if (rmode_ == runtime_entry || is_code_target(rmode_)) {
    int32_t* p = reinterpret_cast<int32_t*>(pc_);
    *p -= delta;  // relocate entry
  } else if (rmode_ == js_return && is_call_instruction()) {
    // Special handling of js_return when a break point is set (call
    // instruction has been inserted).
    int32_t* p = reinterpret_cast<int32_t*>(pc_ + 1);
    *p -= delta;  // relocate entry
  } else if (is_internal_reference(rmode_)) {
    // absolute code pointer inside code object moves with the code object.
    int32_t* p = reinterpret_cast<int32_t*>(pc_);
    *p += delta;  // relocate entry
  }
}


Address RelocInfo::target_address() {
  ASSERT(is_code_target(rmode_) || rmode_ == runtime_entry);
  return Assembler::target_address_at(pc_);
}


void RelocInfo::set_target_address(Address target) {
  ASSERT(is_code_target(rmode_) || rmode_ == runtime_entry);
  Assembler::set_target_address_at(pc_, target);
}


Object* RelocInfo::target_object() {
  ASSERT(is_code_target(rmode_) || rmode_ == embedded_object);
  return *reinterpret_cast<Object**>(pc_);
}


Object** RelocInfo::target_object_address() {
  ASSERT(is_code_target(rmode_) || rmode_ == embedded_object);
  return reinterpret_cast<Object**>(pc_);
}


void RelocInfo::set_target_object(Object* target) {
  ASSERT(is_code_target(rmode_) || rmode_ == embedded_object);
  *reinterpret_cast<Object**>(pc_) = target;
}


Address* RelocInfo::target_reference_address() {
  ASSERT(rmode_ == external_reference);
  return reinterpret_cast<Address*>(pc_);
}


Address RelocInfo::call_address() {
  ASSERT(is_call_instruction());
  return Assembler::target_address_at(pc_ + 1);
}


void RelocInfo::set_call_address(Address target) {
  ASSERT(is_call_instruction());
  Assembler::set_target_address_at(pc_ + 1, target);
}


Object* RelocInfo::call_object() {
  ASSERT(is_call_instruction());
  return *call_object_address();
}


Object** RelocInfo::call_object_address() {
  ASSERT(is_call_instruction());
  return reinterpret_cast<Object**>(pc_ + 1);
}


void RelocInfo::set_call_object(Object* target) {
  ASSERT(is_call_instruction());
  *call_object_address() = target;
}


bool RelocInfo::is_call_instruction() {
  return *pc_ == 0xE8;
}


Immediate::Immediate(int x)  {
  x_ = x;
  rmode_ = no_reloc;
}


Immediate::Immediate(const ExternalReference& ext) {
  x_ = reinterpret_cast<int32_t>(ext.address());
  rmode_ = external_reference;
}

Immediate::Immediate(const char* s) {
  x_ = reinterpret_cast<int32_t>(s);
  rmode_ = embedded_string;
}


Immediate::Immediate(Handle<Object> handle) {
  // Verify all Objects referred by code are NOT in new space.
  Object* obj = *handle;
  ASSERT(!Heap::InNewSpace(obj));
  if (obj->IsHeapObject()) {
    x_ = reinterpret_cast<intptr_t>(handle.location());
    rmode_ = embedded_object;
  } else {
    // no relocation needed
    x_ =  reinterpret_cast<intptr_t>(obj);
    rmode_ = no_reloc;
  }
}


Immediate::Immediate(Smi* value) {
  x_ = reinterpret_cast<intptr_t>(value);
  rmode_ = no_reloc;
}


void Assembler::emit(uint32_t x) {
  *reinterpret_cast<uint32_t*>(pc_) = x;
  pc_ += sizeof(uint32_t);
}


void Assembler::emit(Handle<Object> handle) {
  // Verify all Objects referred by code are NOT in new space.
  Object* obj = *handle;
  ASSERT(!Heap::InNewSpace(obj));
  if (obj->IsHeapObject()) {
    emit(reinterpret_cast<intptr_t>(handle.location()), embedded_object);
  } else {
    // no relocation needed
    emit(reinterpret_cast<intptr_t>(obj));
  }
}


void Assembler::emit(uint32_t x, RelocMode rmode) {
  if (rmode != no_reloc) RecordRelocInfo(rmode);
  emit(x);
}


void Assembler::emit(const Immediate& x) {
  if (x.rmode_ != no_reloc) RecordRelocInfo(x.rmode_);
  emit(x.x_);
}


Address Assembler::target_address_at(Address pc) {
  return pc + sizeof(int32_t) + *reinterpret_cast<int32_t*>(pc);
}


void Assembler::set_target_address_at(Address pc, Address target) {
  int32_t* p = reinterpret_cast<int32_t*>(pc);
  *p = target - (pc + sizeof(int32_t));
  CPU::FlushICache(p, sizeof(int32_t));
}


Displacement Assembler::disp_at(Label* L) {
  return Displacement(long_at(L->pos()));
}


void Assembler::disp_at_put(Label* L, Displacement disp) {
  long_at_put(L->pos(), disp.data());
}


void Assembler::emit_disp(Label* L, Displacement::Type type) {
  Displacement disp(L, type);
  L->link_to(pc_offset());
  emit(static_cast<int>(disp.data()));
}


void Operand::set_modrm(int mod,  // reg == 0
                        Register rm) {
  ASSERT((mod & -4) == 0);
  buf_[0] = mod << 6 | rm.code();
  len_ = 1;
}


void Operand::set_dispr(int32_t disp, RelocMode rmode) {
  ASSERT(len_ == 1 || len_ == 2);
  *reinterpret_cast<int32_t*>(&buf_[len_]) = disp;
  len_ += sizeof(int32_t);
  rmode_ = rmode;
}

Operand::Operand(Register reg) {
  // reg
  set_modrm(3, reg);
}


Operand::Operand(int32_t disp, RelocMode rmode) {
  // [disp/r]
  set_modrm(0, ebp);
  set_dispr(disp, rmode);
}

} }  // namespace v8::internal

#endif  // V8_ASSEMBLER_IA32_INL_H_
