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

#define __ assm.

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

  __ RV_srliw(v0, a6, 8);   // 0x00123456
  __ RV_slliw(v0, v0, 11);  // 0x91A2B000
  __ RV_sraiw(v0, v0, 3);   // 0xFFFFFFFF F2345600
  __ RV_sraw(v0, v0, a4);   // 0xFFFFFFFF FF234560
  __ RV_sllw(v0, v0, a4);   // 0xFFFFFFFF F2345600
  __ RV_srlw(v0, v0, a4);   // 0x0F234560
  __ RV_li(t8, 0x0F234560);
  __ RV_bne(v0, t8, &error);

  __ RV_addw(v0, a4, a5);  // 0x00001238
  __ RV_subw(v0, v0, a4);  // 0x00001234
  __ RV_li(t8, 0x00001234);
  __ RV_bne(v0, t8, &error);
  __ RV_addw(v1, a7, a4);  // 32bit addu result is sign-extended into 64bit reg.
  __ RV_li(t8, 0xFFFFFFFF80000003);
  __ RV_bne(v1, t8, &error);
  __ RV_subw(v1, t3, a4);  // 0x7FFFFFFC
  __ RV_li(t8, 0x7FFFFFFC);
  __ RV_bne(v1, t8, &error);

  __ RV_and(v0, a5, a6);  // 0x0000000000001230
  __ RV_or(v0, v0, a5);   // 0x0000000000001234
  __ RV_xor(v0, v0, a6);  // 0x000000001234444C
  __ RV_or(v0, v0, a6);
  __ RV_not(v0, v0);  // 0xFFFFFFFFEDCBA983
  __ RV_li(t8, 0xFFFFFFFFEDCBA983);
  __ RV_bne(v0, t8, &error);

  // Shift both 32bit number to left, to preserve meaning of next comparison.
  __ RV_slli(a7, a7, 32);
  __ RV_slli(t3, t3, 32);

  __ RV_slt(v0, t3, a7);
  __ RV_li(t8, 1);
  __ RV_bne(v0, t8, &error);
  __ RV_sltu(v0, t3, a7);
  __ RV_bne(v0, zero_reg, &error);

  // Restore original values in registers.
  __ RV_srli(a7, a7, 32);
  __ RV_srli(t3, t3, 32);

  __ RV_li(v0, 0x7421);       // 0x00007421
  __ RV_addi(v0, v0, -0x1);   // 0x00007420
  __ RV_addi(v0, v0, -0x20);  // 0x00007400
  __ RV_li(t8, 0x00007400);
  __ RV_bne(v0, t8, &error);
  __ RV_addiw(v1, a7, 0x1);  // 0x80000000 - result is sign-extended.
  __ RV_li(t8, 0xFFFFFFFF80000000);
  __ RV_bne(v1, t8, &error);

  __ RV_li(t8, 0x00002000);
  __ RV_slt(v0, a5, t8);  // 0x1
  __ RV_li(t9, 0xFFFFFFFFFFFF8000);
  __ RV_slt(v0, v0, t9);  // 0x0
  __ RV_bne(v0, zero_reg, &error);
  __ RV_sltu(v0, a5, t8);  // 0x1
  __ RV_li(t9, 0x00008000);
  __ RV_sltu(v0, v0, t9);  // 0x1
  __ RV_li(t8, 1);
  __ RV_bne(v0, t8, &error);

  __ RV_andi(v0, a5, 0x0F0);  // 0x00000030
  __ RV_ori(v0, v0, 0x200);   // 0x00000230
  __ RV_xori(v0, v0, 0x3CC);  // 0x000001FC
  __ RV_li(t8, 0x000001FC);
  __ RV_bne(v0, t8, &error);
  __ RV_lui(v1, -519628);  // Result is sign-extended into 64bit register.
  __ RV_li(t8, 0xFFFFFFFF81234000);
  __ RV_bne(v1, t8, &error);

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
  __ RV_fld(f4, a0, offsetof(T, a));
  __ RV_fld(f6, a0, offsetof(T, b));
  __ RV_fadd_d(f8, f4, f6);
  __ RV_fsd(a0, f8, offsetof(T, c));  // c = a + b.

  __ RV_fmv_d(f10, f8);   // c
  __ RV_fneg_d(f12, f6);  // -b
  __ RV_fsub_d(f10, f10, f12);
  __ RV_fsd(a0, f10, offsetof(T, d));  // d = c - (-b).

  __ RV_fsd(a0, f4, offsetof(T, b));  // b = a.

  __ RV_li(a4, 120);
  __ RV_fcvt_d_w(f14, a4);
  __ RV_fmul_d(f10, f10, f14);
  __ RV_fsd(a0, f10, offsetof(T, e));  // e = d * 120 = 1.8066e16.

  __ RV_fdiv_d(f12, f10, f4);
  __ RV_fsd(a0, f12, offsetof(T, f));  // f = e / a = 120.44.

  __ RV_fsqrt_d(f14, f12);
  __ RV_fsd(a0, f14, offsetof(T, g));
  // g = sqrt(f) = 10.97451593465515908537

  __ RV_fld(f4, a0, offsetof(T, h));
  __ RV_fld(f6, a0, offsetof(T, i));
  __ RV_fmadd_d(f14, f6, f4, f6);
  __ RV_fsd(a0, f14, offsetof(T, h));

  // // Single precision floating point instructions.
  __ RV_flw(f4, a0, offsetof(T, fa));
  __ RV_flw(f6, a0, offsetof(T, fb));
  __ RV_fadd_s(f8, f4, f6);
  __ RV_fsw(a0, f8, offsetof(T, fc));  // fc = fa + fb.

  __ RV_fneg_s(f10, f6);  // -fb
  __ RV_fsub_s(f10, f8, f10);
  __ RV_fsw(a0, f10, offsetof(T, fd));  // fd = fc - (-fb).

  __ RV_fsw(a0, f4, offsetof(T, fb));  // fb = fa.

  __ RV_li(t0, 120);
  __ RV_fcvt_s_w(f14, t0);  // f14 = 120.0.
  __ RV_fmul_s(f10, f10, f14);
  __ RV_fsw(a0, f10, offsetof(T, fe));  // fe = fd * 120

  __ RV_fdiv_s(f12, f10, f4);
  __ RV_fsw(a0, f12, offsetof(T, ff));  // ff = fe / fa

  __ RV_fsqrt_s(f14, f12);
  __ RV_fsw(a0, f14, offsetof(T, fg));

  __ jr(ra);
  __ nop();

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

#undef __

}  // namespace internal
}  // namespace v8
