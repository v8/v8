// Copyright 2013 the V8 project authors. All rights reserved.
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

#include <stdlib.h>

#include <iostream>  // NOLINT(readability/streams)

#include "src/base/utils/random-number-generator.h"
#include "src/codegen/macro-assembler.h"
#include "src/execution/simulator.h"
#include "src/init/v8.h"
#include "src/objects/heap-number.h"
#include "src/objects/objects-inl.h"
#include "src/utils/ostreams.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

const float qnan_f = std::numeric_limits<float>::quiet_NaN();
const float snan_f = std::numeric_limits<float>::signaling_NaN();
const double qnan_d = std::numeric_limits<double>::quiet_NaN();
const double snan_d = std::numeric_limits<double>::signaling_NaN();

const float min_f = std::numeric_limits<float>::min();
const float max_f = std::numeric_limits<float>::max();
const double min_d = std::numeric_limits<double>::min();
const double max_d = std::numeric_limits<double>::max();

const float inf_f = std::numeric_limits<float>::infinity();
const double inf_d = std::numeric_limits<double>::infinity();
const float minf_f = -inf_f;
const double minf_d = -inf_d;

#define ERROR_CODE 1
#define SUCCESS_CODE 0

typedef union {
  int32_t i32val;
  int64_t i64val;
  float fval;
  double dval;
} Param_T;

// TODO(mips64): Refine these signatures per test case.
using FV = void*(int64_t x, int64_t y, int p2, int p3, int p4);
using F1 = void*(int x, int p1, int p2, int p3, int p4);
using F3 = void*(void* p, int p1, int p2, int p3, int p4);
using F4 = void*(void* p0, void* p1, int p2, int p3, int p4);

#define __ masm->

TEST(LoadConstants) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  int64_t refConstants[64];
  int64_t result[64];

  int64_t mask = 1;
  for (int i = 0; i < 64; i++) {
    refConstants[i] = ~(mask << i);
  }

  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  __ mv(a4, a0);
  for (int i = 0; i < 64; i++) {
    // Load constant.
    __ li(a5, Operand(refConstants[i]));
    __ Sd(a5, MemOperand(a4));
    __ Add64(a4, a4, Operand(kPointerSize));
  }

  __ jr(ra);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  auto f = GeneratedCode<FV>::FromCode(*code);
  (void)f.Call(reinterpret_cast<int64_t>(result), 0, 0, 0, 0);
  // Check results.
  for (int i = 0; i < 64; i++) {
    CHECK(refConstants[i] == result[i]);
  }
}

TEST(LoadAddress) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;
  Label to_jump, skip;
  __ mov(a4, a0);

  __ Branch(&skip);
  __ bind(&to_jump);
  __ nop();
  __ nop();
  __ jr(ra);
  __ nop();
  __ bind(&skip);
  __ li(a4, Operand(masm->jump_address(&to_jump)), ADDRESS_LOAD);
  int check_size = masm->InstructionsGeneratedSince(&skip);
  // FIXME (RISCV): current li generates 8 instructions, if the sequence has
  // changed, need to adjust the CHECK_EQ value too
  CHECK_EQ(8, check_size);
  __ jr(a4);
  __ nop();
  __ stop();
  __ stop();
  __ stop();
  __ stop();
  __ stop();

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  auto f = GeneratedCode<FV>::FromCode(*code);
  (void)f.Call(0, 0, 0, 0, 0);
  // Check results.
}

TEST(jump_tables4) {
  // Similar to test-assembler-mips jump_tables1, with extra test for branch
  // trampoline required before emission of the dd table (where trampolines are
  // blocked), and proper transition to long-branch mode.
  // Regression test for v8:4294.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  const int kNumCases = 128;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];
  Label near_start, end, done;

  __ Push(ra);
  __ mv(a1, zero_reg);

  __ Branch(&end);
  __ bind(&near_start);

  // Generate slightly less than 32K instructions, which will soon require
  // trampoline for branch distance fixup.
  for (int i = 0; i < 32768 - 256; ++i) {
    __ addi(a1, a1, 1);
  }

  __ GenerateSwitchTable(a0, kNumCases,
                         [&labels](size_t i) { return labels + i; });

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ RV_li(a0, values[i]);
    __ Branch(&done);
  }

  __ bind(&done);
  __ Pop(ra);
  __ jr(ra);

  __ bind(&end);
  __ Branch(&near_start);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
#ifdef OBJECT_PRINT
  code->Print(std::cout);
#endif
  auto f = GeneratedCode<F1>::FromCode(*code);
  for (int i = 0; i < kNumCases; ++i) {
    int64_t res = reinterpret_cast<int64_t>(f.Call(i, 0, 0, 0, 0));
    ::printf("f(%d) = %" PRId64 "\n", i, res);
    CHECK_EQ(values[i], res);
  }
}

TEST(jump_tables6) {
  // Similar to test-assembler-mips jump_tables1, with extra test for branch
  // trampoline required after emission of the dd table (where trampolines are
  // blocked). This test checks if number of really generated instructions is
  // greater than number of counted instructions from code, as we are expecting
  // generation of trampoline in this case (when number of kFillInstr
  // instructions is close to 32K)
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  const int kSwitchTableCases = 40;

  const int kMaxBranchOffset = Assembler::kMaxBranchOffset;
  const int kTrampolineSlotsSize = Assembler::kTrampolineSlotsSize;
  const int kSwitchTablePrologueSize = MacroAssembler::kSwitchTablePrologueSize;

  const int kMaxOffsetForTrampolineStart =
      kMaxBranchOffset - 16 * kTrampolineSlotsSize;
  const int kFillInstr = (kMaxOffsetForTrampolineStart / kInstrSize) -
                         (kSwitchTablePrologueSize + 2 * kSwitchTableCases) -
                         20;

  int values[kSwitchTableCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kSwitchTableCases];
  Label near_start, end, done;

  __ Push(ra);
  __ mv(a1, zero_reg);

  int offs1 = masm->pc_offset();
  int gen_insn = 0;

  __ Branch(&end);
  gen_insn += 1;
  __ bind(&near_start);

  // Generate slightly less than 32K instructions, which will soon require
  // trampoline for branch distance fixup.
  for (int i = 0; i < kFillInstr; ++i) {
    __ addi(a1, a1, 1);
  }
  gen_insn += kFillInstr;

  __ GenerateSwitchTable(a0, kSwitchTableCases,
                         [&labels](size_t i) { return labels + i; });
  gen_insn += (kSwitchTablePrologueSize + 2 * kSwitchTableCases);

  for (int i = 0; i < kSwitchTableCases; ++i) {
    __ bind(&labels[i]);
    __ li(a0, Operand(values[i]));
    __ Branch(&done);
  }
  gen_insn += 3 * kSwitchTableCases;

  // If offset from here to first branch instr is greater than max allowed
  // offset for trampoline ...
  CHECK_LT(kMaxOffsetForTrampolineStart, masm->pc_offset() - offs1);
  // ... number of generated instructions must be greater then "gen_insn",
  // as we are expecting trampoline generation
  CHECK_LT(gen_insn, (masm->pc_offset() - offs1) / kInstrSize);

  __ bind(&done);
  __ Pop(ra);
  __ jr(ra);
  __ nop();

  __ bind(&end);
  __ Branch(&near_start);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
#ifdef OBJECT_PRINT
  code->Print(std::cout);
#endif
  auto f = GeneratedCode<F1>::FromCode(*code);
  for (int i = 0; i < kSwitchTableCases; ++i) {
    int64_t res = reinterpret_cast<int64_t>(f.Call(i, 0, 0, 0, 0));
    ::printf("f(%d) = %" PRId64 "\n", i, res);
    CHECK_EQ(values[i], res);
  }
}

static uint64_t run_lsa(uint32_t rt, uint32_t rs, int8_t sa) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  __ Lsa32(a0, a0, a1, sa);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assembler.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  auto f = GeneratedCode<F1>::FromCode(*code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(rt, rs, 0, 0, 0));

  return res;
}

