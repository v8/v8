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
#include <math.h>

#include "src/init/v8.h"

#include "src/base/utils/random-number-generator.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"
#include "src/utils/utils.h"

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

#define MIN_VAL_IMM12 -(1 << 11)
#define LARGE_INT_EXCEED_32_BIT 0x01C9'1075'0321'FB01LL
#define LARGE_INT_UNDER_32_BIT 0x1234'5678
#define LARGE_UINT_EXCEED_32_BIT 0xFDCB'1234'A034'5691ULL

#define PRINT_RES(res, expected_res, in_hex)                         \
  if (in_hex) std::cout << "[hex-form]" << std::hex;                 \
  std::cout << "res = " << (res) << " expected = " << (expected_res) \
            << std::endl;

typedef union {
  int32_t i32val;
  int64_t i64val;
  float fval;
  double dval;
} Param_T;

static void SetParam(Param_T* params, float val) { params->fval = val; }
static void SetParam(Param_T* params, double val) { params->dval = val; }
static void SetParam(Param_T* params, int32_t val) { params->i32val = val; }
static void SetParam(Param_T* params, int64_t val) { params->i64val = val; }

template <typename T, typename std::enable_if<
                          std::is_same<float, T>::value>::type* = nullptr>
static T GetParam(Param_T* params) {
  return params->fval;
}

template <typename T, typename std::enable_if<
                          std::is_same<double, T>::value>::type* = nullptr>
static T GetParam(Param_T* params) {
  return params->dval;
}

template <typename T, typename std::enable_if<
                          std::is_same<int32_t, T>::value>::type* = nullptr>
static T GetParam(Param_T* params) {
  return params->i32val;
}

template <typename T, typename std::enable_if<
                          std::is_same<uint32_t, T>::value>::type* = nullptr>
static T GetParam(Param_T* params) {
  return params->i32val;
}

template <typename T, typename std::enable_if<
                          std::is_same<int64_t, T>::value>::type* = nullptr>
static T GetParam(Param_T* params) {
  return params->i64val;
}

template <typename T, typename std::enable_if<
                          std::is_same<uint64_t, T>::value>::type* = nullptr>
static T GetParam(Param_T* params) {
  return params->i64val;
}

template <typename T, typename std::enable_if<sizeof(T) == 4>::type* = nullptr>
static int32_t GetGPRParam(Param_T* params) {
  return params->i32val;
}

template <typename T, typename std::enable_if<sizeof(T) == 8>::type* = nullptr>
static int64_t GetGPRParam(Param_T* params) {
  return params->i64val;
}

template <typename RETURN_T, typename OUTPUT_T>
static void ValidateResult(RETURN_T generated_res, OUTPUT_T expected_res) {
  DCHECK(sizeof(RETURN_T) == sizeof(OUTPUT_T));

  Param_T t;
  memset(&t, 0, sizeof(t));
  SetParam(&t, generated_res);
  OUTPUT_T converted_res = GetParam<OUTPUT_T>(&t);
  CHECK_EQ(converted_res, expected_res);
}

// f.Call(...) interface is implemented as varargs in V8. For varargs,
// floating-point arguments and return values are passed in GPRs, therefore
// the special handling to reinterpret floating-point as integer values when
// passed in and out of f.Call()
template <typename INPUT_T, typename OUTPUT_T, typename Func>
static void GenAndRunTest(INPUT_T input0, OUTPUT_T expected_res,
                          Func test_generator) {
  DCHECK((sizeof(INPUT_T) == 4 || sizeof(INPUT_T) == 8));

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // handle floating-point parameters
  if (std::is_same<float, INPUT_T>::value) {
    __ fmv_w_x(fa0, a0);
  } else if (std::is_same<double, INPUT_T>::value) {
    __ fmv_d_x(fa0, a0);
  }

  test_generator(assm);

  // handle floating-point result
  if (std::is_same<float, OUTPUT_T>::value) {
    __ fmv_x_w(a0, fa0);
  } else if (std::is_same<double, OUTPUT_T>::value) {
    __ fmv_x_d(a0, fa0);
  }
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  Param_T t;
  memset(&t, 0, sizeof(t));
  SetParam(&t, input0);

  using OINT_T =
      typename std::conditional<sizeof(OUTPUT_T) == 4, int32_t, int64_t>::type;
  using IINT_T =
      typename std::conditional<sizeof(INPUT_T) == 4, int32_t, int64_t>::type;

  auto f = GeneratedCode<OINT_T(IINT_T)>::FromCode(*code);
  auto res = f.Call(GetGPRParam<INPUT_T>(&t));
  ValidateResult(res, expected_res);
}

template <typename INPUT_T, typename OUTPUT_T, typename Func>
static void GenAndRunTest(INPUT_T input0, INPUT_T input1, OUTPUT_T expected_res,
                          Func test_generator) {
  DCHECK((sizeof(INPUT_T) == 4 || sizeof(INPUT_T) == 8));

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // handle floating-point parameters
  if (std::is_same<float, INPUT_T>::value) {
    __ fmv_w_x(fa0, a0);
    __ fmv_w_x(fa1, a1);
  } else if (std::is_same<double, INPUT_T>::value) {
    __ fmv_d_x(fa0, a0);
    __ fmv_d_x(fa1, a1);
  }

  test_generator(assm);

  // handle floating-point result
  if (std::is_same<float, OUTPUT_T>::value) {
    __ fmv_x_w(a0, fa0);
  } else if (std::is_same<double, OUTPUT_T>::value) {
    __ fmv_x_d(a0, fa0);
  }
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  // setup parameters (to pass floats as integers)
  Param_T t[2];
  memset(&t, 0, sizeof(t));
  SetParam(&t[0], input0);
  SetParam(&t[1], input1);

  using IINT_T =
      typename std::conditional<sizeof(INPUT_T) == 4, int32_t, int64_t>::type;
  using OINT_T =
      typename std::conditional<sizeof(OUTPUT_T) == 4, int32_t, int64_t>::type;

  auto f = GeneratedCode<OINT_T(IINT_T, IINT_T)>::FromCode(*code);
  auto res = f.Call(GetGPRParam<INPUT_T>(&t[0]), GetGPRParam<INPUT_T>(&t[1]));
  ValidateResult(res, expected_res);
}

template <typename INPUT_T, typename OUTPUT_T, typename Func>
static void GenAndRunTest(INPUT_T input0, INPUT_T input1, INPUT_T input2,
                          OUTPUT_T expected_res, Func test_generator) {
  DCHECK((sizeof(INPUT_T) == 4 || sizeof(INPUT_T) == 8));
  DCHECK(sizeof(OUTPUT_T) == sizeof(INPUT_T));

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // handle floating-point parameters
  if (std::is_same<float, INPUT_T>::value) {
    __ fmv_w_x(fa0, a0);
    __ fmv_w_x(fa1, a1);
    __ fmv_w_x(fa2, a2);
  } else if (std::is_same<double, INPUT_T>::value) {
    __ fmv_d_x(fa0, a0);
    __ fmv_d_x(fa1, a1);
    __ fmv_d_x(fa2, a2);
  }

  test_generator(assm);

  // handle floating-point result
  if (std::is_same<float, OUTPUT_T>::value) {
    __ fmv_x_w(a0, fa0);
  } else if (std::is_same<double, OUTPUT_T>::value) {
    __ fmv_x_d(a0, fa0);
  }
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  // setup parameters (in case INPUT_T is float)
  Param_T t[3];
  memset(&t, 0, sizeof(t));
  SetParam(&t[0], input0);
  SetParam(&t[1], input1);
  SetParam(&t[2], input2);

  using INT_T =
      typename std::conditional<sizeof(OUTPUT_T) == 4, int32_t, int64_t>::type;
  auto f = GeneratedCode<INT_T(INT_T, INT_T, INT_T)>::FromCode(*code);
  auto res = f.Call(GetGPRParam<INPUT_T>(&t[0]), GetGPRParam<INPUT_T>(&t[1]),
                    GetGPRParam<INPUT_T>(&t[2]));
  ValidateResult(res, expected_res);
}

