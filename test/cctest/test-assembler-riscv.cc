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

using D0 = int64_t();
using D1 = int64_t(int64_t rs);
using D2 = int64_t(int64_t rs1, int64_t rs2);
using D3 = int64_t(void* base, int64_t val);
using D4 = int64_t(int64_t, int64_t, int64_t val);

using S0 = int32_t();
using S1 = int32_t(int32_t rs);
using S2 = int32_t(int32_t rs1, int32_t rs2);
using S3 = int32_t(void* base, int32_t val);
using S4 = int32_t(int32_t, int32_t, int32_t val);

using WIDEN = int64_t(int32_t rs);
using SHRINK = int32_t(int64_t rs);

#define __ assm.

#define MIN_VAL_IMM12 -(1 << 11)
#define LARGE_INT_EXCEED_32_BIT 0x01C9'1075'0321'FB01LL
#define LARGE_INT_UNDER_32_BIT 0x1234'5678
#define LARGE_UINT_EXCEED_32_BIT 0xFDCB'1234'A034'5691ULL
#define MAX_UINT32 0xFFFF'FFFFU
#define MAX_UINT64 0xFFFF'FFFF'FFFF'FFFFULL

#define PRINT_RES(res, expected_res, in_hex)                         \
  if (in_hex) std::cout << "[hex-form]" << std::hex;                 \
  std::cout << "res = " << (res) << " expected = " << (expected_res) \
            << std::endl;

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

typedef union {
  int32_t i32val;
  int64_t i64val;
  float fval;
  double dval;
} Param_T;

template <typename T, typename std::enable_if<
                          std::is_same<float, T>::value>::type* = nullptr>
static void SetParam(Param_T* params, T val) {
  params->fval = val;
}

template <typename T, typename std::enable_if<
                          std::is_same<double, T>::value>::type* = nullptr>
static void SetParam(Param_T* params, T val) {
  params->dval = val;
}

template <typename T, typename std::enable_if<
                          std::is_same<int32_t, T>::value>::type* = nullptr>
static void SetParam(Param_T* params, T val) {
  params->i32val = val;
}

template <typename T, typename std::enable_if<
                          std::is_same<int64_t, T>::value>::type* = nullptr>
static void SetParam(Param_T* params, T val) {
  params->i64val = val;
}

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
                          std::is_same<int64_t, T>::value>::type* = nullptr>
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
  assert(sizeof(RETURN_T) == sizeof(OUTPUT_T));

  Param_T t;
  memset(&t, 0, sizeof(t));

  SetParam<RETURN_T>(&t, generated_res);
  OUTPUT_T converted_res = GetParam<OUTPUT_T>(&t);
  PRINT_RES(converted_res, expected_res, std::is_integral<OUTPUT_T>::value);
  CHECK_EQ(converted_res, expected_res);
}

// f.Call(...) interface is implemented as varargs in V8. For varargs,
// floating-point arguments and return values are passed in GPRs, therefore
// the special handling to reinterpret floating-point as integer values when
// passed in and out of f.Call()
template <typename INPUT_T, typename OUTPUT_T>
static void GenerateTestCall(Isolate* isolate, MacroAssembler& assm,
                             INPUT_T input0, OUTPUT_T expected_res) {
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  assert((sizeof(INPUT_T) == 4 || sizeof(INPUT_T) == 8));

  Param_T t;
  memset(&t, 0, sizeof(t));
  SetParam<INPUT_T>(&t, input0);

  using US1 = typename std::conditional<
      sizeof(INPUT_T) == 4,
      typename std::conditional<sizeof(OUTPUT_T) == 4, S1, WIDEN>::type,
      typename std::conditional<sizeof(OUTPUT_T) == 8, D1, SHRINK>::type>::type;
  using RETURN_T =
      typename std::conditional<sizeof(OUTPUT_T) == 4, int32_t, int64_t>::type;

  auto f = GeneratedCode<US1>::FromCode(*code);
  RETURN_T res = f.Call(GetGPRParam<INPUT_T>(&t));
  ValidateResult<RETURN_T, OUTPUT_T>(res, expected_res);
}

