// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_H_

#include <memory>

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "src/frames.h"
#include "src/macro-assembler.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-value.h"

// Include platform specific definitions.
#if V8_TARGET_ARCH_IA32
#include "src/wasm/baseline/ia32/liftoff-assembler-ia32-defs.h"
#elif V8_TARGET_ARCH_X64
#include "src/wasm/baseline/x64/liftoff-assembler-x64-defs.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/wasm/baseline/arm64/liftoff-assembler-arm64-defs.h"
#elif V8_TARGET_ARCH_ARM
#include "src/wasm/baseline/arm/liftoff-assembler-arm-defs.h"
#elif V8_TARGET_ARCH_PPC
#include "src/wasm/baseline/ppc/liftoff-assembler-ppc-defs.h"
#elif V8_TARGET_ARCH_MIPS
#include "src/wasm/baseline/mips/liftoff-assembler-mips-defs.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/wasm/baseline/mips64/liftoff-assembler-mips64-defs.h"
#else
#error Unsupported architecture.
#endif

namespace v8 {
namespace internal {
namespace wasm {

// Forward declarations.
struct ModuleEnv;

class LiftoffAssembler : public TurboAssembler {
 public:
  class PinnedRegisterScope {
   public:
    PinnedRegisterScope() : pinned_regs_(0) {}
    explicit PinnedRegisterScope(RegList regs) : pinned_regs_(regs) {}

    Register pin(Register reg) {
      pinned_regs_ |= reg.bit();
      return reg;
    }

    RegList pinned_regs() const { return pinned_regs_; }

   private:
    RegList pinned_regs_ = 0;
  };
  static_assert(IS_TRIVIALLY_COPYABLE(PinnedRegisterScope),
                "PinnedRegisterScope can be passed by value");

  explicit LiftoffAssembler(Isolate* isolate);
  ~LiftoffAssembler();

  Register GetBinaryOpTargetRegister(ValueType, PinnedRegisterScope = {});

  struct VarState {
    enum Location { kStack, kRegister, kConstant };
    Location loc;

    union {
      Register reg;
      uint32_t i32_const;
    };
    VarState() : loc(kStack) {}
    explicit VarState(Register r) : loc(kRegister), reg(r) {}
    explicit VarState(uint32_t value) : loc(kConstant), i32_const(value) {}

    bool operator==(const VarState& other) const {
      if (loc != other.loc) return false;
      switch (loc) {
        case kRegister:
          return reg == other.reg;
        case kStack:
          return true;
        case kConstant:
          return i32_const == other.i32_const;
      }
      UNREACHABLE();
    }

    bool is_stack() const { return loc == kStack; }
    bool is_reg() const { return loc == kRegister; }
    bool is_const() const { return loc == kConstant; }
  };

  static_assert(IS_TRIVIALLY_COPYABLE(VarState),
                "VarState should be trivially copyable");

  struct CacheState {
    MOVE_ONLY_WITH_DEFAULT_CONSTRUCTORS(CacheState);

    // TODO(clemensh): Improve memory management here; avoid std::vector.
    std::vector<VarState> stack_state;
    RegList used_registers = 0;
    // TODO(clemensh): Replace this by CountLeadingZeros(kGpCacheRegs) once that
    // method is constexpr.
    static constexpr int kMaxRegisterCode = 7;
    uint32_t register_use_count[kMaxRegisterCode + 1] = {0};
    // TODO(clemensh): Remove stack_base; use ControlBase::stack_depth.
    uint32_t stack_base = 0;
    Register last_spilled_reg = Register::from_code<0>();

    // TODO(clemensh): Don't copy the full parent state (this makes us N^2).
    void InitMerge(const CacheState& source, uint32_t num_locals,
                   uint32_t arity);

    void Steal(CacheState& source);

    void Split(const CacheState& source);

    bool has_unused_register(PinnedRegisterScope pinned_scope = {}) const {
      RegList available_regs =
          kGpCacheRegs & ~used_registers & ~pinned_scope.pinned_regs();
      return available_regs != 0;
    }

    Register unused_register(PinnedRegisterScope pinned_scope = {}) const {
      RegList available_regs =
          kGpCacheRegs & ~used_registers & ~pinned_scope.pinned_regs();
      Register reg =
          Register::from_code(base::bits::CountTrailingZeros64(available_regs));
      DCHECK_EQ(0, used_registers & reg.bit());
      return reg;
    }

    void inc_used(Register reg) {
      used_registers |= reg.bit();
      DCHECK_GE(kMaxRegisterCode, reg.code());
      ++register_use_count[reg.code()];
    }

    // Returns whether this was the last use.
    bool dec_used(Register reg) {
      DCHECK(is_used(reg));
      DCHECK_GE(kMaxRegisterCode, reg.code());
      if (--register_use_count[reg.code()] == 0) {
        used_registers &= ~reg.bit();
        return true;
      }
      return false;
    }

    bool is_used(Register reg) const {
      DCHECK_GE(kMaxRegisterCode, reg.code());
      bool used = used_registers & reg.bit();
      DCHECK_EQ(used, register_use_count[reg.code()] != 0);
      return used;
    }

    bool is_free(Register reg) const { return !is_used(reg); }

    uint32_t stack_height() const {
      return static_cast<uint32_t>(stack_state.size());
    }

