// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_RISCV_MACRO_ASSEMBLER_RISCV_H_
#define V8_CODEGEN_RISCV_MACRO_ASSEMBLER_RISCV_H_

#ifndef INCLUDED_FROM_MACRO_ASSEMBLER_H
#error This header must be included via macro-assembler.h
#endif

#include <optional>

#include "src/codegen/assembler-arch.h"
#include "src/codegen/assembler.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/register.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"
#include "src/execution/isolate-data.h"
#include "src/objects/tagged-index.h"

namespace v8 {
namespace internal {

#define xlen (static_cast<uint8_t>(sizeof(void*) * 8))
// Forward declarations.
enum class AbortReason : uint8_t;

// Reserved Register Usage Summary.
//
// Registers t5, t6, and t3 are reserved for use by the MacroAssembler.
//
// The programmer should know that the MacroAssembler may clobber these three,
// but won't touch other registers except in special cases.
//
// TODO(RISCV): Cannot find info about this ABI. We chose t6 for now.
// Per the RISC-V ABI, register t6 must be used for indirect function call
// via 'jalr t6' or 'jr t6' instructions. This is relied upon by gcc when
// trying to update gp register for position-independent-code. Whenever
// RISC-V generated code calls C code, it must be via t6 register.


// Flags used for the li macro-assembler function.
enum LiFlags {
  // If the constant value can be represented in just 16 bits, then
  // optimize the li to use a single instruction, rather than lui/ori/slli
  // sequence. A number of other optimizations that emits less than
  // maximum number of instructions exists.
  OPTIMIZE_SIZE = 0,
  // Always use 8 instructions (lui/addi/slliw sequence), even if the
  // constant
  // could be loaded with just one, so that this value is patchable later.
  CONSTANT_SIZE = 1,
  // For address loads 8 instruction are required. Used to mark
  // constant load that will be used as address without relocation
  // information. It ensures predictable code size, so specific sites
  // in code are patchable.
  ADDRESS_LOAD = 2
};

enum RAStatus { kRAHasNotBeenSaved, kRAHasBeenSaved };

Register GetRegisterThatIsNotOneOf(Register reg1, Register reg2 = no_reg,
                                   Register reg3 = no_reg,
                                   Register reg4 = no_reg,
                                   Register reg5 = no_reg,
                                   Register reg6 = no_reg);

// -----------------------------------------------------------------------------
// Static helper functions.

#if defined(V8_TARGET_LITTLE_ENDIAN)
#define SmiWordOffset(offset) (offset + kSystemPointerSize / 2)
#else
#define SmiWordOffset(offset) offset
#endif

// Generate a MemOperand for loading a field from an object.
inline MemOperand FieldMemOperand(Register object, int offset) {
  return MemOperand(object, offset - kHeapObjectTag);
}

// Generate a MemOperand for storing arguments 5..N on the stack
// when calling CallCFunction().
// TODO(plind): Currently ONLY used for O32. Should be fixed for
//              n64, and used in RegExp code, and other places
//              with more than 8 arguments.
inline MemOperand CFunctionArgumentOperand(int index) {
  DCHECK_GT(index, kCArgSlotCount);
  // Argument 5 takes the slot just past the four Arg-slots.
  int offset = (index - 5) * kSystemPointerSize + kCArgsSlotsSize;
  return MemOperand(sp, offset);
}

enum StackLimitKind { kInterruptStackLimit, kRealStackLimit };

class V8_EXPORT_PRIVATE MacroAssembler : public MacroAssemblerBase {
 public:
  using MacroAssemblerBase::MacroAssemblerBase;

  // Activation support.
  void EnterFrame(StackFrame::Type type);
  void EnterFrame(StackFrame::Type type, bool load_constant_pool_pointer_reg) {
    // Out-of-line constant pool not implemented on RISC-V.
    UNREACHABLE();
  }
  void LeaveFrame(StackFrame::Type type);

  // Generates function and stub prologue code.
  void StubPrologue(StackFrame::Type type);
  void Prologue();

  void InitializeRootRegister() {
    ExternalReference isolate_root = ExternalReference::isolate_root(isolate());
    li(kRootRegister, Operand(isolate_root));
#ifdef V8_COMPRESS_POINTERS
    LoadRootRelative(kPtrComprCageBaseRegister,
                     IsolateData::cage_base_offset());
#endif
  }

  void LoadIsolateField(const Register& rd, IsolateFieldId id);

  // Jump unconditionally to given label.
  void jmp(Label* L, Label::Distance distance = Label::kFar) {
    Branch(L, distance);
  }

  // -------------------------------------------------------------------------
  // Debugging.

  void Trap();
  void DebugBreak();
#ifdef USE_SIMULATOR
  // See src/codegen/riscv/base-constants-riscv.h DebugParameters.
  void Debug(uint32_t parameters) { break_(parameters, false); }
#endif
  // Calls Abort(msg) if the condition cc is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cc, AbortReason reason, Register rs, Operand rt);

  void AssertJSAny(Register object, Register map_tmp, Register tmp,
                   AbortReason abort_reason);

  // Abort execution if argument is not smi nor in the main pointer
  // compression cage, enabled via --debug-code.
  void AssertSmiOrHeapObjectInMainCompressionCage(Register object)
      NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but always enabled.
  void Check(Condition cc, AbortReason reason, Register rs, Operand rt);

  // Same as Check() but expresses that the check is needed for the sandbox.
  void SbxCheck(Condition cc, AbortReason reason, Register rs, Operand rt);

  // Print a message to stdout and abort execution.
  void Abort(AbortReason msg);

  // Arguments macros.
#define COND_TYPED_ARGS Condition cond, Register r1, const Operand &r2
#define COND_ARGS cond, r1, r2

  // Cases when relocation is not needed.
#define DECLARE_NORELOC_PROTOTYPE(Name, target_type) \
  void Name(target_type target);                     \
  void Name(target_type target, COND_TYPED_ARGS);

#define DECLARE_BRANCH_PROTOTYPES(Name)   \
  DECLARE_NORELOC_PROTOTYPE(Name, Label*) \
  DECLARE_NORELOC_PROTOTYPE(Name, int32_t)

  DECLARE_BRANCH_PROTOTYPES(BranchAndLink)
  DECLARE_BRANCH_PROTOTYPES(BranchShort)

  void Branch(Label* target);
  void Branch(int32_t target);
  void BranchLong(Label* L);
  void Branch(Label* target, Condition cond, Register r1, const Operand& r2,
              Label::Distance distance = Label::kFar);
  void Branch(Label* target, Label::Distance distance) {
    Branch(target, cc_always, zero_reg, Operand(zero_reg), distance);
  }
  void Branch(int32_t target, Condition cond, Register r1, const Operand& r2,
              Label::Distance distance = Label::kFar);
  void Branch(Label* L, Condition cond, Register rj, RootIndex index,
              Label::Distance distance = Label::kFar);
#undef DECLARE_BRANCH_PROTOTYPES
#undef COND_TYPED_ARGS
#undef COND_ARGS

  void BranchRange(Label* L, Condition cond, Register value, Register scratch,
                   unsigned lower_limit, unsigned higher_limit,
                   Label::Distance distance = Label::kFar);
  void AllocateStackSpace(Register bytes) { SubWord(sp, sp, bytes); }

  void AllocateStackSpace(int bytes) {
    DCHECK_GE(bytes, 0);
    if (bytes == 0) return;
    SubWord(sp, sp, Operand(bytes));
  }

  inline void NegateBool(Register rd, Register rs) { Xor(rd, rs, 1); }

  // Compare float, if any operand is NaN, result is false except for NE
  void CompareF32(Register rd, FPUCondition cc, FPURegister cmp1,
                  FPURegister cmp2);
  // Compare double, if any operand is NaN, result is false except for NE
  void CompareF64(Register rd, FPUCondition cc, FPURegister cmp1,
                  FPURegister cmp2);
  void CompareIsNotNanF32(Register rd, FPURegister cmp1, FPURegister cmp2);
  void CompareIsNotNanF64(Register rd, FPURegister cmp1, FPURegister cmp2);
  void CompareIsNanF32(Register rd, FPURegister cmp1, FPURegister cmp2);
  void CompareIsNanF64(Register rd, FPURegister cmp1, FPURegister cmp2);

  // Floating point branches
  void BranchTrueShortF(Register rs, Label* target);
  void BranchFalseShortF(Register rs, Label* target);

  void BranchTrueF(Register rs, Label* target);
  void BranchFalseF(Register rs, Label* target);

  void CompareTaggedAndBranch(Label* label, Condition cond, Register r1,
                              const Operand& r2, bool need_link = false);
  static int InstrCountForLi64Bit(int64_t value);
  inline void LiLower32BitHelper(Register rd, Operand j);
  void li_optimized(Register rd, Operand j, LiFlags mode = OPTIMIZE_SIZE);
  // Load int32 in the rd register.
  void li(Register rd, Operand j, LiFlags mode = OPTIMIZE_SIZE);
  inline void li(Register rd, intptr_t j, LiFlags mode = OPTIMIZE_SIZE) {
    li(rd, Operand(j), mode);
  }

  inline void Move(Register output, MemOperand operand) {
    LoadWord(output, operand);
  }
  void li(Register dst, Handle<HeapObject> value,
          RelocInfo::Mode rmode = RelocInfo::FULL_EMBEDDED_OBJECT);
  void li(Register dst, ExternalReference value, LiFlags mode = OPTIMIZE_SIZE);

  void LoadFromConstantsTable(Register destination, int constant_index) final;
  void LoadRootRegisterOffset(Register destination, intptr_t offset) final;
  void LoadRootRelative(Register destination, int32_t offset) final;
  void StoreRootRelative(int32_t offset, Register value) final;

  // Operand pointing to an external reference.
  // May emit code to set up the scratch register. The operand is
  // only guaranteed to be correct as long as the scratch register
  // isn't changed.
  // If the operand is used more than once, use a scratch register
  // that is guaranteed not to be clobbered.
  MemOperand ExternalReferenceAsOperand(ExternalReference reference,
                                        Register scratch);
  MemOperand ExternalReferenceAsOperand(IsolateFieldId id) {
    return ExternalReferenceAsOperand(ExternalReference::Create(id), no_reg);
  }
  inline void GenPCRelativeJump(Register rd, int32_t imm32) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    DCHECK(is_int32(imm32 + 0x800));
    int32_t Hi20 = ((imm32 + 0x800) >> 12);
    int32_t Lo12 = imm32 << 20 >> 20;
    auipc(rd, Hi20);  // Read PC + Hi20 into scratch.
    jr(rd, Lo12);     // jump PC + Hi20 + Lo12
  }

