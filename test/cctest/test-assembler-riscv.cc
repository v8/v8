// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <iostream>  // NOLINT(readability/streams)

#include "src/init/v8.h"

#include "src/base/utils/random-number-generator.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"

#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

// Define these function prototypes to match JSEntryFunction in execution.cc.
// TODO(mips64): Refine these signatures per test case.
using F1 = void*(int x, int p1, int p2, int p3, int p4);
using F2 = void*(int x, int y, int p2, int p3, int p4);
using F3 = void*(void* p, int p1, int p2, int p3, int p4);
using F4 = void*(int64_t x, int64_t y, int64_t p2, int64_t p3, int64_t p4);
using F5 = void*(void* p0, void* p1, int p2, int p3, int p4);

using U0 = int64_t();
using U1 = int64_t(int64_t rs2);
using U2 = int64_t(int64_t rs1, int64_t rs2);
using U3 = int64_t(void* base, int64_t val);

#define __ assm.

#define MIN_VAL_IMM12 -(1 << 11)
#define LARGE_INT_EXCEED_32_BIT 0x01C910750321FB01

// return the maximal positive number representable by an immediate field of
// nbits
static int32_t max_val(int32_t nbits) {
  CHECK_LE(nbits, 32);
  return (1 << (nbits - 1)) - 1;
}

// return the minimal negative number representable by an immediate field of
// nbits
static int32_t min_val(int32_t nbits) {
  CHECK_LE(nbits, 32);
  return -(1 << (nbits - 1));
}

// check whether a value can be expressed by nbits immediate value
static bool check_imm_range(int32_t val, int32_t nbits) {
  return (val <= max_val(nbits) && val >= min_val(nbits));
}

#define kReturnReg a0
#define kArg0Reg a0
#define kArg1Reg a1
#define kReturnAddrReg ra

#define UTEST_R_FORM_WITH_RES(instr_name, rs1_val, rs2_val, expected_res)  \
  TEST(RISCV_UTEST_##instr_name) {                                         \
    CcTest::InitializeVM();                                                \
    Isolate* isolate = CcTest::i_isolate();                                \
    HandleScope scope(isolate);                                            \
                                                                           \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);  \
    __ RV_##instr_name(kReturnReg, kArg0Reg, kArg1Reg);                    \
    __ RV_jr(kReturnAddrReg);                                              \
                                                                           \
    /* construct the codelet and invoke generated codes via f.Call(...) */ \
    CodeDesc desc;                                                         \
    assm.GetCode(isolate, &desc);                                          \
    Handle<Code> code =                                                    \
        Factory::CodeBuilder(isolate, desc, Code::STUB).Build();           \
                                                                           \
    auto f = GeneratedCode<U2>::FromCode(*code);                           \
    int64_t res = reinterpret_cast<int64_t>(f.Call(rs1_val, rs2_val));     \
    /* std::cout << "res = " << res << std::endl; */                       \
    CHECK_EQ((int64_t)expected_res, res);                                  \
  }

#define UTEST_R_FORM_WITH_OP(instr_name, rs1_val, rs2_val, tested_op) \
  UTEST_R_FORM_WITH_RES(instr_name, rs1_val, rs2_val,                 \
                        ((rs1_val)tested_op(rs2_val)))

#define UTEST_I_FORM_WITH_RES(instr_name, rs1_val, imm12, expected_res)    \
  TEST(RISCV_UTEST_##instr_name) {                                         \
    CcTest::InitializeVM();                                                \
    Isolate* isolate = CcTest::i_isolate();                                \
    HandleScope scope(isolate);                                            \
                                                                           \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);  \
    CHECK_EQ(check_imm_range(imm12, 12), true);                            \
    __ RV_##instr_name(kReturnReg, kArg0Reg, imm12);                       \
    __ RV_jr(kReturnAddrReg);                                              \
                                                                           \
    /* construct the codelet and invoke generated codes via f.Call(...) */ \
    CodeDesc desc;                                                         \
    assm.GetCode(isolate, &desc);                                          \
    Handle<Code> code =                                                    \
        Factory::CodeBuilder(isolate, desc, Code::STUB).Build();           \
    auto f = GeneratedCode<U1>::FromCode(*code);                           \
    int64_t res = reinterpret_cast<int64_t>(f.Call(rs1_val));              \
    CHECK_EQ((int64_t)expected_res, res);                                  \
  }