    Register GetNextSpillReg(PinnedRegisterScope scope = {}) {
      uint32_t mask = (1u << (last_spilled_reg.code() + 1)) - 1;
      RegList unpinned_regs = kGpCacheRegs & ~scope.pinned_regs();
      DCHECK_NE(0, unpinned_regs);
      RegList remaining_regs = unpinned_regs & ~mask;
      if (!remaining_regs) remaining_regs = unpinned_regs;
      last_spilled_reg =
          Register::from_code(base::bits::CountTrailingZeros64(remaining_regs));
      return last_spilled_reg;
    }
  };

  Register PopToRegister(ValueType, PinnedRegisterScope = {});

  void PushRegister(Register reg) {
    cache_state_.inc_used(reg);
    cache_state_.stack_state.emplace_back(reg);
  }

  uint32_t GetNumUses(Register reg) const {
    DCHECK_GT(CacheState::kMaxRegisterCode, reg.code());
    return cache_state_.register_use_count[reg.code()];
  }

  Register GetUnusedRegister(ValueType type,
                             PinnedRegisterScope pinned_regs = {}) {
    DCHECK_EQ(kWasmI32, type);
    if (cache_state_.has_unused_register(pinned_regs)) {
      return cache_state_.unused_register(pinned_regs);
    }
    return SpillRegister(type, pinned_regs);
  }

  void DropStackSlot(VarState* slot) {
    // The only loc we care about is register. Other types don't occupy
    // anything.
    if (slot->loc != VarState::kRegister) return;
    // Free the register, then set the loc to "stack".
    // No need to write back, the value should be dropped.
    cache_state_.dec_used(slot->reg);
    slot->loc = VarState::kStack;
  }

  void MergeFullStackWith(CacheState&);
  void MergeStackWith(CacheState&, uint32_t arity);

  void Spill(uint32_t index);
  void SpillLocals();

  ////////////////////////////////////
  // Platform-specific part.        //
  ////////////////////////////////////

  inline void ReserveStackSpace(uint32_t);

  inline void LoadConstant(Register, WasmValue);
  inline void Load(Register, Address, RelocInfo::Mode = RelocInfo::NONE32);
  inline void Store(Address, Register, PinnedRegisterScope,
                    RelocInfo::Mode = RelocInfo::NONE32);
  inline void LoadCallerFrameSlot(Register, uint32_t caller_slot_idx);
  inline void MoveStackValue(uint32_t dst_index, uint32_t src_index, ValueType);

  inline void MoveToReturnRegister(Register);

  inline void Spill(uint32_t index, Register);
  inline void Spill(uint32_t index, WasmValue);
  inline void Fill(Register, uint32_t index);

  inline void emit_i32_add(Register dst, Register lhs, Register rhs);
  inline void emit_i32_sub(Register dst, Register lhs, Register rhs);
  inline void emit_i32_mul(Register dst, Register lhs, Register rhs);
  inline void emit_i32_and(Register dst, Register lhs, Register rhs);
  inline void emit_i32_or(Register dst, Register lhs, Register rhs);
  inline void emit_i32_xor(Register dst, Register lhs, Register rhs);

  inline void JumpIfZero(Register, Label*);

  // Platform-specific constant.
  static constexpr RegList kGpCacheRegs = kLiftoffAssemblerGpCacheRegs;

  ////////////////////////////////////
  // End of platform-specific part. //
  ////////////////////////////////////

  uint32_t num_locals() const { return num_locals_; }
  void set_num_locals(uint32_t num_locals);

  ValueType local_type(uint32_t index) {
    DCHECK_GT(num_locals_, index);
    ValueType* locals =
        num_locals_ <= kInlineLocalTypes ? local_types_ : more_local_types_;
    return locals[index];
  }

  void set_local_type(uint32_t index, ValueType type) {
    ValueType* locals =
        num_locals_ <= kInlineLocalTypes ? local_types_ : more_local_types_;
    locals[index] = type;
  }

  CacheState* cache_state() { return &cache_state_; }

 private:
  static_assert(
      base::bits::CountPopulation(kGpCacheRegs) >= 2,
      "We need at least two cache registers to execute binary operations");

  uint32_t num_locals_ = 0;
  uint32_t stack_space_ = 0;
  static constexpr uint32_t kInlineLocalTypes = 8;
  union {
    ValueType local_types_[kInlineLocalTypes];
    ValueType* more_local_types_;
  };
  static_assert(sizeof(ValueType) == 1,
                "Reconsider this inlining if ValueType gets bigger");
  CacheState cache_state_;

  Register SpillRegister(ValueType, PinnedRegisterScope = {});
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

// Include platform specific implementation.
#if V8_TARGET_ARCH_IA32
#include "src/wasm/baseline/ia32/liftoff-assembler-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/wasm/baseline/x64/liftoff-assembler-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/wasm/baseline/arm64/liftoff-assembler-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/wasm/baseline/arm/liftoff-assembler-arm.h"
#elif V8_TARGET_ARCH_PPC
#include "src/wasm/baseline/ppc/liftoff-assembler-ppc.h"
#elif V8_TARGET_ARCH_MIPS
#include "src/wasm/baseline/mips/liftoff-assembler-mips.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/wasm/baseline/mips64/liftoff-assembler-mips64.h"
#else
#error Unsupported architecture.
#endif

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_H_