TEST(Lsa32) {
  CcTest::InitializeVM();
  struct TestCaseLsa {
    int32_t rt;
    int32_t rs;
    uint8_t sa;
    uint64_t expected_res;
  };

  struct TestCaseLsa tc[] = {// rt, rs, sa, expected_res
                             {0x4, 0x1, 1, 0x6},
                             {0x4, 0x1, 2, 0x8},
                             {0x4, 0x1, 3, 0xC},
                             {0x4, 0x1, 4, 0x14},
                             {0x4, 0x1, 5, 0x24},
                             {0x0, 0x1, 1, 0x2},
                             {0x0, 0x1, 2, 0x4},
                             {0x0, 0x1, 3, 0x8},
                             {0x0, 0x1, 4, 0x10},
                             {0x0, 0x1, 5, 0x20},
                             {0x4, 0x0, 1, 0x4},
                             {0x4, 0x0, 2, 0x4},
                             {0x4, 0x0, 3, 0x4},
                             {0x4, 0x0, 4, 0x4},
                             {0x4, 0x0, 5, 0x4},

                             // Shift overflow.
                             {0x4, INT32_MAX, 1, 0x2},
                             {0x4, INT32_MAX >> 1, 2, 0x0},
                             {0x4, INT32_MAX >> 2, 3, 0xFFFFFFFFFFFFFFFC},
                             {0x4, INT32_MAX >> 3, 4, 0xFFFFFFFFFFFFFFF4},
                             {0x4, INT32_MAX >> 4, 5, 0xFFFFFFFFFFFFFFE4},

                             // Signed addition overflow.
                             {INT32_MAX - 1, 0x1, 1, 0xFFFFFFFF80000000},
                             {INT32_MAX - 3, 0x1, 2, 0xFFFFFFFF80000000},
                             {INT32_MAX - 7, 0x1, 3, 0xFFFFFFFF80000000},
                             {INT32_MAX - 15, 0x1, 4, 0xFFFFFFFF80000000},
                             {INT32_MAX - 31, 0x1, 5, 0xFFFFFFFF80000000},

                             // Addition overflow.
                             {-2, 0x1, 1, 0x0},
                             {-4, 0x1, 2, 0x0},
                             {-8, 0x1, 3, 0x0},
                             {-16, 0x1, 4, 0x0},
                             {-32, 0x1, 5, 0x0}};

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseLsa);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_lsa(tc[i].rt, tc[i].rs, tc[i].sa);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

static uint64_t run_dlsa(uint64_t rt, uint64_t rs, int8_t sa) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  __ Lsa64(a0, a0, a1, sa);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assembler.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  auto f = GeneratedCode<FV>::FromCode(*code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(rt, rs, 0, 0, 0));

  return res;
}

TEST(Lsa64) {
  CcTest::InitializeVM();
  struct TestCaseLsa {
    int64_t rt;
    int64_t rs;
    uint8_t sa;
    uint64_t expected_res;
  };

  struct TestCaseLsa tc[] = {// rt, rs, sa, expected_res
                             {0x4, 0x1, 1, 0x6},
                             {0x4, 0x1, 2, 0x8},
                             {0x4, 0x1, 3, 0xC},
                             {0x4, 0x1, 4, 0x14},
                             {0x4, 0x1, 5, 0x24},
                             {0x0, 0x1, 1, 0x2},
                             {0x0, 0x1, 2, 0x4},
                             {0x0, 0x1, 3, 0x8},
                             {0x0, 0x1, 4, 0x10},
                             {0x0, 0x1, 5, 0x20},
                             {0x4, 0x0, 1, 0x4},
                             {0x4, 0x0, 2, 0x4},
                             {0x4, 0x0, 3, 0x4},
                             {0x4, 0x0, 4, 0x4},
                             {0x4, 0x0, 5, 0x4},

                             // Shift overflow.
                             {0x4, INT64_MAX, 1, 0x2},
                             {0x4, INT64_MAX >> 1, 2, 0x0},
                             {0x4, INT64_MAX >> 2, 3, 0xFFFFFFFFFFFFFFFC},
                             {0x4, INT64_MAX >> 3, 4, 0xFFFFFFFFFFFFFFF4},
                             {0x4, INT64_MAX >> 4, 5, 0xFFFFFFFFFFFFFFE4},

                             // Signed addition overflow.
                             {INT64_MAX - 1, 0x1, 1, 0x8000000000000000},
                             {INT64_MAX - 3, 0x1, 2, 0x8000000000000000},
                             {INT64_MAX - 7, 0x1, 3, 0x8000000000000000},
                             {INT64_MAX - 15, 0x1, 4, 0x8000000000000000},
                             {INT64_MAX - 31, 0x1, 5, 0x8000000000000000},

                             // Addition overflow.
                             {-2, 0x1, 1, 0x0},
                             {-4, 0x1, 2, 0x0},
                             {-8, 0x1, 3, 0x0},
                             {-16, 0x1, 4, 0x0},
                             {-32, 0x1, 5, 0x0}};

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseLsa);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_dlsa(tc[i].rt, tc[i].rs, tc[i].sa);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

