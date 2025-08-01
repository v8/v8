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
// Copyright 2021 the V8 project authors. All rights reserved.

#ifndef V8_CODEGEN_RISCV_ASSEMBLER_RISCV_INL_H_
#define V8_CODEGEN_RISCV_ASSEMBLER_RISCV_INL_H_

#include "src/codegen/riscv/assembler-riscv.h"
// Include the non-inl header before the rest of the headers.

#include "src/codegen/assembler-arch.h"
#include "src/codegen/assembler.h"
#include "src/debug/debug.h"
#include "src/diagnostics/disasm.h"
#include "src/diagnostics/disassembler.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/heap-layout.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

[[nodiscard]] static inline Instr SetHi20Offset(int32_t hi29, Instr instr);
[[nodiscard]] static inline Instr SetLo12Offset(int32_t lo12, Instr instr);

bool CpuFeatures::SupportsOptimizer() { return IsSupported(FPU); }

void Assembler::CheckBuffer() {
  if (buffer_space() <= kGap) {
    GrowBuffer();
  }
}

// -----------------------------------------------------------------------------
// WritableRelocInfo.

void WritableRelocInfo::apply(intptr_t delta) {
  if (IsInternalReference(rmode_) || IsInternalReferenceEncoded(rmode_)) {
    // Absolute code pointer inside code object moves with the code object.
    Assembler::RelocateInternalReference(rmode_, pc_, delta, &jit_allocation_);
  } else {
    DCHECK(IsRelativeCodeTarget(rmode_) || IsNearBuiltinEntry(rmode_));
    Assembler::RelocateRelativeReference(rmode_, pc_, delta, &jit_allocation_);
  }
}

Address RelocInfo::target_address() {
  DCHECK(IsCodeTargetMode(rmode_) || IsWasmCall(rmode_) ||
         IsNearBuiltinEntry(rmode_) || IsWasmStubCall(rmode_) ||
         IsExternalReference(rmode_));
  return Assembler::target_address_at(pc_, constant_pool_);
}

Address RelocInfo::target_address_address() {
  DCHECK(HasTargetAddressAddress());
  // Read the address of the word containing the target_address in an
  // instruction stream.
  // The only architecture-independent user of this function is the serializer.
  // The serializer uses it to find out how many raw bytes of instruction to
  // output before the next target.
  // For an instruction like LUI/ORI where the target bits are mixed into the
  // instruction bits, the size of the target will be zero, indicating that the
  // serializer should not step forward in memory after a target is resolved
  // and written. In this case the target_address_address function should
  // return the end of the instructions to be patched, allowing the
  // deserializer to deserialize the instructions as raw bytes and put them in
  // place, ready to be patched with the target. After jump optimization,
  // that is the address of the instruction that follows J/JAL/JR/JALR
  // instruction.
#ifdef V8_TARGET_ARCH_RISCV64
  return pc_ + Assembler::kInstructionsFor64BitConstant * kInstrSize;
#elif defined(V8_TARGET_ARCH_RISCV32)
  return pc_ + Assembler::kInstructionsFor32BitConstant * kInstrSize;
#endif
}

Address RelocInfo::constant_pool_entry_address() { UNREACHABLE(); }

int RelocInfo::target_address_size() {
  if (IsCodedSpecially()) {
    return Assembler::kSpecialTargetSize;
  } else {
    return kSystemPointerSize;
  }
}

void Assembler::set_target_compressed_address_at(
    Address pc, Address constant_pool, Tagged_t target,
    WritableJitAllocation* jit_allocation, ICacheFlushMode icache_flush_mode) {
  if (COMPRESS_POINTERS_BOOL) {
    Assembler::set_uint32_constant_at(pc, constant_pool,
                                      static_cast<uint32_t>(target),
                                      jit_allocation, icache_flush_mode);
  } else {
    UNREACHABLE();
  }
}

Tagged_t Assembler::target_compressed_address_at(Address pc,
                                                 Address constant_pool) {
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  base::EmbeddedVector<char, 128> disasm_buffer;

  disasm.InstructionDecode(disasm_buffer, reinterpret_cast<uint8_t*>(pc));
  DEBUG_PRINTF("%s\n", disasm_buffer.begin());
  disasm.InstructionDecode(disasm_buffer,
                           reinterpret_cast<uint8_t*>(pc + kInstrSize));
  DEBUG_PRINTF("%s\n", disasm_buffer.begin());

  DEBUG_PRINTF("\t target_compressed_address_at %d\n",
               uint32_constant_at(pc, constant_pool));
  return static_cast<Tagged_t>(uint32_constant_at(pc, constant_pool));
}