#define UTEST_I_FORM_WITH_OP(instr_name, rs1_val, imm12, tested_op) \
  UTEST_I_FORM_WITH_RES(instr_name, rs1_val, imm12, ((rs1_val)tested_op(imm12)))

#define UTEST_LOAD_STORE(ldname, stname, value)                            \
  TEST(RISCV_UTEST_##stname##ldname) {                                     \
    CcTest::InitializeVM();                                                \
    Isolate* isolate = CcTest::i_isolate();                                \
    HandleScope scope(isolate);                                            \
                                                                           \
    int64_t tmp = 0;                                                       \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);  \
    __ RV_##stname(kArg1Reg, kArg0Reg, 0);                                 \
    __ RV_##ldname(kReturnReg, kArg0Reg, 0);                               \
    __ RV_jr(kReturnAddrReg);                                              \
                                                                           \
    /* construct the codelet and invoke generated codes via f.Call(...) */ \
    CodeDesc desc;                                                         \
    assm.GetCode(isolate, &desc);                                          \
    Handle<Code> code =                                                    \
        Factory::CodeBuilder(isolate, desc, Code::STUB).Build();           \
    auto f = GeneratedCode<U3>::FromCode(*code);                           \
    int64_t res = reinterpret_cast<int64_t>(f.Call(&tmp, value));          \
    /* std::cout << std::hex << "res = " << res << std::endl;   */         \
    CHECK_EQ(((int64_t)value), res);                                       \
  }  // namespace internal

// RISCV I-set
UTEST_R_FORM_WITH_OP(add, LARGE_INT_EXCEED_32_BIT, 20, +)
UTEST_I_FORM_WITH_OP(addi, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, +)
UTEST_LOAD_STORE(ld, sd, 0xFBB10A9C12345678)
UTEST_LOAD_STORE(lw, sw, 0x456AF894)

TEST(RISCV0) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Addition.
  __ RV_addw(a0, a0, a1);
  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F2>::FromCode(*code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0xAB0, 0xC, 0, 0, 0));
  CHECK_EQ(0xABCL, res);
}

TEST(RISCV1) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label L, C;

  __ RV_mv(a1, a0);
  __ RV_li(a0, 0l);
  __ RV_j(&C);

  __ RV_bind(&L);
  __ RV_add(a0, a0, a1);
  __ RV_addi(a1, a1, -1);

  __ RV_bind(&C);
  __ RV_xori(a2, a1, 0);
  __ RV_bnez(a2, &L);

  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F1>::FromCode(*code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(50, 0, 0, 0, 0));
  CHECK_EQ(1275L, res);
}