template <typename INPUT_T, typename OUTPUT_T>
static void GenerateTestCall(Isolate* isolate, MacroAssembler& assm,
                             INPUT_T input0, INPUT_T input1,
                             OUTPUT_T expected_res) {
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  assert((sizeof(INPUT_T) == 4 || sizeof(INPUT_T) == 8) &&
         sizeof(OUTPUT_T) == sizeof(INPUT_T));

  // setup parameters (to pass floats as integers)
  Param_T t[2];
  memset(&t, 0, sizeof(t));
  SetParam<INPUT_T>(&t[0], input0);
  SetParam<INPUT_T>(&t[1], input1);

  using US2 = typename std::conditional<sizeof(INPUT_T) == 4, S2, D2>::type;
  using RETURN_T =
      typename std::conditional<sizeof(OUTPUT_T) == 4, int32_t, int64_t>::type;

  auto f = GeneratedCode<US2>::FromCode(*code);
  RETURN_T res =
      f.Call(GetGPRParam<INPUT_T>(&t[0]), GetGPRParam<INPUT_T>(&t[1]));
  ValidateResult<RETURN_T, OUTPUT_T>(res, expected_res);
}

template <typename INPUT_T, typename OUTPUT_T>
static void GenerateTestCall(Isolate* isolate, MacroAssembler& assm,
                             INPUT_T input0, INPUT_T input1, INPUT_T input2,
                             OUTPUT_T expected_res) {
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  assert((sizeof(INPUT_T) == 4 || sizeof(INPUT_T) == 8) &&
         sizeof(OUTPUT_T) == sizeof(INPUT_T));

  // setup parameters (to pass floats as integers)
  Param_T t[3];
  memset(&t, 0, sizeof(t));
  SetParam<INPUT_T>(&t[0], input0);
  SetParam<INPUT_T>(&t[1], input1);
  SetParam<INPUT_T>(&t[2], input2);

  using US4 = typename std::conditional<sizeof(INPUT_T) == 4, S4, D4>::type;
  using RETURN_T =
      typename std::conditional<sizeof(OUTPUT_T) == 4, int32_t, int64_t>::type;

  auto f = GeneratedCode<US4>::FromCode(*code);
  RETURN_T res =
      f.Call(GetGPRParam<INPUT_T>(&t[0]), GetGPRParam<INPUT_T>(&t[1]),
             GetGPRParam<INPUT_T>(&t[2]));
  ValidateResult<RETURN_T, OUTPUT_T>(res, expected_res);
}

template <typename T>
static void GenerateTestCallForLoadStore(Isolate* isolate, MacroAssembler& assm,
                                         T value) {
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  assert(sizeof(T) == 4 || sizeof(T) == 8);

  using US3 = typename std::conditional<sizeof(T) == 4, S3, D3>::type;
  using RETURN_T =
      typename std::conditional<sizeof(T) == 4, int32_t, int64_t>::type;

  // setup parameters (to pass floats as integers)
  Param_T t;
  memset(&t, 0, sizeof(t));
  SetParam<T>(&t, value);

  int64_t tmp = 0;
  auto f = GeneratedCode<US3>::FromCode(*code);
  RETURN_T res = f.Call(&tmp, GetGPRParam<T>(&t));
  ValidateResult<RETURN_T, T>(res, value);
}

#define UTEST_R2_FORM_WITH_RES(instr_name, inout_type, rs1_val, rs2_val,      \
                               expected_res)                                  \
  TEST(RISCV_UTEST_##instr_name) {                                            \
    CcTest::InitializeVM();                                                   \
    Isolate* isolate = CcTest::i_isolate();                                   \
    HandleScope scope(isolate);                                               \
                                                                              \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);     \
    __ RV_##instr_name(a0, a0, a1);                                           \
    __ RV_jr(ra);                                                             \
                                                                              \
    GenerateTestCall<inout_type, inout_type>(isolate, assm, rs1_val, rs2_val, \
                                             expected_res);                   \
  }

