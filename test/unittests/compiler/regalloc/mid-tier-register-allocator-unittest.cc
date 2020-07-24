// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/compiler/pipeline.h"
#include "test/unittests/compiler/backend/instruction-sequence-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

class FastRegisterAllocatorTest : public InstructionSequenceTest {
 public:
  void Allocate() {
    WireBlocks();
    Pipeline::AllocateRegistersForTesting(config(), sequence(), true, true);
  }
};

TEST_F(FastRegisterAllocatorTest, CanAllocateThreeRegisters) {
  // return p0 + p1;
  StartBlock();
  auto a_reg = Parameter();
  auto b_reg = Parameter();
  auto c_reg = EmitOI(Reg(1), Reg(a_reg, 1), Reg(b_reg));
  Return(c_reg);
  EndBlock(Last());

  Allocate();
}

TEST_F(FastRegisterAllocatorTest, CanAllocateFPRegisters) {
  StartBlock();
  TestOperand inputs[] = {
      Reg(FPParameter(kFloat64)), Reg(FPParameter(kFloat64)),
      Reg(FPParameter(kFloat32)), Reg(FPParameter(kFloat32)),
      Reg(FPParameter(kSimd128)), Reg(FPParameter(kSimd128))};
  VReg out1 = EmitOI(FPReg(1, kFloat64), arraysize(inputs), inputs);
  Return(out1);
  EndBlock(Last());

  Allocate();
}

}  // namespace
}  // namespace compiler
}  // namespace internal
}  // namespace v8
