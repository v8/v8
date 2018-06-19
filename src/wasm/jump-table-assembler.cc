// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/jump-table-assembler.h"

#include "src/assembler-inl.h"
#include "src/macro-assembler-inl.h"

namespace v8 {
namespace internal {
namespace wasm {

void JumpTableAssembler::EmitJumpTrampoline(Address target) {
#if V8_TARGET_ARCH_X64
  movq(kScratchRegister, static_cast<uint64_t>(target));
  jmp(kScratchRegister);
#elif V8_TARGET_ARCH_ARM64
  UseScratchRegisterScope temps(this);
  Register scratch = temps.AcquireX();
  Mov(scratch, static_cast<uint64_t>(target));
  Br(scratch);
#elif V8_TARGET_ARCH_S390X
  mov(ip, Operand(bit_cast<intptr_t, Address>(target)));
  b(ip);
#else
  UNIMPLEMENTED();
#endif
}

// The implementation is compact enough to implement it inline here. If it gets
// much bigger, we might want to split it in a separate file per architecture.
#if V8_TARGET_ARCH_X64
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  // TODO(clemensh): Try more efficient sequences.
  // Alternative 1:
  // [header]:  mov r10, [lazy_compile_target]
  //            jmp r10
  // [slot 0]:  push [0]
  //            jmp [header]  // pc-relative --> slot size: 10 bytes
  //
  // Alternative 2:
  // [header]:  lea r10, [rip - [header]]
  //            shr r10, 3  // compute index from offset
  //            push r10
  //            mov r10, [lazy_compile_target]
  //            jmp r10
  // [slot 0]:  call [header]
  //            ret   // -> slot size: 5 bytes

  // Use a push, because mov to an extended register takes 6 bytes.
  pushq(Immediate(func_index));                           // max 5 bytes
  movq(kScratchRegister, uint64_t{lazy_compile_target});  // max 10 bytes
  jmp(kScratchRegister);                                  // 3 bytes
}

void JumpTableAssembler::EmitJumpSlot(Address target) {
  movq(kScratchRegister, static_cast<uint64_t>(target));
  jmp(kScratchRegister);
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  Nop(bytes);
}

#elif V8_TARGET_ARCH_IA32
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  mov(edi, func_index);                       // 5 bytes
  jmp(lazy_compile_target, RelocInfo::NONE);  // 5 bytes
}

void JumpTableAssembler::EmitJumpSlot(Address target) {
  jmp(target, RelocInfo::NONE);
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  Nop(bytes);
}

#elif V8_TARGET_ARCH_ARM
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  // Load function index to r4.
  // This generates <= 3 instructions: ldr, const pool start, constant
  Move32BitImmediate(r4, Operand(func_index));
  // Jump to {lazy_compile_target}.
  int offset =
      lazy_compile_target - reinterpret_cast<Address>(pc_) - kPcLoadDelta;
  DCHECK_EQ(0, offset % kInstrSize);
  DCHECK(is_int26(offset));     // 26 bit imm
  b(offset);                    // 1 instr
  CheckConstPool(true, false);  // force emit of const pool
}

void JumpTableAssembler::EmitJumpSlot(Address target) {
  int offset = target - reinterpret_cast<Address>(pc_) - kPcLoadDelta;
  DCHECK_EQ(0, offset % kInstrSize);
  DCHECK(is_int26(offset));  // 26 bit imm
  b(offset);
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % kInstrSize);
  for (; bytes > 0; bytes -= kInstrSize) {
    nop();
  }
}

#elif V8_TARGET_ARCH_ARM64
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  Mov(w8, func_index);                         // max. 2 instr
  Jump(lazy_compile_target, RelocInfo::NONE);  // 1 instr
}

void JumpTableAssembler::EmitJumpSlot(Address target) {
  Jump(target, RelocInfo::NONE);
}

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  DCHECK_EQ(0, bytes % kInstructionSize);
  for (; bytes > 0; bytes -= kInstructionSize) {
    nop();
  }
}

#else
void JumpTableAssembler::EmitLazyCompileJumpSlot(uint32_t func_index,
                                                 Address lazy_compile_target) {
  UNIMPLEMENTED();
}

void JumpTableAssembler::EmitJumpSlot(Address target) { UNIMPLEMENTED(); }

void JumpTableAssembler::NopBytes(int bytes) {
  DCHECK_LE(0, bytes);
  UNIMPLEMENTED();
}
#endif

}  // namespace wasm
}  // namespace internal
}  // namespace v8