static const std::vector<uint32_t> cvt_trunc_uint32_test_values() {
  static const uint32_t kValues[] = {0x00000000, 0x00000001, 0x00FFFF00,
                                     0x7FFFFFFF, 0x80000000, 0x80000001,
                                     0x80FFFF00, 0x8FFFFFFF /*, 0xFFFFFFFF */};
  return std::vector<uint32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<int32_t> cvt_trunc_int32_test_values() {
  static const int32_t kValues[] = {
      static_cast<int32_t>(0x00000000), static_cast<int32_t>(0x00000001),
      static_cast<int32_t>(0x00FFFF00), static_cast<int32_t>(0x7FFFFFFF),
      static_cast<int32_t>(0x80000000), static_cast<int32_t>(0x80000001),
      static_cast<int32_t>(0x80FFFF00), static_cast<int32_t>(0x8FFFFFFF),
      static_cast<int32_t>(0xFFFFFFFF)};
  return std::vector<int32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<uint64_t> cvt_trunc_uint64_test_values() {
  static const uint64_t kValues[] = {
      0x0000000000000000, 0x0000000000000001, 0x0000FFFFFFFF0000,
      0x7FFFFFFFFFFFFFFF, 0x8000000000000000, 0x8000000000000001,
      0x8000FFFFFFFF0000, 0x8FFFFFFFFFFFFFFF /*, 0xFFFFFFFFFFFFFFFF*/};
  return std::vector<uint64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<int64_t> cvt_trunc_int64_test_values() {
  static const int64_t kValues[] = {static_cast<int64_t>(0x0000000000000000),
                                    static_cast<int64_t>(0x0000000000000001),
                                    static_cast<int64_t>(0x0000FFFFFFFF0000),
                                    // static_cast<int64_t>(0x7FFFFFFFFFFFFFFF),
                                    static_cast<int64_t>(0x8000000000000000),
                                    static_cast<int64_t>(0x8000000000000001),
                                    static_cast<int64_t>(0x8000FFFFFFFF0000),
                                    static_cast<int64_t>(0x8FFFFFFFFFFFFFFF),
                                    static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)};
  return std::vector<int64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

// Helper macros that can be used in FOR_INT32_INPUTS(i) { ... *i ... }
#define FOR_INPUTS(ctype, itype, var, test_vector)           \
  std::vector<ctype> var##_vec = test_vector();              \
  for (std::vector<ctype>::iterator var = var##_vec.begin(); \
       var != var##_vec.end(); ++var)

#define FOR_INPUTS2(ctype, itype, var, var2, test_vector)  \
  std::vector<ctype> var##_vec = test_vector();            \
  std::vector<ctype>::iterator var;                        \
  std::vector<ctype>::reverse_iterator var2;               \
  for (var = var##_vec.begin(), var2 = var##_vec.rbegin(); \
       var != var##_vec.end(); ++var, ++var2)

#define FOR_ENUM_INPUTS(var, type, test_vector) \
  FOR_INPUTS(enum type, type, var, test_vector)
#define FOR_STRUCT_INPUTS(var, type, test_vector) \
  FOR_INPUTS(struct type, type, var, test_vector)
#define FOR_INT32_INPUTS(var, test_vector) \
  FOR_INPUTS(int32_t, int32, var, test_vector)
#define FOR_INT32_INPUTS2(var, var2, test_vector) \
  FOR_INPUTS2(int32_t, int32, var, var2, test_vector)
#define FOR_INT64_INPUTS(var, test_vector) \
  FOR_INPUTS(int64_t, int64, var, test_vector)
#define FOR_UINT32_INPUTS(var, test_vector) \
  FOR_INPUTS(uint32_t, uint32, var, test_vector)
#define FOR_UINT64_INPUTS(var, test_vector) \
  FOR_INPUTS(uint64_t, uint64, var, test_vector)
#define FOR_FLOAT_INPUTS(var, test_vector) \
  FOR_INPUTS(float, float, var, test_vector)
#define FOR_DOUBLE_INPUTS(var, test_vector) \
  FOR_INPUTS(double, double, var, test_vector)

template <typename RET_TYPE, typename IN_TYPE, typename Func>
RET_TYPE run_Cvt(IN_TYPE x, Func GenerateConvertInstructionFunc) {
  DCHECK(std::is_integral<RET_TYPE>::value);

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assm;

  // Vararg f.Call() passes floating-point params via GPRs, so move arguments to
  // FPRs first
  if (std::is_same<IN_TYPE, float>::value) {
    __ fmv_w_x(fa0, a0);
  } else {
    __ fmv_d_x(fa0, a0);
  }

  GenerateConvertInstructionFunc(masm);
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  // deal w/ passing floating-point parameters to f.Call
  using IIN_TYPE =
      typename std::conditional<sizeof(IN_TYPE) == 4, int32_t, int64_t>::type;
  auto f = GeneratedCode<RET_TYPE(IIN_TYPE)>::FromCode(*code);

  Param_T t;
  memset(&t, 0, sizeof(t));
  if (std::is_same<IN_TYPE, float>::value) {
    t.fval = x;
    return f.Call(t.i32val);
  } else if (std::is_same<IN_TYPE, double>::value) {
    t.dval = x;
    return f.Call(t.i64val);
  } else if (std::is_integral<IN_TYPE>::value) {
    if (sizeof(IN_TYPE) <= 4) {
      t.i32val = x;
      return f.Call(t.i32val);
    } else {
      t.i64val = x;
      return f.Call(t.i64val);
    }
  } else {
    UNREACHABLE();
  }
}

TEST(Cvt_s_uw_Trunc_uw_s) {
  CcTest::InitializeVM();
  FOR_UINT32_INPUTS(i, cvt_trunc_uint32_test_values) {
    uint32_t input = *i;
    auto fn = [](MacroAssembler* masm) {
      __ Cvt_s_uw(fa0, a0);
      __ Trunc_uw_s(a0, fa0);
    };
    // some integers cannot be represented precisely in float,  input may
    // not directly match the return value of run_Cvt
    CHECK_EQ(static_cast<uint32_t>(static_cast<float>(input)),
             run_Cvt<uint32_t>(input, fn));
  }
}

TEST(Cvt_s_ul_Trunc_ul_s) {
  CcTest::InitializeVM();
  FOR_UINT64_INPUTS(i, cvt_trunc_uint64_test_values) {
    uint64_t input = *i;
    auto fn = [](MacroAssembler* masm) {
      __ Cvt_s_ul(fa0, a0);
      __ Trunc_ul_s(a0, fa0);
    };
    CHECK_EQ(static_cast<uint64_t>(static_cast<float>(input)),
             run_Cvt<uint64_t>(input, fn));
  }
}

TEST(Cvt_d_ul_Trunc_ul_d) {
  CcTest::InitializeVM();
  FOR_UINT64_INPUTS(i, cvt_trunc_uint64_test_values) {
    uint64_t input = *i;
    auto fn = [](MacroAssembler* masm) {
      __ Cvt_d_ul(fa0, a0);
      __ Trunc_ul_d(a0, fa0);
    };
    CHECK_EQ(static_cast<uint64_t>(static_cast<double>(input)),
             run_Cvt<uint64_t>(input, fn));
  }
}

TEST(cvt_d_l_Trunc_l_d) {
  CcTest::InitializeVM();
  FOR_INT64_INPUTS(i, cvt_trunc_int64_test_values) {
    int64_t input = *i;
    auto fn = [](MacroAssembler* masm) {
      __ fcvt_d_l(fa0, a0);
      __ Trunc_l_d(a0, fa0);
    };
    CHECK_EQ(static_cast<int64_t>(static_cast<double>(input)),
             run_Cvt<int64_t>(input, fn));
  }
}

TEST(cvt_d_w_Trunc_w_d) {
  CcTest::InitializeVM();
  FOR_INT32_INPUTS(i, cvt_trunc_int32_test_values) {
    int32_t input = *i;
    auto fn = [](MacroAssembler* masm) {
      __ fcvt_d_w(fa0, a0);
      __ Trunc_w_d(a0, fa0);
    };
    CHECK_EQ(static_cast<int32_t>(static_cast<double>(input)),
             run_Cvt<int32_t>(input, fn));
  }
}

static const std::vector<int64_t> overflow_int64_test_values() {
  static const int64_t kValues[] = {static_cast<int64_t>(0xF000000000000000),
                                    static_cast<int64_t>(0x0000000000000001),
                                    static_cast<int64_t>(0xFF00000000000000),
                                    static_cast<int64_t>(0x0000F00111111110),
                                    static_cast<int64_t>(0x0F00001000000000),
                                    static_cast<int64_t>(0x991234AB12A96731),
                                    static_cast<int64_t>(0xB0FFFF0F0F0F0F01),
                                    static_cast<int64_t>(0x00006FFFFFFFFFFF),
                                    static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)};
  return std::vector<int64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

TEST(OverflowInstructions) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  struct T {
    int64_t lhs;
    int64_t rhs;
    int64_t output_add;
    int64_t output_add2;
    int64_t output_sub;
    int64_t output_sub2;
    int64_t output_mul;
    int64_t output_mul2;
    int64_t overflow_add;
    int64_t overflow_add2;
    int64_t overflow_sub;
    int64_t overflow_sub2;
    int64_t overflow_mul;
    int64_t overflow_mul2;
  };
  T t;

  FOR_INT64_INPUTS(i, overflow_int64_test_values) {
    FOR_INT64_INPUTS(j, overflow_int64_test_values) {
      int64_t ii = *i;
      int64_t jj = *j;
      int64_t expected_add, expected_sub;
      int32_t ii32 = static_cast<int32_t>(ii);
      int32_t jj32 = static_cast<int32_t>(jj);
      int32_t expected_mul;
      int64_t expected_add_ovf, expected_sub_ovf, expected_mul_ovf;
      MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
      MacroAssembler* masm = &assembler;

      __ Ld(t0, MemOperand(a0, offsetof(T, lhs)));
      __ Ld(t1, MemOperand(a0, offsetof(T, rhs)));

      __ AddOverflow64(t2, t0, Operand(t1), a1);
      __ Sd(t2, MemOperand(a0, offsetof(T, output_add)));
      __ Sd(a1, MemOperand(a0, offsetof(T, overflow_add)));
      __ mov(a1, zero_reg);
      __ AddOverflow64(t0, t0, Operand(t1), a1);
      __ Sd(t0, MemOperand(a0, offsetof(T, output_add2)));
      __ Sd(a1, MemOperand(a0, offsetof(T, overflow_add2)));

      __ Ld(t0, MemOperand(a0, offsetof(T, lhs)));
      __ Ld(t1, MemOperand(a0, offsetof(T, rhs)));

      __ SubOverflow64(t2, t0, Operand(t1), a1);
      __ Sd(t2, MemOperand(a0, offsetof(T, output_sub)));
      __ Sd(a1, MemOperand(a0, offsetof(T, overflow_sub)));
      __ mov(a1, zero_reg);
      __ SubOverflow64(t0, t0, Operand(t1), a1);
      __ Sd(t0, MemOperand(a0, offsetof(T, output_sub2)));
      __ Sd(a1, MemOperand(a0, offsetof(T, overflow_sub2)));

      __ Ld(t0, MemOperand(a0, offsetof(T, lhs)));
      __ Ld(t1, MemOperand(a0, offsetof(T, rhs)));
      __ slliw(t0, t0, 0);
      __ slliw(t1, t1, 0);
      __ MulOverflow32(t2, t0, Operand(t1), a1);
      __ Sd(t2, MemOperand(a0, offsetof(T, output_mul)));
      __ Sd(a1, MemOperand(a0, offsetof(T, overflow_mul)));
      __ mov(a1, zero_reg);
      __ MulOverflow32(t0, t0, Operand(t1), a1);
      __ Sd(t0, MemOperand(a0, offsetof(T, output_mul2)));
      __ Sd(a1, MemOperand(a0, offsetof(T, overflow_mul2)));

      __ jr(ra);
      __ nop();

      CodeDesc desc;
      masm->GetCode(isolate, &desc);
      Handle<Code> code =
          Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
      auto f = GeneratedCode<F3>::FromCode(*code);
      t.lhs = ii;
      t.rhs = jj;
      f.Call(&t, 0, 0, 0, 0);

      expected_add_ovf = base::bits::SignedAddOverflow64(ii, jj, &expected_add);
      expected_sub_ovf = base::bits::SignedSubOverflow64(ii, jj, &expected_sub);
      expected_mul_ovf =
          base::bits::SignedMulOverflow32(ii32, jj32, &expected_mul);

      CHECK_EQ(expected_add_ovf, t.overflow_add < 0);
      CHECK_EQ(expected_sub_ovf, t.overflow_sub < 0);
      CHECK_EQ(expected_mul_ovf, t.overflow_mul != 0);

      CHECK_EQ(t.overflow_add, t.overflow_add2);
      CHECK_EQ(t.overflow_sub, t.overflow_sub2);
      CHECK_EQ(t.overflow_mul, t.overflow_mul2);

      CHECK_EQ(expected_add, t.output_add);
      CHECK_EQ(expected_add, t.output_add2);
      CHECK_EQ(expected_sub, t.output_sub);
      CHECK_EQ(expected_sub, t.output_sub2);
      if (!expected_mul_ovf) {
        CHECK_EQ(expected_mul, t.output_mul);
        CHECK_EQ(expected_mul, t.output_mul2);
      }
    }
  }
}

TEST(min_max_nan) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  struct TestFloat {
    double a;
    double b;
    double c;
    double d;
    float e;
    float f;
    float g;
    float h;
  };

  TestFloat test;

  const int kTableLength = 13;

  double inputsa[kTableLength] = {2.0,   3.0,    -0.0,  0.0,    42.0,
                                  inf_d, minf_d, inf_d, qnan_d, 3.0,
                                  inf_d, qnan_d, qnan_d};
  double inputsb[kTableLength] = {3.0,    2.0,   0.0,    -0.0, inf_d,
                                  42.0,   inf_d, minf_d, 3.0,  qnan_d,
                                  qnan_d, inf_d, qnan_d};
  double outputsdmin[kTableLength] = {2.0,    2.0,    -0.0,   -0.0,   42.0,
                                      42.0,   minf_d, minf_d, qnan_d, qnan_d,
                                      qnan_d, qnan_d, qnan_d};
  double outputsdmax[kTableLength] = {3.0,    3.0,    0.0,   0.0,    inf_d,
                                      inf_d,  inf_d,  inf_d, qnan_d, qnan_d,
                                      qnan_d, qnan_d, qnan_d};

  float inputse[kTableLength] = {2.0,   3.0,    -0.0,  0.0,    42.0,
                                 inf_f, minf_f, inf_f, qnan_f, 3.0,
                                 inf_f, qnan_f, qnan_f};
  float inputsf[kTableLength] = {3.0,    2.0,   0.0,    -0.0, inf_f,
                                 42.0,   inf_f, minf_f, 3.0,  qnan_f,
                                 qnan_f, inf_f, qnan_f};
  float outputsfmin[kTableLength] = {2.0,    2.0,    -0.0,   -0.0,   42.0,
                                     42.0,   minf_f, minf_f, qnan_f, qnan_f,
                                     qnan_f, qnan_f, qnan_f};
  float outputsfmax[kTableLength] = {3.0,    3.0,    0.0,   0.0,    inf_f,
                                     inf_f,  inf_f,  inf_f, qnan_f, qnan_f,
                                     qnan_f, qnan_f, qnan_f};

  __ push(s6);
  __ InitializeRootRegister();
  __ LoadDouble(fa3, MemOperand(a0, offsetof(TestFloat, a)));
  __ LoadDouble(fa4, MemOperand(a0, offsetof(TestFloat, b)));
  __ LoadFloat(fa1, MemOperand(a0, offsetof(TestFloat, e)));
  __ LoadFloat(fa2, MemOperand(a0, offsetof(TestFloat, f)));
  __ Float64Min(fa5, fa3, fa4);
  __ Float64Max(fa6, fa3, fa4);
  __ Float32Min(fa7, fa1, fa2);
  __ Float32Max(fa0, fa1, fa2);
  __ StoreDouble(fa5, MemOperand(a0, offsetof(TestFloat, c)));
  __ StoreDouble(fa6, MemOperand(a0, offsetof(TestFloat, d)));
  __ StoreFloat(fa7, MemOperand(a0, offsetof(TestFloat, g)));
  __ StoreFloat(fa0, MemOperand(a0, offsetof(TestFloat, h)));
  __ pop(s6);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<F3>::FromCode(*code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputsa[i];
    test.b = inputsb[i];
    test.e = inputse[i];
    test.f = inputsf[i];

    f.Call(&test, 0, 0, 0, 0);

    CHECK_EQ(0, memcmp(&test.c, &outputsdmin[i], sizeof(test.c)));
    CHECK_EQ(0, memcmp(&test.d, &outputsdmax[i], sizeof(test.d)));
    CHECK_EQ(0, memcmp(&test.g, &outputsfmin[i], sizeof(test.g)));
    CHECK_EQ(0, memcmp(&test.h, &outputsfmax[i], sizeof(test.h)));
  }
}

template <typename IN_TYPE, typename Func>
bool run_Unaligned(char* memory_buffer, int32_t in_offset, int32_t out_offset,
                   IN_TYPE value, Func GenerateUnalignedInstructionFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assm;
  IN_TYPE res;

  GenerateUnalignedInstructionFunc(masm, in_offset, out_offset);
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<int32_t(char*)>::FromCode(*code);

  MemCopy(memory_buffer + in_offset, &value, sizeof(IN_TYPE));
  f.Call(memory_buffer);
  MemCopy(&res, memory_buffer + out_offset, sizeof(IN_TYPE));

  return res == value;
}

static const std::vector<uint64_t> unsigned_test_values() {
  static const uint64_t kValues[] = {
      0x2180F18A06384414, 0x000A714532102277, 0xBC1ACCCF180649F0,
      0x8000000080008000, 0x0000000000000001, 0xFFFFFFFFFFFFFFFF,
  };
  return std::vector<uint64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<int32_t> unsigned_test_offset() {
  static const int32_t kValues[] = {// value, offset
                                    -132 * KB, -21 * KB, 0, 19 * KB, 135 * KB};
  return std::vector<int32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<int32_t> unsigned_test_offset_increment() {
  static const int32_t kValues[] = {-7, -6, -5, -4, -3, -2, -1, 0,
                                    1,  2,  3,  4,  5,  6,  7};
  return std::vector<int32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

TEST(Ulh) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  FOR_UINT64_INPUTS(i, unsigned_test_values) {
    FOR_INT32_INPUTS2(j1, j2, unsigned_test_offset) {
      FOR_INT32_INPUTS2(k1, k2, unsigned_test_offset_increment) {
        uint16_t value = static_cast<uint64_t>(*i & 0xFFFF);
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        auto fn_1 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ Ulh(t0, MemOperand(a0, in_offset));
          __ Ush(t0, MemOperand(a0, out_offset));
        };
        CHECK_EQ(true, run_Unaligned<uint16_t>(buffer_middle, in_offset,
                                               out_offset, value, fn_1));

        // test when loaded value overwrites base-register of load address
        auto fn_2 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ mov(t0, a0);
          __ Ulh(a0, MemOperand(a0, in_offset));
          __ Ush(a0, MemOperand(t0, out_offset));
        };
        CHECK_EQ(true, run_Unaligned<uint16_t>(buffer_middle, in_offset,
                                               out_offset, value, fn_2));

        // test when loaded value overwrites base-register of load address
        auto fn_3 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ mov(t0, a0);
          __ Ulhu(a0, MemOperand(a0, in_offset));
          __ Ush(a0, MemOperand(t0, out_offset));
        };
        CHECK_EQ(true, run_Unaligned<uint16_t>(buffer_middle, in_offset,
                                               out_offset, value, fn_3));

        auto fn_4 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ Ulhu(t0, MemOperand(a0, in_offset));
          __ Ush(t0, MemOperand(a0, out_offset));
        };
        CHECK_EQ(true, run_Unaligned<uint16_t>(buffer_middle, in_offset,
                                               out_offset, value, fn_4));
      }
    }
  }
}

TEST(Ulh_bitextension) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  FOR_UINT64_INPUTS(i, unsigned_test_values) {
    FOR_INT32_INPUTS2(j1, j2, unsigned_test_offset) {
      FOR_INT32_INPUTS2(k1, k2, unsigned_test_offset_increment) {
        uint16_t value = static_cast<uint64_t>(*i & 0xFFFF);
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        auto fn = [](MacroAssembler* masm, int32_t in_offset,
                     int32_t out_offset) {
          Label success, fail, end, different;
          __ Ulh(t0, MemOperand(a0, in_offset));
          __ Ulhu(t1, MemOperand(a0, in_offset));
          __ Branch(&different, ne, t0, Operand(t1));

          // If signed and unsigned values are same, check
          // the upper bits to see if they are zero
          __ sraiw(t0, t0, 15);
          __ Branch(&success, eq, t0, Operand(zero_reg));
          __ Branch(&fail);

          // If signed and unsigned values are different,
          // check that the upper bits are complementary
          __ bind(&different);
          __ sraiw(t1, t1, 15);
          __ Branch(&fail, ne, t1, Operand(1));
          __ sraiw(t0, t0, 15);
          __ addiw(t0, t0, 1);
          __ Branch(&fail, ne, t0, Operand(zero_reg));
          // Fall through to success

          __ bind(&success);
          __ Ulh(t0, MemOperand(a0, in_offset));
          __ Ush(t0, MemOperand(a0, out_offset));
          __ Branch(&end);
          __ bind(&fail);
          __ Ush(zero_reg, MemOperand(a0, out_offset));
          __ bind(&end);
        };
        CHECK_EQ(true, run_Unaligned<uint16_t>(buffer_middle, in_offset,
                                               out_offset, value, fn));
      }
    }
  }
}

