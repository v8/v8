// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_H_

#include <iosfwd>
#include <memory>

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "src/base/bits.h"
#include "src/frames.h"
#include "src/macro-assembler.h"
#include "src/wasm/baseline/liftoff-assembler-defs.h"
#include "src/wasm/baseline/liftoff-register.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-value.h"

namespace v8 {
namespace internal {
namespace wasm {

// Forward declarations.
struct ModuleEnv;

class LiftoffAssembler : public TurboAssembler {
 public:
  // TODO(clemensh): Remove this limitation by allocating more stack space if
  // needed.
  static constexpr int kMaxValueStackHeight = 8;

  // Each slot in our stack frame currently has exactly 8 bytes.
  static constexpr uint32_t kStackSlotSize = 8;

  class VarState {
   public:
    enum Location : uint8_t { kStack, kRegister, kConstant };

    explicit VarState(ValueType type) : loc_(kStack), type_(type) {}
    explicit VarState(ValueType type, LiftoffRegister r)
        : loc_(kRegister), type_(type), reg_(r) {
      DCHECK_EQ(r.reg_class(), reg_class_for(type));
    }
    explicit VarState(ValueType type, uint32_t i32_const)
        : loc_(kConstant), type_(type), i32_const_(i32_const) {
      DCHECK(type_ == kWasmI32 || type_ == kWasmI64);
    }

    bool operator==(const VarState& other) const {
      if (loc_ != other.loc_) return false;
      switch (loc_) {
        case kStack:
          return true;
        case kRegister:
          return reg_ == other.reg_;
        case kConstant:
          return i32_const_ == other.i32_const_;
      }
      UNREACHABLE();
    }

    bool is_stack() const { return loc_ == kStack; }
    bool is_gp_reg() const { return loc_ == kRegister && reg_.is_gp(); }
    bool is_fp_reg() const { return loc_ == kRegister && reg_.is_fp(); }
    bool is_reg() const { return loc_ == kRegister; }
    bool is_const() const { return loc_ == kConstant; }

    ValueType type() const { return type_; }

    Location loc() const { return loc_; }

    uint32_t i32_const() const {
      DCHECK_EQ(loc_, kConstant);
      return i32_const_;
    }
    Register gp_reg() const { return reg().gp(); }
    DoubleRegister fp_reg() const { return reg().fp(); }
    LiftoffRegister reg() const {
      DCHECK_EQ(loc_, kRegister);
      return reg_;
    }
    RegClass reg_class() const { return reg().reg_class(); }

    void MakeStack() { loc_ = kStack; }

   private:
    Location loc_;
    // TODO(wasm): This is redundant, the decoder already knows the type of each
    // stack value. Try to collapse.
    ValueType type_;

    union {
      LiftoffRegister reg_;  // used if loc_ == kRegister
      uint32_t i32_const_;  // used if loc_ == kConstant
    };
  };

  static_assert(IS_TRIVIALLY_COPYABLE(VarState),
                "VarState should be trivially copyable");

  struct CacheState {
    // Allow default construction, move construction, and move assignment.
    CacheState() = default;
    CacheState(CacheState&&) = default;
    CacheState& operator=(CacheState&&) = default;

    // TODO(clemensh): Improve memory management here; avoid std::vector.
    std::vector<VarState> stack_state;
    LiftoffRegList used_registers;
    uint32_t register_use_count[kAfterMaxLiftoffRegCode] = {0};
    LiftoffRegister last_spilled_gp_reg = kGpCacheRegList.GetFirstRegSet();
    LiftoffRegister last_spilled_fp_reg = kFpCacheRegList.GetFirstRegSet();
    // TODO(clemensh): Remove stack_base; use ControlBase::stack_depth.
    uint32_t stack_base = 0;

    bool has_unused_register(RegClass rc,
                             LiftoffRegList pinned_scope = {}) const {
      DCHECK(rc == kGpReg || rc == kFpReg);
      LiftoffRegList cache_regs = GetCacheRegList(rc);
      LiftoffRegList available_regs =
          cache_regs & ~used_registers & ~pinned_scope;
      return !available_regs.is_empty();
    }