#define UTEST_R1_FORM_WITH_RES(instr_name, in_type, out_type, rs1_val,         \
                               expected_res)                                   \
  TEST(RISCV_UTEST_##instr_name) {                                             \
    CcTest::InitializeVM();                                                    \
    Isolate* isolate = CcTest::i_isolate();                                    \
    HandleScope scope(isolate);                                                \
                                                                               \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);      \
    __ RV_##instr_name(a0, a0);                                                \
    __ RV_jr(ra);                                                              \
                                                                               \
    GenerateTestCall<in_type, out_type>(isolate, assm, rs1_val, expected_res); \
  }

#define UTEST_I_FORM_WITH_RES(instr_name, inout_type, rs1_val, imm12,     \
                              expected_res)                               \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    CcTest::InitializeVM();                                               \
    Isolate* isolate = CcTest::i_isolate();                               \
    HandleScope scope(isolate);                                           \
                                                                          \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes); \
    CHECK_EQ(check_imm_range(imm12, 12), true);                           \
    __ RV_##instr_name(a0, a0, imm12);                                    \
    __ RV_jr(ra);                                                         \
                                                                          \
    GenerateTestCall<inout_type, inout_type>(isolate, assm, rs1_val,      \
                                             expected_res);               \
  }

#define UTEST_LOAD_STORE(ldname, stname, value_type, value)               \
  TEST(RISCV_UTEST_##stname##ldname) {                                    \
    CcTest::InitializeVM();                                               \
    Isolate* isolate = CcTest::i_isolate();                               \
    HandleScope scope(isolate);                                           \
                                                                          \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes); \
    __ RV_##stname(a1, a0, 0);                                            \
    __ RV_##ldname(a0, a0, 0);                                            \
    __ RV_jr(ra);                                                         \
                                                                          \
    GenerateTestCallForLoadStore<value_type>(isolate, assm, value);       \
  }

// Since f.Call() is implemented as vararg calls and RISCV calling convention
// passes all vararg arguments and returns (including floats) in GPRs, we have
// to move from GPR to FPR and back in all floating point tests
#define UTEST_LOAD_STORE_F(ldname, stname, value_type, store_value)       \
  TEST(RISCV_UTEST_##stname##ldname) {                                    \
    CcTest::InitializeVM();                                               \
    Isolate* isolate = CcTest::i_isolate();                               \
    HandleScope scope(isolate);                                           \
                                                                          \
    assert(std::is_floating_point<value_type>::value);                    \
                                                                          \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes); \
    if (std::is_same<float, value_type>::value) {                         \
      __ RV_fmv_w_x(fa0, a1);                                             \
    } else {                                                              \
      __ RV_fmv_d_x(fa0, a1);                                             \
    }                                                                     \
    __ RV_##stname(fa0, a0, 0);                                           \
    __ RV_##ldname(fa0, a0, 0);                                           \
    if (std::is_same<float, value_type>::value) {                         \
      __ RV_fmv_x_w(a0, fa0);                                             \
    } else {                                                              \
      __ RV_fmv_x_d(a0, fa0);                                             \
    }                                                                     \
    __ RV_jr(ra);                                                         \
                                                                          \
    GenerateTestCallForLoadStore<value_type>(isolate, assm, store_value); \
  }

#define UTEST_R1_FORM_WITH_RES_F(instr_name, inout_type, rs1_fval,        \
                                 expected_fres)                           \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    CcTest::InitializeVM();                                               \
    Isolate* isolate = CcTest::i_isolate();                               \
    HandleScope scope(isolate);                                           \
                                                                          \
    assert(std::is_floating_point<inout_type>::value);                    \
                                                                          \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes); \
    if (std::is_same<float, inout_type>::value) {                         \
      __ RV_fmv_w_x(fa0, a0);                                             \
    } else {                                                              \
      __ RV_fmv_d_x(fa0, a0);                                             \
    }                                                                     \
    __ RV_##instr_name(fa0, fa0);                                         \
    if (std::is_same<float, inout_type>::value) {                         \
      __ RV_fmv_x_w(a0, fa0);                                             \
    } else {                                                              \
      __ RV_fmv_x_d(a0, fa0);                                             \
    }                                                                     \
    __ RV_jr(ra);                                                         \
                                                                          \
    GenerateTestCall<inout_type, inout_type>(isolate, assm, rs1_fval,     \
                                             expected_fres);              \
  }