  inline void GenPCRelativeJumpAndLink(Register rd, int32_t imm32) {
    BlockTrampolinePoolScope block_trampoline_pool(this);
    DCHECK(is_int32(imm32 + 0x800));
    int32_t Hi20 = ((imm32 + 0x800) >> 12);
    int32_t Lo12 = imm32 << 20 >> 20;
    auipc(rd, Hi20);  // Read PC + Hi20 into scratch.
    jalr(rd, Lo12);   // jump PC + Hi20 + Lo12
  }

  // Generate a B immediate instruction with the corresponding relocation info.
  // 'offset' is the immediate to encode in the B instruction (so it is the
  // difference between the target and the PC of the instruction, divided by
  // the instruction size).
  void near_jump(int offset, RelocInfo::Mode rmode) {
    UseScratchRegisterScope temps(this);
    Register temp = temps.Acquire();
    if (!RelocInfo::IsNoInfo(rmode)) RecordRelocInfo(rmode, offset);
    GenPCRelativeJump(temp, offset);
  }
  // Generate a auipc+jalr instruction with the corresponding relocation info.
  // As for near_jump, 'offset' is the immediate to encode in the auipc+jalr
  // instruction.
  void near_call(int offset, RelocInfo::Mode rmode) {
    UseScratchRegisterScope temps(this);
    Register temp = temps.Acquire();
    if (!RelocInfo::IsNoInfo(rmode)) RecordRelocInfo(rmode, offset);
    GenPCRelativeJumpAndLink(temp, offset);
  }
  // Generate a BL immediate instruction with the corresponding relocation info
  // for the input HeapNumberRequest.
  void near_call(HeapNumberRequest request) { UNIMPLEMENTED(); }

// Jump, Call, and Ret pseudo instructions implementing inter-working.
#define COND_ARGS                              \
  Condition cond = al, Register rs = zero_reg, \
            const Operand &rt = Operand(zero_reg)

  void Jump(Register target, COND_ARGS);
  void Jump(intptr_t target, RelocInfo::Mode rmode, COND_ARGS);
  void Jump(Address target, RelocInfo::Mode rmode, COND_ARGS);
  // Deffer from li, this method save target to the memory, and then load
  // it to register use ld, it can be used in wasm jump table for concurrent
  // patching.

  // We should not use near calls or jumps for calls to external references,
  // since the code spaces are not guaranteed to be close to each other.
  bool CanUseNearCallOrJump(RelocInfo::Mode rmode) {
    return rmode != RelocInfo::EXTERNAL_REFERENCE;
  }
  static int64_t CalculateTargetOffset(Address target, RelocInfo::Mode rmode,
                                       uint8_t* pc);
  void Jump(Handle<Code> code, RelocInfo::Mode rmode, COND_ARGS);
  void Jump(const ExternalReference& reference);
  void Call(Register target, COND_ARGS);
  void Call(Address target, RelocInfo::Mode rmode, COND_ARGS);
  void Call(Handle<Code> code, RelocInfo::Mode rmode = RelocInfo::CODE_TARGET,
            COND_ARGS);
  void Call(Label* target);
  void LoadAddress(
      Register dst, Label* target,
      RelocInfo::Mode rmode = RelocInfo::INTERNAL_REFERENCE_ENCODED);

  // Load the code entry point from the Code object.
  void LoadCodeInstructionStart(
      Register destination, Register code_object,
      CodeEntrypointTag tag = kDefaultCodeEntrypointTag);
  void CallCodeObject(Register code_object, CodeEntrypointTag tag);
  void JumpCodeObject(Register code_object, CodeEntrypointTag tag,
                      JumpMode jump_mode = JumpMode::kJump);

  // Convenience functions to call/jmp to the code of a JSFunction object.
  void CallJSFunction(Register function_object, uint16_t argument_count);
  void JumpJSFunction(Register function_object,
                      JumpMode jump_mode = JumpMode::kJump);
#ifdef V8_ENABLE_LEAPTIERING
  void CallJSDispatchEntry(JSDispatchHandle dispatch_handle,
                           uint16_t argument_count);
#endif
#ifdef V8_ENABLE_WEBASSEMBLY
  void ResolveWasmCodePointer(Register target, uint64_t signature_hash);
  void CallWasmCodePointer(Register target, uint64_t signature_hash,
                           CallJumpMode call_jump_mode = CallJumpMode::kCall);
  void CallWasmCodePointerNoSignatureCheck(Register target);
  void LoadWasmCodePointer(Register dst, MemOperand src);
#endif

  // Load the builtin given by the Smi in |builtin| into the same
  // register.
  // Load the builtin given by the Smi in |builtin_index| into |target|.
  void LoadEntryFromBuiltinIndex(Register builtin_index, Register target);
  void LoadEntryFromBuiltin(Builtin builtin, Register destination);
  MemOperand EntryFromBuiltinAsOperand(Builtin builtin);
  void CallBuiltinByIndex(Register builtin_index, Register target);
  void CallBuiltin(Builtin builtin);
  void TailCallBuiltin(Builtin builtin);
  void TailCallBuiltin(Builtin builtin, Condition cond, Register type,
                       Operand range);

  // Generates an instruction sequence s.t. the return address points to the
  // instruction following the call.
  // The return address on the stack is used by frame iteration.
  void StoreReturnAddressAndCall(Register target);
#ifdef V8_TARGET_ARCH_RISCV32
  // Enforce platform specific stack alignment.
  void EnforceStackAlignment();
#endif
  void BailoutIfDeoptimized();
  void CallForDeoptimization(Builtin target, int deopt_id, Label* exit,
                             DeoptimizeKind kind, Label* ret,
                             Label* jump_deoptimization_entry_label);

  void Ret(COND_ARGS);

  // Emit code to discard a non-negative number of pointer-sized elements
  // from the stack, clobbering only the sp register.
  void Drop(int count, Condition cond = cc_always, Register reg = no_reg,
            const Operand& op = Operand(no_reg));

  // Trivial case of DropAndRet that only emits 2 instructions.
  void DropAndRet(int drop);

  void DropAndRet(int drop, Condition cond, Register reg, const Operand& op);

  void push(Register src) {
    AddWord(sp, sp, Operand(-kSystemPointerSize));
    StoreWord(src, MemOperand(sp, 0));
  }
  void Push(Register src) { push(src); }
  void Push(Handle<HeapObject> handle);
  void Push(Tagged<Smi> smi);
  void Push(Tagged<TaggedIndex> index);

 private:
  template <typename... Rs>
  void push_helper(Register r, Rs... rs) {
    StoreWord(r, MemOperand(sp, sizeof...(rs) * kSystemPointerSize));
    push_helper(rs...);
  }

  void push_helper() {}

 public:
  // Push a number of registers. The leftmost register first (to the highest
  // address).
  template <typename... Rs>
  void Push(Register r, Rs... rs) {
    SubWord(sp, sp, (sizeof...(rs) + 1) * kSystemPointerSize);
    push_helper(r, rs...);
  }

  void Push(Register src, Condition cond, Register tst1, Register tst2) {
    // Since we don't have conditional execution we use a Branch.
    Branch(3, cond, tst1, Operand(tst2));
    SubWord(sp, sp, Operand(kSystemPointerSize));
    StoreWord(src, MemOperand(sp, 0));
  }

  enum PushArrayOrder { kNormal, kReverse };
  void PushArray(Register array, Register size, PushArrayOrder order = kNormal);

  // Caution: if {value} is a 32-bit negative int, it should be sign-extended
  // to 64-bit before calling this function.
  void Switch(Register scratch, Register value, int case_value_base,
              Label** labels, int num_labels);

  void MaybeSaveRegisters(RegList registers);
  void MaybeRestoreRegisters(RegList registers);

  void CallEphemeronKeyBarrier(Register object, Operand offset,
                               SaveFPRegsMode fp_mode);
  void CallIndirectPointerBarrier(Register object, Operand offset,
                                  SaveFPRegsMode fp_mode,
                                  IndirectPointerTag tag);
  void CallRecordWriteStubSaveRegisters(
      Register object, Operand offset, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);
  void CallRecordWriteStub(
      Register object, Register slot_address, SaveFPRegsMode fp_mode,
      StubCallMode mode = StubCallMode::kCallBuiltinPointer);

  // For a given |object| and |offset|:
  //   - Move |object| to |dst_object|.
  //   - Compute the address of the slot pointed to by |offset| in |object| and
  //     write it to |dst_slot|.
  // This method makes sure |object| and |offset| are allowed to overlap with
  // the destination registers.
  void MoveObjectAndSlot(Register dst_object, Register dst_slot,
                         Register object, Operand offset);

  // These PushAll/PopAll respect the order of the registers in the stack from
  // low index to high.
  void PushAll(RegList registers) {
    if (registers.is_empty()) return;
    ASM_CODE_COMMENT(this);
    // TODO(victorgomes): pushes/pops registers in the opposite order
    // as expected by Maglev frame. Consider massaging Maglev to accept this
    // order instead.
    int16_t num_to_push = registers.Count();
    int16_t stack_offset = num_to_push * kSystemPointerSize;

    SubWord(sp, sp, Operand(stack_offset));
    for (int16_t i = 0; i < kNumRegisters; i++) {
      if ((registers.bits() & (1 << i)) != 0) {
        stack_offset -= kSystemPointerSize;
        StoreWord(Register::from_code(i), MemOperand(sp, stack_offset));
      }
    }
  }

  void PopAll(RegList registers) {
    if (registers.is_empty()) return;
    int16_t stack_offset = 0;
    for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
      if ((registers.bits() & (1 << i)) != 0) {
        LoadWord(Register::from_code(i), MemOperand(sp, stack_offset));
        stack_offset += kSystemPointerSize;
      }
    }
    AddWord(sp, sp, stack_offset);
  }

  void PushAll(DoubleRegList registers, int stack_slot_size = kDoubleSize) {
    DCHECK_EQ(stack_slot_size, kDoubleSize);
    int16_t num_to_push = registers.Count();
    int16_t stack_offset = num_to_push * kDoubleSize;

    SubWord(sp, sp, Operand(stack_offset));
    for (int16_t i = 0; i < kNumRegisters; i++) {
      if ((registers.bits() & (1 << i)) != 0) {
        stack_offset -= kDoubleSize;
        StoreDouble(FPURegister::from_code(i), MemOperand(sp, stack_offset));
      }
    }
  }