TEST(Ulw) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  FOR_UINT64_INPUTS(i, unsigned_test_values) {
    FOR_INT32_INPUTS2(j1, j2, unsigned_test_offset) {
      FOR_INT32_INPUTS2(k1, k2, unsigned_test_offset_increment) {
        uint32_t value = static_cast<uint32_t>(*i & 0xFFFFFFFF);
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        auto fn_1 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ Ulw(t0, MemOperand(a0, in_offset));
          __ Usw(t0, MemOperand(a0, out_offset));
        };
        CHECK_EQ(true, run_Unaligned<uint32_t>(buffer_middle, in_offset,
                                               out_offset, value, fn_1));

        // test when loaded value overwrites base-register of load address
        auto fn_2 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ mov(t0, a0);
          __ Ulw(a0, MemOperand(a0, in_offset));
          __ Usw(a0, MemOperand(t0, out_offset));
        };
        CHECK_EQ(true,
                 run_Unaligned<uint32_t>(buffer_middle, in_offset, out_offset,
                                         (uint32_t)value, fn_2));

        auto fn_3 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ Ulwu(t0, MemOperand(a0, in_offset));
          __ Usw(t0, MemOperand(a0, out_offset));
        };
        CHECK_EQ(true, run_Unaligned<uint32_t>(buffer_middle, in_offset,
                                               out_offset, value, fn_3));

        // test when loaded value overwrites base-register of load address
        auto fn_4 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ mov(t0, a0);
          __ Ulwu(a0, MemOperand(a0, in_offset));
          __ Usw(a0, MemOperand(t0, out_offset));
        };
        CHECK_EQ(true,
                 run_Unaligned<uint32_t>(buffer_middle, in_offset, out_offset,
                                         (uint32_t)value, fn_4));
      }
    }
  }
}