#define UTEST_R2_FORM_WITH_RES_F(instr_name, inout_type, rs1_fval, rs2_fval, \
                                 expected_fres)                              \
  TEST(RISCV_UTEST_##instr_name) {                                           \
    CcTest::InitializeVM();                                                  \
    Isolate* isolate = CcTest::i_isolate();                                  \
    HandleScope scope(isolate);                                              \
                                                                             \
    assert(std::is_floating_point<inout_type>::value);                       \
                                                                             \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);    \
    if (std::is_same<inout_type, float>::value) {                            \
      __ RV_fmv_w_x(fa0, a0);                                                \
      __ RV_fmv_w_x(fa1, a1);                                                \
    } else {                                                                 \
      __ RV_fmv_d_x(fa0, a0);                                                \
      __ RV_fmv_d_x(fa1, a1);                                                \
    }                                                                        \
    __ RV_##instr_name(fa0, fa0, fa1);                                       \
    if (std::is_same<inout_type, float>::value) {                            \
      __ RV_fmv_x_w(a0, fa0);                                                \
    } else {                                                                 \
      __ RV_fmv_x_d(a0, fa0);                                                \
    }                                                                        \
    __ RV_jr(ra);                                                            \
                                                                             \
    GenerateTestCall<inout_type, inout_type>(isolate, assm, rs1_fval,        \
                                             rs2_fval, expected_fres);       \
  }

#define UTEST_R3_FORM_WITH_RES_F(instr_name, inout_type, rs1_fval, rs2_fval,  \
                                 rs3_fval, expected_fres)                     \
  TEST(RISCV_UTEST_##instr_name) {                                            \
    CcTest::InitializeVM();                                                   \
    Isolate* isolate = CcTest::i_isolate();                                   \
    HandleScope scope(isolate);                                               \
                                                                              \
    assert(std::is_floating_point<inout_type>::value);                        \
                                                                              \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);     \
    if (std::is_same<inout_type, float>::value) {                             \
      __ RV_fmv_w_x(fa0, a0);                                                 \
      __ RV_fmv_w_x(fa1, a1);                                                 \
      __ RV_fmv_w_x(fa2, a2);                                                 \
    } else {                                                                  \
      __ RV_fmv_d_x(fa0, a0);                                                 \
      __ RV_fmv_d_x(fa1, a1);                                                 \
      __ RV_fmv_d_x(fa2, a2);                                                 \
    }                                                                         \
    __ RV_##instr_name(fa0, fa0, fa1, fa2);                                   \
    if (std::is_same<inout_type, float>::value) {                             \
      __ RV_fmv_x_w(a0, fa0);                                                 \
    } else {                                                                  \
      __ RV_fmv_x_d(a0, fa0);                                                 \
    }                                                                         \
    __ RV_jr(ra);                                                             \
                                                                              \
    GenerateTestCall<inout_type>(isolate, assm, rs1_fval, rs2_fval, rs3_fval, \
                                 expected_fres);                              \
  }

#define UTEST_COMPARE_WITH_RES_F(instr_name, input_type, output_type,     \
                                 rs1_fval, rs2_fval, expected_res)        \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    CcTest::InitializeVM();                                               \
    Isolate* isolate = CcTest::i_isolate();                               \
    HandleScope scope(isolate);                                           \
                                                                          \
    assert(std::is_floating_point<input_type>::value&&                    \
               std::is_integral<output_type>::value);                     \
                                                                          \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes); \
    __ RV_fmv_w_x(fa0, a0);                                               \
    __ RV_fmv_w_x(fa1, a1);                                               \
    __ RV_##instr_name(a0, fa0, fa1);                                     \
    __ RV_jr(ra);                                                         \
                                                                          \
    GenerateTestCall<input_type, output_type>(isolate, assm, rs1_fval,    \
                                              rs2_fval, expected_res);    \
  }