    LiftoffRegister unused_register(RegClass rc,
                                    LiftoffRegList pinned_scope = {}) const {
      DCHECK(rc == kGpReg || rc == kFpReg);
      LiftoffRegList cache_regs = GetCacheRegList(rc);
      LiftoffRegList available_regs =
          cache_regs & ~used_registers & ~pinned_scope;
      return available_regs.GetFirstRegSet();
    }

    void inc_used(LiftoffRegister reg) {
      used_registers.set(reg);
      DCHECK_GT(kMaxInt, register_use_count[reg.liftoff_code()]);
      ++register_use_count[reg.liftoff_code()];
    }

    // Returns whether this was the last use.
    bool dec_used(LiftoffRegister reg) {
      DCHECK(is_used(reg));
      int code = reg.liftoff_code();
      DCHECK_LT(0, register_use_count[code]);
      if (--register_use_count[code] != 0) return false;
      used_registers.clear(reg);
      return true;
    }

    bool is_used(LiftoffRegister reg) const {
      bool used = used_registers.has(reg);
      DCHECK_EQ(used, register_use_count[reg.liftoff_code()] != 0);
      return used;
    }

    uint32_t get_use_count(LiftoffRegister reg) const {
      DCHECK_GT(arraysize(register_use_count), reg.liftoff_code());
      return register_use_count[reg.liftoff_code()];
    }

    void clear_used(LiftoffRegister reg) {
      register_use_count[reg.liftoff_code()] = 0;
      used_registers.clear(reg);
    }

    bool is_free(LiftoffRegister reg) const { return !is_used(reg); }

    LiftoffRegister GetNextSpillReg(RegClass rc, LiftoffRegList pinned = {}) {
      LiftoffRegister* last_spilled_p =
          rc == kGpReg ? &last_spilled_gp_reg : &last_spilled_fp_reg;
      LiftoffRegList cache_regs = GetCacheRegList(rc);
      LiftoffRegList unpinned = cache_regs & ~pinned;
      DCHECK(!unpinned.is_empty());
      LiftoffRegList remaining_regs =
          unpinned.MaskOut((1u << (last_spilled_p->liftoff_code() + 1)) - 1);
      if (remaining_regs.is_empty()) remaining_regs = unpinned;
      LiftoffRegister reg = remaining_regs.GetFirstRegSet();
      *last_spilled_p = reg;
      return reg;
    }

    // TODO(clemensh): Don't copy the full parent state (this makes us N^2).
    void InitMerge(const CacheState& source, uint32_t num_locals,
                   uint32_t arity);

    void Steal(CacheState& source);

    void Split(const CacheState& source);

    uint32_t stack_height() const {
      return static_cast<uint32_t>(stack_state.size());
    }

   private:
    // Make the copy assignment operator private (to be used from {Split()}).
    CacheState& operator=(const CacheState&) = default;
    // Disallow copy construction.
    CacheState(const CacheState&) = delete;
  };

  explicit LiftoffAssembler(Isolate* isolate);
  ~LiftoffAssembler();

  LiftoffRegister GetBinaryOpTargetRegister(RegClass,
                                            LiftoffRegList pinned = {});

  LiftoffRegister PopToRegister(RegClass, LiftoffRegList pinned = {});

  void PushRegister(ValueType type, LiftoffRegister reg) {
    DCHECK_EQ(reg_class_for(type), reg.reg_class());
    cache_state_.inc_used(reg);
    cache_state_.stack_state.emplace_back(type, reg);
  }

  uint32_t GetNumUses(LiftoffRegister reg) {
    return cache_state_.get_use_count(reg);
  }

  LiftoffRegister GetUnusedRegister(RegClass rc, LiftoffRegList pinned = {}) {
    if (cache_state_.has_unused_register(rc, pinned)) {
      return cache_state_.unused_register(rc, pinned);
    }
    return SpillOneRegister(rc, pinned);
  }

