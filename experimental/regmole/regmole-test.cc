// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/baseline/baseline-assembler-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/wasm/baseline/liftoff-register.h"

namespace v8 {
namespace internal {

#define __ masm->

// -----------------------------------------------------------------------------
// 1. Symbolic & Physical Register Use-After-Clobber Tests
// -----------------------------------------------------------------------------

void TestUseAfterScratchClobberBad(MacroAssembler* masm, Register obj,
                                   Register scratch) {
  __ IsObjectType(obj, JS_OBJECT_TYPE, scratch);
  // EXPECT-WARNING: Use of register 'scratch' after it was clobbered
  __ addq(obj, scratch);
}

void TestUseAfterScratchRedefinedGood(MacroAssembler* masm, Register obj,
                                      Register scratch) {
  __ IsObjectType(obj, JS_OBJECT_TYPE, scratch);
  __ movq(scratch, Immediate(42));
  __ addq(obj, scratch);
  __ IsObjectType(obj, JS_OBJECT_TYPE, scratch);
  __ xorq(scratch, scratch);
  __ addq(obj, scratch);
}

void TestUseAfterCallClobberBad(MacroAssembler* masm, Register val) {
  __ CallRuntime(Runtime::kAbort);
  // EXPECT-WARNING: Use of register 'val' after it was clobbered
  __ pushq(val);
}

void TestPushPopPreservesAcrossCallGood(MacroAssembler* masm, Register val) {
  __ pushq(val);
  __ CallRuntime(Runtime::kAbort);
  __ popq(val);
  __ incq(val);
}

void TestPhysicalRegClobberBad(MacroAssembler* masm, Register divisor,
                               Register obj) {
  __ movq(rcx, Immediate(3));
  __ IsObjectType(obj, JS_OBJECT_TYPE, rcx);
  // EXPECT-WARNING: implicitly reads register 'rcx' after it was clobbered
  __ shlq_cl(divisor);
}

// -----------------------------------------------------------------------------
// 2. Operand, ExternalReferenceAsOperand & Generalized GetMemOp Clobber Tests
// -----------------------------------------------------------------------------

void TestOperandBaseClobberedBad(MacroAssembler* masm, Register base,
                                 Register dst) {
  Operand field_op(base, 16);
  __ CallRuntime(Runtime::kAbort);
  // EXPECT-WARNING: Use of Operand 'field_op' whose base/index register 'base'
  __ movq(dst, field_op);
}

void TestExternalReferenceOperandOverwrittenBad(MacroAssembler* masm,
                                                ExternalReference ref1,
                                                ExternalReference ref2,
                                                Register dst) {
  Operand op1 = __ ExternalReferenceAsOperand(ref1);
  Operand op2 = __ ExternalReferenceAsOperand(ref2);
  __ movq(dst, op2);
  // EXPECT-WARNING: Use of Operand 'op1' after its underlying scratch register
  __ addq(dst, op1);
}

void TestExternalReferenceOperandGood(MacroAssembler* masm,
                                      ExternalReference ref1,
                                      ExternalReference ref2, Register dst) {
  Operand op1 = __ ExternalReferenceAsOperand(ref1);
  __ movq(dst, op1);
  Operand op2 = __ ExternalReferenceAsOperand(ref2);
  __ addq(dst, op2);
}

// Regression test modeled on crbug.com/378779897 (Liftoff GetMemOp +
// AtomicBinop):
inline Operand LiftoffGetMemOpUsingScratch1(MacroAssembler* masm, Register addr,
                                            Register offset_reg,
                                            uintptr_t offset_imm) {
  if (is_uint31(offset_imm)) {
    return offset_reg == no_reg
               ? Operand(addr, static_cast<int32_t>(offset_imm))
               : Operand(addr, offset_reg, times_1,
                         static_cast<int32_t>(offset_imm));
  }
  Register scratch = kScratchRegister;
  masm->Move(scratch, offset_imm);
  if (offset_reg != no_reg) masm->addq(scratch, offset_reg);
  return Operand(addr, scratch, times_1, 0);
}

inline Operand LiftoffGetMemOpUsingScratch2(MacroAssembler* masm, Register addr,
                                            Register offset_reg,
                                            uintptr_t offset_imm) {
  if (is_uint31(offset_imm)) {
    return offset_reg == no_reg
               ? Operand(addr, static_cast<int32_t>(offset_imm))
               : Operand(addr, offset_reg, times_1,
                         static_cast<int32_t>(offset_imm));
  }
  Register scratch = r11;
  masm->Move(scratch, offset_imm);
  if (offset_reg != no_reg) masm->addq(scratch, offset_reg);
  return Operand(addr, scratch, times_1, 0);
}

void TestLiftoffGetMemOpClobberBad(MacroAssembler* masm, Register dst_addr,
                                   Register offset_reg, uintptr_t offset_imm,
                                   wasm::LiftoffRegister value,
                                   wasm::LiftoffRegister result) {
  Operand dst_op =
      LiftoffGetMemOpUsingScratch1(masm, dst_addr, offset_reg, offset_imm);
  __ movq(kScratchRegister, value.gp());
  // EXPECT-WARNING: Use of Operand 'dst_op' after its underlying scratch
  __ movq(result.gp(), dst_op);
}

void TestLiftoffGetMemOpSeparateScratchGood(MacroAssembler* masm,
                                            Register dst_addr,
                                            Register offset_reg,
                                            uintptr_t offset_imm,
                                            wasm::LiftoffRegister value,
                                            wasm::LiftoffRegister result) {
  Operand dst_op =
      LiftoffGetMemOpUsingScratch2(masm, dst_addr, offset_reg, offset_imm);
  __ movq(kScratchRegister, value.gp());
  __ movq(result.gp(), dst_op);
}

// -----------------------------------------------------------------------------
// 3. Control-Flow (Forward/Backward Label Branch, Bind, Loop, No-Fallthrough)
// -----------------------------------------------------------------------------

void TestBranchClobberPathMergesIntoBindBad(MacroAssembler* masm, Register val,
                                            Register scratch) {
  Label done;
  __ testq(val, val);
  __ j(zero, &done);
  __ IsObjectType(scratch, JS_OBJECT_TYPE, val);
  __ bind(&done);
  // EXPECT-WARNING: Use of register 'val' after it was clobbered
  __ incq(val);
}

void TestNoFallthroughAbortDoesNotPoisonBindGood(MacroAssembler* masm,
                                                 Register val) {
  Label ok;
  __ testq(val, val);
  __ j(not_zero, &ok);
  __ CallRuntime(Runtime::kAbort);
  __ Abort(AbortReason::kNoReason);
  __ bind(&ok);
  __ incq(val);
}

// Regression test modeled on crbug.com/1439691 (loop-carried register clobber
// across backward branch to bound Label):
void TestBackwardBranchLoopClobberBad(MacroAssembler* masm, Register current,
                                      Register scratch, int count) {
  Label loop;
  __ bind(&loop);
  // On iteration 2+, `current` was clobbered at the bottom of iteration 1!
  // EXPECT-WARNING: Use of register 'current' after it was clobbered
  __ cmpq(current, Immediate(count));
  __ IsObjectType(scratch, JS_OBJECT_TYPE, current);
  __ j(not_equal, &loop);
}

void TestBackwardBranchLoopRedefinedGood(MacroAssembler* masm, Register current,
                                         Register base, Register scratch) {
  Label loop;
  __ bind(&loop);
  __ cmpq(current, base);
  __ IsObjectType(base, JS_OBJECT_TYPE, scratch);
  __ movq(current, Operand(base, 8));
  __ j(not_equal, &loop);
}

void TestCppForLoopClobberBad(MacroAssembler* masm, Register val,
                              Register scratch, int n) {
  for (int i = 0; i < n; ++i) {
    // EXPECT-WARNING: Use of register 'val' after it was clobbered
    __ addq(scratch, val);
    __ IsObjectType(scratch, JS_OBJECT_TYPE, val);
  }
}

void TestCppForLoopGood(MacroAssembler* masm, Register val, Register scratch,
                        int n) {
  for (int i = 0; i < n; ++i) {
    __ IsObjectType(val, JS_OBJECT_TYPE, scratch);
    __ movq(scratch, val);
  }
}

// -----------------------------------------------------------------------------
// 4. Early-Clobber / Input-Output Aliasing Tests
// -----------------------------------------------------------------------------

void HelperEarlyClobber(MacroAssembler* masm, Register V8_REG_OUT dst,
                        Register src) {
  __ movq(dst, Immediate(10));
  __ addq(dst, src);
}

void HelperSafeSameInstruction(MacroAssembler* masm, Register V8_REG_OUT dst,
                               Register src) {
  __ movq(dst, Operand(src, 8));
  __ addq(dst, Immediate(10));
}

void HelperSafeGuarded(MacroAssembler* masm, Register V8_REG_OUT dst,
                       Register src) {
  if (dst != src) {
    __ movq(dst, Immediate(10));
    __ addq(dst, src);
  } else {
    __ addq(dst, Immediate(10));
  }
}

void HelperTwoScratches(MacroAssembler* masm, Register V8_REG_SCRATCH s1,
                        Register V8_REG_SCRATCH s2) {
  __ movq(s1, Immediate(1));
  __ movq(s2, Immediate(2));
}

void TestEarlyClobberCallSites(MacroAssembler* masm, Register r) {
  // EXPECT-WARNING: Register passed as 'dst' to 'HelperEarlyClobber' aliases
  HelperEarlyClobber(masm, r, r);

  HelperSafeSameInstruction(masm, r, r);
  HelperSafeGuarded(masm, r, r);

  // EXPECT-WARNING: Distinct output/scratch parameters 's1' and 's2'
  HelperTwoScratches(masm, r, r);
}

// -----------------------------------------------------------------------------
// 5. BaselineAssembler::ScratchRegisterScope Tests
// -----------------------------------------------------------------------------

void TestBaselineScratchLeakedScopeBad(baseline::BaselineAssembler* basm) {
  Register leaked = no_reg;
  {
    baseline::BaselineAssembler::ScratchRegisterScope scope1(basm);
    leaked = scope1.AcquireScratch();
    basm->Move(leaked, 1);
  }
  {
    baseline::BaselineAssembler::ScratchRegisterScope scope2(basm);
    Register other = scope2.AcquireScratch();
    basm->Move(other, 2);
  }
  // EXPECT-WARNING: Use of register 'leaked' after it was clobbered
  basm->Push(leaked);
}

void TestBaselineNestedScratchScopesGood(baseline::BaselineAssembler* basm) {
  baseline::BaselineAssembler::ScratchRegisterScope scope1(basm);
  Register r1 = scope1.AcquireScratch();
  basm->Move(r1, 1);
  {
    baseline::BaselineAssembler::ScratchRegisterScope scope2(basm);
    Register r2 = scope2.AcquireScratch();
    basm->Move(r2, 2);
    basm->Push(r2);
  }
  basm->Push(r1);
}

// -----------------------------------------------------------------------------
// 6. Annotation Verification Mismatch Tests
// -----------------------------------------------------------------------------

// EXPECT-WARNING: Parameter 'x' of 'v8::internal::BadAnnotationInModified'
void BadAnnotationInModified(MacroAssembler* masm, Register V8_REG_IN x) {
  __ incq(x);
}

// EXPECT-WARNING: Parameter 'out' of 'v8::internal::BadAnnotationOutReadFirst'
void BadAnnotationOutReadFirst(MacroAssembler* masm, Register V8_REG_OUT out) {
  __ addq(out, Immediate(5));
}

// EXPECT-WARNING: modifies/clobbers physical register(s) {rcx}
void BadAnnotationMissingPhysClobber(MacroAssembler* masm,
                                     Register V8_REG_IN x) {
  __ movq(rcx, x);
}

// -----------------------------------------------------------------------------
// 7. Read-Only Provenance: V8_REG_RDONLY & Maglev UseRegister Checking
// -----------------------------------------------------------------------------

// Regression test modeled on crbug.com/516128144 (modifying a read-only
// input/scratch):
void TestRdOnlyParamModifiedBad(MacroAssembler* masm, Register V8_REG_OUT dst,
                                Register V8_REG_RDONLY src) {
  __ movq(dst, src);
  // EXPECT-WARNING: Cannot modify/clobber read-only register 'src'
  __ addq(src, Immediate(1));
}

void TestRdOnlyParamSavedRestoredGood(MacroAssembler* masm,
                                      Register V8_REG_OUT dst,
                                      Register V8_REG_RDONLY src) {
  __ pushq(src);
  __ addq(src, Immediate(1));
  __ movq(dst, src);
  __ popq(src);
}

// Regression test modeled on crbug.com/466701290 (Maglev CheckJSDataViewBounds
// mutating a UseRegister input without UseAndClobberRegister):
struct FakeInput {};
Register ToRegister(FakeInput);
void UseRegister(FakeInput);
void UseAndClobberRegister(FakeInput);
template <typename T>
void DefineSameAsFirst(T*);

struct FakeDataViewBoundsBuggy {
  FakeInput IndexInput() { return {}; }
  FakeInput ByteLengthInput() { return {}; }