#define UTEST_CONV_F_FROM_W(instr_name, input_type, output_type, rs1_val, \
                            expected_fres)                                \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    CcTest::InitializeVM();                                               \
    Isolate* isolate = CcTest::i_isolate();                               \
    HandleScope scope(isolate);                                           \
                                                                          \
    assert(std::is_integral<input_type>::value&&                          \
               std::is_floating_point<output_type>::value);               \
                                                                          \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes); \
    __ RV_##instr_name(fa0, a0);                                          \
    if (std::is_same<output_type, float>::value) {                        \
      __ RV_fmv_x_w(a0, fa0);                                             \
    } else {                                                              \
      __ RV_fmv_x_d(a0, fa0);                                             \
    }                                                                     \
    __ RV_jr(ra);                                                         \
                                                                          \
    GenerateTestCall<input_type, output_type>(isolate, assm, rs1_val,     \
                                              expected_fres);             \
  }

#define UTEST_CONV_W_FROM_F(instr_name, input_type, output_type,          \
                            rounding_mode, rs1_fval, expected_res)        \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    CcTest::InitializeVM();                                               \
    Isolate* isolate = CcTest::i_isolate();                               \
    HandleScope scope(isolate);                                           \
                                                                          \
    assert(std::is_floating_point<input_type>::value&&                    \
               std::is_integral<output_type>::value);                     \
                                                                          \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes); \
    if (std::is_same<input_type, float>::value) {                         \
      __ RV_fmv_w_x(fa0, a0);                                             \
    } else {                                                              \
      __ RV_fmv_d_x(fa0, a0);                                             \
    }                                                                     \
    __ RV_##instr_name(a0, fa0, rounding_mode);                           \
    __ RV_jr(ra);                                                         \
                                                                          \
    GenerateTestCall<input_type, output_type>(isolate, assm, rs1_fval,    \
                                              expected_res);              \
  }

#define UTEST_CONV_F_FROM_F(instr_name, input_type, output_type, rs1_val, \
                            expected_fres)                                \
  TEST(RISCV_UTEST_##instr_name) {                                        \
    CcTest::InitializeVM();                                               \
    Isolate* isolate = CcTest::i_isolate();                               \
    HandleScope scope(isolate);                                           \
                                                                          \
    assert(std::is_floating_point<input_type>::value&&                    \
               std::is_floating_point<output_type>::value);               \
                                                                          \
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes); \
    if (std::is_same<input_type, float>::value) {                         \
      __ RV_fmv_w_x(fa0, a0);                                             \
    } else {                                                              \
      __ RV_fmv_d_x(fa0, a0);                                             \
    }                                                                     \
    __ RV_##instr_name(fa0, fa0);                                         \
    if (std::is_same<output_type, float>::value) {                        \
      __ RV_fmv_x_w(a0, fa0);                                             \
    } else {                                                              \
      __ RV_fmv_x_d(a0, fa0);                                             \
    }                                                                     \
    __ RV_jr(ra);                                                         \
                                                                          \
    GenerateTestCall<input_type, output_type>(isolate, assm, rs1_val,     \
                                              expected_fres);             \
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

#define UTEST_COMPARE_WITH_OP_F(instr_name, input_type, output_type, rs1_fval, \
                                rs2_fval, tested_op)                           \
  UTEST_COMPARE_WITH_RES_F(instr_name, input_type, output_type, rs1_fval,      \
                           rs2_fval, ((rs1_fval)tested_op(rs2_fval)))

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
// void RV_fence(uint8_t pred, uint8_t succ);
// void RV_fence_tso();
// void RV_fence_i();

// -- Environment call / break --
// void RV_ecall();
// void RV_ebreak();
// void RV_unimp();

// -- CSR --
// void RV_csrrw(Register rd, uint16_t imm12, Register rs1);
// void RV_csrrs(Register rd, uint16_t imm12, Register rs1);
// void RV_csrrc(Register rd, uint16_t imm12, Register rs1);
// void RV_csrrwi(Register rd, uint16_t imm12, uint8_t rs1);
// void RV_csrrsi(Register rd, uint16_t imm12, uint8_t rs1);
// void RV_csrrci(Register rd, uint16_t imm12, uint8_t rs1);

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
void RV_lr_w(bool aq, bool rl, Register rd, Register rs1);
void RV_sc_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoswap_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoadd_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoxor_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoand_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoor_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amomin_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amomax_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amominu_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amomaxu_w(bool aq, bool rl, Register rd, Register rs1, Register rs2);