  void PopAll(DoubleRegList registers, int stack_slot_size = kDoubleSize) {
    DCHECK_EQ(stack_slot_size, kDoubleSize);
    int16_t stack_offset = 0;
    for (int16_t i = kNumRegisters - 1; i >= 0; i--) {
      if ((registers.bits() & (1 << i)) != 0) {
        LoadDouble(FPURegister::from_code(i), MemOperand(sp, stack_offset));
        stack_offset += kDoubleSize;
      }
    }
    AddWord(sp, sp, stack_offset);
  }

  // Push multiple registers on the stack.
  // Registers are saved in numerical order, with higher numbered registers
  // saved in higher memory addresses.
  void MultiPush(RegList regs);
  void MultiPushFPU(DoubleRegList regs);

  // Calculate how much stack space (in bytes) are required to store caller
  // registers excluding those specified in the arguments.
  int RequiredStackSizeForCallerSaved(SaveFPRegsMode fp_mode,
                                      Register exclusion1 = no_reg,
                                      Register exclusion2 = no_reg,
                                      Register exclusion3 = no_reg) const;

  // Push caller saved registers on the stack, and return the number of bytes
  // stack pointer is adjusted.
  int PushCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1 = no_reg,
                      Register exclusion2 = no_reg,
                      Register exclusion3 = no_reg);
  // Restore caller saved registers from the stack, and return the number of
  // bytes stack pointer is adjusted.
  int PopCallerSaved(SaveFPRegsMode fp_mode, Register exclusion1 = no_reg,
                     Register exclusion2 = no_reg,
                     Register exclusion3 = no_reg);

  void pop(Register dst) {
    LoadWord(dst, MemOperand(sp, 0));
    AddWord(sp, sp, Operand(kSystemPointerSize));
  }
  void Pop(Register dst) { pop(dst); }

 private:
  template <typename... Rs>
  void pop_helper(Register r, Rs... rs) {
    pop_helper(rs...);
    LoadWord(r, MemOperand(sp, sizeof...(rs) * kSystemPointerSize));
  }

  void pop_helper() {}

 public:
  // Pop a number of registers. The leftmost register last (from the highest
  // address).
  template <typename... Rs>
  void Pop(Register r, Rs... rs) {
    pop_helper(r, rs...);
    AddWord(sp, sp, (sizeof...(rs) + 1) * kSystemPointerSize);
  }

  void Pop(uint32_t count = 1) {
    AddWord(sp, sp, Operand(count * kSystemPointerSize));
  }

  // Pops multiple values from the stack and load them in the
  // registers specified in regs. Pop order is the opposite as in MultiPush.
  void MultiPop(RegList regs);
  void MultiPopFPU(DoubleRegList regs);

#define DEFINE_INSTRUCTION(instr)                          \
  void instr(Register rd, Register rs, const Operand& rt); \
  void instr(Register rd, Register rs, Register rt) {      \
    instr(rd, rs, Operand(rt));                            \
  }                                                        \
  void instr(Register rs, Register rt, int32_t j) { instr(rs, rt, Operand(j)); }

#define DEFINE_INSTRUCTION2(instr)                                 \
  void instr(Register rs, const Operand& rt);                      \
  void instr(Register rs, Register rt) { instr(rs, Operand(rt)); } \
  void instr(Register rs, int32_t j) { instr(rs, Operand(j)); }

#define DEFINE_INSTRUCTION3(instr) void instr(Register rd, intptr_t imm);

  DEFINE_INSTRUCTION(AddWord)
  DEFINE_INSTRUCTION(SubWord)
  DEFINE_INSTRUCTION(SllWord)
  DEFINE_INSTRUCTION(SrlWord)
  DEFINE_INSTRUCTION(SraWord)
#if V8_TARGET_ARCH_RISCV64
  DEFINE_INSTRUCTION(Add32)
  DEFINE_INSTRUCTION(Add64)
  DEFINE_INSTRUCTION(Div32)
  DEFINE_INSTRUCTION(Divu32)
  DEFINE_INSTRUCTION(Divu64)
  DEFINE_INSTRUCTION(Mod32)
  DEFINE_INSTRUCTION(Modu32)
  DEFINE_INSTRUCTION(Div64)
  DEFINE_INSTRUCTION(Sub32)
  DEFINE_INSTRUCTION(Sub64)
  DEFINE_INSTRUCTION(Mod64)
  DEFINE_INSTRUCTION(Modu64)
  DEFINE_INSTRUCTION(Mul32)
  DEFINE_INSTRUCTION(Mulh32)
  DEFINE_INSTRUCTION(Mul64)
  DEFINE_INSTRUCTION(Mulh64)
  DEFINE_INSTRUCTION(Mulhu64)
  DEFINE_INSTRUCTION2(Div32)
  DEFINE_INSTRUCTION2(Div64)
  DEFINE_INSTRUCTION2(Divu32)
  DEFINE_INSTRUCTION2(Divu64)
  DEFINE_INSTRUCTION(Sll64)
  DEFINE_INSTRUCTION(Sra64)
  DEFINE_INSTRUCTION(Srl64)
  DEFINE_INSTRUCTION(Dror)
#elif V8_TARGET_ARCH_RISCV32
  DEFINE_INSTRUCTION(Add32)
  DEFINE_INSTRUCTION(Div)
  DEFINE_INSTRUCTION(Divu)
  DEFINE_INSTRUCTION(Mod)
  DEFINE_INSTRUCTION(Modu)
  DEFINE_INSTRUCTION(Sub32)
  DEFINE_INSTRUCTION(Mul)
  DEFINE_INSTRUCTION(Mul32)
  DEFINE_INSTRUCTION(Mulh)
  DEFINE_INSTRUCTION2(Div)
  DEFINE_INSTRUCTION2(Divu)
#endif
  DEFINE_INSTRUCTION(And)
  DEFINE_INSTRUCTION(Or)
  DEFINE_INSTRUCTION(Xor)
  DEFINE_INSTRUCTION(Nor)
  DEFINE_INSTRUCTION2(Neg)

  DEFINE_INSTRUCTION(Slt)
  DEFINE_INSTRUCTION(Sltu)
  DEFINE_INSTRUCTION(Sle)
  DEFINE_INSTRUCTION(Sleu)
  DEFINE_INSTRUCTION(Sgt)
  DEFINE_INSTRUCTION(Sgtu)
  DEFINE_INSTRUCTION(Sge)
  DEFINE_INSTRUCTION(Sgeu)
  DEFINE_INSTRUCTION(Seq)
  DEFINE_INSTRUCTION(Sne)
  DEFINE_INSTRUCTION(Sll32)
  DEFINE_INSTRUCTION(Sra32)
  DEFINE_INSTRUCTION(Srl32)

  DEFINE_INSTRUCTION2(Seqz)
  DEFINE_INSTRUCTION2(Snez)

  DEFINE_INSTRUCTION(Ror)

  DEFINE_INSTRUCTION3(Li)
  DEFINE_INSTRUCTION2(Mv)

#undef DEFINE_INSTRUCTION
#undef DEFINE_INSTRUCTION2
#undef DEFINE_INSTRUCTION3

  void Amosub_w(bool aq, bool rl, Register rd, Register rs1, Register rs2) {
    UseScratchRegisterScope temps(this);
    Register temp = temps.Acquire();
    sub(temp, zero_reg, rs2);
    amoadd_w(aq, rl, rd, rs1, temp);
  }

  // Convert smi to word-size sign-extended value.
  void SmiUntag(Register dst, const MemOperand& src);
  void SmiUntag(Register dst, Register src) {
#if V8_TARGET_ARCH_RISCV64
    DCHECK(SmiValuesAre32Bits() || SmiValuesAre31Bits());
    if (COMPRESS_POINTERS_BOOL) {
      sraiw(dst, src, kSmiShift);
    } else {
      SraWord(dst, src, kSmiShift);
    }
#elif V8_TARGET_ARCH_RISCV32
    DCHECK(SmiValuesAre31Bits());
    SraWord(dst, src, kSmiShift);
#endif
  }

  void SmiUntag(Register reg) { SmiUntag(reg, reg); }
  // Convert smi to 32-bit value.
  void SmiToInt32(Register smi);
  void SmiToInt32(Register dst, Register smi);

  // Enabled via --debug-code.
  void AssertNotSmi(Register object,
                    AbortReason reason = AbortReason::kOperandIsASmi);
  void AssertSmi(Register object,
                 AbortReason reason = AbortReason::kOperandIsASmi);

  // Abort execution if a 64 bit register containing a 32 bit payload does
  // not have zeros in the top 32 bits, enabled via --debug-code.
  void AssertZeroExtended(Register int32_register) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if a 64 bit register containing a 32 bit payload does
  // not have all zeros or all ones in the top 32 bits, enabled via
  // --debug-code.
  void AssertSignExtended(Register int32_register) NOOP_UNLESS_DEBUG_CODE;

  void AssertRange(Condition cond, AbortReason reason, Register value,
                   Register scratch, unsigned lower_limit,
                   unsigned higher_limit) NOOP_UNLESS_DEBUG_CODE;

  int CalculateStackPassedDWords(int num_gp_arguments, int num_fp_arguments);

  // Before calling a C-function from generated code, align arguments on stack.
  // After aligning the frame, non-register arguments must be stored on the
  // stack, using helper: CFunctionArgumentOperand().
  // The argument count assumes all arguments are word sized.
  // Some compilers/platforms require the stack to be aligned when calling
  // C++ code.
  // Needs a scratch register to do some arithmetic. This register will be
  // trashed.
  void PrepareCallCFunction(int num_reg_arguments, int num_double_registers,
                            Register scratch);
  void PrepareCallCFunction(int num_reg_arguments, Register scratch);

  // Arguments 1-8 are placed in registers a0 through a7 respectively.
  // Arguments 9..n are stored to stack

  // Calls a C function and cleans up the space for arguments allocated
  // by PrepareCallCFunction. The called function is not allowed to trigger a
  // garbage collection, since that might move the code and invalidate the
  // return address (unless this is somehow accounted for by the called
  // function).
  int CallCFunction(
      ExternalReference function, int num_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  int CallCFunction(
      Register function, int num_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  int CallCFunction(
      ExternalReference function, int num_reg_arguments,
      int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  int CallCFunction(
      Register function, int num_reg_arguments, int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);
  void MovFromFloatResult(DoubleRegister dst);
  void MovFromFloatParameter(DoubleRegister dst);

  // These functions abstract parameter passing for the three different ways
  // we call C functions from generated code.
  void MovToFloatParameter(DoubleRegister src);
  void MovToFloatParameters(DoubleRegister src1, DoubleRegister src2);
  void MovToFloatResult(DoubleRegister src);

  // See comments at the beginning of Builtins::Generate_CEntry.
  inline void PrepareCEntryArgs(int num_args) { li(a0, num_args); }
  inline void PrepareCEntryFunction(const ExternalReference& ref) {
    li(a1, ref);
  }

  void CheckPageFlag(Register object, int mask, Condition cc,
                     Label* condition_met);

  void CheckPageFlag(const Register& object, Register scratch, int mask,
                     Condition cc, Label* condition_met) {
    CheckPageFlag(object, mask, cc, condition_met);
  }
#undef COND_ARGS

  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32.
  // Exits with 'result' holding the answer.
  void TruncateDoubleToI(Isolate* isolate, Zone* zone, Register result,
                         DoubleRegister double_input, StubCallMode stub_mode);

  void CompareI(Register rd, Register rs, const Operand& rt, Condition cond);

  void LoadZeroIfConditionNotZero(Register dest, Register condition);
  void LoadZeroIfConditionZero(Register dest, Register condition);

  void SignExtendByte(Register rd, Register rs) {
    if (CpuFeatures::IsSupported(ZBB)) {
      sextb(rd, rs);
    } else {
      SllWord(rd, rs, xlen - 8);
      SraWord(rd, rd, xlen - 8);
    }
  }

  void SignExtendShort(Register rd, Register rs) {
    if (CpuFeatures::IsSupported(ZBB)) {
      sexth(rd, rs);
    } else {
      SllWord(rd, rs, xlen - 16);
      SraWord(rd, rd, xlen - 16);
    }
  }

  void Clz32(Register rd, Register rs);
  void Ctz32(Register rd, Register rs);
  void Popcnt32(Register rd, Register rs, Register scratch);

#if V8_TARGET_ARCH_RISCV64
  void SignExtendWord(Register rd, Register rs) { sext_w(rd, rs); }
  void ZeroExtendWord(Register rd, Register rs) {
    if (CpuFeatures::IsSupported(ZBA)) {
      zextw(rd, rs);
    } else {
      Sll64(rd, rs, 32);
      Srl64(rd, rd, 32);
    }
  }
  void Popcnt64(Register rd, Register rs, Register scratch);
  void Ctz64(Register rd, Register rs);
  void Clz64(Register rd, Register rs);
#elif V8_TARGET_ARCH_RISCV32
  void AddPair(Register dst_low, Register dst_high, Register left_low,
               Register left_high, Register right_low, Register right_high,
               Register scratch1, Register scratch2);

  void SubPair(Register dst_low, Register dst_high, Register left_low,
               Register left_high, Register right_low, Register right_high,
               Register scratch1, Register scratch2);

  void AndPair(Register dst_low, Register dst_high, Register left_low,
               Register left_high, Register right_low, Register right_high);

  void OrPair(Register dst_low, Register dst_high, Register left_low,
              Register left_high, Register right_low, Register right_high);

  void XorPair(Register dst_low, Register dst_high, Register left_low,
               Register left_high, Register right_low, Register right_high);

  void MulPair(Register dst_low, Register dst_high, Register left_low,
               Register left_high, Register right_low, Register right_high,
               Register scratch1, Register scratch2);

  void ShlPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, Register shift, Register scratch1,
               Register scratch2);
  void ShlPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, int32_t shift, Register scratch1,
               Register scratch2);

  void ShrPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, Register shift, Register scratch1,
               Register scratch2);

  void ShrPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, int32_t shift, Register scratch1,
               Register scratch2);

  void SarPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, Register shift, Register scratch1,
               Register scratch2);
  void SarPair(Register dst_low, Register dst_high, Register src_low,
               Register src_high, int32_t shift, Register scratch1,
               Register scratch2);