TEST(RISCV2) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label exit, error;

  // ----- Test all instructions.

  // Test lui, ori, and addiu, used in the li pseudo-instruction.
  // This way we can then safely load registers with chosen values.

  __ RV_ori(a4, zero_reg, 0);
  __ RV_lui(a4, 0x12345);
  __ RV_ori(a4, a4, 0);
  __ RV_ori(a4, a4, 0xF0F);
  __ RV_ori(a4, a4, 0x0F0);
  __ RV_addiw(a5, a4, 1);
  __ RV_addiw(a6, a5, -0x10);

  // Load values in temporary registers.
  __ RV_li(a4, 0x00000004);
  __ RV_li(a5, 0x00001234);
  __ RV_li(a6, 0x12345678);
  __ RV_li(a7, 0x7FFFFFFF);
  __ RV_li(t0, 0xFFFFFFFC);
  __ RV_li(t1, 0xFFFFEDCC);
  __ RV_li(t2, 0xEDCBA988);
  __ RV_li(t3, 0x80000000);

  __ RV_srliw(t0, a6, 8);   // 0x00123456
  __ RV_slliw(t0, t0, 11);  // 0x91A2B000
  __ RV_sraiw(t0, t0, 3);   // 0xFFFFFFFF F2345600
  __ RV_sraw(t0, t0, a4);   // 0xFFFFFFFF FF234560
  __ RV_sllw(t0, t0, a4);   // 0xFFFFFFFF F2345600
  __ RV_srlw(t0, t0, a4);   // 0x0F234560
  __ RV_li(t5, 0x0F234560);
  __ RV_bne(t0, t5, &error);

  __ RV_addw(t0, a4, a5);  // 0x00001238
  __ RV_subw(t0, t0, a4);  // 0x00001234
  __ RV_li(t5, 0x00001234);
  __ RV_bne(t0, t5, &error);
  __ RV_addw(a1, a7, a4);  // 32bit addu result is sign-extended into 64bit reg.
  __ RV_li(t5, 0xFFFFFFFF80000003);
  __ RV_bne(a1, t5, &error);
  __ RV_subw(a1, t3, a4);  // 0x7FFFFFFC
  __ RV_li(t5, 0x7FFFFFFC);
  __ RV_bne(a1, t5, &error);

  __ RV_and_(t0, a5, a6);  // 0x0000000000001230
  __ RV_or_(t0, t0, a5);   // 0x0000000000001234
  __ RV_xor_(t0, t0, a6);  // 0x000000001234444C
  __ RV_or_(t0, t0, a6);
  __ RV_not(t0, t0);  // 0xFFFFFFFFEDCBA983
  __ RV_li(t5, 0xFFFFFFFFEDCBA983);
  __ RV_bne(t0, t5, &error);

  // Shift both 32bit number to left, to preserve meaning of next comparison.
  __ RV_slli(a7, a7, 32);
  __ RV_slli(t3, t3, 32);

  __ RV_slt(t0, t3, a7);
  __ RV_li(t5, 1);
  __ RV_bne(t0, t5, &error);
  __ RV_sltu(t0, t3, a7);
  __ RV_bne(t0, zero_reg, &error);

  // Restore original values in registers.
  __ RV_srli(a7, a7, 32);
  __ RV_srli(t3, t3, 32);

  __ RV_li(t0, 0x7421);       // 0x00007421
  __ RV_addi(t0, t0, -0x1);   // 0x00007420
  __ RV_addi(t0, t0, -0x20);  // 0x00007400
  __ RV_li(t5, 0x00007400);
  __ RV_bne(t0, t5, &error);
  __ RV_addiw(a1, a7, 0x1);  // 0x80000000 - result is sign-extended.
  __ RV_li(t5, 0xFFFFFFFF80000000);
  __ RV_bne(a1, t5, &error);

  __ RV_li(t5, 0x00002000);
  __ RV_slt(t0, a5, t5);  // 0x1
  __ RV_li(t6, 0xFFFFFFFFFFFF8000);
  __ RV_slt(t0, t0, t6);  // 0x0
  __ RV_bne(t0, zero_reg, &error);
  __ RV_sltu(t0, a5, t5);  // 0x1
  __ RV_li(t6, 0x00008000);
  __ RV_sltu(t0, t0, t6);  // 0x1
  __ RV_li(t5, 1);
  __ RV_bne(t0, t5, &error);

  __ RV_andi(t0, a5, 0x0F0);  // 0x00000030
  __ RV_ori(t0, t0, 0x200);   // 0x00000230
  __ RV_xori(t0, t0, 0x3CC);  // 0x000001FC
  __ RV_li(t5, 0x000001FC);
  __ RV_bne(t0, t5, &error);
  __ RV_lui(a1, -519628);  // Result is sign-extended into 64bit register.
  __ RV_li(t5, 0xFFFFFFFF81234000);
  __ RV_bne(a1, t5, &error);

  // Everything was correctly executed. Load the expected result.
  __ RV_li(a0, 0x31415926);
  __ RV_j(&exit);

  __ RV_bind(&error);
  // Got an error. Return a wrong result.
  __ RV_li(a0, 666);

  __ RV_bind(&exit);
  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F2>::FromCode(*code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0xAB0, 0xC, 0, 0, 0));

  CHECK_EQ(0x31415926L, res);
}