WasmCodePointer RelocInfo::wasm_code_pointer_table_entry() const {
  DCHECK(rmode_ == WASM_CODE_POINTER_TABLE_ENTRY);
  return WasmCodePointer(Assembler::uint32_constant_at(pc_, constant_pool_));
}

void WritableRelocInfo::set_wasm_code_pointer_table_entry(
    WasmCodePointer target, ICacheFlushMode icache_flush_mode) {
  DCHECK(rmode_ == RelocInfo::WASM_CODE_POINTER_TABLE_ENTRY);
  Assembler::set_uint32_constant_at(pc_, constant_pool_, target.value(),
                                    &jit_allocation_, icache_flush_mode);
}

Handle<Object> Assembler::code_target_object_handle_at(Address pc,
                                                       Address constant_pool) {
  int index =
      static_cast<int>(target_address_at(pc, constant_pool)) & 0xFFFFFFFF;
  return GetCodeTarget(index);
}

Handle<HeapObject> Assembler::compressed_embedded_object_handle_at(
    Address pc, Address const_pool) {
  DEBUG_PRINTF("\tcompressed_embedded_object_handle_at: pc: 0x%" PRIxPTR " \t ",
               pc);
  return GetEmbeddedObject(target_compressed_address_at(pc, const_pool));
}

Handle<HeapObject> Assembler::embedded_object_handle_at(Address pc) {
  DEBUG_PRINTF("\tembedded_object_handle_at: pc: 0x%" PRIxPTR " \n", pc);
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  base::EmbeddedVector<char, 128> disasm_buffer;

  disasm.InstructionDecode(disasm_buffer, reinterpret_cast<uint8_t*>(pc));
  DEBUG_PRINTF("%s\n", disasm_buffer.begin());
  disasm.InstructionDecode(disasm_buffer,
                           reinterpret_cast<uint8_t*>(pc + kInstrSize));
  DEBUG_PRINTF("%s\n", disasm_buffer.begin());
#if V8_TARGET_ARCH_RISCV64
  Instr instr1 = Assembler::instr_at(pc);
  Instr instr2 = Assembler::instr_at(pc + kInstrSize);
  DCHECK(IsAuipc(instr1));
  DCHECK(IsLd(instr2));
  int32_t embedded_target_offset = BranchLongOffset(instr1, instr2);
  DEBUG_PRINTF("\tembedded_target_offset %d\n", embedded_target_offset);
  static_assert(sizeof(EmbeddedObjectIndex) == sizeof(intptr_t));
  DEBUG_PRINTF("\t EmbeddedObjectIndex %lu\n",
               Memory<EmbeddedObjectIndex>(pc + embedded_target_offset));
  return GetEmbeddedObject(
      Memory<EmbeddedObjectIndex>(pc + embedded_target_offset));
#else
  DCHECK(IsLui(Assembler::instr_at(pc)));
  DCHECK(IsAddi(Assembler::instr_at(pc + kInstrSize)));
  auto target = target_address_at(pc, kNullAddress);
  DEBUG_PRINTF("\ttarget %d\n", target);
  return Handle<HeapObject>(reinterpret_cast<Address*>(target));
#endif
}

#if V8_TARGET_ARCH_RISCV64
void Assembler::set_embedded_object_index_referenced_from(
    Address pc, EmbeddedObjectIndex data) {
  Instr instr1 = Assembler::instr_at(pc);
  Instr instr2 = Assembler::instr_at(pc + kInstrSize);
  DCHECK(IsAuipc(instr1));
  DCHECK(IsLd(instr2));
  int32_t embedded_target_offset = BranchLongOffset(instr1, instr2);
  Memory<EmbeddedObjectIndex>(pc + embedded_target_offset) = data;
}
#endif

void Assembler::deserialization_set_special_target_at(
    Address instruction_payload, Tagged<Code> code, Address target) {
  set_target_address_at(instruction_payload,
                        !code.is_null() ? code->constant_pool() : kNullAddress,
                        target);
}

int Assembler::deserialization_special_target_size(
    Address instruction_payload) {
  return kSpecialTargetSize;
}

void Assembler::set_target_internal_reference_encoded_at(Address pc,
                                                         Address target) {
#ifdef V8_TARGET_ARCH_RISCV64
  set_target_value_at(pc, static_cast<uint64_t>(target));
#elif defined(V8_TARGET_ARCH_RISCV32)
  set_target_value_at(pc, static_cast<uint32_t>(target));
#endif
}

void Assembler::deserialization_set_target_internal_reference_at(
    Address pc, Address target, WritableJitAllocation& jit_allocation,
    RelocInfo::Mode mode) {
  jit_allocation.WriteUnalignedValue<Address>(pc, target);
}