#endif

  // Bit field starts at bit pos and extending for size bits is extracted from
  // rs and stored zero/sign-extended and right-justified in rt
  void ExtractBits(Register rt, Register rs, uint16_t pos, uint16_t size,
                   bool sign_extend = false);
  void ExtractBits(Register dest, Register source, Register pos, int size,
                   bool sign_extend = false) {
    sra(dest, source, pos);
    ExtractBits(dest, dest, 0, size, sign_extend);
  }

  // Insert bits [0, size) of source to bits [pos, pos+size) of dest
  void InsertBits(Register dest, Register source, Register pos, int size);

  void Neg_s(FPURegister fd, FPURegister fs);
  void Neg_d(FPURegister fd, FPURegister fs);

  // Change endianness
  void ByteSwap(Register dest, Register src, int operand_size,
                Register scratch = no_reg);

  // helper function for bytes reverse
  template <int NBYTES>
  void ReverseBytesHelper(Register rd, Register rs, Register tmp1,
                          Register tmp2);

  void Clear_if_nan_d(Register rd, FPURegister fs);
  void Clear_if_nan_s(Register rd, FPURegister fs);
  // Convert single to unsigned word.
  void Trunc_uw_s(Register rd, FPURegister fs, Register result = no_reg);

  // helper functions for unaligned load/store
  template <int NBYTES, bool IS_SIGNED>
  void UnalignedLoadHelper(Register rd, const MemOperand& rs);
  template <int NBYTES>
  void UnalignedStoreHelper(Register rd, const MemOperand& rs,
                            Register scratch_other = no_reg);

  template <int NBYTES>
  void UnalignedFLoadHelper(FPURegister frd, const MemOperand& rs);
  template <int NBYTES>
  void UnalignedFStoreHelper(FPURegister frd, const MemOperand& rs);
#if V8_TARGET_ARCH_RISCV32
  void UnalignedDoubleHelper(FPURegister frd, const MemOperand& rs);
  void UnalignedDStoreHelper(FPURegister frd, const MemOperand& rs);