TEST(RISCV3) {
  // Test floating point instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
    double g;
    double h;
    double i;
    float fa;
    float fb;
    float fc;
    float fd;
    float fe;
    float ff;
    float fg;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles t.a ... t.f.
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Double precision floating point instructions.
  __ RV_fld(ft0, a0, offsetof(T, a));
  __ RV_fld(ft1, a0, offsetof(T, b));
  __ RV_fadd_d(ft2, ft0, ft1);
  __ RV_fsd(ft2, a0, offsetof(T, c));  // c = a + b.

  __ RV_fmv_d(ft3, ft2);   // c
  __ RV_fneg_d(fa0, ft1);  // -b
  __ RV_fsub_d(ft3, ft3, fa0);
  __ RV_fsd(ft3, a0, offsetof(T, d));  // d = c - (-b).

  __ RV_fsd(ft0, a0, offsetof(T, b));  // b = a.

  __ RV_li(a4, 120);
  __ RV_fcvt_d_w(ft5, a4);
  __ RV_fmul_d(ft3, ft3, ft5);
  __ RV_fsd(ft3, a0, offsetof(T, e));  // e = d * 120 = 1.8066e16.

  __ RV_fdiv_d(ft4, ft3, ft0);
  __ RV_fsd(ft4, a0, offsetof(T, f));  // f = e / a = 120.44.

  __ RV_fsqrt_d(ft5, ft4);
  __ RV_fsd(ft5, a0, offsetof(T, g));
  // g = sqrt(f) = 10.97451593465515908537

  __ RV_fld(ft0, a0, offsetof(T, h));
  __ RV_fld(ft1, a0, offsetof(T, i));
  __ RV_fmadd_d(ft5, ft1, ft0, ft1);
  __ RV_fsd(ft5, a0, offsetof(T, h));

  // // Single precision floating point instructions.
  __ RV_flw(ft0, a0, offsetof(T, fa));
  __ RV_flw(ft1, a0, offsetof(T, fb));
  __ RV_fadd_s(ft2, ft0, ft1);
  __ RV_fsw(ft2, a0, offsetof(T, fc));  // fc = fa + fb.

  __ RV_fneg_s(ft3, ft1);  // -fb
  __ RV_fsub_s(ft3, ft2, ft3);
  __ RV_fsw(ft3, a0, offsetof(T, fd));  // fd = fc - (-fb).

  __ RV_fsw(ft0, a0, offsetof(T, fb));  // fb = fa.

  __ RV_li(t0, 120);
  __ RV_fcvt_s_w(ft5, t0);  // ft5 = 120.0.
  __ RV_fmul_s(ft3, ft3, ft5);
  __ RV_fsw(ft3, a0, offsetof(T, fe));  // fe = fd * 120

  __ RV_fdiv_s(ft4, ft3, ft0);
  __ RV_fsw(ft4, a0, offsetof(T, ff));  // ff = fe / fa

  __ RV_fsqrt_s(ft5, ft4);
  __ RV_fsw(ft5, a0, offsetof(T, fg));

  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F3>::FromCode(*code);
  // Double test values.
  t.a = 1.5e14;
  t.b = 2.75e11;
  t.c = 0.0;
  t.d = 0.0;
  t.e = 0.0;
  t.f = 0.0;
  t.h = 1.5;
  t.i = 2.75;
  // Single test values.
  t.fa = 1.5e6;
  t.fb = 2.75e4;
  t.fc = 0.0;
  t.fd = 0.0;
  t.fe = 0.0;
  t.ff = 0.0;
  f.Call(&t, 0, 0, 0, 0);
  // Expected double results.
  CHECK_EQ(1.5e14, t.a);
  CHECK_EQ(1.5e14, t.b);
  CHECK_EQ(1.50275e14, t.c);
  CHECK_EQ(1.50550e14, t.d);
  CHECK_EQ(1.8066e16, t.e);
  CHECK_EQ(120.44, t.f);
  CHECK_EQ(10.97451593465515908537, t.g);
  CHECK_EQ(6.875, t.h);
  // Expected single results.
  CHECK_EQ(1.5e6, t.fa);
  CHECK_EQ(1.5e6, t.fb);
  CHECK_EQ(1.5275e06, t.fc);
  CHECK_EQ(1.5550e06, t.fd);
  CHECK_EQ(1.866e08, t.fe);
  CHECK_EQ(124.40000152587890625, t.ff);
  CHECK_EQ(11.1534748077392578125, t.fg);
}
TEST(RISCV4) {
  // Test moves between floating point and integer registers.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double a;
    double b;
    double c;
    float d;
    int64_t e;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ RV_fld(ft0, a0, offsetof(T, a));
  __ RV_fld(fa1, a0, offsetof(T, b));

  // Swap ft0 and fa1, by using 2 integer registers, a4-a5,

  __ RV_fmv_x_d(a4, ft0);
  __ RV_fmv_x_d(a5, fa1);

  __ RV_fmv_d_x(fa1, a4);
  __ RV_fmv_d_x(ft0, a5);

  // Store the swapped ft0 and fa1 back to memory.
  __ RV_fsd(ft0, a0, offsetof(T, a));
  __ RV_fsd(fa1, a0, offsetof(T, c));

  // Test sign extension of move operations from coprocessor.
  __ RV_flw(ft0, a0, offsetof(T, d));
  __ RV_fmv_x_w(a4, ft0);

  __ RV_sd(a4, a0, offsetof(T, e));

  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F3>::FromCode(*code);
  t.a = 1.5e22;
  t.b = 2.75e11;
  t.c = 17.17;
  t.d = -2.75e11;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(2.75e11, t.a);
  CHECK_EQ(2.75e11, t.b);
  CHECK_EQ(1.5e22, t.c);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFD2800E8EL), t.e);
}