Tagged<HeapObject> RelocInfo::target_object(PtrComprCageBase cage_base) {
  DCHECK(IsCodeTarget(rmode_) || IsEmbeddedObjectMode(rmode_));
  if (IsCompressedEmbeddedObject(rmode_)) {
    return Cast<HeapObject>(
        Tagged<Object>(V8HeapCompressionScheme::DecompressTagged(
            Assembler::target_compressed_address_at(pc_, constant_pool_))));
  } else {
    return Cast<HeapObject>(
        Tagged<Object>(Assembler::target_address_at(pc_, constant_pool_)));
  }
}

DirectHandle<HeapObject> RelocInfo::target_object_handle(Assembler* origin) {
  if (IsCodeTarget(rmode_)) {
    return Cast<HeapObject>(
        origin->code_target_object_handle_at(pc_, constant_pool_));
  } else if (IsCompressedEmbeddedObject(rmode_)) {
    return origin->compressed_embedded_object_handle_at(pc_, constant_pool_);
  } else if (IsFullEmbeddedObject(rmode_)) {
    return origin->embedded_object_handle_at(pc_);
  } else {
    DCHECK(IsRelativeCodeTarget(rmode_));
    return origin->relative_code_target_object_handle_at(pc_);
  }
}

void WritableRelocInfo::set_target_object(Tagged<HeapObject> target,
                                          ICacheFlushMode icache_flush_mode) {
  DCHECK(IsCodeTarget(rmode_) || IsEmbeddedObjectMode(rmode_));
  if (IsCompressedEmbeddedObject(rmode_)) {
    DCHECK(COMPRESS_POINTERS_BOOL);
    // We must not compress pointers to objects outside of the main pointer
    // compression cage as we wouldn't be able to decompress them with the
    // correct cage base.
    DCHECK_IMPLIES(V8_ENABLE_SANDBOX_BOOL, !HeapLayout::InTrustedSpace(target));
    DCHECK_IMPLIES(V8_EXTERNAL_CODE_SPACE_BOOL,
                   !HeapLayout::InCodeSpace(target));
    Assembler::set_target_compressed_address_at(
        pc_, constant_pool_,
        V8HeapCompressionScheme::CompressObject(target.ptr()), &jit_allocation_,
        icache_flush_mode);
  } else {
    DCHECK(IsFullEmbeddedObject(rmode_));
    Assembler::set_target_address_at(pc_, constant_pool_, target.ptr(),
                                     &jit_allocation_, icache_flush_mode);
  }
}

Address RelocInfo::target_external_reference() {
  DCHECK(rmode_ == EXTERNAL_REFERENCE);
  return Assembler::target_address_at(pc_, constant_pool_);
}

void WritableRelocInfo::set_target_external_reference(
    Address target, ICacheFlushMode icache_flush_mode) {
  DCHECK(rmode_ == RelocInfo::EXTERNAL_REFERENCE);
  Assembler::set_target_address_at(pc_, constant_pool_, target,
                                   &jit_allocation_, icache_flush_mode);
}

Address RelocInfo::target_internal_reference() {
  if (IsInternalReference(rmode_)) {
    return Memory<Address>(pc_);
  } else {
    // Encoded internal references are j/jal instructions.
    DCHECK(IsInternalReferenceEncoded(rmode_));
    DCHECK(Assembler::IsLui(Assembler::instr_at(pc_ + 0 * kInstrSize)));
    Address address = Assembler::target_constant_address_at(pc_);
    return address;
  }
}

Address RelocInfo::target_internal_reference_address() {
  DCHECK(IsInternalReference(rmode_) || IsInternalReferenceEncoded(rmode_));
  return pc_;
}

JSDispatchHandle RelocInfo::js_dispatch_handle() {
  DCHECK(rmode_ == JS_DISPATCH_HANDLE);
  return JSDispatchHandle(Assembler::uint32_constant_at(pc_, constant_pool_));
}

Handle<Code> Assembler::relative_code_target_object_handle_at(
    Address pc) const {
  Instr instr1 = Assembler::instr_at(pc);
  Instr instr2 = Assembler::instr_at(pc + kInstrSize);
  DCHECK(IsAuipc(instr1));
  DCHECK(IsJalr(instr2));
  int32_t code_target_index = BranchLongOffset(instr1, instr2);
  return Cast<Code>(GetEmbeddedObject(code_target_index));
}

Builtin Assembler::target_builtin_at(Address pc) {
  Instr instr1 = Assembler::instr_at(pc);
  Instr instr2 = Assembler::instr_at(pc + kInstrSize);
  DCHECK(IsAuipc(instr1));
  DCHECK(IsJalr(instr2));
  int32_t builtin_id = BranchLongOffset(instr1, instr2);
  DCHECK(Builtins::IsBuiltinId(builtin_id));
  return static_cast<Builtin>(builtin_id);
}

