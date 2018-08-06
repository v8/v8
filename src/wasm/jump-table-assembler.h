// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_JUMP_TABLE_ASSEMBLER_H_
#define V8_WASM_JUMP_TABLE_ASSEMBLER_H_

#include "src/macro-assembler.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8 {
namespace internal {
namespace wasm {

class JumpTableAssembler : public TurboAssembler {
 public:
  // {JumpTableAssembler} is never used during snapshot generation, and its code
  // must be independent of the code range of any isolate anyway. So just use
  // this default {Options} for each {JumpTableAssembler}.
  JumpTableAssembler()
      : TurboAssembler(nullptr, AssemblerOptions{}, nullptr, 0,
                       CodeObjectRequired::kNo) {}

  // Instantiate a {JumpTableAssembler} for patching.
  explicit JumpTableAssembler(Address slot_addr, int size = 256)
      : TurboAssembler(nullptr, AssemblerOptions{},
                       reinterpret_cast<void*>(slot_addr), size,
                       CodeObjectRequired::kNo) {}

  // To allow concurrent patching of the jump table entries we need to ensure
  // that slots do not cross cache-line boundaries. Hence translation between
  // slot offsets and index is encapsulated in the following methods.
  static uint32_t SlotOffsetToIndex(uint32_t slot_offset) {
    DCHECK_EQ(0, slot_offset % kJumpTableSlotSize);
    return slot_offset / kJumpTableSlotSize;
  }
  static uint32_t SlotIndexToOffset(uint32_t slot_index) {
    return slot_index * kJumpTableSlotSize;
  }

  // Determine the size of a jump table containing the given number of slots.
  static size_t SizeForNumberOfSlots(uint32_t slot_count) {
    return slot_count * kJumpTableSlotSize;
  }

#if V8_TARGET_ARCH_X64
  static constexpr int kJumpTableSlotSize = 18;
#elif V8_TARGET_ARCH_IA32
  static constexpr int kJumpTableSlotSize = 10;
#elif V8_TARGET_ARCH_ARM
  static constexpr int kJumpTableSlotSize = 5 * kInstrSize;
#elif V8_TARGET_ARCH_ARM64
  static constexpr int kJumpTableSlotSize = 3 * kInstructionSize;
#elif V8_TARGET_ARCH_S390X
  static constexpr int kJumpTableSlotSize = 20;
#elif V8_TARGET_ARCH_S390
  static constexpr int kJumpTableSlotSize = 14;
#elif V8_TARGET_ARCH_PPC64
  static constexpr int kJumpTableSlotSize = 48;
#elif V8_TARGET_ARCH_PPC
  static constexpr int kJumpTableSlotSize = 24;
#elif V8_TARGET_ARCH_MIPS
  static constexpr int kJumpTableSlotSize = 6 * kInstrSize;
#elif V8_TARGET_ARCH_MIPS64
  static constexpr int kJumpTableSlotSize = 8 * kInstrSize;
#else
  static constexpr int kJumpTableSlotSize = 1;
#endif

  static void EmitLazyCompileJumpSlot(Address base, uint32_t slot_index,
                                      uint32_t func_index,
                                      Address lazy_compile_target,
                                      WasmCode::FlushICache flush_i_cache) {
    Address slot = base + SlotIndexToOffset(slot_index);
    JumpTableAssembler jtasm(slot);
    jtasm.EmitLazyCompileJumpSlot(func_index, lazy_compile_target);
    jtasm.NopBytes(kJumpTableSlotSize - jtasm.pc_offset());
    if (flush_i_cache) {
      Assembler::FlushICache(slot, kJumpTableSlotSize);
    }
  }

  static void PatchJumpTableSlot(Address base, uint32_t slot_index,
                                 Address new_target,
                                 WasmCode::FlushICache flush_i_cache) {
    Address slot = base + SlotIndexToOffset(slot_index);
    JumpTableAssembler jtasm(slot);
    jtasm.EmitJumpSlot(new_target);
    jtasm.NopBytes(kJumpTableSlotSize - jtasm.pc_offset());
    if (flush_i_cache) {
      Assembler::FlushICache(slot, kJumpTableSlotSize);
    }
  }

 private:
  void EmitLazyCompileJumpSlot(uint32_t func_index,
                               Address lazy_compile_target);

  void EmitJumpSlot(Address target);

  void NopBytes(int bytes);
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_JUMP_TABLE_ASSEMBLER_H_