TEST(Ulw_extension) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  FOR_UINT64_INPUTS(i, unsigned_test_values) {
    FOR_INT32_INPUTS2(j1, j2, unsigned_test_offset) {
      FOR_INT32_INPUTS2(k1, k2, unsigned_test_offset_increment) {
        uint32_t value = static_cast<uint32_t>(*i & 0xFFFFFFFF);
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        auto fn = [](MacroAssembler* masm, int32_t in_offset,
                     int32_t out_offset) {
          Label success, fail, end, different;
          __ Ulw(t0, MemOperand(a0, in_offset));
          __ Ulwu(t1, MemOperand(a0, in_offset));
          __ Branch(&different, ne, t0, Operand(t1));

          // If signed and unsigned values are same, check
          // the upper bits to see if they are zero
          __ srai(t0, t0, 31);
          __ Branch(&success, eq, t0, Operand(zero_reg));
          __ Branch(&fail);

          // If signed and unsigned values are different,
          // check that the upper bits are complementary
          __ bind(&different);
          __ srai(t1, t1, 31);
          __ Branch(&fail, ne, t1, Operand(1));
          __ srai(t0, t0, 31);
          __ addi(t0, t0, 1);
          __ Branch(&fail, ne, t0, Operand(zero_reg));
          // Fall through to success

          __ bind(&success);
          __ Ulw(t0, MemOperand(a0, in_offset));
          __ Usw(t0, MemOperand(a0, out_offset));
          __ Branch(&end);
          __ bind(&fail);
          __ Usw(zero_reg, MemOperand(a0, out_offset));
          __ bind(&end);
        };
        CHECK_EQ(true, run_Unaligned<uint32_t>(buffer_middle, in_offset,
                                               out_offset, value, fn));
      }
    }
  }
}

TEST(Uld) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  FOR_UINT64_INPUTS(i, unsigned_test_values) {
    FOR_INT32_INPUTS2(j1, j2, unsigned_test_offset) {
      FOR_INT32_INPUTS2(k1, k2, unsigned_test_offset_increment) {
        uint64_t value = *i;
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        auto fn_1 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ Uld(t0, MemOperand(a0, in_offset));
          __ Usd(t0, MemOperand(a0, out_offset));
        };
        CHECK_EQ(true, run_Unaligned<uint64_t>(buffer_middle, in_offset,
                                               out_offset, value, fn_1));

        // test when loaded value overwrites base-register of load address
        auto fn_2 = [](MacroAssembler* masm, int32_t in_offset,
                       int32_t out_offset) {
          __ mov(t0, a0);
          __ Uld(a0, MemOperand(a0, in_offset));
          __ Usd(a0, MemOperand(t0, out_offset));
        };
        CHECK_EQ(true,
                 run_Unaligned<uint64_t>(buffer_middle, in_offset, out_offset,
                                         (uint32_t)value, fn_2));
      }
    }
  }
}

TEST(ULoadFloat) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  FOR_UINT64_INPUTS(i, unsigned_test_values) {
    FOR_INT32_INPUTS2(j1, j2, unsigned_test_offset) {
      FOR_INT32_INPUTS2(k1, k2, unsigned_test_offset_increment) {
        float value = static_cast<float>(*i & 0xFFFFFFFF);
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        auto fn = [](MacroAssembler* masm, int32_t in_offset,
                     int32_t out_offset) {
          __ ULoadFloat(fa0, MemOperand(a0, in_offset), t0);
          __ UStoreFloat(fa0, MemOperand(a0, out_offset), t0);
        };
        CHECK_EQ(true, run_Unaligned<float>(buffer_middle, in_offset,
                                            out_offset, value, fn));
      }
    }
  }
}