Builtin RelocInfo::target_builtin_at(Assembler* origin) {
  DCHECK(IsNearBuiltinEntry(rmode_));
  return Assembler::target_builtin_at(pc_);
}

Address RelocInfo::target_off_heap_target() {
  DCHECK(IsOffHeapTarget(rmode_));
  return Assembler::target_address_at(pc_, constant_pool_);
}

EnsureSpace::EnsureSpace(Assembler* assembler) { assembler->CheckBuffer(); }

int32_t Assembler::target_constant32_at(Address pc) {
  Instruction* instr0 = Instruction::At((unsigned char*)pc);
  Instruction* instr1 = Instruction::At((unsigned char*)(pc + 1 * kInstrSize));

  // Interpret instructions for address generated by li: See listing in
  // Assembler::set_target_address_at() just below.
  if (IsLui(*reinterpret_cast<Instr*>(instr0)) &&
      IsAddi(*reinterpret_cast<Instr*>(instr1))) {
    // Assemble the 32bit value.
    int32_t constant32 =
        static_cast<int32_t>(instr0->Imm20UValue() << kImm20Shift) +
        static_cast<int32_t>(instr1->Imm12Value());
    return constant32;
  }
  // We should never get here, force a bad address if we do.
  UNREACHABLE();
}

void Assembler::set_target_constant32_at(Address pc, uint32_t target,
                                         WritableJitAllocation* jit_allocation,
                                         ICacheFlushMode icache_flush_mode) {
  Instruction* instr0 = Instruction::At((unsigned char*)pc);
  Instruction* instr1 = Instruction::At((unsigned char*)(pc + 1 * kInstrSize));
#ifdef DEBUG
  // Check we have the result from a li macro-instruction.
  DCHECK(IsLui(*reinterpret_cast<Instr*>(instr0)) &&
         IsAddi(*reinterpret_cast<Instr*>(instr1)));
#endif
  int32_t high_20 = (static_cast<int32_t>(target) + 0x800) >> 12;  // 20 bits
  int32_t low_12 = static_cast<int32_t>(target) << 20 >> 20;       // 12 bits
  instr_at_put(pc, SetHi20Offset(high_20, instr0->InstructionBits()),
               jit_allocation);
  instr_at_put(pc + 1 * kInstrSize,
               SetLo12Offset(low_12, instr1->InstructionBits()),
               jit_allocation);
  if (icache_flush_mode != SKIP_ICACHE_FLUSH) {
    FlushInstructionCache(pc, 2 * kInstrSize);
  }
  DCHECK_EQ(static_cast<uint32_t>(target_constant32_at(pc)), target);
}

uint32_t Assembler::uint32_constant_at(Address pc, Address constant_pool) {
  Instruction* instr0 = reinterpret_cast<Instruction*>(pc);
  Instruction* instr1 = reinterpret_cast<Instruction*>(pc + 1 * kInstrSize);
  CHECK(IsLui(*reinterpret_cast<Instr*>(instr0)));
  CHECK(IsAddi(*reinterpret_cast<Instr*>(instr1)));
  return target_constant32_at(pc);
}
void Assembler::set_uint32_constant_at(Address pc, Address constant_pool,
                                       uint32_t new_constant,
                                       WritableJitAllocation* jit_allocation,
                                       ICacheFlushMode icache_flush_mode) {
  Instruction* instr1 = reinterpret_cast<Instruction*>(pc);
  Instruction* instr2 = reinterpret_cast<Instruction*>(pc + 1 * kInstrSize);
  CHECK(IsLui(*reinterpret_cast<Instr*>(instr1)));
  CHECK(IsAddi(*reinterpret_cast<Instr*>(instr2)));
  set_target_constant32_at(pc, new_constant, jit_allocation, icache_flush_mode);
}

[[nodiscard]] static inline Instr SetHi20Offset(int32_t hi20, Instr instr) {
  DCHECK(Assembler::IsAuipc(instr) || Assembler::IsLui(instr));
  DCHECK(is_int20(hi20));
  instr = (instr & ~kImm31_12Mask) | ((hi20 & kImm19_0Mask) << 12);
  return instr;
}

[[nodiscard]] static inline Instr SetLo12Offset(int32_t lo12, Instr instr) {
  DCHECK(Assembler::IsJalr(instr) || Assembler::IsAddi(instr));
  DCHECK(is_int12(lo12));
  instr &= ~kImm12Mask;
  int32_t imm12 = lo12 << kImm12Shift;
  DCHECK(Assembler::IsJalr(instr | (imm12 & kImm12Mask)) ||
         Assembler::IsAddi(instr | (imm12 & kImm12Mask)));
  return instr | (imm12 & kImm12Mask);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RISCV_ASSEMBLER_RISCV_INL_H_