TEST(RISCV5) {
  // Test conversions between doubles and integers.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double a;
    double b;
    int i;
    int j;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Load all structure elements to registers.
  __ RV_fld(ft0, a0, offsetof(T, a));
  __ RV_fld(ft1, a0, offsetof(T, b));
  __ RV_lw(a4, a0, offsetof(T, i));
  __ RV_lw(a5, a0, offsetof(T, j));

  // Convert double in ft0 to int in element i.
  __ RV_fcvt_l_d(a6, ft0);
  __ RV_sw(a6, a0, offsetof(T, i));

  // Convert double in ft1 to int in element j.
  __ RV_fcvt_l_d(a7, ft1);
  __ RV_sw(a7, a0, offsetof(T, j));

  // Convert int in original i (a4) to double in a.
  __ RV_fcvt_d_l(fa0, a4);
  __ RV_fsd(fa0, a0, offsetof(T, a));

  // Convert int in original j (a5) to double in b.
  __ RV_fcvt_d_l(fa1, a5);
  __ RV_fsd(fa1, a0, offsetof(T, b));

  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F3>::FromCode(*code);
  t.a = 1.5e4;
  t.b = 2.75e8;
  t.i = 12345678;
  t.j = -100000;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(12345678.0, t.a);
  CHECK_EQ(-100000.0, t.b);
  CHECK_EQ(15000, t.i);
  CHECK_EQ(275000000, t.j);
}