#endif

  template <typename Reg_T, typename Func>
  void AlignedLoadHelper(Reg_T target, const MemOperand& rs, Func generator);
  template <typename Reg_T, typename Func>
  void AlignedStoreHelper(Reg_T value, const MemOperand& rs, Func generator);

  template <int NBYTES, bool LOAD_SIGNED>
  void LoadNBytes(Register rd, const MemOperand& rs, Register scratch);
  template <int NBYTES, bool LOAD_SIGNED>
  void LoadNBytesOverwritingBaseReg(const MemOperand& rs, Register scratch0,
                                    Register scratch1);
  // load/store macros
  void Ulh(Register rd, const MemOperand& rs);
  void Ulhu(Register rd, const MemOperand& rs);
  void Ush(Register rd, const MemOperand& rs);

  void Ulw(Register rd, const MemOperand& rs);
  void Usw(Register rd, const MemOperand& rs);

  void Uld(Register rd, const MemOperand& rs);
  void Usd(Register rd, const MemOperand& rs);

  void ULoadFloat(FPURegister fd, const MemOperand& rs);
  void UStoreFloat(FPURegister fd, const MemOperand& rs);

  void ULoadDouble(FPURegister fd, const MemOperand& rs);
  void UStoreDouble(FPURegister fd, const MemOperand& rs);

  using Trapper = std::function<void(int)>;

  void Lb(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Lbu(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Sb(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});

  void Lh(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Lhu(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Sh(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});

  void Lw(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Sw(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});

#if V8_TARGET_ARCH_RISCV64
  void Ulwu(Register rd, const MemOperand& rs);
  void Lwu(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Ld(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Sd(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Lld(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Scd(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});

  inline void Load32U(Register rd, const MemOperand& rs,
                      Trapper&& trapper = [](int){}) {
    Lwu(rd, rs, std::forward<Trapper>(trapper));
  }
  inline void LoadWord(Register rd, const MemOperand& rs,
                       Trapper&& trapper = [](int){}) {
    Ld(rd, rs, std::forward<Trapper>(trapper));
  }
  inline void StoreWord(Register rd, const MemOperand& rs,
                        Trapper&& trapper = [](int){}) {
    Sd(rd, rs, std::forward<Trapper>(trapper));
  }
#elif V8_TARGET_ARCH_RISCV32
  inline void Load32U(
      Register rd, const MemOperand& rs, Trapper&& trapper = [](int){}) {
    Lw(rd, rs, std::forward<Trapper>(trapper));
  }
  inline void LoadWord(
      Register rd, const MemOperand& rs, Trapper&& trapper = [](int){}) {
    Lw(rd, rs, std::forward<Trapper>(trapper));
  }
  inline void StoreWord(
      Register rd, const MemOperand& rs, Trapper&& trapper = [](int){}) {
    Sw(rd, rs, std::forward<Trapper>(trapper));
  }
#endif
  void LoadFloat(
      FPURegister fd, const MemOperand& src, Trapper&& trapper = [](int){});
  void StoreFloat(
      FPURegister fs, const MemOperand& dst, Trapper&& trapper = [](int){});

  void LoadDouble(
      FPURegister fd, const MemOperand& src, Trapper&& trapper = [](int){});
  void StoreDouble(
      FPURegister fs, const MemOperand& dst, Trapper&& trapper = [](int){});

  void Ll(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});
  void Sc(Register rd, const MemOperand& rs, Trapper&& trapper = [](int){});

  void Float32Max(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float32Min(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float64Max(FPURegister dst, FPURegister src1, FPURegister src2);
  void Float64Min(FPURegister dst, FPURegister src1, FPURegister src2);
  template <typename F>
  void FloatMinMaxHelper(FPURegister dst, FPURegister src1, FPURegister src2,
                         MaxMinKind kind);

  bool IsDoubleZeroRegSet() { return has_double_zero_reg_set_; }
  bool IsSingleZeroRegSet() { return has_single_zero_reg_set_; }

  inline void MoveIfZero(Register rd, Register rj, Register rk) {
    CHECK(CpuFeatures::IsSupported(ZICOND));
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    czero_nez(scratch, rj, rk);
    czero_eqz(rd, rd, rk);
    or_(rd, rd, scratch);
  }

  inline void Move(Register dst, Tagged<Smi> smi) { li(dst, Operand(smi)); }

  inline void Move(Register dst, Register src) {
    if (dst != src) {
      mv(dst, src);
    }
  }

  inline void MoveDouble(FPURegister dst, FPURegister src) {
    if (dst != src) fmv_d(dst, src);
  }

  inline void MoveFloat(FPURegister dst, FPURegister src) {
    if (dst != src) fmv_s(dst, src);
  }

  inline void Move(FPURegister dst, FPURegister src) { MoveDouble(dst, src); }

#if V8_TARGET_ARCH_RISCV64
  inline void Move(Register dst_low, Register dst_high, FPURegister src) {
    fmv_x_d(dst_high, src);
    fmv_x_w(dst_low, src);
    Srl64(dst_high, dst_high, 32);
  }

  inline void Move(Register dst, FPURegister src) { fmv_x_d(dst, src); }

  inline void Move(FPURegister dst, Register src) { fmv_d_x(dst, src); }
#elif V8_TARGET_ARCH_RISCV32
  inline void Move(Register dst, FPURegister src) { fmv_x_w(dst, src); }

  inline void Move(FPURegister dst, Register src) { fmv_w_x(dst, src); }
#endif

  // Extract sign-extended word from high-half of FPR to GPR
  inline void ExtractHighWordFromF64(Register dst_high, FPURegister src) {
#if V8_TARGET_ARCH_RISCV64
    fmv_x_d(dst_high, src);
    Sra64(dst_high, dst_high, 32);
#elif V8_TARGET_ARCH_RISCV32
    // todo(riscv32): delete storedouble
    AddWord(sp, sp, Operand(-8));
    StoreDouble(src, MemOperand(sp, 0));
    Lw(dst_high, MemOperand(sp, 4));
    AddWord(sp, sp, Operand(8));
#endif
  }

  // Insert low-word from GPR (src_high) to the high-half of FPR (dst)
  void InsertHighWordF64(FPURegister dst, Register src_high);

  // Extract sign-extended word from low-half of FPR to GPR
  inline void ExtractLowWordFromF64(Register dst_low, FPURegister src) {
    fmv_x_w(dst_low, src);
  }

  // Insert low-word from GPR (src_high) to the low-half of FPR (dst)
  void InsertLowWordF64(FPURegister dst, Register src_low);

  void LoadFPRImmediate(FPURegister dst, float imm) {
    LoadFPRImmediate(dst, base::bit_cast<uint32_t>(imm));
  }
  void LoadFPRImmediate(FPURegister dst, double imm) {
    LoadFPRImmediate(dst, base::bit_cast<uint64_t>(imm));
  }
  void LoadFPRImmediate(FPURegister dst, uint32_t src);
  void LoadFPRImmediate(FPURegister dst, uint64_t src);
  // AddOverflowWord sets overflow register to a negative value if
  // overflow occurred, otherwise it is zero or positive
  void AddOverflowWord(Register dst, Register left, const Operand& right,
                       Register overflow);
  // SubOverflowWord sets overflow register to a negative value if
  // overflow occurred, otherwise it is zero or positive
  void SubOverflowWord(Register dst, Register left, const Operand& right,
                       Register overflow);
#if V8_TARGET_ARCH_RISCV64
  // AddOverflow32 sets overflow register to a negative value if
  // overflow occurred, otherwise it is zero or positive
  void AddOverflow32(Register dst, Register left, const Operand& right,
                     Register overflow);
  // SubOverflow32 sets overflow register to a negative value if
  // overflow occurred, otherwise it is zero or positive
  void SubOverflow32(Register dst, Register left, const Operand& right,
                     Register overflow);
  // MIPS-style 32-bit unsigned mulh
  void Mulhu32(Register dst, Register left, const Operand& right,
               Register left_zero, Register right_zero);
#elif V8_TARGET_ARCH_RISCV32
  // MIPS-style 32-bit unsigned mulh
  void Mulhu(Register dst, Register left, const Operand& right,
             Register left_zero, Register right_zero);
#endif
  // MulOverflow32 sets overflow register to zero if no overflow occurred
  void MulOverflow32(Register dst, Register left, const Operand& right,
                     Register overflow, bool sign_extend_inputs = true);
  // MulOverflow64 sets overflow register to zero if no overflow occurred
  void MulOverflow64(Register dst, Register left, const Operand& right,
                     Register overflow);
  // Number of instructions needed for calculation of switch table entry address
  static const int kSwitchTablePrologueSize = 6;

  // GetLabelFunction must be lambda '[](size_t index) -> Label*' or a
  // functor/function with 'Label *func(size_t index)' declaration.
  template <typename Func>
  void GenerateSwitchTable(Register index, size_t case_count,
                           Func GetLabelFunction, int case_value_base = 0,
                           Register scratch = no_reg);

  // Load an object from the root table.
  void LoadRoot(Register destination, RootIndex index) final;
  void LoadTaggedRoot(Register destination, RootIndex index);
  void LoadCompressedTaggedRoot(Register destination, RootIndex index);

  void LoadMap(Register destination, Register object);
  void LoadCompressedMap(Register dst, Register object);

  void LoadFeedbackVector(Register dst, Register closure, Register scratch,
                          Label* fbv_undef);

  void LoadInterpreterDataBytecodeArray(Register destination,
                                        Register interpreter_data);
  void LoadInterpreterDataInterpreterTrampoline(Register destination,
                                                Register interpreter_data);

  // If the value is a NaN, canonicalize the value else, do nothing.
  void FPUCanonicalizeNaN(const DoubleRegister dst, const DoubleRegister src);

  // ---------------------------------------------------------------------------
  // FPU macros. These do not handle special cases like NaN or +- inf.

  // Convert unsigned word to double.
  void Cvt_d_uw(FPURegister fd, Register rs);

  // convert signed word to double.
  void Cvt_d_w(FPURegister fd, Register rs);

  // Convert unsigned long to double.
  void Cvt_d_ul(FPURegister fd, Register rs);

  // Convert unsigned word to float.
  void Cvt_s_uw(FPURegister fd, Register rs);

  // convert signed word to float.
  void Cvt_s_w(FPURegister fd, Register rs);

  // Convert unsigned long to float.
  void Cvt_s_ul(FPURegister fd, Register rs);

  // Convert double to unsigned word.
  void Trunc_uw_d(Register rd, FPURegister fs, Register result = no_reg);

  // Convert double to signed word.
  void Trunc_w_d(Register rd, FPURegister fs, Register result = no_reg);

  // Convert single to signed word.
  void Trunc_w_s(Register rd, FPURegister fs, Register result = no_reg);
#if V8_TARGET_ARCH_RISCV64
  // Convert double to unsigned long.
  void Trunc_ul_d(Register rd, FPURegister fs, Register result = no_reg);

  // Convert singled to signed long.
  void Trunc_l_d(Register rd, FPURegister fs, Register result = no_reg);

  // Convert single to unsigned long.
  void Trunc_ul_s(Register rd, FPURegister fs, Register result = no_reg);

  // Convert singled to signed long.
  void Trunc_l_s(Register rd, FPURegister fs, Register result = no_reg);

  // Round double functions
  void Trunc_d_d(FPURegister fd, FPURegister fs, FPURegister fpu_scratch);
  void Round_d_d(FPURegister fd, FPURegister fs, FPURegister fpu_scratch);
  void Floor_d_d(FPURegister fd, FPURegister fs, FPURegister fpu_scratch);
  void Ceil_d_d(FPURegister fd, FPURegister fs, FPURegister fpu_scratch);
#endif
  // Round single to signed word.
  void Round_w_s(Register rd, FPURegister fs, Register result = no_reg);

  // Round double to signed word.
  void Round_w_d(Register rd, FPURegister fs, Register result = no_reg);

  // Ceil single to signed word.
  void Ceil_w_s(Register rd, FPURegister fs, Register result = no_reg);

  // Ceil double to signed word.
  void Ceil_w_d(Register rd, FPURegister fs, Register result = no_reg);

  // Floor single to signed word.
  void Floor_w_s(Register rd, FPURegister fs, Register result = no_reg);

  // Floor double to signed word.
  void Floor_w_d(Register rd, FPURegister fs, Register result = no_reg);

  // Round float functions
  void Trunc_s_s(FPURegister fd, FPURegister fs, FPURegister fpu_scratch);
  void Round_s_s(FPURegister fd, FPURegister fs, FPURegister fpu_scratch);
  void Floor_s_s(FPURegister fd, FPURegister fs, FPURegister fpu_scratch);
  void Ceil_s_s(FPURegister fd, FPURegister fs, FPURegister fpu_scratch);

  void Ceil_f(VRegister dst, VRegister src, Register scratch,
              VRegister v_scratch);

  void Ceil_d(VRegister dst, VRegister src, Register scratch,
              VRegister v_scratch);

  void Floor_f(VRegister dst, VRegister src, Register scratch,
               VRegister v_scratch);
  void Floor_d(VRegister dst, VRegister src, Register scratch,
               VRegister v_scratch);
  void Trunc_f(VRegister dst, VRegister src, Register scratch,
               VRegister v_scratch);
  void Trunc_d(VRegister dst, VRegister src, Register scratch,
               VRegister v_scratch);
  void Round_f(VRegister dst, VRegister src, Register scratch,
               VRegister v_scratch);
  void Round_d(VRegister dst, VRegister src, Register scratch,
               VRegister v_scratch);

  void FaddS(FPURegister dst, FPURegister lhs, FPURegister rhs);
  // -------------------------------------------------------------------------
  // Smi utilities.

  void SmiTag(Register dst, Register src) {
    static_assert(kSmiTag == 0);
#if V8_TARGET_ARCH_RISCV64
    if (SmiValuesAre32Bits()) {
      // Smi goes to upper 32
      Sll64(dst, src, 32);
    } else {
      DCHECK(SmiValuesAre31Bits());
      // Smi is shifted left by 1
      Add32(dst, src, src);
    }
#elif V8_TARGET_ARCH_RISCV32

    DCHECK(SmiValuesAre31Bits());
    // Smi is shifted left by 1
    Sll32(dst, src, kSmiShift);
#endif
  }

  void SmiTag(Register reg) { SmiTag(reg, reg); }

  // Jump the register contains a smi.
  void JumpIfSmi(Register value, Label* smi_label,
                 Label::Distance distance = Label::kFar);

  // AssembleArchBinarySearchSwitchRange Use JumpIfEqual and JumpIfLessThan.
  // In V8_COMPRESS_POINTERS, the compare is done with the lower 32 bits of the
  // input.
  void JumpIfEqual(Register a, int32_t b, Label* dest) {
#ifdef V8_COMPRESS_POINTERS
    SignExtendWord(a, a);
#endif
    Branch(dest, eq, a, Operand(b));
  }

  void JumpIfLessThan(Register a, int32_t b, Label* dest) {
#ifdef V8_COMPRESS_POINTERS
    SignExtendWord(a, a);
#endif
    Branch(dest, lt, a, Operand(b));
  }

  void JumpIfUnsignedLessThan(Register x, int32_t y, Label* dest) {
    AssertZeroExtended(x);
    Branch(dest, ult, x, Operand(y));
  }

  void JumpIfMarking(Label* is_marking,
                     Label::Distance condition_met_distance = Label::kFar);
  void JumpIfNotMarking(Label* not_marking,
                        Label::Distance condition_met_distance = Label::kFar);

  // Push a standard frame, consisting of ra, fp, context and JS function.
  void PushStandardFrame(Register function_reg);

  // Get the actual activation frame alignment for target environment.
  static int ActivationFrameAlignment();

  // Calculated scaled address (rd) as rt + rs << sa
  void CalcScaledAddress(Register rd, Register rt, Register rs, uint8_t sa);

  // Compute the start of the generated instruction stream from the current PC.
  // This is an alternative to embedding the {CodeObject} handle as a reference.
  void ComputeCodeStartAddress(Register dst);

  // Load a trusted pointer field.
  // When the sandbox is enabled, these are indirect pointers using the trusted
  // pointer table. Otherwise they are regular tagged fields.
  void LoadTrustedPointerField(Register destination, MemOperand field_operand,
                               IndirectPointerTag tag);
  // Store a trusted pointer field.
  void StoreTrustedPointerField(Register value, MemOperand dst_field_operand);
  // Load a code pointer field.
  // These are special versions of trusted pointers that, when the sandbox is
  // enabled, reference code objects through the code pointer table.
  void LoadCodePointerField(Register destination, MemOperand field_operand) {
    LoadTrustedPointerField(destination, field_operand,
                            kCodeIndirectPointerTag);
  }
  // Store a code pointer field.
  void StoreCodePointerField(Register value, MemOperand dst_field_operand) {
    StoreTrustedPointerField(value, dst_field_operand);
  }

  // Loads a field containing an off-heap ("external") pointer and does
  // necessary decoding if sandbox is enabled.
  void LoadExternalPointerField(Register destination, MemOperand field_operand,
                                ExternalPointerTagRange tag_range,
                                Register isolate_root = no_reg);

#if V8_TARGET_ARCH_RISCV64
  // ---------------------------------------------------------------------------
  // Pointer compression Support

  // Loads a field containing any tagged value and decompresses it if necessary.
  void LoadTaggedField(const Register& destination,
                       const MemOperand& field_operand,
                       Trapper&& trapper = [](int){});

  // Loads a field containing any tagged value but never decompresses it.
  void LoadTaggedFieldWithoutDecompressing(const Register& destination,
                                           const MemOperand& field_operand);

  // Loads a field containing a tagged signed value and decompresses it if
  // necessary.
  void LoadTaggedSignedField(const Register& destination,
                             const MemOperand& field_operand);

  // Loads a field containing smi value and untags it.
  void SmiUntagField(Register dst, const MemOperand& src);

  // Compresses and stores tagged value to given on-heap location.
  void StoreTaggedField(const Register& value,
                        const MemOperand& dst_field_operand,
                        Trapper&& trapper = [](int){});
  void AtomicStoreTaggedField(Register dst, const MemOperand& src,
                              Trapper&& trapper = [](int){});

  void DecompressTaggedSigned(const Register& destination,
                              const MemOperand& field_operand,
                              Trapper&& trapper = [](int){});
  void DecompressTagged(const Register& destination,
                        const MemOperand& field_operand,
                        Trapper&& trapper = [](int){});
  void DecompressTagged(const Register& destination, const Register& source);
  void DecompressTagged(Register dst, Tagged_t immediate);
  void DecompressProtected(const Register& destination,
                           const MemOperand& field_operand,
                           Trapper&& trapper = [](int){});

  // ---------------------------------------------------------------------------
  // V8 Sandbox support

  // Transform a SandboxedPointer from/to its encoded form, which is used when
  // the pointer is stored on the heap and ensures that the pointer will always
  // point into the sandbox.
  void DecodeSandboxedPointer(Register value);
  void LoadSandboxedPointerField(Register destination,
                                 const MemOperand& field_operand,
                                 Trapper&& trapper = [](int){});
  void StoreSandboxedPointerField(Register value,
                                  const MemOperand& dst_field_operand,
                                  Trapper&& trapper = [](int){});

  // Loads an indirect pointer field.
  // Only available when the sandbox is enabled, but always visible to avoid
  // having to place the #ifdefs into the caller.
  void LoadIndirectPointerField(Register destination, MemOperand field_operand,
                                IndirectPointerTag tag);
  // Store an indirect pointer field.
  // Only available when the sandbox is enabled, but always visible to avoid
  // having to place the #ifdefs into the caller.
  void StoreIndirectPointerField(Register value, MemOperand dst_field_operand,
                                 Trapper&& trapper = [](int){});

#ifdef V8_ENABLE_SANDBOX
  // Retrieve the heap object referenced by the given indirect pointer handle,
  // which can either be a trusted pointer handle or a code pointer handle.
  void ResolveIndirectPointerHandle(Register destination, Register handle,
                                    IndirectPointerTag tag);

  // Retrieve the heap object referenced by the given trusted pointer handle.
  void ResolveTrustedPointerHandle(Register destination, Register handle,
                                   IndirectPointerTag tag);
  // Retrieve the Code object referenced by the given code pointer handle.
  void ResolveCodePointerHandle(Register destination, Register handle);

  // Load the pointer to a Code's entrypoint via a code pointer.
  // Only available when the sandbox is enabled as it requires the code pointer
  // table.
  void LoadCodeEntrypointViaCodePointer(Register destination,
                                        MemOperand field_operand,
                                        CodeEntrypointTag tag);

  // Load the value of Code pointer table corresponding to
  // IsolateGroup::current()->code_pointer_table_.
  // Only available when the sandbox is enabled.
  void LoadCodePointerTableBase(Register destination);
#endif

  void AtomicDecompressTaggedSigned(Register dst, const MemOperand& src,
                                    Trapper&& trapper = [](int){});
  void AtomicDecompressTagged(Register dst, const MemOperand& src,
                              Trapper&& trapper = [](int){});

  void CmpTagged(const Register& rd, const Register& rs1, const Register& rs2) {
    if (COMPRESS_POINTERS_BOOL) {
      Sub32(rd, rs1, rs2);
    } else {
      SubWord(rd, rs1, rs2);
    }
  }

#elif V8_TARGET_ARCH_RISCV32
  // ---------------------------------------------------------------------------
  // Pointer compression Support
  // rv32 don't support Pointer compression. Defines these functions for
  // simplify builtins.
  inline void LoadTaggedField(const Register& destination,
                              const MemOperand& field_operand,
                              Trapper&& trapper = [](int){}) {
    Lw(destination, field_operand, std::forward<Trapper>(trapper));
  }

  inline void LoadTaggedSignedField(const Register& destination,
                                    const MemOperand& field_operand) {
    Lw(destination, field_operand);
  }

  inline void SmiUntagField(Register dst, const MemOperand& src) {
    SmiUntag(dst, src);
  }

  // Compresses and stores tagged value to given on-heap location.
  void StoreTaggedField(
      const Register& value, const MemOperand& dst_field_operand,
      Trapper&& trapper = [](int){}) {
    Sw(value, dst_field_operand, std::forward<Trapper>(trapper));
  }

  void AtomicStoreTaggedField(
      Register src, const MemOperand& dst, Trapper&& trapper = [](int) {}) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    AddWord(scratch, dst.rm(), dst.offset());
    trapper(pc_offset());
    amoswap_w(true, true, zero_reg, scratch, src);
  }
#endif

  // NB: Control-flow integrity (V8_ENABLE_CONTROL_FLOW_INTEGRITY)
  // is not supported for RISC-V.
  //
  // Define a function entrypoint.
  void CodeEntry() {}
  // Define an exception handler.
  void ExceptionHandler() { JumpTarget(); }
  void JumpTarget() {}
  void BindJumpTarget(Label* label) { bind(label); }
  void CallTarget() {}
  void JumpOrCallTarget() {}
  void BindCallTarget(Label* label) { bind(label); }
  void BindJumpOrCallTarget(Label* label) { bind(label); }
  // Define an exception handler and bind a label.
  void BindExceptionHandler(Label* label) { bind(label); }

  // Wasm into RVV
  void WasmRvvExtractLane(Register dst, VRegister src, int8_t idx, VSew sew,
                          Vlmul lmul) {
    VU.set(kScratchReg, sew, lmul);
    VRegister Vsrc = idx != 0 ? kSimd128ScratchReg : src;
    if (idx != 0) {
      vslidedown_vi(kSimd128ScratchReg, src, idx);
    }
    vmv_xs(dst, Vsrc);
  }

  void WasmRvvEq(VRegister dst, VRegister lhs, VRegister rhs, VSew sew,
                 Vlmul lmul);
  void WasmRvvNe(VRegister dst, VRegister lhs, VRegister rhs, VSew sew,
                 Vlmul lmul);
  void WasmRvvGeS(VRegister dst, VRegister lhs, VRegister rhs, VSew sew,
                  Vlmul lmul);
  void WasmRvvGeU(VRegister dst, VRegister lhs, VRegister rhs, VSew sew,
                  Vlmul lmul);
  void WasmRvvGtS(VRegister dst, VRegister lhs, VRegister rhs, VSew sew,
                  Vlmul lmul);
  void WasmRvvGtU(VRegister dst, VRegister lhs, VRegister rhs, VSew sew,
                  Vlmul lmul);

  void WasmRvvS128const(VRegister dst, const uint8_t imms[16]);

  void LoadLane(
      VSew sew, VRegister dst, uint8_t laneidx, MemOperand src,
      Trapper&& trapper = [](int) {});
  void StoreLane(
      VSew sew, VRegister src, uint8_t laneidx, MemOperand dst,
      Trapper&& trapper = [](int) {});

  // It assumes that the arguments are located below the stack pointer.
  void LoadReceiver(Register dest) { LoadWord(dest, MemOperand(sp, 0)); }
  void StoreReceiver(Register rec) { StoreWord(rec, MemOperand(sp, 0)); }

  bool IsNear(Label* L, Condition cond, int rs_reg);

  // Swap two registers.  If the scratch register is omitted then a slightly
  // less efficient form using xor instead of mov is emitted.
  void Swap(Register reg1, Register reg2, Register scratch = no_reg);

  void PushRoot(RootIndex index) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    LoadRoot(scratch, index);
    Push(scratch);
  }

  // Compare the object in a register to a value from the root list.
  void CompareRootAndBranch(const Register& obj, RootIndex index, Condition cc,
                            Label* target,
                            ComparisonMode mode = ComparisonMode::kDefault);
  void CompareTaggedRootAndBranch(const Register& with, RootIndex index,
                                  Condition cc, Label* target);
  // Compare the object in a register to a value and jump if they are equal.
  void JumpIfRoot(Register with, RootIndex index, Label* if_equal,
                  Label::Distance distance = Label::kFar) {
    Branch(if_equal, eq, with, index, distance);
  }

  // Compare the object in a register to a value from the root list.
  void CompareRoot(const Register& obj, RootIndex index, const Register& result,
                   ComparisonMode mode = ComparisonMode::kDefault);
  void CompareTaggedRoot(const Register& with, RootIndex index,
                         const Register& result);

  // Compare the object in a register to a value and jump if they are not equal.
  void JumpIfNotRoot(Register with, RootIndex index, Label* if_not_equal,
                     Label::Distance distance = Label::kFar) {
    Branch(if_not_equal, ne, with, index, distance);
  }
#if V8_STATIC_ROOTS_BOOL
  // Fast variant which is guaranteed to not actually load the instance type
  // from the map.
  void BranchObjectTypeFast(Label* target, Condition cc, Register heap_object,
                            Register compressed_map_scratch, InstanceType type);
  void BranchInstanceTypeWithUniqueCompressedMap(Label* target, Condition cc,
                                                 Register map, Register scratch,
                                                 InstanceType type);
#endif  // V8_STATIC_ROOTS_BOOL
  // Checks if value is in range [lower_limit, higher_limit] using a single
  // comparison.
  void JumpIfIsInRange(Register value, unsigned lower_limit,
                       unsigned higher_limit, Label* on_in_range);
  void JumpIfObjectType(Label* target, Condition cc, Register object,
                        InstanceType instance_type, Register scratch = no_reg);

  // Fast check if the object is a js receiver type. Assumes only primitive
  // objects or js receivers are passed.
  void JumpIfJSAnyIsNotPrimitive(
      Register heap_object, Register scratch, Label* target,
      Label::Distance distance = Label::kFar,
      Condition condition = Condition::kUnsignedGreaterThanEqual);
  void JumpIfJSAnyIsPrimitive(Register heap_object, Register scratch,
                              Label* target,
                              Label::Distance distance = Label::kFar) {
    return JumpIfJSAnyIsNotPrimitive(heap_object, scratch, target, distance,
                                     Condition::kUnsignedLessThan);
  }
  // ---------------------------------------------------------------------------
  // GC Support

  // Notify the garbage collector that we wrote a pointer into an object.
  // |object| is the object being stored into, |value| is the object being
  // stored.  value and scratch registers are clobbered by the operation.
  // The offset is the offset from the start of the object, not the offset from
  // the tagged HeapObject pointer.  For use with FieldOperand(reg, off).
  void RecordWriteField(
      Register object, int offset, Register value, RAStatus ra_status,
      SaveFPRegsMode save_fp, SmiCheck smi_check = SmiCheck::kInline,
      ReadOnlyCheck ro_check = ReadOnlyCheck::kInline,
      SlotDescriptor slot = SlotDescriptor::ForDirectPointerSlot());

  // For a given |object| notify the garbage collector that the slot |address|
  // has been written.  |value| is the object being stored. The value and
  // address registers are clobbered by the operation.
  void RecordWrite(
      Register object, Operand offset, Register value, RAStatus ra_status,
      SaveFPRegsMode save_fp, SmiCheck smi_check = SmiCheck::kInline,
      ReadOnlyCheck ro_check = ReadOnlyCheck::kInline,
      SlotDescriptor slot = SlotDescriptor::ForDirectPointerSlot());

  // void Pref(int32_t hint, const MemOperand& rs);

  // ---------------------------------------------------------------------------
  // Pseudo-instructions.

  void LoadWordPair(Register rd, const MemOperand& rs);
  void StoreWordPair(Register rd, const MemOperand& rs);

  void Madd_s(FPURegister fd, FPURegister fr, FPURegister fs, FPURegister ft);
  void Madd_d(FPURegister fd, FPURegister fr, FPURegister fs, FPURegister ft);
  void Msub_s(FPURegister fd, FPURegister fr, FPURegister fs, FPURegister ft);
  void Msub_d(FPURegister fd, FPURegister fr, FPURegister fs, FPURegister ft);

  // stack_space - extra stack space.
  void EnterExitFrame(Register scratch, int stack_space,
                      StackFrame::Type frame_type);
  // Leave the current exit frame.
  void LeaveExitFrame(Register scratch);

  // Make sure the stack is aligned. Only emits code in debug mode.
  void AssertStackIsAligned();

  // Load the global proxy from the current context.
  void LoadGlobalProxy(Register dst) {
    LoadNativeContextSlot(dst, Context::GLOBAL_PROXY_INDEX);
  }

  void LoadNativeContextSlot(Register dst, int index);

  // Falls through and sets scratch_and_result to 0 on failure, jumps to
  // on_result on success.
  void TryLoadOptimizedOsrCode(Register scratch_and_result,
                               CodeKind min_opt_level, Register feedback_vector,
                               FeedbackSlot slot, Label* on_result,
                               Label::Distance distance);

  // Load the initial map from the global function. The registers
  // function and map can be the same, function is then overwritten.
  void LoadGlobalFunctionInitialMap(Register function, Register map,
                                    Register scratch);

  // -------------------------------------------------------------------------
  // JavaScript invokes.
  // On function call, call into the debugger.
  void CallDebugOnFunctionCall(
      Register fun, Register new_target,
      Register expected_parameter_count_or_dispatch_handle,
      Register actual_parameter_count);

  // The way we invoke JSFunctions differs depending on whether leaptiering is
  // enabled. As such, these functions exist in two variants. In the future,
  // leaptiering will be used on all platforms. At that point, the
  // non-leaptiering variants will disappear.

#if defined(V8_ENABLE_LEAPTIERING) && defined(V8_TARGET_ARCH_RISCV64)
  // Invoke the JavaScript function in the given register. Changes the
  // current context to the context in the function before invoking.
  void InvokeFunction(Register function, Register actual_parameter_count,
                      InvokeType type,
                      ArgumentAdaptionMode argument_adaption_mode =
                          ArgumentAdaptionMode::kAdapt);
  // Invoke the JavaScript function in the given register.
  // Changes the current context to the context in the function before invoking.
  void InvokeFunctionWithNewTarget(Register function, Register new_target,
                                   Register actual_parameter_count,
                                   InvokeType type);
  // Invoke the JavaScript function code by either calling or jumping.
  void InvokeFunctionCode(Register function, Register new_target,
                          Register actual_parameter_count, InvokeType type,
                          ArgumentAdaptionMode argument_adaption_mode =
                              ArgumentAdaptionMode::kAdapt);
#else
  void InvokeFunction(Register function, Register expected_parameter_count,
                      Register actual_parameter_count, InvokeType type);
  // Invoke the JavaScript function in the given register. Changes the
  // current context to the context in the function before invoking.
  void InvokeFunctionWithNewTarget(Register function, Register new_target,
                                   Register actual_parameter_count,
                                   InvokeType type);
  // Invoke the JavaScript function code by either calling or jumping.
  void InvokeFunctionCode(Register function, Register new_target,
                          Register expected_parameter_count,
                          Register actual_parameter_count, InvokeType type);
#endif

  // On function call, call into the debugger if necessary.
  void CheckDebugHook(Register fun, Register new_target,
                      Register expected_parameter_count,
                      Register actual_parameter_count);
  // ---- InstructionStream generation helpers ----

  // ---------------------------------------------------------------------------
  // Support functions.

  // Compare object type for heap object.  heap_object contains a non-Smi
  // whose object type should be compared with the given type.  This both
  // sets the flags and leaves the object type in the type_reg register.
  // It leaves the map in the map register (unless the type_reg and map register
  // are the same register).  It leaves the heap object in the heap_object
  // register unless the heap_object register is the same register as one of the
  // other registers.
  void CompareObjectTypeAndJump(Register heap_object, Register map,
                                Register type_reg, InstanceType type,
                                Condition cond, Label* target,
                                Label::Distance distance);
  // Variant of the above, which only guarantees to set the correct eq/ne flag.
  // Neither map, nor type_reg might be set to any particular value.
  void IsObjectType(Register heap_object, Register scratch1, Register scratch2,
                    InstanceType type);

  // Exception handling.

  // Push a new stack handler and link into stack handler chain.
  void PushStackHandler();

  // Unlink the stack handler on top of the stack from the stack handler chain.
  // Must preserve the result register.
  void PopStackHandler();

  // Tiering support.
  void AssertFeedbackCell(Register object,
                          Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void AssertFeedbackVector(Register object,
                            Register scratch) NOOP_UNLESS_DEBUG_CODE;
  void ReplaceClosureCodeWithOptimizedCode(Register optimized_code,
                                           Register closure);
  void GenerateTailCallToReturnedCode(Runtime::FunctionId function_id);

#ifndef V8_ENABLE_LEAPTIERING
  void LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      Register flags, Register feedback_vector, CodeKind current_code_kind,
      Label* flags_need_processing);
  void OptimizeCodeOrTailCallOptimizedCodeSlot(Register flags,
                                               Register feedback_vector);
#endif

  // -------------------------------------------------------------------------
  // Support functions.

  void GetObjectType(Register function, Register map, Register type_reg);

  void GetInstanceTypeRange(Register map, Register type_reg,
                            InstanceType lower_limit, Register range);

  // -------------------------------------------------------------------------
  // Runtime calls.

  // Call a runtime routine.
  void CallRuntime(const Runtime::Function* f, int num_arguments);

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid) {
    const Runtime::Function* function = Runtime::FunctionForId(fid);
    CallRuntime(function, function->nargs);
  }

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId fid, int num_arguments) {
    CallRuntime(Runtime::FunctionForId(fid), num_arguments);
  }

  // Convenience function: tail call a runtime routine (jump).
  void TailCallRuntime(Runtime::FunctionId fid);

  // Jump to the builtin routine.
  void JumpToExternalReference(const ExternalReference& builtin,
                               bool builtin_exit_frame = false);
  // ---------------------------------------------------------------------------
  // In-place weak references.
  void LoadWeakValue(Register out, Register in, Label* target_if_cleared);

  // -------------------------------------------------------------------------
  // StatsCounter support.

  void IncrementCounter(StatsCounter* counter, int value, Register scratch1,
                        Register scratch2) {
    if (!v8_flags.native_code_counters) return;
    EmitIncrementCounter(counter, value, scratch1, scratch2);
  }
  void EmitIncrementCounter(StatsCounter* counter, int value, Register scratch1,
                            Register scratch2);
  void DecrementCounter(StatsCounter* counter, int value, Register scratch1,
                        Register scratch2) {
    if (!v8_flags.native_code_counters) return;
    EmitDecrementCounter(counter, value, scratch1, scratch2);
  }
  void EmitDecrementCounter(StatsCounter* counter, int value, Register scratch1,
                            Register scratch2);

  // -------------------------------------------------------------------------
  // Stack limit utilities
  void LoadStackLimit(Register destination, StackLimitKind kind);
  void StackOverflowCheck(Register num_args, Register scratch1,
                          Register scratch2, Label* stack_overflow,
                          Label* done = nullptr);

  // Left-shifted from int32 equivalent of Smi.
  void SmiScale(Register dst, Register src, int scale) {
#if V8_TARGET_ARCH_RISCV64
    if (SmiValuesAre32Bits()) {
      // The int portion is upper 32-bits of 64-bit word.
      Sra64(dst, src, (kSmiShift - scale) & 0x3F);
    } else {
      DCHECK(SmiValuesAre31Bits());
      DCHECK_GE(scale, kSmiTagSize);
      Sra32(dst, src, scale - kSmiTagSize);
    }
#elif V8_TARGET_ARCH_RISCV32
    DCHECK(SmiValuesAre31Bits());
    DCHECK_GE(scale, kSmiTagSize);
    Sll32(dst, src, scale - kSmiTagSize);
#endif
  }

  // Test if the register contains a smi.
  inline void SmiTst(Register value, Register scratch) {
    And(scratch, value, Operand(kSmiTagMask));
  }

  enum ArgumentsCountMode { kCountIncludesReceiver, kCountExcludesReceiver };
  enum ArgumentsCountType { kCountIsInteger, kCountIsSmi };
  void DropArguments(Register count);
  void DropArgumentsAndPushNewReceiver(Register argc, Register receiver);

  void JumpIfCodeIsMarkedForDeoptimization(Register code, Register scratch,
                                           Label* if_marked_for_deoptimization);
  Operand ClearedValue() const;

  // Jump if the register contains a non-smi.
  void JumpIfNotSmi(Register value, Label* not_smi_label,
                    Label::Distance dist = Label::kFar);
  // Abort execution if argument is not a Constructor, enabled via --debug-code.
  void AssertConstructor(Register object);

  // Abort execution if argument is not a JSFunction, enabled via --debug-code.
  void AssertFunction(Register object);

  // Abort execution if argument is not a callable JSFunction, enabled via
  // --debug-code.
  void AssertCallableFunction(Register object);

  // Abort execution if argument is not a JSBoundFunction,
  // enabled via --debug-code.
  void AssertBoundFunction(Register object);

  // Abort execution if argument is not a JSGeneratorObject (or subclass),
  // enabled via --debug-code.
  void AssertGeneratorObject(Register object);

  // Calls Abort(msg) if the condition cond is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cond, AbortReason reason) NOOP_UNLESS_DEBUG_CODE;

  // Like Assert(), but without condition.
  // Use --debug_code to enable.
  void AssertUnreachable(AbortReason reason) NOOP_UNLESS_DEBUG_CODE;

  // Abort execution if argument is not undefined or an AllocationSite, enabled
  // via --debug-code.
  void AssertUndefinedOrAllocationSite(Register object, Register scratch);

  template <typename Field>
  void DecodeField(Register dst, Register src) {
    ExtractBits(dst, src, Field::kShift, Field::kSize);
  }

  template <typename Field>
  void DecodeField(Register reg) {
    DecodeField<Field>(reg, reg);
  }

#ifdef V8_ENABLE_LEAPTIERING
  // Load the entrypoint pointer of a JSDispatchTable entry.
  void LoadEntrypointFromJSDispatchTable(Register destination,
                                         Register dispatch_handle,
                                         Register scratch);
  void LoadEntrypointFromJSDispatchTable(Register destination,
                                         JSDispatchHandle dispatch_handle,
                                         Register scratch);
#ifdef V8_TARGET_ARCH_RISCV64
  // On 32 bit architectures only the mark bit is shared with the pointer.
  // see src/sandbox/js-dispatch-table.h
  void LoadParameterCountFromJSDispatchTable(Register destination,
                                             Register dispatch_handle,
                                             Register scratch);
  void LoadEntrypointAndParameterCountFromJSDispatchTable(
      Register entrypoint, Register parameter_count, Register dispatch_handle,
      Register scratch);
#endif  // V8_TARGET_ARCH_RISCV64
#endif  // V8_ENABLE_LEAPTIERING
  // Load a protected pointer field.
  void LoadProtectedPointerField(Register destination,
                                 MemOperand field_operand);
  // Performs a truncating conversion of a floating point number as used by
  // the JS bitwise operations. See ECMA-262 9.5: ToInt32. Goes to 'done' if it
  // succeeds, otherwise falls through if result is saturated. On return
  // 'result' either holds answer, or is clobbered on fall through.
  void TryInlineTruncateDoubleToI(Register result, DoubleRegister input,
                                  Label* done);

 protected:
  inline Register GetRtAsRegisterHelper(const Operand& rt, Register scratch);
  inline int32_t GetOffset(int32_t offset, Label* L, OffsetSize bits);

 private:
  bool has_double_zero_reg_set_ = false;
  bool has_single_zero_reg_set_ = false;

  int CallCFunctionHelper(
      Register function, int num_reg_arguments, int num_double_arguments,
      SetIsolateDataSlots set_isolate_data_slots = SetIsolateDataSlots::kYes,
      Label* return_location = nullptr);

  // TODO(RISCV) Reorder parameters so out parameters come last.
  bool CalculateOffset(Label* L, int32_t* offset, OffsetSize bits);
  bool CalculateOffset(Label* L, int32_t* offset, OffsetSize bits,
                       Register* scratch, const Operand& rt);

  void BranchShortHelper(int32_t offset, Label* L);
  bool BranchShortHelper(int32_t offset, Label* L, Condition cond, Register rs,
                         const Operand& rt);
  bool BranchShortCheck(int32_t offset, Label* L, Condition cond, Register rs,
                        const Operand& rt);

  void BranchAndLinkShortHelper(int32_t offset, Label* L);
  void BranchAndLinkShort(int32_t offset);
  void BranchAndLinkShort(Label* L);
  bool BranchAndLinkShortHelper(int32_t offset, Label* L, Condition cond,
                                Register rs, const Operand& rt);
  bool BranchAndLinkShortCheck(int32_t offset, Label* L, Condition cond,
                               Register rs, const Operand& rt);
  void BranchAndLinkLong(Label* L);
#if V8_TARGET_ARCH_RISCV64
  template <typename F_TYPE>
  void RoundHelper(FPURegister dst, FPURegister src, FPURegister fpu_scratch,
                   FPURoundingMode mode);
#elif V8_TARGET_ARCH_RISCV32
  void RoundDouble(FPURegister dst, FPURegister src, FPURegister fpu_scratch,
                   FPURoundingMode mode);

  void RoundFloat(FPURegister dst, FPURegister src, FPURegister fpu_scratch,
                  FPURoundingMode mode);
#endif
  template <typename F>
  void RoundHelper(VRegister dst, VRegister src, Register scratch,
                   VRegister v_scratch, FPURoundingMode frm,
                   bool keep_nan_same = true);

  template <typename TruncFunc>
  void RoundFloatingPointToInteger(Register rd, FPURegister fs, Register result,
                                   TruncFunc trunc);

  // Push a fixed frame, consisting of ra, fp.
  void PushCommonFrame(Register marker_reg = no_reg);

  // Helper functions for generating invokes.
  void InvokePrologue(Register expected_parameter_count,
                      Register actual_parameter_count, InvokeType type);

  // Compute memory operands for safepoint stack slots.
  static int SafepointRegisterStackIndex(int reg_code);

  // Needs access to SafepointRegisterStackIndex for compiled frame
  // traversal.
  friend class CommonFrame;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MacroAssembler);
};