template <typename T, typename Func>
static void GenAndRunTestForLoadStore(T value, Func test_generator) {
  DCHECK(sizeof(T) == 4 || sizeof(T) == 8);

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  if (std::is_same<float, T>::value) {
    __ fmv_w_x(fa0, a1);
  } else if (std::is_same<double, T>::value) {
    __ fmv_d_x(fa0, a1);
  }

  test_generator(assm);

  if (std::is_same<float, T>::value) {
    __ fmv_x_w(a0, fa0);
  } else if (std::is_same<double, T>::value) {
    __ fmv_x_d(a0, fa0);
  }
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  using INT_T =
      typename std::conditional<sizeof(T) == 4, int32_t, int64_t>::type;

  // setup parameters (to pass floats as integers)
  Param_T t;
  memset(&t, 0, sizeof(t));
  SetParam(&t, value);

  int64_t tmp = 0;
  auto f = GeneratedCode<INT_T(void* base, INT_T val)>::FromCode(*code);
  auto res = f.Call(&tmp, GetGPRParam<T>(&t));
  ValidateResult(res, value);
}

template <typename Func>
static void GenAndRunTest(int64_t expected_res, Func test_generator) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  test_generator(assm);
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  auto f = GeneratedCode<int64_t()>::FromCode(*code);
  auto res = f.Call();
  CHECK_EQ(res, expected_res);
}

#define UTEST_R2_FORM_WITH_RES(instr_name, type, rs1_val, rs2_val,     \
                               expected_res)                           \
  TEST(RISCV_UTEST_##instr_name) {                                     \
    CcTest::InitializeVM();                                            \
    auto fn = [](MacroAssembler& assm) { __ instr_name(a0, a0, a1); }; \
    GenAndRunTest<type, type>(rs1_val, rs2_val, expected_res, fn);     \
  }

#define UTEST_R1_FORM_WITH_RES(instr_name, in_type, out_type, rs1_val, \
                               expected_res)                           \
  TEST(RISCV_UTEST_##instr_name) {                                     \
    CcTest::InitializeVM();                                            \
    auto fn = [](MacroAssembler& assm) { __ instr_name(a0, a0); };     \
    GenAndRunTest<in_type, out_type>(rs1_val, expected_res, fn);       \
  }

#define UTEST_I_FORM_WITH_RES(instr_name, inout_type, rs1_val, imm12,     \
                              expected_res)                               \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    CcTest::InitializeVM();                                               \
    CHECK_EQ(is_intn(imm12, 12), true);                                   \
    auto fn = [](MacroAssembler& assm) { __ instr_name(a0, a0, imm12); }; \
    GenAndRunTest<inout_type, inout_type>(rs1_val, expected_res, fn);     \
  }

#define UTEST_LOAD_STORE(ldname, stname, value_type, value) \
  TEST(RISCV_UTEST_##stname##ldname) {                      \
    CcTest::InitializeVM();                                 \
    auto fn = [](MacroAssembler& assm) {                    \
      __ stname(a1, a0, 0);                                 \
      __ ldname(a0, a0, 0);                                 \
    };                                                      \
    GenAndRunTestForLoadStore<value_type>(value, fn);       \
  }

// Since f.Call() is implemented as vararg calls and RISCV calling convention
// passes all vararg arguments and returns (including floats) in GPRs, we have
// to move from GPR to FPR and back in all floating point tests
#define UTEST_LOAD_STORE_F(ldname, stname, value_type, store_value) \
  TEST(RISCV_UTEST_##stname##ldname) {                              \
    DCHECK(std::is_floating_point<value_type>::value);              \
                                                                    \
    CcTest::InitializeVM();                                         \
    auto fn = [](MacroAssembler& assm) {                            \
      __ stname(fa0, a0, 0);                                        \
      __ ldname(fa0, a0, 0);                                        \
    };                                                              \
    GenAndRunTestForLoadStore<value_type>(store_value, fn);         \
  }

#define UTEST_R1_FORM_WITH_RES_F(instr_name, inout_type, rs1_fval,      \
                                 expected_fres)                         \
  TEST(RISCV_UTEST_##instr_name) {                                      \
    DCHECK(std::is_floating_point<inout_type>::value);                  \
    CcTest::InitializeVM();                                             \
    auto fn = [](MacroAssembler& assm) { __ instr_name(fa0, fa0); };    \
    GenAndRunTest<inout_type, inout_type>(rs1_fval, expected_fres, fn); \
  }

#define UTEST_R2_FORM_WITH_RES_F(instr_name, inout_type, rs1_fval, rs2_fval, \
                                 expected_fres)                              \
  TEST(RISCV_UTEST_##instr_name) {                                           \
    DCHECK(std::is_floating_point<inout_type>::value);                       \
    CcTest::InitializeVM();                                                  \
    auto fn = [](MacroAssembler& assm) { __ instr_name(fa0, fa0, fa1); };    \
    GenAndRunTest<inout_type, inout_type>(rs1_fval, rs2_fval, expected_fres, \
                                          fn);                               \
  }

#define UTEST_R3_FORM_WITH_RES_F(instr_name, inout_type, rs1_fval, rs2_fval,   \
                                 rs3_fval, expected_fres)                      \
  TEST(RISCV_UTEST_##instr_name) {                                             \
    DCHECK(std::is_floating_point<inout_type>::value);                         \
    CcTest::InitializeVM();                                                    \
    auto fn = [](MacroAssembler& assm) { __ instr_name(fa0, fa0, fa1, fa2); }; \
    GenAndRunTest<inout_type, inout_type>(rs1_fval, rs2_fval, rs3_fval,        \
                                          expected_fres, fn);                  \
  }

#define UTEST_COMPARE_WITH_RES_F(instr_name, input_type, rs1_fval, rs2_fval,  \
                                 expected_res)                                \
  TEST(RISCV_UTEST_##instr_name) {                                            \
    CcTest::InitializeVM();                                                   \
    auto fn = [](MacroAssembler& assm) { __ instr_name(a0, fa0, fa1); };      \
    GenAndRunTest<input_type, int32_t>(rs1_fval, rs2_fval, expected_res, fn); \
  }

#define UTEST_CONV_F_FROM_I(instr_name, input_type, output_type, rs1_val, \
                            expected_fres)                                \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    DCHECK(std::is_integral<input_type>::value&&                          \
               std::is_floating_point<output_type>::value);               \
                                                                          \
    CcTest::InitializeVM();                                               \
    auto fn = [](MacroAssembler& assm) { __ instr_name(fa0, a0); };       \
    GenAndRunTest<input_type, output_type>(rs1_val, expected_fres, fn);   \
  }

#define UTEST_CONV_I_FROM_F(instr_name, input_type, output_type,        \
                            rounding_mode, rs1_fval, expected_res)      \
  TEST(RISCV_UTEST_##instr_name) {                                      \
    DCHECK(std::is_floating_point<input_type>::value&&                  \
               std::is_integral<output_type>::value);                   \
                                                                        \
    CcTest::InitializeVM();                                             \
    auto fn = [](MacroAssembler& assm) {                                \
      __ instr_name(a0, fa0, rounding_mode);                            \
    };                                                                  \
    GenAndRunTest<input_type, output_type>(rs1_fval, expected_res, fn); \
  }                                                                     \
                                                                        \
  TEST(RISCV_UTEST_dyn_##instr_name) {                                  \
    DCHECK(std::is_floating_point<input_type>::value&&                  \
               std::is_integral<output_type>::value);                   \
                                                                        \
    CcTest::InitializeVM();                                             \
    auto fn = [](MacroAssembler& assm) {                                \
      __ csrwi(csr_frm, rounding_mode);                                 \
      __ instr_name(a0, fa0, DYN);                                      \
    };                                                                  \
    GenAndRunTest<input_type, output_type>(rs1_fval, expected_res, fn); \
  }