  void DropStackSlot(VarState* slot) {
    // The only loc we care about is register. Other types don't occupy
    // anything.
    if (!slot->is_reg()) return;
    // Free the register, then set the loc to "stack".
    // No need to write back, the value should be dropped.
    cache_state_.dec_used(slot->reg());
    slot->MakeStack();
  }

  void MergeFullStackWith(CacheState&);
  void MergeStackWith(CacheState&, uint32_t arity);

  void Spill(uint32_t index);
  void SpillLocals();

  ////////////////////////////////////
  // Platform-specific part.        //
  ////////////////////////////////////

  inline void ReserveStackSpace(uint32_t bytes);

  inline void LoadConstant(LiftoffRegister, WasmValue);
  inline void LoadFromContext(Register dst, uint32_t offset, int size);
  inline void SpillContext(Register context);
  inline void Load(LiftoffRegister dst, Register src_addr, Register offset_reg,
                   uint32_t offset_imm, LoadType type,
                   LiftoffRegList pinned = {});
  inline void Store(Register dst_addr, Register offset_reg, uint32_t offset_imm,
                    LiftoffRegister src, StoreType type,
                    LiftoffRegList pinned = {});
  inline void LoadCallerFrameSlot(LiftoffRegister, uint32_t caller_slot_idx);
  inline void MoveStackValue(uint32_t dst_index, uint32_t src_index);

  inline void MoveToReturnRegister(LiftoffRegister);
  // TODO(clemensh): Pass the type to {Move}, to emit more efficient code.
  inline void Move(LiftoffRegister dst, LiftoffRegister src);

  inline void Spill(uint32_t index, LiftoffRegister);
  inline void Spill(uint32_t index, WasmValue);
  inline void Fill(LiftoffRegister, uint32_t index);

  inline void emit_i32_add(Register dst, Register lhs, Register rhs);
  inline void emit_i32_sub(Register dst, Register lhs, Register rhs);
  inline void emit_i32_mul(Register dst, Register lhs, Register rhs);
  inline void emit_i32_and(Register dst, Register lhs, Register rhs);
  inline void emit_i32_or(Register dst, Register lhs, Register rhs);
  inline void emit_i32_xor(Register dst, Register lhs, Register rhs);

  inline void emit_ptrsize_add(Register dst, Register lhs, Register rhs);

  inline void emit_f32_add(DoubleRegister dst, DoubleRegister lhs,
                           DoubleRegister rhs);
  inline void emit_f32_sub(DoubleRegister dst, DoubleRegister lhs,
                           DoubleRegister rhs);
  inline void emit_f32_mul(DoubleRegister dst, DoubleRegister lhs,
                           DoubleRegister rhs);

  inline void emit_i32_test(Register);
  inline void emit_i32_compare(Register, Register);
  inline void emit_jump(Label*);
  inline void emit_cond_jump(Condition, Label*);

  inline void StackCheck(Label* ool_code);

  inline void CallTrapCallbackForTesting();

  inline void AssertUnreachable(AbortReason reason);

  inline void PushRegisters(LiftoffRegList);
  inline void PopRegisters(LiftoffRegList);

  inline void DropStackSlotsAndRet(uint32_t num_stack_slots);

  ////////////////////////////////////
  // End of platform-specific part. //
  ////////////////////////////////////

  uint32_t num_locals() const { return num_locals_; }
  void set_num_locals(uint32_t num_locals);

  uint32_t GetTotalFrameSlotCount() const;

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
  uint32_t num_locals_ = 0;
  static constexpr uint32_t kInlineLocalTypes = 8;
  union {
    ValueType local_types_[kInlineLocalTypes];
    ValueType* more_local_types_;
  };
  static_assert(sizeof(ValueType) == 1,
                "Reconsider this inlining if ValueType gets bigger");
  CacheState cache_state_;

  LiftoffRegister SpillOneRegister(RegClass rc, LiftoffRegList pinned);
};

std::ostream& operator<<(std::ostream& os, LiftoffAssembler::VarState);

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
#elif V8_TARGET_ARCH_S390
#include "src/wasm/baseline/s390/liftoff-assembler-s390.h"
#else
#error Unsupported architecture.
#endif

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_H_