TEST(ULoadDouble) {
  CcTest::InitializeVM();

  static const int kBufferSize = 300 * KB;
  char memory_buffer[kBufferSize];
  char* buffer_middle = memory_buffer + (kBufferSize / 2);

  FOR_UINT64_INPUTS(i, unsigned_test_values) {
    FOR_INT32_INPUTS2(j1, j2, unsigned_test_offset) {
      FOR_INT32_INPUTS2(k1, k2, unsigned_test_offset_increment) {
        double value = static_cast<double>(*i);
        int32_t in_offset = *j1 + *k1;
        int32_t out_offset = *j2 + *k2;

        auto fn = [](MacroAssembler* masm, int32_t in_offset,
                     int32_t out_offset) {
          __ ULoadDouble(fa0, MemOperand(a0, in_offset), t0);
          __ UStoreDouble(fa0, MemOperand(a0, out_offset), t0);
        };
        CHECK_EQ(true, run_Unaligned<double>(buffer_middle, in_offset,
                                             out_offset, value, fn));
      }
    }
  }
}

static const std::vector<uint64_t> sltu_test_values() {
  static const uint64_t kValues[] = {
      0,
      1,
      0x7FFE,
      0x7FFF,
      0x8000,
      0x8001,
      0xFFFE,
      0xFFFF,
      0xFFFFFFFFFFFF7FFE,
      0xFFFFFFFFFFFF7FFF,
      0xFFFFFFFFFFFF8000,
      0xFFFFFFFFFFFF8001,
      0xFFFFFFFFFFFFFFFE,
      0xFFFFFFFFFFFFFFFF,
  };
  return std::vector<uint64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

template <typename Func>
bool run_Sltu(uint64_t rs, uint64_t rd, Func GenerateSltuInstructionFunc) {
  using F_CVT = int64_t(uint64_t x0, uint64_t x1);

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assm;

  GenerateSltuInstructionFunc(masm, rd);
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  auto f = GeneratedCode<F_CVT>::FromCode(*code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(rs, rd));
  return res == 1;
}

TEST(Sltu) {
  CcTest::InitializeVM();

  FOR_UINT64_INPUTS(i, sltu_test_values) {
    FOR_UINT64_INPUTS(j, sltu_test_values) {
      uint64_t rs = *i;
      uint64_t rd = *j;

      auto fn_1 = [](MacroAssembler* masm, uint64_t imm) {
        __ Sltu(a0, a0, Operand(imm));
      };
      CHECK_EQ(rs < rd, run_Sltu(rs, rd, fn_1));

      auto fn_2 = [](MacroAssembler* masm, uint64_t imm) {
        __ Sltu(a0, a0, a1);
      };
      CHECK_EQ(rs < rd, run_Sltu(rs, rd, fn_2));
    }
  }
}

template <typename T, typename Inputs, typename Results>
static GeneratedCode<F4> GenerateMacroFloat32MinMax(MacroAssembler* masm) {
  T a = T::from_code(4);  // f4
  T b = T::from_code(6);  // f6
  T c = T::from_code(8);  // f8

#define FLOAT_MIN_MAX(fminmax, res, x, y, res_field)        \
  __ LoadFloat(x, MemOperand(a0, offsetof(Inputs, src1_))); \
  __ LoadFloat(y, MemOperand(a0, offsetof(Inputs, src2_))); \
  __ fminmax(res, x, y);                                    \
  __ StoreFloat(a, MemOperand(a1, offsetof(Results, res_field)))

  // a = min(b, c);
  FLOAT_MIN_MAX(Float32Min, a, b, c, min_abc_);
  // a = min(a, b);
  FLOAT_MIN_MAX(Float32Min, a, a, b, min_aab_);
  // a = min(b, a);
  FLOAT_MIN_MAX(Float32Min, a, b, a, min_aba_);

  // a = max(b, c);
  FLOAT_MIN_MAX(Float32Max, a, b, c, max_abc_);
  // a = max(a, b);
  FLOAT_MIN_MAX(Float32Max, a, a, b, max_aab_);
  // a = max(b, a);
  FLOAT_MIN_MAX(Float32Max, a, b, a, max_aba_);

#undef FLOAT_MIN_MAX

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  masm->GetCode(masm->isolate(), &desc);
  Handle<Code> code =
      Factory::CodeBuilder(masm->isolate(), desc, Code::STUB).Build();
#ifdef DEBUG
  StdoutStream os;
  code->Print(os);
#endif
  return GeneratedCode<F4>::FromCode(*code);
}

TEST(macro_float_minmax_f32) {
  // Test the Float32Min and Float32Max macros.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  struct Inputs {
    float src1_;
    float src2_;
  };

  struct Results {
    // Check all register aliasing possibilities in order to exercise all
    // code-paths in the macro assembler.
    float min_abc_;
    float min_aab_;
    float min_aba_;
    float max_abc_;
    float max_aab_;
    float max_aba_;
  };

  GeneratedCode<F4> f =
      GenerateMacroFloat32MinMax<FPURegister, Inputs, Results>(masm);

#define CHECK_MINMAX(src1, src2, min, max)                                    \
  do {                                                                        \
    Inputs inputs = {src1, src2};                                             \
    Results results;                                                          \
    f.Call(&inputs, &results, 0, 0, 0);                                       \
    CHECK_EQ(bit_cast<uint32_t>(min), bit_cast<uint32_t>(results.min_abc_));  \
    CHECK_EQ(bit_cast<uint32_t>(min), bit_cast<uint32_t>(results.min_aab_));  \
    CHECK_EQ(bit_cast<uint32_t>(min), bit_cast<uint32_t>(results.min_aba_));  \
    CHECK_EQ(bit_cast<uint32_t>(max), bit_cast<uint32_t>(results.max_abc_));  \
    CHECK_EQ(bit_cast<uint32_t>(max), bit_cast<uint32_t>(results.max_aab_));  \
    CHECK_EQ(                                                                 \
        bit_cast<uint32_t>(max),                                              \
        bit_cast<uint32_t>(results.max_aba_)); /* Use a bit_cast to correctly \
                                                  identify -0.0 and NaNs. */  \
  } while (0)

  float nan_a = std::numeric_limits<float>::quiet_NaN();
  float nan_b = std::numeric_limits<float>::quiet_NaN();

  CHECK_MINMAX(1.0f, -1.0f, -1.0f, 1.0f);
  CHECK_MINMAX(-1.0f, 1.0f, -1.0f, 1.0f);
  CHECK_MINMAX(0.0f, -1.0f, -1.0f, 0.0f);
  CHECK_MINMAX(-1.0f, 0.0f, -1.0f, 0.0f);
  CHECK_MINMAX(-0.0f, -1.0f, -1.0f, -0.0f);
  CHECK_MINMAX(-1.0f, -0.0f, -1.0f, -0.0f);
  CHECK_MINMAX(0.0f, 1.0f, 0.0f, 1.0f);
  CHECK_MINMAX(1.0f, 0.0f, 0.0f, 1.0f);

  CHECK_MINMAX(0.0f, 0.0f, 0.0f, 0.0f);
  CHECK_MINMAX(-0.0f, -0.0f, -0.0f, -0.0f);
  CHECK_MINMAX(-0.0f, 0.0f, -0.0f, 0.0f);
  CHECK_MINMAX(0.0f, -0.0f, -0.0f, 0.0f);

  CHECK_MINMAX(0.0f, nan_a, nan_a, nan_a);
  CHECK_MINMAX(nan_a, 0.0f, nan_a, nan_a);
  CHECK_MINMAX(nan_a, nan_b, nan_a, nan_a);
  CHECK_MINMAX(nan_b, nan_a, nan_b, nan_b);

#undef CHECK_MINMAX
}

template <typename T, typename Inputs, typename Results>
static GeneratedCode<F4> GenerateMacroFloat64MinMax(MacroAssembler* masm) {
  T a = T::from_code(4);  // f4
  T b = T::from_code(6);  // f6
  T c = T::from_code(8);  // f8

#define FLOAT_MIN_MAX(fminmax, res, x, y, res_field)         \
  __ LoadDouble(x, MemOperand(a0, offsetof(Inputs, src1_))); \
  __ LoadDouble(y, MemOperand(a0, offsetof(Inputs, src2_))); \
  __ fminmax(res, x, y);                                     \
  __ StoreDouble(a, MemOperand(a1, offsetof(Results, res_field)))

  // a = min(b, c);
  FLOAT_MIN_MAX(Float64Min, a, b, c, min_abc_);
  // a = min(a, b);
  FLOAT_MIN_MAX(Float64Min, a, a, b, min_aab_);
  // a = min(b, a);
  FLOAT_MIN_MAX(Float64Min, a, b, a, min_aba_);

  // a = max(b, c);
  FLOAT_MIN_MAX(Float64Max, a, b, c, max_abc_);
  // a = max(a, b);
  FLOAT_MIN_MAX(Float64Max, a, a, b, max_aab_);
  // a = max(b, a);
  FLOAT_MIN_MAX(Float64Max, a, b, a, max_aba_);

#undef FLOAT_MIN_MAX

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  masm->GetCode(masm->isolate(), &desc);
  Handle<Code> code =
      Factory::CodeBuilder(masm->isolate(), desc, Code::STUB).Build();
#ifdef DEBUG
  StdoutStream os;
  code->Print(os);
#endif
  return GeneratedCode<F4>::FromCode(*code);
}

