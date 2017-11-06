// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/baseline/liftoff-assembler.h"

#include "src/assembler-inl.h"
#include "src/compiler/linkage.h"
#include "src/compiler/wasm-compiler.h"
#include "src/counters.h"
#include "src/macro-assembler-inl.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/wasm-opcodes.h"

namespace v8 {
namespace internal {
namespace wasm {

// Note: "State" suffix added to avoid jumbo conflicts with liftoff-compiler.cc
constexpr auto kRegisterState = LiftoffAssembler::VarState::kRegister;
constexpr auto kConstantState = LiftoffAssembler::VarState::kConstant;
constexpr auto kStackState = LiftoffAssembler::VarState::kStack;

namespace {

#define __ asm_->

#define TRACE(...)                                            \
  do {                                                        \
    if (FLAG_trace_liftoff) PrintF("[liftoff] " __VA_ARGS__); \
  } while (false)

class StackTransferRecipe {
 public:
  explicit StackTransferRecipe(LiftoffAssembler* wasm_asm) : asm_(wasm_asm) {}
  ~StackTransferRecipe() { Execute(); }

  void Execute() {
    // TODO(clemensh): Find suitable schedule.
    for (RegisterMove& rm : register_moves) {
      asm_->Move(rm.dst, rm.src);
    }
    for (RegisterLoad& rl : register_loads) {
      if (rl.is_constant_load) {
        asm_->LoadConstant(rl.dst, rl.constant);
      } else {
        asm_->Fill(rl.dst, rl.stack_slot);
      }
    }
  }

  void TransferStackSlot(const LiftoffAssembler::CacheState& dst_state,
                         uint32_t dst_index, uint32_t src_index) {
    const LiftoffAssembler::VarState& dst = dst_state.stack_state[dst_index];
    const LiftoffAssembler::VarState& src =
        __ cache_state()->stack_state[src_index];
    switch (dst.loc) {
      case kConstantState:
        DCHECK_EQ(dst, src);
        break;
      case kRegisterState:
        switch (src.loc) {
          case kConstantState:
            LoadConstant(dst.reg, WasmValue(src.i32_const));
            break;
          case kRegisterState:
            if (dst.reg != src.reg) MoveRegister(dst.reg, src.reg);
            break;
          case kStackState:
            LoadStackSlot(dst.reg, src_index);
            break;
        }
        break;
      case kStackState:
        switch (src.loc) {
          case kConstantState:
            // TODO(clemensh): Handle other types than i32.
            asm_->Spill(dst_index, WasmValue(src.i32_const));
            break;
          case kRegisterState:
            asm_->Spill(dst_index, src.reg);
            break;
          case kStackState:
            if (src_index == dst_index) break;
            // TODO(clemensh): Implement other types than i32.
            asm_->MoveStackValue(dst_index, src_index, wasm::kWasmI32);
            break;
        }
    }
  }

  void MoveRegister(Register dst, Register src) {
    register_moves.emplace_back(dst, src);
  }

  void LoadConstant(Register dst, WasmValue value) {
    register_loads.emplace_back(dst, value);
  }

  void LoadStackSlot(Register dst, uint32_t stack_index) {
    register_loads.emplace_back(dst, stack_index);
  }

 private:
  struct RegisterMove {
    Register dst;
    Register src;
    RegisterMove(Register dst, Register src) : dst(dst), src(src) {}
  };
  struct RegisterLoad {
    Register dst;
    bool is_constant_load;  // otherwise load it from the stack.
    union {
      uint32_t stack_slot;
      WasmValue constant;
    };
    RegisterLoad(Register dst, WasmValue constant)
        : dst(dst), is_constant_load(true), constant(constant) {}
    RegisterLoad(Register dst, uint32_t stack_slot)
        : dst(dst), is_constant_load(false), stack_slot(stack_slot) {}
  };