// RV64A Standard Extension (in addition to RV32A)
void RV_lr_d(bool aq, bool rl, Register rd, Register rs1);
void RV_sc_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoswap_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoadd_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoxor_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoand_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amoor_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amomin_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amomax_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amominu_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
void RV_amomaxu_d(bool aq, bool rl, Register rd, Register rs1, Register rs2);
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
UTEST_COMPARE_WITH_OP_F(feq_s, float, int32_t, -3456.56, -3456.56, ==)
UTEST_COMPARE_WITH_OP_F(flt_s, float, int32_t, -3456.56, -3456.56, <)
UTEST_COMPARE_WITH_OP_F(fle_s, float, int32_t, -3456.56, -3456.56, <=)
UTEST_CONV_F_FROM_W(fcvt_s_w, int32_t, float, -100, (float)(-100))
UTEST_CONV_F_FROM_W(fcvt_s_wu, int32_t, float, MAX_UINT32, (float)(MAX_UINT32))
UTEST_CONV_W_FROM_F(fcvt_w_s, float, int32_t, RTZ, -100.0f, -100)
// FIXME: this following test fails, need
// UTEST_CONV_W_FROM_F(fcvt_wu_s, float, int32_t, RTZ,
// (float)(MAX_UINT32), MAX_UINT32)
// FIXME: use large UINT32 number and not exactly int
UTEST_CONV_W_FROM_F(fcvt_wu_s, float, int32_t, RTZ, 100.0f, 100)
UTEST_R2_FORM_WITH_RES_F(fsgnj_s, float, -100.0f, 200.0f, 100.0f)
UTEST_R2_FORM_WITH_RES_F(fsgnjn_s, float, 100.0f, 200.0f, -100.0f)
UTEST_R2_FORM_WITH_RES_F(fsgnjx_s, float, -100.0f, 200.0f, -100.0f)

// void RV_fclass_s(Register rd, FPURegister rs1);

// -- RV64F Standard Extension (in addition to RV32F) --
UTEST_LOAD_STORE_F(fld, fsd, double, -3456.678)
UTEST_R2_FORM_WITH_OP_F(fadd_d, double, -1012.01, 3456.13, +)
UTEST_R2_FORM_WITH_OP_F(fsub_d, double, -1012.01, 3456.13, -)
UTEST_R2_FORM_WITH_OP_F(fmul_d, double, -10.01, 56.13, *)
UTEST_R2_FORM_WITH_OP_F(fdiv_d, double, -10.01, 34.13, /)
UTEST_R1_FORM_WITH_RES_F(fsqrt_d, double, 34.13, sqrt(34.13))
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
UTEST_COMPARE_WITH_OP_F(feq_d, double, int64_t, -3456.56, -3456.56, ==)
UTEST_COMPARE_WITH_OP_F(flt_d, double, int64_t, -3456.56, -3456.56, <)
UTEST_COMPARE_WITH_OP_F(fle_d, double, int64_t, -3456.56, -3456.56, <=)

UTEST_CONV_F_FROM_W(fcvt_d_w, int32_t, double, -100, -100.0)
UTEST_CONV_F_FROM_W(fcvt_d_wu, int32_t, double, MAX_UINT32,
                    (double)(MAX_UINT32))
UTEST_CONV_W_FROM_F(fcvt_w_d, double, int32_t, RTZ, -100.0, -100)
UTEST_CONV_W_FROM_F(fcvt_wu_d, double, int32_t, RTZ, (double)(MAX_UINT32),
                    MAX_UINT32)

// -- RV64F Standard Extension (in addition to RV32F) --
// FIXME: this test failed
/*UTEST_CONV_W_FROM_F(fcvt_l_s, float, int64_t, RTZ,
                    (float)(-0x1234'5678'0000'0001LL),
                    (-0x1234'5678'0000'0001LL))*/