TEST(macro_float_minmax_f64) {
  // Test the Float64Min and Float64Max macros.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  struct Inputs {
    double src1_;
    double src2_;
  };

  struct Results {
    // Check all register aliasing possibilities in order to exercise all
    // code-paths in the macro assembler.
    double min_abc_;
    double min_aab_;
    double min_aba_;
    double max_abc_;
    double max_aab_;
    double max_aba_;
  };

  GeneratedCode<F4> f =
      GenerateMacroFloat64MinMax<DoubleRegister, Inputs, Results>(masm);

#define CHECK_MINMAX(src1, src2, min, max)                                   \
  do {                                                                       \
    Inputs inputs = {src1, src2};                                            \
    Results results;                                                         \
    f.Call(&inputs, &results, 0, 0, 0);                                      \
    CHECK_EQ(bit_cast<uint64_t>(min), bit_cast<uint64_t>(results.min_abc_)); \
    CHECK_EQ(bit_cast<uint64_t>(min), bit_cast<uint64_t>(results.min_aab_)); \
    CHECK_EQ(bit_cast<uint64_t>(min), bit_cast<uint64_t>(results.min_aba_)); \
    CHECK_EQ(bit_cast<uint64_t>(max), bit_cast<uint64_t>(results.max_abc_)); \
    CHECK_EQ(bit_cast<uint64_t>(max), bit_cast<uint64_t>(results.max_aab_)); \
    CHECK_EQ(bit_cast<uint64_t>(max), bit_cast<uint64_t>(results.max_aba_)); \
    /* Use a bit_cast to correctly identify -0.0 and NaNs. */                \
  } while (0)

  double nan_a = qnan_d;
  double nan_b = qnan_d;

  CHECK_MINMAX(1.0, -1.0, -1.0, 1.0);
  CHECK_MINMAX(-1.0, 1.0, -1.0, 1.0);
  CHECK_MINMAX(0.0, -1.0, -1.0, 0.0);
  CHECK_MINMAX(-1.0, 0.0, -1.0, 0.0);
  CHECK_MINMAX(-0.0, -1.0, -1.0, -0.0);
  CHECK_MINMAX(-1.0, -0.0, -1.0, -0.0);
  CHECK_MINMAX(0.0, 1.0, 0.0, 1.0);
  CHECK_MINMAX(1.0, 0.0, 0.0, 1.0);

  CHECK_MINMAX(0.0, 0.0, 0.0, 0.0);
  CHECK_MINMAX(-0.0, -0.0, -0.0, -0.0);
  CHECK_MINMAX(-0.0, 0.0, -0.0, 0.0);
  CHECK_MINMAX(0.0, -0.0, -0.0, 0.0);

  CHECK_MINMAX(0.0, nan_a, nan_a, nan_a);
  CHECK_MINMAX(nan_a, 0.0, nan_a, nan_a);
  CHECK_MINMAX(nan_a, nan_b, nan_a, nan_a);
  CHECK_MINMAX(nan_b, nan_a, nan_b, nan_b);

#undef CHECK_MINMAX
}

template <typename IN_TYPE, typename Func>
int32_t run_CompareF(IN_TYPE x1, IN_TYPE x2, bool expected_res,
                     Func CompareGenerator) {
  DCHECK(std::is_floating_point<IN_TYPE>::value);

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assm;

  Label error, done;

  // Vararg f.Call() passes floating-point params via GPRs, so move arguments to
  // FPRs first
  if (std::is_same<IN_TYPE, float>::value) {
    __ fmv_w_x(fa0, a0);
    __ fmv_w_x(fa1, a1);
  } else {
    __ fmv_d_x(fa0, a0);
    __ fmv_d_x(fa1, a1);
  }

  // Generate actual compare instruction, compare result in a1
  CompareGenerator(masm);

  __ RV_li(a0, SUCCESS_CODE);

  if (expected_res) {
    // jump to done
    __ BranchTrueF(a1, &done);
  } else {
    // jump to done
    __ BranchFalseF(a1, &done);
  }
  // error path
  __ RV_li(a0, ERROR_CODE);

  __ bind(&done);
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  // deal w/ passing floating-point parameters to f.Call
  using IIN_TYPE =
      typename std::conditional<sizeof(IN_TYPE) == 4, int32_t, int64_t>::type;
  auto f = GeneratedCode<int32_t(IIN_TYPE, IIN_TYPE)>::FromCode(*code);

  Param_T t[2];
  memset(&t, 0, sizeof(t));
  if (std::is_same<IN_TYPE, float>::value) {
    t[0].fval = x1;
    t[1].fval = x2;
    return f.Call(t[0].i32val, t[1].i32val);
  } else {
    t[0].dval = x1;
    t[1].dval = x2;
    return f.Call(t[0].i64val, t[1].i64val);
  }
}

