// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>

#include "test/cctest/compiler/instruction-selector-tester.h"

using namespace v8::internal;
using namespace v8::internal::compiler;

namespace {

struct DPI {
  Operator* op;
  ArchOpcode arch_opcode;
};


// ARM64 Logical instructions.
class LogicalInstructions V8_FINAL : public std::list<DPI>,
                                     private HandleAndZoneScope {
 public:
  LogicalInstructions() {
    MachineOperatorBuilder machine(main_zone());
    DPI and32 = {machine.Word32And(), kArm64And32};
    push_back(and32);
    DPI and64 = {machine.Word64And(), kArm64And};
    push_back(and64);
    DPI or32 = {machine.Word32Or(), kArm64Or32};
    push_back(or32);
    DPI or64 = {machine.Word64Or(), kArm64Or};
    push_back(or64);
    DPI xor32 = {machine.Word32Xor(), kArm64Xor32};
    push_back(xor32);
    DPI xor64 = {machine.Word64Xor(), kArm64Xor};
    push_back(xor64);
  }
};


// ARM64 Arithmetic instructions.
class AddSubInstructions V8_FINAL : public std::list<DPI>,
                                    private HandleAndZoneScope {
 public:
  AddSubInstructions() {
    MachineOperatorBuilder machine(main_zone());
    DPI add32 = {machine.Int32Add(), kArm64Add32};
    push_back(add32);
    DPI add64 = {machine.Int64Add(), kArm64Add};
    push_back(add64);
    DPI sub32 = {machine.Int32Sub(), kArm64Sub32};
    push_back(sub32);
    DPI sub64 = {machine.Int64Sub(), kArm64Sub};
    push_back(sub64);
  }
};


// ARM64 Add/Sub immediates.
class AddSubImmediates V8_FINAL : public std::list<int32_t> {
 public:
  AddSubImmediates() {
    for (int32_t imm12 = 0; imm12 < 4096; ++imm12) {
      CHECK(Assembler::IsImmAddSub(imm12));
      CHECK(Assembler::IsImmAddSub(imm12 << 12));
      push_back(imm12);
      push_back(imm12 << 12);
    }
  }
};


// ARM64 Arithmetic instructions.
class MulDivInstructions V8_FINAL : public std::list<DPI>,
                                    private HandleAndZoneScope {
 public:
  MulDivInstructions() {
    MachineOperatorBuilder machine(main_zone());
    DPI mul32 = {machine.Int32Mul(), kArm64Mul32};
    push_back(mul32);
    DPI mul64 = {machine.Int64Mul(), kArm64Mul};
    push_back(mul64);
    DPI sdiv32 = {machine.Int32Div(), kArm64Idiv32};
    push_back(sdiv32);
    DPI sdiv64 = {machine.Int64Div(), kArm64Idiv};
    push_back(sdiv64);
    DPI udiv32 = {machine.Int32UDiv(), kArm64Udiv32};
    push_back(udiv32);
    DPI udiv64 = {machine.Int64UDiv(), kArm64Udiv};
    push_back(udiv64);
  }
};

}  // namespace


TEST(InstructionSelectorLogicalP) {
  LogicalInstructions instructions;
  for (LogicalInstructions::const_iterator i = instructions.begin();
       i != instructions.end(); ++i) {
    DPI dpi = *i;
    InstructionSelectorTester m;
    m.Return(m.NewNode(dpi.op, m.Parameter(0), m.Parameter(1)));
    m.SelectInstructions();
    CHECK_EQ(1, m.code.size());
    CHECK_EQ(dpi.arch_opcode, m.code[0]->arch_opcode());
  }
}


TEST(InstructionSelectorAddSubP) {
  AddSubInstructions instructions;
  for (AddSubInstructions::const_iterator i = instructions.begin();
       i != instructions.end(); ++i) {
    DPI dpi = *i;
    InstructionSelectorTester m;
    m.Return(m.NewNode(dpi.op, m.Parameter(0), m.Parameter(1)));
    m.SelectInstructions();
    CHECK_EQ(1, m.code.size());
    CHECK_EQ(dpi.arch_opcode, m.code[0]->arch_opcode());
  }
}


TEST(InstructionSelectorAddSubImm) {
  AddSubInstructions instructions;
  AddSubImmediates immediates;
  for (AddSubInstructions::const_iterator i = instructions.begin();
       i != instructions.end(); ++i) {
    DPI dpi = *i;
    for (AddSubImmediates::const_iterator j = immediates.begin();
         j != immediates.end(); ++j) {
      int32_t imm = *j;
      InstructionSelectorTester m;
      m.Return(m.NewNode(dpi.op, m.Parameter(0), m.Int32Constant(imm)));
      m.SelectInstructions();
      CHECK_EQ(1, m.code.size());
      CHECK_EQ(dpi.arch_opcode, m.code[0]->arch_opcode());
      CHECK(m.code[0]->InputAt(1)->IsImmediate());
    }
  }
}


TEST(InstructionSelectorMulDivP) {
  MulDivInstructions instructions;
  for (MulDivInstructions::const_iterator i = instructions.begin();
       i != instructions.end(); ++i) {
    DPI dpi = *i;
    InstructionSelectorTester m;
    m.Return(m.NewNode(dpi.op, m.Parameter(0), m.Parameter(1)));
    m.SelectInstructions();
    CHECK_EQ(1, m.code.size());
    CHECK_EQ(dpi.arch_opcode, m.code[0]->arch_opcode());
  }
}