template <typename Func>
void MacroAssembler::GenerateSwitchTable(Register index, size_t case_count,
                                         Func GetLabelFunction,
                                         int case_value_base,
                                         Register scratch) {
  UseScratchRegisterScope temps(this);
  Register table = scratch;
  if (table == no_reg) {
    table = temps.Acquire();
  }
  Label fallthrough, jump_table;
  if (case_value_base != 0) {
    SubWord(index, index, Operand(case_value_base));
  }
  Branch(&fallthrough, Condition::Ugreater_equal, index, Operand(case_count));
  LoadAddress(table, &jump_table);
  CalcScaledAddress(table, table, index, kSystemPointerSizeLog2);
  LoadWord(table, MemOperand(table, 0));
  Jump(table);
  // Calculate label area size and let MASM know that it will be impossible to
  // create the trampoline within the range. That forces MASM to create the
  // trampoline right here if necessary, i.e. if label area is too large and
  // all unbound forward branches cannot be bound over it.
  int aligned_label_area_size =
      static_cast<int>(case_count) * kUIntptrSize + kSystemPointerSize;
  BlockTrampolinePoolScope block_trampoline_pool(this, aligned_label_area_size);
  // Emit the jump table inline, under the assumption that it's not too big.
  Align(kSystemPointerSize);
  bind(&jump_table);
  for (size_t i = 0; i < case_count; ++i) {
    dd(GetLabelFunction(i));
  }
  bind(&fallthrough);
}