// FIXME (RISCV): add tests for NaN, infinity, etc
static const std::vector<float> compare_float_test_values() {
  static const float kValues[] = {0.0f,  -0.0f,  100.23f, -1034.78f, max_f,
                                  min_f, qnan_f, inf_f,   -inf_f};
  return std::vector<float>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<double> compare_double_test_values() {
  static const float kValues[] = {0.0,   -0.0,   100.23, -1034.78, max_d,
                                  min_d, qnan_d, inf_d,  -inf_d};
  return std::vector<double>(&kValues[0], &kValues[arraysize(kValues)]);
}

template <typename T>
static bool Compare(T input1, T input2, FPUCondition cond) {
  if (std::isnan(input1) || std::isnan(input2)) return false;

  switch (cond) {
    case EQ:  // Equal.
      return (input1 == input2);
    case LT:  // Ordered or Less Than, on Mips release >= 6.
      return (input1 < input2);
    case LE:  // Ordered or Less Than or Equal, on Mips release >= 6.
      return (input1 <= input2);
    default:
      UNREACHABLE();
  }
}

static void FCompare32Helper(FPUCondition cond) {
  FOR_FLOAT_INPUTS(i, compare_float_test_values) {
    FOR_FLOAT_INPUTS(j, compare_float_test_values) {
      auto input1 = *i;
      auto input2 = *j;
      bool comp_res = Compare(input1, input2, cond);
      auto fn = [cond](MacroAssembler* masm) {
        __ CompareF32(a1, cond, fa0, fa1);
      };
      CHECK_EQ(SUCCESS_CODE, run_CompareF(input1, input2, comp_res, fn));
    }
  }
}

static void FCompare64Helper(FPUCondition cond) {
  FOR_DOUBLE_INPUTS(i, compare_double_test_values) {
    FOR_DOUBLE_INPUTS(j, compare_double_test_values) {
      auto input1 = *i;
      auto input2 = *j;
      bool comp_res = Compare(input1, input2, cond);
      auto fn = [cond](MacroAssembler* masm) {
        __ CompareF64(a1, cond, fa0, fa1);
      };
      CHECK_EQ(SUCCESS_CODE, run_CompareF(input1, input2, comp_res, fn));
    }
  }
}

TEST(FCompare32_Branch) {
  CcTest::InitializeVM();

  FCompare32Helper(EQ);
  FCompare32Helper(LT);
  FCompare32Helper(LE);

  // test CompareIsNanF32: return true if any operand isnan
  auto fn = [](MacroAssembler* masm) { __ CompareIsNanF32(a1, fa0, fa1); };
  CHECK_EQ(SUCCESS_CODE, run_CompareF(1023.01f, -100.23f, false, fn));
  CHECK_EQ(SUCCESS_CODE, run_CompareF(1023.01f, snan_f, true, fn));
  CHECK_EQ(SUCCESS_CODE, run_CompareF(snan_f, -100.23f, true, fn));
  CHECK_EQ(SUCCESS_CODE, run_CompareF(snan_f, qnan_f, true, fn));
}

TEST(FCompare64_Branch) {
  CcTest::InitializeVM();
  FCompare64Helper(EQ);
  FCompare64Helper(LT);
  FCompare64Helper(LE);

  // test CompareIsNanF64: return true if any operand isnan
  auto fn = [](MacroAssembler* masm) { __ CompareIsNanF64(a1, fa0, fa1); };
  CHECK_EQ(SUCCESS_CODE, run_CompareF(1023.01, -100.23, false, fn));
  CHECK_EQ(SUCCESS_CODE, run_CompareF(1023.01, snan_d, true, fn));
  CHECK_EQ(SUCCESS_CODE, run_CompareF(snan_d, -100.23, true, fn));
  CHECK_EQ(SUCCESS_CODE, run_CompareF(snan_d, qnan_d, true, fn));
}

static const std::vector<uint32_t> cltz_uint32_test_values() {
  static const uint32_t kValues[] = {0x00000001, 0x00FFFF00, 0x7FFBD100,
                                     0x00123400, 0x0000FF10, 0x20FFFF00,
                                     0x8FFFFFFF, 0xFFFFFFFF};
  return std::vector<uint32_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

static const std::vector<uint64_t> cltz_uint64_test_values() {
  static const uint64_t kValues[] = {0x00000001'10002300, 0x00FFFF00'00000000,
                                     0x100001AB'7FFBD100, 0xF00000F0'00123400,
                                     0x00000001'0000FF10, 0x0AB10020'FFFF0000,
                                     0x000000FF'8FFFFFFF, 0xFFFFFFFF'FFFFFFFF};
  return std::vector<uint64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

TEST(Clz32) {
  CcTest::InitializeVM();
  FOR_UINT32_INPUTS(i, cltz_uint32_test_values) {
    uint32_t input = *i;
    auto fn = [](MacroAssembler* masm) { __ Clz32(a0, a0); };
    CHECK_EQ(__builtin_clz(input), run_Cvt<int>(input, fn));
  }
}

TEST(Ctz32) {
  CcTest::InitializeVM();
  FOR_UINT32_INPUTS(i, cltz_uint32_test_values) {
    uint32_t input = *i;
    auto fn = [](MacroAssembler* masm) { __ Ctz32(a0, a0); };
    CHECK_EQ(__builtin_ctz(input), run_Cvt<int>(input, fn));
  }
}

TEST(Clz64) {
  CcTest::InitializeVM();
  FOR_UINT64_INPUTS(i, cltz_uint64_test_values) {
    uint64_t input = *i;
    auto fn = [](MacroAssembler* masm) { __ Clz64(a0, a0); };
    CHECK_EQ(__builtin_clzll(input), run_Cvt<int>(input, fn));
  }
}

TEST(Ctz64) {
  CcTest::InitializeVM();
  FOR_UINT64_INPUTS(i, cltz_uint64_test_values) {
    uint64_t input = *i;
    auto fn = [](MacroAssembler* masm) { __ Ctz64(a0, a0); };
    CHECK_EQ(__builtin_ctzll(input), run_Cvt<int>(input, fn));
  }
}

TEST(ByteSwap) {
  CcTest::InitializeVM();
  auto fn0 = [](MacroAssembler* masm) { __ ByteSwap(a0, a0, 4); };
  CHECK_EQ((int32_t)0x89ab'cdef, run_Cvt<int32_t>(0xefcd'ab89, fn0));

  auto fn1 = [](MacroAssembler* masm) { __ ByteSwap(a0, a0, 8); };
  CHECK_EQ((int64_t)0x0123'4567'89ab'cdef,
           run_Cvt<int64_t>(0xefcd'ab89'6745'2301, fn1));
}

TEST(Dpopcnt) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  uint64_t in[9];
  uint64_t out[9];
  uint64_t result[9];
  uint64_t val = 0xffffffffffffffffl;
  uint64_t cnt = 64;

  for (int i = 0; i < 7; i++) {
    in[i] = val;
    out[i] = cnt;
    cnt >>= 1;
    val >>= cnt;
  }

  in[7] = 0xaf1000000000000bl;
  out[7] = 10;
  in[8] = 0xe030000f00003000l;
  out[8] = 11;
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  __ mv(a4, a0);
  for (int i = 0; i < 7; i++) {
    // Load constant.
    __ li(a3, Operand(in[i]));
    __ Popcnt64(a5, a3);
    __ Sd(a5, MemOperand(a4));
    __ Add64(a4, a4, Operand(kPointerSize));
  }
  __ li(a3, Operand(in[7]));
  __ Popcnt64(a5, a3);
  __ Sd(a5, MemOperand(a4));
  __ Add64(a4, a4, Operand(kPointerSize));

  __ li(a3, Operand(in[8]));
  __ Popcnt64(a5, a3);
  __ Sd(a5, MemOperand(a4));
  __ Add64(a4, a4, Operand(kPointerSize));

  __ jr(ra);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  auto f = GeneratedCode<FV>::FromCode(*code);
  (void)f.Call(reinterpret_cast<int64_t>(result), 0, 0, 0, 0);
  // Check results.
  for (int i = 0; i < 9; i++) {
    CHECK(out[i] == result[i]);
  }
}

TEST(Popcnt) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  uint64_t in[8];
  uint64_t out[8];
  uint64_t result[8];
  uint64_t val = 0xffffffff;
  uint64_t cnt = 32;

  for (int i = 0; i < 6; i++) {
    in[i] = val;
    out[i] = cnt;
    cnt >>= 1;
    val >>= cnt;
  }

  in[6] = 0xaf10000b;
  out[6] = 10;
  in[7] = 0xe03f3000;
  out[7] = 11;
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes);
  MacroAssembler* masm = &assembler;

  __ mv(a4, a0);
  for (int i = 0; i < 6; i++) {
    // Load constant.
    __ li(a3, Operand(in[i]));
    __ Popcnt32(a5, a3);
    __ Sd(a5, MemOperand(a4));
    __ Add64(a4, a4, Operand(kPointerSize));
  }

  __ li(a3, Operand(in[6]));
  __ Popcnt64(a5, a3);
  __ Sd(a5, MemOperand(a4));
  __ Add64(a4, a4, Operand(kPointerSize));

  __ li(a3, Operand(in[7]));
  __ Popcnt64(a5, a3);
  __ Sd(a5, MemOperand(a4));
  __ Add64(a4, a4, Operand(kPointerSize));

  __ jr(ra);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  auto f = GeneratedCode<FV>::FromCode(*code);
  (void)f.Call(reinterpret_cast<int64_t>(result), 0, 0, 0, 0);
  // Check results.
  for (int i = 0; i < 8; i++) {
    CHECK(out[i] == result[i]);
  }
}

TEST(Move) {
  CcTest::InitializeVM();
  union {
    double dval;
    int32_t ival[2];
  } t;

  {
    auto fn = [](MacroAssembler* masm) { __ FmoveHigh(a0, fa0); };
    t.ival[0] = 256;
    t.ival[1] = -123;
    CHECK_EQ(static_cast<int64_t>(t.ival[1]), run_Cvt<int64_t>(t.dval, fn));
    t.ival[0] = 645;
    t.ival[1] = 127;
    CHECK_EQ(static_cast<int64_t>(t.ival[1]), run_Cvt<int64_t>(t.dval, fn));
  }

  {
    auto fn = [](MacroAssembler* masm) { __ FmoveLow(a0, fa0); };
    t.ival[0] = 256;
    t.ival[1] = -123;
    CHECK_EQ(static_cast<int64_t>(t.ival[0]), run_Cvt<int64_t>(t.dval, fn));
    t.ival[0] = -645;
    t.ival[1] = 127;
    CHECK_EQ(static_cast<int64_t>(t.ival[0]), run_Cvt<int64_t>(t.dval, fn));
  }
}

#undef __

}  // namespace internal
}  // namespace v8