#define UTEST_CONV_F_FROM_F(instr_name, input_type, output_type, rs1_val, \
                            expected_fres)                                \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    CcTest::InitializeVM();                                               \
    auto fn = [](MacroAssembler& assm) { __ instr_name(fa0, fa0); };      \
    GenAndRunTest<input_type, output_type>(rs1_val, expected_fres, fn);   \
  }

#define UTEST_CSRI(csr_reg, csr_write_val, csr_set_clear_val)               \
  TEST(RISCV_UTEST_CSRI_##csr_reg) {                                        \
    CHECK_EQ(is_uint5(csr_write_val) && is_uint5(csr_set_clear_val), true); \
                                                                            \
    CcTest::InitializeVM();                                                 \
    int64_t expected_res = 111;                                             \
    Label exit, error;                                                      \
    auto fn = [&exit, &error, expected_res](MacroAssembler& assm) {         \
      /* test csr-write and csr-read */                                     \
      __ csrwi(csr_reg, csr_write_val);                                     \
      __ csrr(a0, csr_reg);                                                 \
      __ RV_li(a1, csr_write_val);                                          \
      __ bne(a0, a1, &error);                                               \
      /* test csr_set */                                                    \
      __ csrsi(csr_reg, csr_set_clear_val);                                 \
      __ csrr(a0, csr_reg);                                                 \
      __ RV_li(a1, (csr_write_val) | (csr_set_clear_val));                  \
      __ bne(a0, a1, &error);                                               \
      /* test csr_clear */                                                  \
      __ csrci(csr_reg, csr_set_clear_val);                                 \
      __ csrr(a0, csr_reg);                                                 \
      __ RV_li(a1, (csr_write_val) & (~(csr_set_clear_val)));               \
      __ bne(a0, a1, &error);                                               \
      /* everyhing runs correctly, return 111 */                            \
      __ RV_li(a0, expected_res);                                           \
      __ j(&exit);                                                          \
                                                                            \
      __ bind(&error);                                                      \
      /* got an error, return 666 */                                        \
      __ RV_li(a0, 666);                                                    \
                                                                            \
      __ bind(&exit);                                                       \
    };                                                                      \
    GenAndRunTest(expected_res, fn);                                        \
  }

#define UTEST_CSR(csr_reg, csr_write_val, csr_set_clear_val)        \
  TEST(RISCV_UTEST_CSR_##csr_reg) {                                 \
    Label exit, error;                                              \
    int64_t expected_res = 111;                                     \
    auto fn = [&exit, &error, expected_res](MacroAssembler& assm) { \
      /* test csr-write and csr-read */                             \
      __ RV_li(t0, csr_write_val);                                  \
      __ csrw(csr_reg, t0);                                         \
      __ csrr(a0, csr_reg);                                         \
      __ RV_li(a1, csr_write_val);                                  \
      __ bne(a0, a1, &error);                                       \
      /* test csr_set */                                            \
      __ RV_li(t0, csr_set_clear_val);                              \
      __ csrs(csr_reg, t0);                                         \
      __ csrr(a0, csr_reg);                                         \
      __ RV_li(a1, (csr_write_val) | (csr_set_clear_val));          \
      __ bne(a0, a1, &error);                                       \
      /* test csr_clear */                                          \
      __ RV_li(t0, csr_set_clear_val);                              \
      __ csrc(csr_reg, t0);                                         \
      __ csrr(a0, csr_reg);                                         \
      __ RV_li(a1, (csr_write_val) & (~(csr_set_clear_val)));       \
      __ bne(a0, a1, &error);                                       \
      /* everyhing runs correctly, return 111 */                    \
      __ RV_li(a0, expected_res);                                   \
      __ j(&exit);                                                  \
                                                                    \
      __ bind(&error);                                              \
      /* got an error, return 666 */                                \
      __ RV_li(a0, 666);                                            \
                                                                    \
      __ bind(&exit);                                               \
    };                                                              \
                                                                    \
    GenAndRunTest(expected_res, fn);                                \
  }

#define UTEST_R2_FORM_WITH_OP(instr_name, inout_type, rs1_val, rs2_val, \
                              tested_op)                                \
  UTEST_R2_FORM_WITH_RES(instr_name, inout_type, rs1_val, rs2_val,      \
                         ((rs1_val)tested_op(rs2_val)))

#define UTEST_I_FORM_WITH_OP(instr_name, inout_type, rs1_val, imm12, \
                             tested_op)                              \
  UTEST_I_FORM_WITH_RES(instr_name, inout_type, rs1_val, imm12,      \
                        ((rs1_val)tested_op(imm12)))

#define UTEST_R2_FORM_WITH_OP_F(instr_name, inout_type, rs1_fval, rs2_fval, \
                                tested_op)                                  \
  UTEST_R2_FORM_WITH_RES_F(instr_name, inout_type, rs1_fval, rs2_fval,      \
                           ((rs1_fval)tested_op(rs2_fval)))

#define UTEST_COMPARE_WITH_OP_F(instr_name, input_type, rs1_fval, rs2_fval, \
                                tested_op)                                  \
  UTEST_COMPARE_WITH_RES_F(instr_name, input_type, rs1_fval, rs2_fval,      \
                           ((rs1_fval)tested_op(rs2_fval)))

// -- test load-store --
UTEST_LOAD_STORE(ld, sd, int64_t, 0xFBB10A9C12345678)
// due to sign-extension of lw
// instruction, value-to-stored must have
// its 32th least significant bit be 0
UTEST_LOAD_STORE(lw, sw, int32_t, 0x456AF894)
// set the 32th least significant bit of
// value-to-store to 1 to test
// zero-extension by lwu
UTEST_LOAD_STORE(lwu, sw, int32_t, 0x856AF894)
// due to sign-extension of lh
// instruction, value-to-stored must have
// its 16th least significant bit be 0
UTEST_LOAD_STORE(lh, sh, int32_t, 0x7894)
// set the 16th least significant bit of
// value-to-store to 1 to test
// zero-extension by lhu
UTEST_LOAD_STORE(lhu, sh, int32_t, 0xF894)
// due to sign-extension of lb
// instruction, value-to-stored must have
// its 8th least significant bit be 0
UTEST_LOAD_STORE(lb, sb, int32_t, 0x54)
// set the 8th least significant bit of
// value-to-store to 1 to test
// zero-extension by lbu
UTEST_LOAD_STORE(lbu, sb, int32_t, 0x94)