  void SetValueLocationConstraints() {
    UseRegister(IndexInput());
    UseRegister(ByteLengthInput());
  }

  void GenerateCode(MacroAssembler* masm, int element_size) {
    Register index = ToRegister(IndexInput());
    Register byte_length = ToRegister(ByteLengthInput());
    if (element_size > 1) {
      // EXPECT-WARNING: Cannot modify/clobber Maglev input register
      __ subq(byte_length, Immediate(element_size - 1));
    }
    // In-place 32->64 bit extension on `index` preserves low 32 bits and is OK:
    __ movsxlq(index, index);
    __ cmpq(index, byte_length);
  }
};

struct FakeDataViewBoundsFixed {
  FakeInput IndexInput() { return {}; }
  FakeInput ByteLengthInput() { return {}; }
  int element_size_ = 4;

  void SetValueLocationConstraints() {
    UseRegister(IndexInput());
    if (element_size_ > 1) {
      UseAndClobberRegister(ByteLengthInput());
    } else {
      UseRegister(ByteLengthInput());
    }
  }

  void GenerateCode(MacroAssembler* masm) {
    Register index = ToRegister(IndexInput());
    Register byte_length = ToRegister(ByteLengthInput());
    if (element_size_ > 1) {
      __ subq(byte_length, Immediate(element_size_ - 1));
    }
    __ movsxlq(index, index);
    __ cmpq(index, byte_length);
  }
};

struct FakeBinopDefineSameAsFirstGood {
  FakeInput LeftInput() { return {}; }
  FakeInput RightInput() { return {}; }