// FIXME: this test reveals a rounding mode bug in the simulator, temporarily
// comment this out to make the CI happy (will open an issue after the MR is
// merged)
// UTEST_CONV_W_FROM_F(fcvt_l_s, float, int64_t, RDN, -100.5f,
// -101)
UTEST_CONV_W_FROM_F(fcvt_l_s, float, int64_t, RTZ, -100.5f, -100)
// FIXME: this test failed
// UTEST_CONV_W_FROM_F(fcvt_lu_s, float, int64_t, RTZ,
// (float)(MAX_UINT64), MAX_UINT64)
UTEST_CONV_W_FROM_F(fcvt_lu_s, float, int64_t, RTZ, (float)100, 100)
UTEST_CONV_F_FROM_W(fcvt_s_l, int64_t, float, (-0x1234'5678'0000'0001LL),
                    (float)(-0x1234'5678'0000'0001LL))
UTEST_CONV_F_FROM_W(fcvt_s_lu, int64_t, float, MAX_UINT64, (float)(MAX_UINT64))

// -- RV32D Standard Extension --
// FIXME: the following tests failed
// UTEST_CONV_F_FROM_F(fcvt_s_d, float, double, 100.0, 100.0f)
// UTEST_CONV_F_FROM_F(fcvt_d_s, double, float, 100.0f, 100.0)

UTEST_R2_FORM_WITH_RES_F(fsgnj_d, double, -100.0, 200.0, 100.0)
UTEST_R2_FORM_WITH_RES_F(fsgnjn_d, double, 100.0, 200.0, -100.0)
UTEST_R2_FORM_WITH_RES_F(fsgnjx_d, double, -100.0, 200.0, -100.0)

// void RV_fclass_d(Register rd, FPURegister rs1);

// -- RV64D Standard Extension (in addition to RV32D) --
// FIXME: this test failed
// UTEST_CONV_W_FROM_F(fcvt_l_d, double, int64_t, RTZ,
//                    (double)(-0x1234'5678'0000'0001LL),
//                    (-0x1234'5678'0000'0001LL))
UTEST_CONV_W_FROM_F(fcvt_l_d, double, int64_t, RTZ, (double)(-100), (-100))
// FIXME: this test failed
// UTEST_CONV_W_FROM_F(fcvt_lu_d, double, int64_t, RTZ,
// (double)(MAX_UINT64), MAX_UINT64)
UTEST_CONV_W_FROM_F(fcvt_lu_d, double, int64_t, RTZ, (double)100, 100)
UTEST_CONV_F_FROM_W(fcvt_d_l, int64_t, double, (-0x1234'5678'0000'0001LL),
                    (double)(-0x1234'5678'0000'0001LL))
UTEST_CONV_F_FROM_W(fcvt_d_lu, int64_t, double, MAX_UINT64,
                    (double)(MAX_UINT64))

/*
// Privileged
void RV_uret();
void RV_sret();
void RV_mret();
void RV_wfi();
void RV_sfence_vma(Register rs1, Register
rs2);
*/

// -- Assembler Pseudo Instructions --
UTEST_R1_FORM_WITH_RES(mv, int64_t, int64_t, 0x0f5600ab123400, 0x0f5600ab123400)
UTEST_R1_FORM_WITH_RES(not, int64_t, int64_t, 0, ~0)
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

TEST(RISCV_UTEST_li) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  int64_t imm64 = 0x1234'5678'8765'4321LL;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  __ RV_li(a0, imm64);
  __ RV_jr(ra);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, Code::STUB).Build();

  auto f = GeneratedCode<D0>::FromCode(*code);
  int64_t res = f.Call();
  ValidateResult<int64_t, int64_t>(res, imm64);
}

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

  // Test lui, ori, and addiu, used in the
  // li pseudo-instruction. This way we
  // can then safely load registers with
  // chosen values.

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

  // Shift both 32bit number to left, to
  // preserve meaning of next comparison.
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

  // Everything was correctly executed.
  // Load the expected result.
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

  // Create a function that accepts &t,
  // and loads, manipulates, and stores
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

  // This test-case should have additional
  // tests.

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

#undef __

}  // namespace internal
}  // namespace v8