// -- arithmetic w/ immediate --
UTEST_I_FORM_WITH_OP(addi, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, +)
UTEST_I_FORM_WITH_OP(slti, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, <)
UTEST_I_FORM_WITH_OP(sltiu, int64_t, LARGE_UINT_EXCEED_32_BIT, 0x4FB, <)
UTEST_I_FORM_WITH_OP(xori, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, ^)
UTEST_I_FORM_WITH_OP(ori, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, |)
UTEST_I_FORM_WITH_OP(andi, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, &)
UTEST_I_FORM_WITH_OP(slli, int64_t, 0x1234'5678ULL, 33, <<)
UTEST_I_FORM_WITH_OP(srli, int64_t, 0x8234'5678'0000'0000ULL, 33, >>)
UTEST_I_FORM_WITH_OP(srai, int64_t, -0x1234'5678'0000'0000LL, 33, >>)

// -- arithmetic --
UTEST_R2_FORM_WITH_OP(add, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, +)
UTEST_R2_FORM_WITH_OP(sub, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, -)
UTEST_R2_FORM_WITH_OP(slt, int64_t, MIN_VAL_IMM12, LARGE_INT_EXCEED_32_BIT, <)
UTEST_R2_FORM_WITH_OP(sltu, int64_t, 0x4FB, LARGE_UINT_EXCEED_32_BIT, <)
UTEST_R2_FORM_WITH_OP(xor_, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, ^)
UTEST_R2_FORM_WITH_OP(or_, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, |)
UTEST_R2_FORM_WITH_OP(and_, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, &)
UTEST_R2_FORM_WITH_OP(sll, int64_t, 0x12345678ULL, 33, <<)
UTEST_R2_FORM_WITH_OP(srl, int64_t, 0x8234567800000000ULL, 33, >>)
UTEST_R2_FORM_WITH_OP(sra, int64_t, -0x1234'5678'0000'0000LL, 33, >>)

// -- Memory fences --
// void fence(uint8_t pred, uint8_t succ);
// void fence_tso();

// -- Environment call / break --
// void ecall();
// void ebreak();
// void unimp();

// -- CSR --
UTEST_CSRI(csr_frm, DYN, RUP)
UTEST_CSRI(csr_fflags, kInexact | kInvalidOperation, kInvalidOperation)
UTEST_CSRI(csr_fcsr, kDivideByZero | kOverflow, kUnderflow)
UTEST_CSR(csr_frm, DYN, RUP)
UTEST_CSR(csr_fflags, kInexact | kInvalidOperation, kInvalidOperation)
UTEST_CSR(csr_fcsr, kDivideByZero | kOverflow | (RDN << kFcsrFrmShift),
          kUnderflow | (RNE << kFcsrFrmShift))

// -- RV64I --
UTEST_I_FORM_WITH_OP(addiw, int32_t, LARGE_INT_UNDER_32_BIT, MIN_VAL_IMM12, +)
UTEST_I_FORM_WITH_OP(slliw, int32_t, 0x12345678U, 12, <<)
UTEST_I_FORM_WITH_OP(srliw, int32_t, 0x82345678U, 12, >>)
UTEST_I_FORM_WITH_OP(sraiw, int32_t, -123, 12, >>)

UTEST_R2_FORM_WITH_OP(addw, int32_t, LARGE_INT_UNDER_32_BIT, MIN_VAL_IMM12, +)
UTEST_R2_FORM_WITH_OP(subw, int32_t, LARGE_INT_UNDER_32_BIT, MIN_VAL_IMM12, -)
UTEST_R2_FORM_WITH_OP(sllw, int32_t, 0x12345678U, 12, <<)
UTEST_R2_FORM_WITH_OP(srlw, int32_t, 0x82345678U, 12, >>)
UTEST_R2_FORM_WITH_OP(sraw, int32_t, -123, 12, >>)

// -- RV32M Standard Extension --
UTEST_R2_FORM_WITH_OP(mul, int64_t, 0x0F945001L, MIN_VAL_IMM12, *)
UTEST_R2_FORM_WITH_RES(mulh, int64_t, 0x1234567800000000LL,
                       -0x1234'5617'0000'0000LL, 0x12345678LL * -0x1234'5617LL)
UTEST_R2_FORM_WITH_RES(mulhu, int64_t, 0x1234'5678'0000'0000ULL,
                       0xF896'7021'0000'0000ULL,
                       0x1234'5678ULL * 0xF896'7021ULL)
UTEST_R2_FORM_WITH_RES(mulhsu, int64_t, -0x1234'56780000'0000LL,
                       0xF234'5678'0000'0000ULL,
                       -0x1234'5678LL * 0xF234'5678ULL)
UTEST_R2_FORM_WITH_OP(div, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, /)
UTEST_R2_FORM_WITH_OP(divu, int64_t, LARGE_UINT_EXCEED_32_BIT, 100, /)
UTEST_R2_FORM_WITH_OP(rem, int64_t, LARGE_INT_EXCEED_32_BIT, MIN_VAL_IMM12, %)
UTEST_R2_FORM_WITH_OP(remu, int64_t, LARGE_UINT_EXCEED_32_BIT, 100, %)

// -- RV64M Standard Extension (in addition to RV32M) --
UTEST_R2_FORM_WITH_OP(mulw, int32_t, -20, 56, *)
UTEST_R2_FORM_WITH_OP(divw, int32_t, 200, -10, /)
UTEST_R2_FORM_WITH_OP(divuw, int32_t, 1000, 100, /)
UTEST_R2_FORM_WITH_OP(remw, int32_t, 1234, -91, %)
UTEST_R2_FORM_WITH_OP(remuw, int32_t, 1234, 43, %)

/*
// RV32A Standard Extension
void lr_w(bool aq, bool rl, Register rd, Register rs1);
void sc_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoswap_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoadd_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoxor_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoand_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoor_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amomin_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amomax_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amominu_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amomaxu_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);

// RV64A Standard Extension (in addition to RV32A)
void lr_d(bool aq, bool rl, Register rd, Register rs1);
void sc_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoswap_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoadd_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoxor_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoand_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amoor_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amomin_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amomax_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amominu_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void amomaxu_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
*/

// -- RV32F Standard Extension --
UTEST_LOAD_STORE_F(flw, fsw, float, -2345.678f)
UTEST_R2_FORM_WITH_OP_F(fadd_s, float, -1012.01f, 3456.13f, +)
UTEST_R2_FORM_WITH_OP_F(fsub_s, float, -1012.01f, 3456.13f, -)
UTEST_R2_FORM_WITH_OP_F(fmul_s, float, -10.01f, 56.13f, *)
UTEST_R2_FORM_WITH_OP_F(fdiv_s, float, -10.01f, 34.13f, /)
UTEST_R1_FORM_WITH_RES_F(fsqrt_s, float, 34.13f, sqrtf(34.13f))
UTEST_R2_FORM_WITH_RES_F(fmin_s, float, -1012.0f, 3456.13f, -1012.0f)
UTEST_R2_FORM_WITH_RES_F(fmax_s, float, -1012.0f, 3456.13f, 3456.13f)
UTEST_R3_FORM_WITH_RES_F(fmadd_s, float, 67.56f, -1012.01f, 3456.13f,
                         (67.56f * (-1012.01f) + 3456.13f))
UTEST_R3_FORM_WITH_RES_F(fmsub_s, float, 67.56f, -1012.01f, 3456.13f,
                         (67.56f * (-1012.01f) - 3456.13f))
UTEST_R3_FORM_WITH_RES_F(fnmsub_s, float, 67.56f, -1012.01f, 3456.13f,
                         (-(67.56f * (-1012.01f)) + 3456.13f))
UTEST_R3_FORM_WITH_RES_F(fnmadd_s, float, 67.56f, -1012.01f, 3456.13f,
                         (-(67.56f * (-1012.01f)) - 3456.13f))
UTEST_COMPARE_WITH_OP_F(feq_s, float, -3456.56, -3456.56, ==)
UTEST_COMPARE_WITH_OP_F(flt_s, float, -3456.56, -3456.56, <)
UTEST_COMPARE_WITH_OP_F(fle_s, float, -3456.56, -3456.56, <=)
UTEST_CONV_F_FROM_I(fcvt_s_w, int32_t, float, -100, (float)(-100))
UTEST_CONV_F_FROM_I(fcvt_s_wu, int32_t, float,
                    std::numeric_limits<uint32_t>::max(),
                    (float)(std::numeric_limits<uint32_t>::max()))
UTEST_CONV_I_FROM_F(fcvt_w_s, float, int32_t, RMM, -100.5f, -101)
UTEST_CONV_I_FROM_F(fcvt_wu_s, float, int32_t, RUP, 256.1f, 257)
UTEST_R2_FORM_WITH_RES_F(fsgnj_s, float, -100.0f, 200.0f, 100.0f)
UTEST_R2_FORM_WITH_RES_F(fsgnjn_s, float, 100.0f, 200.0f, -100.0f)
UTEST_R2_FORM_WITH_RES_F(fsgnjx_s, float, -100.0f, 200.0f, -100.0f)

// -- RV64F Standard Extension (in addition to RV32F) --
UTEST_LOAD_STORE_F(fld, fsd, double, -3456.678)
UTEST_R2_FORM_WITH_OP_F(fadd_d, double, -1012.01, 3456.13, +)
UTEST_R2_FORM_WITH_OP_F(fsub_d, double, -1012.01, 3456.13, -)
UTEST_R2_FORM_WITH_OP_F(fmul_d, double, -10.01, 56.13, *)
UTEST_R2_FORM_WITH_OP_F(fdiv_d, double, -10.01, 34.13, /)
UTEST_R1_FORM_WITH_RES_F(fsqrt_d, double, 34.13, std::sqrt(34.13))
UTEST_R2_FORM_WITH_RES_F(fmin_d, double, -1012.0, 3456.13, -1012.0)
UTEST_R2_FORM_WITH_RES_F(fmax_d, double, -1012.0, 3456.13, 3456.13)

UTEST_R3_FORM_WITH_RES_F(fmadd_d, double, 67.56, -1012.01, 3456.13,
                         (67.56 * (-1012.01) + 3456.13))
UTEST_R3_FORM_WITH_RES_F(fmsub_d, double, 67.56, -1012.01, 3456.13,
                         (67.56 * (-1012.01) - 3456.13))
UTEST_R3_FORM_WITH_RES_F(fnmsub_d, double, 67.56, -1012.01, 3456.13,
                         (-(67.56 * (-1012.01)) + 3456.13))
UTEST_R3_FORM_WITH_RES_F(fnmadd_d, double, 67.56, -1012.01, 3456.13,
                         (-(67.56 * (-1012.01)) - 3456.13))

UTEST_COMPARE_WITH_OP_F(feq_d, double, -3456.56, -3456.56, ==)
UTEST_COMPARE_WITH_OP_F(flt_d, double, -3456.56, -3456.56, <)
UTEST_COMPARE_WITH_OP_F(fle_d, double, -3456.56, -3456.56, <=)

UTEST_CONV_F_FROM_I(fcvt_d_w, int32_t, double, -100, -100.0)
UTEST_CONV_F_FROM_I(fcvt_d_wu, int32_t, double,
                    std::numeric_limits<uint32_t>::max(),
                    (double)(std::numeric_limits<uint32_t>::max()))
UTEST_CONV_I_FROM_F(fcvt_w_d, double, int32_t, RTZ, -100.0, -100)
UTEST_CONV_I_FROM_F(fcvt_wu_d, double, int32_t, RTZ,
                    (double)(std::numeric_limits<uint32_t>::max()),
                    std::numeric_limits<uint32_t>::max())

// -- RV64F Standard Extension (in addition to RV32F) --
UTEST_CONV_I_FROM_F(fcvt_l_s, float, int64_t, RDN, -100.5f, -101)
UTEST_CONV_I_FROM_F(fcvt_lu_s, float, int64_t, RTZ, 1000001.0f, 1000001)
UTEST_CONV_F_FROM_I(fcvt_s_l, int64_t, float, (-0x1234'5678'0000'0001LL),
                    (float)(-0x1234'5678'0000'0001LL))
UTEST_CONV_F_FROM_I(fcvt_s_lu, int64_t, float,
                    std::numeric_limits<uint64_t>::max(),
                    (float)(std::numeric_limits<uint64_t>::max()))

// -- RV32D Standard Extension --
UTEST_CONV_F_FROM_F(fcvt_s_d, double, float, 100.0, 100.0f)
UTEST_CONV_F_FROM_F(fcvt_d_s, float, double, 100.0f, 100.0)

UTEST_R2_FORM_WITH_RES_F(fsgnj_d, double, -100.0, 200.0, 100.0)
UTEST_R2_FORM_WITH_RES_F(fsgnjn_d, double, 100.0, 200.0, -100.0)
UTEST_R2_FORM_WITH_RES_F(fsgnjx_d, double, -100.0, 200.0, -100.0)

// -- RV64D Standard Extension (in addition to RV32D) --
UTEST_CONV_I_FROM_F(fcvt_l_d, double, int64_t, RNE, -100.5, -100)
UTEST_CONV_I_FROM_F(fcvt_lu_d, double, int64_t, RTZ, 2456.5, 2456)
UTEST_CONV_F_FROM_I(fcvt_d_l, int64_t, double, (-0x1234'5678'0000'0001LL),
                    (double)(-0x1234'5678'0000'0001LL))
UTEST_CONV_F_FROM_I(fcvt_d_lu, int64_t, double,
                    std::numeric_limits<uint64_t>::max(),
                    (double)(std::numeric_limits<uint64_t>::max()))

// -- Assembler Pseudo Instructions --
UTEST_R1_FORM_WITH_RES(mv, int64_t, int64_t, 0x0f5600ab123400, 0x0f5600ab123400)
UTEST_R1_FORM_WITH_RES(not_, int64_t, int64_t, 0, ~0)
UTEST_R1_FORM_WITH_RES(neg, int64_t, int64_t, 0x0f5600ab123400LL,
                       -(0x0f5600ab123400LL))
UTEST_R1_FORM_WITH_RES(negw, int32_t, int32_t, 0xab123400, -(0xab123400))
UTEST_R1_FORM_WITH_RES(sext_w, int32_t, int64_t, 0xFA01'1234,
                       0xFFFFFFFFFA011234LL)
UTEST_R1_FORM_WITH_RES(seqz, int64_t, int64_t, 20, 20 == 0)
UTEST_R1_FORM_WITH_RES(snez, int64_t, int64_t, 20, 20 != 0)
UTEST_R1_FORM_WITH_RES(sltz, int64_t, int64_t, -20, -20 < 0)
UTEST_R1_FORM_WITH_RES(sgtz, int64_t, int64_t, -20, -20 > 0)

UTEST_R1_FORM_WITH_RES_F(fmv_s, float, -23.5f, -23.5f)
UTEST_R1_FORM_WITH_RES_F(fabs_s, float, -23.5f, 23.5f)
UTEST_R1_FORM_WITH_RES_F(fneg_s, float, 23.5f, -23.5f)
UTEST_R1_FORM_WITH_RES_F(fmv_d, double, -23.5, -23.5)
UTEST_R1_FORM_WITH_RES_F(fabs_d, double, -23.5, 23.5)
UTEST_R1_FORM_WITH_RES_F(fneg_d, double, 23.5, -23.5)

// Helper macros that can be used in FOR_INT32_INPUTS(i) { ... *i ... }
#define FOR_INPUTS(ctype, var, test_vector)                  \
  std::vector<ctype> var##_vec = test_vector();              \
  for (std::vector<ctype>::iterator var = var##_vec.begin(); \
       var != var##_vec.end(); ++var)

#define FOR_INT64_INPUTS(var, test_vector) FOR_INPUTS(int64_t, var, test_vector)

static const std::vector<int64_t> li_test_values() {
  static const int64_t kValues[] = {static_cast<int64_t>(0x0000000000000000),
                                    static_cast<int64_t>(0x0000000000000001),
                                    static_cast<int64_t>(0x0000FFFFFFFF0000),
                                    static_cast<int64_t>(0x7FFFFFFFFFFFFFFF),
                                    static_cast<int64_t>(0x8000000000000000),
                                    static_cast<int64_t>(0x8000000000000001),
                                    static_cast<int64_t>(0x8000FFFFFFFF0000),
                                    static_cast<int64_t>(0x8FFFFFFFFFFFFFFF),
                                    static_cast<int64_t>(0x123456789ABCDEF1),
                                    static_cast<int64_t>(0xFFFFFFFFFFFFFFFF)};
  return std::vector<int64_t>(&kValues[0], &kValues[arraysize(kValues)]);
}

// Test LI
TEST(RISCV0) {
  CcTest::InitializeVM();

  FOR_INT64_INPUTS(i, li_test_values) {
    int64_t input = *i;
    auto fn = [input](MacroAssembler& assm) { __ RV_li(a0, input); };
    GenAndRunTest(input, fn);
  }
}

TEST(RISCV1) {
  CcTest::InitializeVM();

  Label L, C;
  auto fn = [&L, &C](MacroAssembler& assm) {
    __ mv(a1, a0);
    __ RV_li(a0, 0l);
    __ j(&C);

    __ bind(&L);
    __ add(a0, a0, a1);
    __ addi(a1, a1, -1);

    __ bind(&C);
    __ xori(a2, a1, 0);
    __ bnez(a2, &L);
  };

  int64_t input = 50;
  int64_t expected_res = 1275L;
  GenAndRunTest(input, expected_res, fn);
}

TEST(RISCV2) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label exit, error;
  int64_t expected_res = 0x31415926L;

  // ----- Test all instructions.

  // Test lui, ori, and addiw, used in the
  // li pseudo-instruction. This way we
  // can then safely load registers with
  // chosen values.
  auto fn = [&exit, &error, expected_res](MacroAssembler& assm) {
    __ ori(a4, zero_reg, 0);
    __ lui(a4, 0x12345);
    __ ori(a4, a4, 0);
    __ ori(a4, a4, 0xF0F);
    __ ori(a4, a4, 0x0F0);
    __ addiw(a5, a4, 1);
    __ addiw(a6, a5, -0x10);

    // Load values in temporary registers.
    __ RV_li(a4, 0x00000004);
    __ RV_li(a5, 0x00001234);
    __ RV_li(a6, 0x12345678);
    __ RV_li(a7, 0x7FFFFFFF);
    __ RV_li(t0, 0xFFFFFFFC);
    __ RV_li(t1, 0xFFFFEDCC);
    __ RV_li(t2, 0xEDCBA988);
    __ RV_li(t3, 0x80000000);

    __ srliw(t0, a6, 8);   // 0x00123456
    __ slliw(t0, t0, 11);  // 0x91A2B000
    __ sraiw(t0, t0, 3);   // 0xFFFFFFFF F2345600
    __ sraw(t0, t0, a4);   // 0xFFFFFFFF FF234560
    __ sllw(t0, t0, a4);   // 0xFFFFFFFF F2345600
    __ srlw(t0, t0, a4);   // 0x0F234560
    __ RV_li(t5, 0x0F234560);
    __ bne(t0, t5, &error);

    __ addw(t0, a4, a5);  // 0x00001238
    __ subw(t0, t0, a4);  // 0x00001234
    __ RV_li(t5, 0x00001234);
    __ bne(t0, t5, &error);
    __ addw(a1, a7,
            a4);  // 32bit addu result is sign-extended into 64bit reg.
    __ RV_li(t5, 0xFFFFFFFF80000003);
    __ bne(a1, t5, &error);
    __ subw(a1, t3, a4);  // 0x7FFFFFFC
    __ RV_li(t5, 0x7FFFFFFC);
    __ bne(a1, t5, &error);

    __ and_(t0, a5, a6);  // 0x0000000000001230
    __ or_(t0, t0, a5);   // 0x0000000000001234
    __ xor_(t0, t0, a6);  // 0x000000001234444C
    __ or_(t0, t0, a6);
    __ not_(t0, t0);  // 0xFFFFFFFFEDCBA983
    __ RV_li(t5, 0xFFFFFFFFEDCBA983);
    __ bne(t0, t5, &error);

    // Shift both 32bit number to left, to
    // preserve meaning of next comparison.
    __ slli(a7, a7, 32);
    __ slli(t3, t3, 32);

    __ slt(t0, t3, a7);
    __ RV_li(t5, 1);
    __ bne(t0, t5, &error);
    __ sltu(t0, t3, a7);
    __ bne(t0, zero_reg, &error);

    // Restore original values in registers.
    __ srli(a7, a7, 32);
    __ srli(t3, t3, 32);

    __ RV_li(t0, 0x7421);    // 0x00007421
    __ addi(t0, t0, -0x1);   // 0x00007420
    __ addi(t0, t0, -0x20);  // 0x00007400
    __ RV_li(t5, 0x00007400);
    __ bne(t0, t5, &error);
    __ addiw(a1, a7, 0x1);  // 0x80000000 - result is sign-extended.
    __ RV_li(t5, 0xFFFFFFFF80000000);
    __ bne(a1, t5, &error);

    __ RV_li(t5, 0x00002000);
    __ slt(t0, a5, t5);  // 0x1
    __ RV_li(t6, 0xFFFFFFFFFFFF8000);
    __ slt(t0, t0, t6);  // 0x0
    __ bne(t0, zero_reg, &error);
    __ sltu(t0, a5, t5);  // 0x1
    __ RV_li(t6, 0x00008000);
    __ sltu(t0, t0, t6);  // 0x1
    __ RV_li(t5, 1);
    __ bne(t0, t5, &error);

    __ andi(t0, a5, 0x0F0);  // 0x00000030
    __ ori(t0, t0, 0x200);   // 0x00000230
    __ xori(t0, t0, 0x3CC);  // 0x000001FC
    __ RV_li(t5, 0x000001FC);
    __ bne(t0, t5, &error);
    __ lui(a1, -519628);  // Result is sign-extended into 64bit register.
    __ RV_li(t5, 0xFFFFFFFF81234000);
    __ bne(a1, t5, &error);

    // Everything was correctly executed.
    // Load the expected result.
    __ RV_li(a0, expected_res);
    __ j(&exit);

    __ bind(&error);
    // Got an error. Return a wrong result.
    __ RV_li(a0, 666);

    __ bind(&exit);
  };
  GenAndRunTest(expected_res, fn);
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

  // Create a function that accepts &t,
  // and loads, manipulates, and stores
  // the doubles t.a ... t.f.
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Double precision floating point instructions.
  __ fld(ft0, a0, offsetof(T, a));
  __ fld(ft1, a0, offsetof(T, b));
  __ fadd_d(ft2, ft0, ft1);
  __ fsd(ft2, a0, offsetof(T, c));  // c = a + b.

  __ fmv_d(ft3, ft2);   // c
  __ fneg_d(fa0, ft1);  // -b
  __ fsub_d(ft3, ft3, fa0);
  __ fsd(ft3, a0, offsetof(T, d));  // d = c - (-b).

  __ fsd(ft0, a0, offsetof(T, b));  // b = a.

  __ RV_li(a4, 120);
  __ fcvt_d_w(ft5, a4);
  __ fmul_d(ft3, ft3, ft5);
  __ fsd(ft3, a0, offsetof(T, e));  // e = d * 120 = 1.8066e16.

  __ fdiv_d(ft4, ft3, ft0);
  __ fsd(ft4, a0, offsetof(T, f));  // f = e / a = 120.44.

  __ fsqrt_d(ft5, ft4);
  __ fsd(ft5, a0, offsetof(T, g));
  // g = sqrt(f) = 10.97451593465515908537

  __ fld(ft0, a0, offsetof(T, h));
  __ fld(ft1, a0, offsetof(T, i));
  __ fmadd_d(ft5, ft1, ft0, ft1);
  __ fsd(ft5, a0, offsetof(T, h));

  // // Single precision floating point instructions.
  __ flw(ft0, a0, offsetof(T, fa));
  __ flw(ft1, a0, offsetof(T, fb));
  __ fadd_s(ft2, ft0, ft1);
  __ fsw(ft2, a0, offsetof(T, fc));  // fc = fa + fb.

  __ fneg_s(ft3, ft1);  // -fb
  __ fsub_s(ft3, ft2, ft3);
  __ fsw(ft3, a0, offsetof(T, fd));  // fd = fc - (-fb).

  __ fsw(ft0, a0, offsetof(T, fb));  // fb = fa.

  __ RV_li(t0, 120);
  __ fcvt_s_w(ft5, t0);  // ft5 = 120.0.
  __ fmul_s(ft3, ft3, ft5);
  __ fsw(ft3, a0, offsetof(T, fe));  // fe = fd * 120

  __ fdiv_s(ft4, ft3, ft0);
  __ fsw(ft4, a0, offsetof(T, ff));  // ff = fe / fa

  __ fsqrt_s(ft5, ft4);
  __ fsw(ft5, a0, offsetof(T, fg));

  __ jr(ra);

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
  // Test moves between floating point and
  // integer registers.
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

  __ fld(ft0, a0, offsetof(T, a));
  __ fld(fa1, a0, offsetof(T, b));

  // Swap ft0 and fa1, by using 2 integer registers, a4-a5,

  __ fmv_x_d(a4, ft0);
  __ fmv_x_d(a5, fa1);

  __ fmv_d_x(fa1, a4);
  __ fmv_d_x(ft0, a5);

  // Store the swapped ft0 and fa1 back to memory.
  __ fsd(ft0, a0, offsetof(T, a));
  __ fsd(fa1, a0, offsetof(T, c));

  // Test sign extension of move operations from coprocessor.
  __ flw(ft0, a0, offsetof(T, d));
  __ fmv_x_w(a4, ft0);

  __ sd(a4, a0, offsetof(T, e));

  __ jr(ra);

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
  // Test conversions between doubles and
  // integers.
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
  __ fld(ft0, a0, offsetof(T, a));
  __ fld(ft1, a0, offsetof(T, b));
  __ lw(a4, a0, offsetof(T, i));
  __ lw(a5, a0, offsetof(T, j));

  // Convert double in ft0 to int in element i.
  __ fcvt_l_d(a6, ft0);
  __ sw(a6, a0, offsetof(T, i));

  // Convert double in ft1 to int in element j.
  __ fcvt_l_d(a7, ft1);
  __ sw(a7, a0, offsetof(T, j));

  // Convert int in original i (a4) to double in a.
  __ fcvt_d_l(fa0, a4);
  __ fsd(fa0, a0, offsetof(T, a));

  // Convert int in original j (a5) to double in b.
  __ fcvt_d_l(fa1, a5);
  __ fsd(fa1, a0, offsetof(T, b));

  __ jr(ra);

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
  __ lw(a4, a0, offsetof(T, ui));
  __ sw(a4, a0, offsetof(T, r1));

  // lh with positive data.
  __ lh(a5, a0, offsetof(T, ui));
  __ sw(a5, a0, offsetof(T, r2));

  // lh with negative data.
  __ lh(a6, a0, offsetof(T, si));
  __ sw(a6, a0, offsetof(T, r3));

  // lhu with negative data.
  __ lhu(a7, a0, offsetof(T, si));
  __ sw(a7, a0, offsetof(T, r4));

  // Lb with negative data.
  __ lb(t0, a0, offsetof(T, si));
  __ sw(t0, a0, offsetof(T, r5));

  // sh writes only 1/2 of word.
  __ RV_li(t1, 0x33333333);
  __ sw(t1, a0, offsetof(T, r6));
  __ lhu(t1, a0, offsetof(T, si));
  __ sh(t1, a0, offsetof(T, r6));

  __ jr(ra);

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

// pair.first is the F_TYPE input to test, pair.second is I_TYPE expected result
template <typename T>
static const std::vector<std::pair<T, uint64_t>> fclass_test_values() {
  static const std::pair<T, uint64_t> kValues[] = {
      std::make_pair(-std::numeric_limits<T>::infinity(), kNegativeInfinity),
      std::make_pair(-10240.56, kNegativeNormalNumber),
      std::make_pair(-(std::numeric_limits<T>::min() / 2),
                     kNegativeSubnormalNumber),
      std::make_pair(-0.0, kNegativeZero),
      std::make_pair(+0.0, kPositiveZero),
      std::make_pair((std::numeric_limits<T>::min() / 2),
                     kPositiveSubnormalNumber),
      std::make_pair(10240.56, kPositiveNormalNumber),
      std::make_pair(std::numeric_limits<T>::infinity(), kPositiveInfinity),
      std::make_pair(std::numeric_limits<T>::signaling_NaN(), kSignalingNaN),
      std::make_pair(std::numeric_limits<T>::quiet_NaN(), kQuietNaN)};
  return std::vector<std::pair<T, uint64_t>>(&kValues[0],
                                             &kValues[arraysize(kValues)]);
}

TEST(FCLASS) {
  CcTest::InitializeVM();
  {
    auto i_vec = fclass_test_values<float>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fclass_s(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }

  {
    auto i_vec = fclass_test_values<double>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fclass_d(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }
}

TEST(RISCV7) {
  // Test floating point compare and
  // branch instructions.
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

  // Create a function that accepts &t,
  // and loads, manipulates, and stores
  // the doubles t.a ... t.f.
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label neither_is_nan, less_than, outa_here;

  __ fld(ft0, a0, offsetof(T, a));
  __ fld(ft1, a0, offsetof(T, b));

  __ fclass_d(t5, ft0);
  __ fclass_d(t6, ft1);
  __ or_(t5, t5, t6);
  __ andi(t5, t5, kSignalingNaN | kQuietNaN);
  __ beq(t5, zero_reg, &neither_is_nan);
  __ sw(zero_reg, a0, offsetof(T, result));
  __ j(&outa_here);

  __ bind(&neither_is_nan);

  __ flt_d(t5, ft1, ft0);
  __ bne(t5, zero_reg, &less_than);

  __ sw(zero_reg, a0, offsetof(T, result));
  __ j(&outa_here);

  __ bind(&less_than);
  __ RV_li(a4, 1);
  __ sw(a4, a0, offsetof(T, result));  // Set true.

  // This test-case should have additional
  // tests.

  __ bind(&outa_here);

  __ jr(ra);

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

TEST(RISCV9) {
  // Test BRANCH improvements.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label exit, exit2, exit3;

  __ Branch(&exit, ge, a0, Operand(zero_reg));
  __ Branch(&exit2, ge, a0, Operand(0x00001FFF));
  __ Branch(&exit3, ge, a0, Operand(0x0001FFFF));

  __ bind(&exit);
  __ bind(&exit2);
  __ bind(&exit3);
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
  USE(code);
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
  __ set_target_value_at(static_cast<Address>(addr), 0xfedcba9876543210,
                         FLUSH_ICACHE_IF_NEEDED);
  Address res = __ target_address_at(static_cast<Address>(addr));

  CHECK_EQ(0xfedcba9876543210, res);
}

// pair.first is the F_TYPE input to test, pair.second is I_TYPE expected result
template <typename F_TYPE, typename I_TYPE>
static const std::vector<std::pair<F_TYPE, I_TYPE>> out_of_range_test_values() {
  static const std::pair<F_TYPE, I_TYPE> kValues[] = {
      std::make_pair(std::numeric_limits<F_TYPE>::quiet_NaN(),
                     std::numeric_limits<I_TYPE>::max()),
      std::make_pair(std::numeric_limits<F_TYPE>::signaling_NaN(),
                     std::numeric_limits<I_TYPE>::max()),
      std::make_pair(std::numeric_limits<F_TYPE>::infinity(),
                     std::numeric_limits<I_TYPE>::max()),
      std::make_pair(-std::numeric_limits<F_TYPE>::infinity(),
                     std::numeric_limits<I_TYPE>::min()),
      std::make_pair(
          static_cast<F_TYPE>(std::numeric_limits<I_TYPE>::max()) + 1024,
          std::numeric_limits<I_TYPE>::max()),
      std::make_pair(
          static_cast<F_TYPE>(std::numeric_limits<I_TYPE>::min()) - 1024,
          std::numeric_limits<I_TYPE>::min()),
  };
  return std::vector<std::pair<F_TYPE, I_TYPE>>(&kValues[0],
                                                &kValues[arraysize(kValues)]);
}

// Test conversion from wider to narrower types w/ out-of-range values or from
// nan, inf, -inf
TEST(OUT_OF_RANGE_CVT) {
  CcTest::InitializeVM();

  {  // test fvt_w_d
    auto i_vec = out_of_range_test_values<double, int32_t>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fcvt_w_d(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }

  {  // test fvt_w_s
    auto i_vec = out_of_range_test_values<float, int32_t>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fcvt_w_s(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }

  {  // test fvt_wu_d
    auto i_vec = out_of_range_test_values<double, uint32_t>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fcvt_wu_d(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }

  {  // test fvt_wu_s
    auto i_vec = out_of_range_test_values<float, uint32_t>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fcvt_wu_s(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }

  {  // test fvt_l_d
    auto i_vec = out_of_range_test_values<double, int64_t>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fcvt_l_d(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }

  {  // test fvt_l_s
    auto i_vec = out_of_range_test_values<float, int64_t>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fcvt_l_s(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }

  {  // test fvt_lu_d
    auto i_vec = out_of_range_test_values<double, uint64_t>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fcvt_lu_d(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }

  {  // test fvt_lu_s
    auto i_vec = out_of_range_test_values<float, uint64_t>();
    for (auto i = i_vec.begin(); i != i_vec.end(); ++i) {
      auto input = *i;
      auto fn = [](MacroAssembler& assm) { __ fcvt_lu_s(a0, fa0); };
      GenAndRunTest(input.first, input.second, fn);
    }
  }
}

#define FCMP_TEST_HELPER(F, fn, op)                                          \
  GenAndRunTest<F, int32_t>(std::numeric_limits<F>::quiet_NaN(),             \
                            static_cast<F>(1.0), false, fn);                 \
  GenAndRunTest<F, int32_t>(std::numeric_limits<F>::quiet_NaN(),             \
                            std::numeric_limits<F>::quiet_NaN(), false, fn); \
  GenAndRunTest<F, int32_t>(std::numeric_limits<F>::signaling_NaN(),         \
                            std::numeric_limits<F>::quiet_NaN(), false, fn); \
  GenAndRunTest<F, int32_t>(std::numeric_limits<F>::quiet_NaN(),             \
                            std::numeric_limits<F>::infinity(), false, fn);  \
  GenAndRunTest<F, int32_t>(std::numeric_limits<F>::infinity(),              \
                            std::numeric_limits<F>::infinity(),              \
                            (std::numeric_limits<F>::infinity()              \
                                 op std::numeric_limits<F>::infinity()),     \
                            fn);                                             \
  GenAndRunTest<F, int32_t>(-std::numeric_limits<F>::infinity(),             \
                            std::numeric_limits<F>::infinity(),              \
                            (-std::numeric_limits<F>::infinity()             \
                                 op std::numeric_limits<F>::infinity()),     \
                            fn);

TEST(F_NAN) {
  // test floating-point compare w/ NaN, +/-Inf
  CcTest::InitializeVM();

  // floating compare
  auto fn1 = [](MacroAssembler& assm) { __ feq_s(a0, fa0, fa1); };
  FCMP_TEST_HELPER(float, fn1, ==);
  auto fn2 = [](MacroAssembler& assm) { __ flt_s(a0, fa0, fa1); };
  FCMP_TEST_HELPER(float, fn2, <);
  auto fn3 = [](MacroAssembler& assm) { __ fle_s(a0, fa0, fa1); };
  FCMP_TEST_HELPER(float, fn3, <=);

  // double compare
  auto fn4 = [](MacroAssembler& assm) { __ feq_d(a0, fa0, fa1); };
  FCMP_TEST_HELPER(double, fn4, ==);
  auto fn5 = [](MacroAssembler& assm) { __ flt_d(a0, fa0, fa1); };
  FCMP_TEST_HELPER(double, fn5, <);
  auto fn6 = [](MacroAssembler& assm) { __ fle_d(a0, fa0, fa1); };
  FCMP_TEST_HELPER(double, fn6, <=);
}

TEST(jump_tables1) {
  // Test jump tables with forward jumps.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 128;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];

  __ addi(sp, sp, -8);
  __ Sd(ra, MemOperand(sp));
  __ Align(8);

  Label done;
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);

    __ auipc(ra, 0);
    __ slli(t3, a0, 3);
    __ add(t3, t3, ra);
    __ Ld(t3, MemOperand(t3, 6 * kInstrSize));
    __ jr(t3);
    __ nop();  // For 16-byte alignment
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ lui(a0, (values[i] + 0x800) >> 12);
    __ addi(a0, a0, (values[i] << 20 >> 20));
    __ j(&done);
  }

  __ bind(&done);
  __ Ld(ra, MemOperand(sp));
  __ addi(sp, sp, 8);
  __ jr(ra);

  CHECK_EQ(0, assm.UnboundLabelsCount());

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
#ifdef OBJECT_PRINT
  code->Print(std::cout);
#endif
  auto f = GeneratedCode<F1>::FromCode(*code);
  for (int i = 0; i < kNumCases; ++i) {
    int64_t res = reinterpret_cast<int64_t>(f.Call(i, 0, 0, 0, 0));
    ::printf("f(%d) = %" PRId64 "\n", i, res);
    CHECK_EQ(values[i], static_cast<int>(res));
  }
}

TEST(jump_tables2) {
  // Test jump tables with backward jumps.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 128;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];

  __ addi(sp, sp, -8);
  __ Sd(ra, MemOperand(sp));

  Label done, dispatch;
  __ j(&dispatch);

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ lui(a0, (values[i] + 0x800) >> 12);
    __ addi(a0, a0, (values[i] << 20 >> 20));
    __ j(&done);
  }

  __ Align(8);
  __ bind(&dispatch);
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);

    __ auipc(ra, 0);
    __ slli(t3, a0, 3);
    __ add(t3, t3, ra);
    __ Ld(t3, MemOperand(t3, 6 * kInstrSize));
    __ jr(t3);
    __ nop();  // For 16-byte alignment
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  __ bind(&done);
  __ Ld(ra, MemOperand(sp));
  __ addi(sp, sp, 8);
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
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

TEST(jump_tables3) {
  // Test jump tables with backward jumps and embedded heap objects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 128;
  Handle<Object> values[kNumCases];
  for (int i = 0; i < kNumCases; ++i) {
    double value = isolate->random_number_generator()->NextDouble();
    values[i] = isolate->factory()->NewHeapNumber<AllocationType::kOld>(value);
  }
  Label labels[kNumCases];
  Object obj;
  int64_t imm64;

  __ addi(sp, sp, -8);
  __ Sd(ra, MemOperand(sp));

  Label done, dispatch;
  __ j(&dispatch);

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    obj = *values[i];
    imm64 = obj.ptr();
    __ RV_li(a0, imm64);
    __ j(&done);
  }

  __ Align(8);
  __ bind(&dispatch);
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);

    __ auipc(ra, 0);
    __ slli(t3, a0, 3);
    __ add(t3, t3, ra);
    __ Ld(t3, MemOperand(t3, 6 * kInstrSize));
    __ jr(t3);
    __ nop();  // For 16-byte alignment
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  __ bind(&done);
  __ Ld(ra, MemOperand(sp));
  __ addi(sp, sp, 8);
  __ jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();
#ifdef OBJECT_PRINT
  code->Print(std::cout);
#endif
  auto f = GeneratedCode<F1>::FromCode(*code);
  for (int i = 0; i < kNumCases; ++i) {
    Handle<Object> result(
        Object(reinterpret_cast<Address>(f.Call(i, 0, 0, 0, 0))), isolate);
#ifdef OBJECT_PRINT
    ::printf("f(%d) = ", i);
    result->Print(std::cout);
    ::printf("\n");
#endif
    CHECK(values[i].is_identical_to(result));
  }
}

#undef __

}  // namespace internal
}  // namespace v8