  std::vector<RegisterMove> register_moves;
  std::vector<RegisterLoad> register_loads;
  LiftoffAssembler* asm_;
};

}  // namespace

// TODO(clemensh): Don't copy the full parent state (this makes us N^2).
void LiftoffAssembler::CacheState::InitMerge(const CacheState& source,
                                             uint32_t num_locals,
                                             uint32_t arity) {
  DCHECK(stack_state.empty());
  DCHECK_GE(source.stack_height(), stack_base);
  stack_state.resize(stack_base + arity);
  auto slot = stack_state.begin();

  // TODO(clemensh): Avoid using registers which are already in use in source.
  PinnedRegisterScope used_regs;
  auto InitStackSlot = [this, &used_regs, &slot](const VarState& src) {
    Register reg = no_reg;
    if (src.is_reg() && !used_regs.has(src.reg)) {
      reg = src.reg;
    } else if (has_unused_register(used_regs)) {
      reg = unused_register(used_regs);
    } else {
      // Keep this a stack slot (which is the initial value).
      DCHECK(slot->is_stack());
      return;
    }
    *slot = VarState(reg);
    ++slot;
    inc_used(reg);
    used_regs.pin(reg);
  };

  auto source_slot = source.stack_state.begin();
  for (uint32_t i = 0; i < stack_base; ++i, ++source_slot) {
    InitStackSlot(*source_slot);
  }
  DCHECK_GE(source.stack_height(), stack_base + arity);
  source_slot = source.stack_state.end() - arity;
  for (uint32_t i = 0; i < arity; ++i, ++source_slot) {
    InitStackSlot(*source_slot);
  }
  DCHECK_EQ(slot, stack_state.end());
  last_spilled_reg = source.last_spilled_reg;
}

void LiftoffAssembler::CacheState::Steal(CacheState& source) {
  stack_state.swap(source.stack_state);
  used_registers = source.used_registers;
  memcpy(register_use_count, source.register_use_count,
         sizeof(register_use_count));
  last_spilled_reg = source.last_spilled_reg;
}

void LiftoffAssembler::CacheState::Split(const CacheState& source) {
  stack_state = source.stack_state;
  used_registers = source.used_registers;
  memcpy(register_use_count, source.register_use_count,
         sizeof(register_use_count));
  last_spilled_reg = source.last_spilled_reg;
}

LiftoffAssembler::LiftoffAssembler(Isolate* isolate)
    : TurboAssembler(isolate, nullptr, 0, CodeObjectRequired::kYes) {}

LiftoffAssembler::~LiftoffAssembler() {
  if (num_locals_ > kInlineLocalTypes) {
    free(more_local_types_);
  }
}

Register LiftoffAssembler::GetBinaryOpTargetRegister(
    ValueType type, PinnedRegisterScope pinned_regs) {
  auto& slot_lhs = *(cache_state_.stack_state.end() - 2);
  if (slot_lhs.loc == kRegisterState && GetNumUses(slot_lhs.reg) == 1) {
    return slot_lhs.reg;
  }
  auto& slot_rhs = *(cache_state_.stack_state.end() - 1);
  if (slot_rhs.loc == kRegisterState && GetNumUses(slot_rhs.reg) == 1) {
    return slot_rhs.reg;
  }
  return GetUnusedRegister(type, pinned_regs);
}

void LiftoffAssembler::MergeFullStackWith(CacheState& target) {
  DCHECK_EQ(cache_state_.stack_height(), target.stack_height());
  // TODO(clemensh): Reuse the same StackTransferRecipe object to save some
  // allocations.
  StackTransferRecipe transfers(this);
  for (uint32_t i = 0, e = cache_state_.stack_height(); i < e; ++i) {
    transfers.TransferStackSlot(target, i, i);
  }
}

void LiftoffAssembler::MergeStackWith(CacheState& target, uint32_t arity) {
  // Before: ----------------|------ pop_count -----|--- arity ---|
  //                         ^target_stack_height   ^stack_base   ^stack_height
  // After:  ----|-- arity --|
  //             ^           ^target_stack_height
  //             ^target_stack_base
  uint32_t stack_height = cache_state_.stack_height();
  uint32_t target_stack_height = target.stack_height();
  uint32_t stack_base = stack_height - arity;
  uint32_t target_stack_base = target_stack_height - arity;
  StackTransferRecipe transfers(this);
  for (uint32_t i = 0; i < target_stack_base; ++i) {
    transfers.TransferStackSlot(target, i, i);
  }
  for (uint32_t i = 0; i < arity; ++i) {
    transfers.TransferStackSlot(target, target_stack_base + i, stack_base + i);
  }
}

void LiftoffAssembler::Spill(uint32_t index) {
  auto& slot = cache_state_.stack_state[index];
  switch (slot.loc) {
    case kRegisterState:
      Spill(index, slot.reg);
      cache_state_.dec_used(slot.reg);
      break;
    case kConstantState:
      Spill(index, WasmValue(slot.i32_const));
      break;
    case kStackState:
      return;
  }
  slot.loc = kStackState;
}

void LiftoffAssembler::SpillLocals() {
  for (uint32_t i = 0; i < num_locals_; ++i) {
    Spill(i);
  }
}

Register LiftoffAssembler::PopToRegister(ValueType type,
                                         PinnedRegisterScope pinned_regs) {
  DCHECK(!cache_state_.stack_state.empty());
  VarState slot = cache_state_.stack_state.back();
  cache_state_.stack_state.pop_back();
  switch (slot.loc) {
    case kRegisterState:
      cache_state_.dec_used(slot.reg);
      return slot.reg;
    case kConstantState: {
      Register reg = GetUnusedRegister(type, pinned_regs);
      LoadConstant(reg, WasmValue(slot.i32_const));
      return reg;
    }
    case kStackState: {
      Register reg = GetUnusedRegister(type, pinned_regs);
      Fill(reg, cache_state_.stack_height());
      return reg;
    }
  }
  UNREACHABLE();
}

Register LiftoffAssembler::SpillRegister(ValueType type,
                                         PinnedRegisterScope pinned_regs) {
  DCHECK_EQ(kWasmI32, type);

  // Spill one cached value to free a register.
  Register spill_reg = cache_state_.GetNextSpillReg(pinned_regs);
  int remaining_uses = cache_state_.register_use_count[spill_reg.code()];
  DCHECK_LT(0, remaining_uses);
  for (uint32_t idx = cache_state_.stack_height() - 1;; --idx) {
    DCHECK_GT(cache_state_.stack_height(), idx);
    auto& slot = cache_state_.stack_state[idx];
    if (!slot.is_reg() || slot.reg != spill_reg) continue;
    Spill(idx, spill_reg);
    slot.loc = kStackState;
    if (--remaining_uses == 0) break;
  }
  cache_state_.register_use_count[spill_reg.code()] = 0;
  cache_state_.used_registers &= ~spill_reg.bit();
  return spill_reg;
}

void LiftoffAssembler::set_num_locals(uint32_t num_locals) {
  DCHECK_EQ(0, num_locals_);  // only call this once.
  num_locals_ = num_locals;
  if (num_locals > kInlineLocalTypes) {
    more_local_types_ =
        reinterpret_cast<ValueType*>(malloc(num_locals * sizeof(ValueType)));
    DCHECK_NOT_NULL(more_local_types_);
  }
}

#undef __
#undef TRACE

}  // namespace wasm
}  // namespace internal
}  // namespace v8