  void SetValueLocationConstraints() {
    UseRegister(LeftInput());
    UseRegister(RightInput());
    DefineSameAsFirst(this);
  }

  void GenerateCode(MacroAssembler* masm) {
    Register left = ToRegister(LeftInput());
    Register right = ToRegister(RightInput());
    __ addl(left, right);
  }
};

// -----------------------------------------------------------------------------
// 8. Partial-Width Writes, Loop-Carried Scratch Operands & Pool Exhaustion
// -----------------------------------------------------------------------------

void TestSetccUpperBitsClobberBad(MacroAssembler* masm, Register dst,
                                  Register obj) {
  __ xorl(dst, dst);
  __ IsObjectType(obj, JS_OBJECT_TYPE, dst);
  // `setcc` only writes the low 8 bits of `dst`; upper bits were clobbered!
  // EXPECT-WARNING: Use of register 'dst' after it was clobbered
  __ setcc(equal, dst);
}

void TestLoopCarriedScratchOperandClobberBad(MacroAssembler* masm,
                                             ExternalReference ref,
                                             Register dst, int n) {
  Operand op = __ ExternalReferenceAsOperand(ref);
  for (int i = 0; i < n; ++i) {
    // On iteration 2+, `op`'s underlying `r10` was overwritten at loop end!
    // EXPECT-WARNING: Use of Operand 'op' after its underlying scratch register
    __ addq(dst, op);
    __ Move(kScratchRegister, 42);
  }
}

void HelperTwoBaselineScratches(baseline::BaselineAssembler* basm) {
  baseline::BaselineAssembler::ScratchRegisterScope scope(basm);
  Register s1 = scope.AcquireScratch();
  Register s2 = scope.AcquireScratch();
  basm->Move(s1, 1);
  basm->Move(s2, 2);
}

void TestBaselineScratchPoolExhaustedBad(baseline::BaselineAssembler* basm) {
  baseline::BaselineAssembler::ScratchRegisterScope scope(basm);
  Register s1 = scope.AcquireScratch();
  Register s2 = scope.AcquireScratch();
  Register s3 = scope.AcquireScratch();
  Register s4 = scope.AcquireScratch();
  basm->Move(s1, 1);
  basm->Move(s2, 2);
  basm->Move(s3, 3);
  basm->Move(s4, 4);
  // Only 1 scratch remains in the 5-register pool, so requiring 2 exhausts it:
  // EXPECT-WARNING: exhausting BaselineAssembler::ScratchRegisterScope pool
  HelperTwoBaselineScratches(basm);
}

#undef __

}  // namespace internal
}  // namespace v8