struct MoveCycleState {
  // List of scratch registers reserved for pending moves in a move cycle, and
  // which should therefore not be used as a temporary location by
  // {MoveToTempLocation}.
  RegList scratch_regs;
  // Available scratch registers during the move cycle resolution scope.
  std::optional<UseScratchRegisterScope> temps;
  // Scratch register picked by {MoveToTempLocation}.
  std::optional<Register> scratch_reg;
};

inline MemOperand ExitFrameStackSlotOperand(int offset) {
  static constexpr int kSPOffset = 1 * kSystemPointerSize;
  return MemOperand(sp, kSPOffset + offset);
}

inline MemOperand ExitFrameCallerStackSlotOperand(int index) {
  return MemOperand(fp, (ExitFrameConstants::kFixedSlotCountAboveFp + index) *
                            kSystemPointerSize);
}

// Calls an API function. Allocates HandleScope, extracts returned value
// from handle and propagates exceptions. Clobbers C argument registers
// and C caller-saved registers. Restores context. On return removes
//   (*argc_operand + slots_to_drop_on_return) * kSystemPointerSize
// (GCed, includes the call JS arguments space and the additional space
// allocated for the fast call).
void CallApiFunctionAndReturn(MacroAssembler* masm, bool with_profiling,
                              Register function_address,
                              ExternalReference thunk_ref, Register thunk_arg,
                              int slots_to_drop_on_return,
                              MemOperand* argc_operand,
                              MemOperand return_value_operand);

#define ACCESS_MASM(masm) masm->

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_RISCV_MACRO_ASSEMBLER_RISCV_H_