TEST(RISCV6) {
  // Test simple memory loads and stores.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint32_t ui;
    int32_t si;
    int32_t r1;
    int32_t r2;
    int32_t r3;
    int32_t r4;
    int32_t r5;
    int32_t r6;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Basic word load/store.
  __ RV_lw(a4, a0, offsetof(T, ui));
  __ RV_sw(a4, a0, offsetof(T, r1));

  // lh with positive data.
  __ RV_lh(a5, a0, offsetof(T, ui));
  __ RV_sw(a5, a0, offsetof(T, r2));

  // lh with negative data.
  __ RV_lh(a6, a0, offsetof(T, si));
  __ RV_sw(a6, a0, offsetof(T, r3));

  // lhu with negative data.
  __ RV_lhu(a7, a0, offsetof(T, si));
  __ RV_sw(a7, a0, offsetof(T, r4));

  // Lb with negative data.
  __ RV_lb(t0, a0, offsetof(T, si));
  __ RV_sw(t0, a0, offsetof(T, r5));

  // sh writes only 1/2 of word.
  __ RV_li(t1, 0x33333333);
  __ RV_sw(t1, a0, offsetof(T, r6));
  __ RV_lhu(t1, a0, offsetof(T, si));
  __ RV_sh(t1, a0, offsetof(T, r6));

  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F3>::FromCode(*code);
  t.ui = 0x11223344;
  t.si = 0x99AABBCC;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(static_cast<int32_t>(0x11223344), t.r1);
  if (kArchEndian == kLittle) {
    CHECK_EQ(static_cast<int32_t>(0x3344), t.r2);
    CHECK_EQ(static_cast<int32_t>(0xFFFFBBCC), t.r3);
    CHECK_EQ(static_cast<int32_t>(0x0000BBCC), t.r4);
    CHECK_EQ(static_cast<int32_t>(0xFFFFFFCC), t.r5);
    CHECK_EQ(static_cast<int32_t>(0x3333BBCC), t.r6);
  } else {
    CHECK_EQ(static_cast<int32_t>(0x1122), t.r2);
    CHECK_EQ(static_cast<int32_t>(0xFFFF99AA), t.r3);
    CHECK_EQ(static_cast<int32_t>(0x000099AA), t.r4);
    CHECK_EQ(static_cast<int32_t>(0xFFFFFF99), t.r5);
    CHECK_EQ(static_cast<int32_t>(0x99AA3333), t.r6);
  }
}

TEST(RISCV7) {
  // Test floating point compare and branch instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
    int32_t result;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles t.a ... t.f.
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label neither_is_nan, less_than, outa_here;

  __ RV_fld(ft0, a0, offsetof(T, a));
  __ RV_fld(ft1, a0, offsetof(T, b));

  __ RV_fclass_d(t5, ft0);
  __ RV_fclass_d(t6, ft1);
  __ RV_or_(t5, t5, t6);
  __ RV_andi(t5, t5, 0b1100000000);
  __ RV_beq(t5, zero_reg, &neither_is_nan);
  __ RV_sw(zero_reg, a0, offsetof(T, result));
  __ RV_j(&outa_here);

  __ RV_bind(&neither_is_nan);

  __ RV_flt_d(t5, ft1, ft0);
  __ RV_bne(t5, zero_reg, &less_than);

  __ RV_sw(zero_reg, a0, offsetof(T, result));
  __ RV_j(&outa_here);

  __ RV_bind(&less_than);
  __ RV_li(a4, 1);
  __ RV_sw(a4, a0, offsetof(T, result));  // Set true.

  // This test-case should have additional tests.

  __ RV_bind(&outa_here);

  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F3>::FromCode(*code);
  t.a = 1.5e14;
  t.b = 2.75e11;
  t.c = 2.0;
  t.d = -4.0;
  t.e = 0.0;
  t.f = 0.0;
  t.result = 0;
  f.Call(&t, 0, 0, 0, 0);
  CHECK_EQ(1.5e14, t.a);
  CHECK_EQ(2.75e11, t.b);
  CHECK_EQ(1, t.result);
}

TEST(TARGET_ADDR) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  // This is the series of instructions to load 0x123456789abcdef0
  uint32_t buffer[8] = {0x01234237, 0x5682021b, 0x00c21213, 0x89b20213,
                        0x00c21213, 0xbce20213, 0x00c21213, 0xef020213};

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  uintptr_t addr = reinterpret_cast<uintptr_t>(&buffer[0]);
  Address res = __ target_address_at(static_cast<Address>(addr));

  CHECK_EQ(0x123456789abcdef0L, res);
}

TEST(SET_TARGET_ADDR) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  // This is the series of instructions to load 0x123456789abcdef0
  uint32_t buffer[8] = {0x01234237, 0x5682021b, 0x00c21213, 0x89b20213,
                        0x00c21213, 0xbce20213, 0x00c21213, 0xef020213};

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  uintptr_t addr = reinterpret_cast<uintptr_t>(&buffer[0]);
  __ set_target_value_at(static_cast<Address>(addr), 0xfedcba9876543210, FLUSH_ICACHE_IF_NEEDED);
  Address res = __ target_address_at(static_cast<Address>(addr));

  CHECK_EQ(0xfedcba9876543210, res);
}


#undef __

}  // namespace internal
}  // namespace v8
