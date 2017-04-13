// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmath>
#include <limits>

#include "src/arm64/decoder-arm64-inl.h"
#include "src/arm64/disasm-arm64.h"
#include "src/arm64/simulator-arm64.h"
#include "src/arm64/utils-arm64.h"
#include "src/base/platform/platform.h"
#include "src/base/utils/random-number-generator.h"
#include "src/macro-assembler-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/test-simulator-neon-inputs-arm64.h"
#include "test/cctest/test-simulator-neon-traces-arm64.h"
#include "test/cctest/test-utils-arm64.h"

using namespace v8::internal;

// Test infrastructure.
//
// Tests are functions which accept no parameters and have no return values.
// The testing code should not perform an explicit return once completed. For
// example to test the mov immediate instruction a very simple test would be:
//
//   SIMTEST(mov_x0_one) {
//     SETUP();
//
//     START();
//     __ mov(x0, Operand(1));
//     END();
//
//     RUN();
//
//     CHECK_EQUAL_64(1, x0);
//
//     TEARDOWN();
//   }
//
// Within a START ... END block all registers but sp can be modified. sp has to
// be explicitly saved/restored. The END() macro replaces the function return
// so it may appear multiple times in a test if the test has multiple exit
// points.
//
// Once the test has been run all integer and floating point registers as well
// as flags are accessible through a RegisterDump instance, see
// test-utils-arm64.h for more info on RegisterDump.
//
// We provide some helper assert to handle common cases:
//
//   CHECK_EQUAL_32(int32_t, int32_t)
//   CHECK_EQUAL_FP32(float, float)
//   CHECK_EQUAL_32(int32_t, W register)
//   CHECK_EQUAL_FP32(float, S register)
//   CHECK_EQUAL_64(int64_t, int64_t)
//   CHECK_EQUAL_FP64(double, double)
//   CHECK_EQUAL_64(int64_t, X register)
//   CHECK_EQUAL_64(X register, X register)
//   CHECK_EQUAL_FP64(double, D register)
//
// e.g. CHECK_EQUAL_64(0.5, d30);
//
// If more advance computation is required before the assert then access the
// RegisterDump named core directly:
//
//   CHECK_EQUAL_64(0x1234, core.xreg(0) & 0xffff);

#if 0  // TODO(all): enable.
static v8::Persistent<v8::Context> env;

static void InitializeVM() {
  if (env.IsEmpty()) {
    env = v8::Context::New();
  }
}
#endif

#define __ masm.
#define SIMTEST(name) TEST(SIM_##name)

#define BUF_SIZE 8192
#define SETUP() SETUP_SIZE(BUF_SIZE)

#define INIT_V8() CcTest::InitializeVM();

#ifdef USE_SIMULATOR

// Run tests with the simulator.
#define SETUP_SIZE(buf_size)                                   \
  Isolate* isolate = CcTest::i_isolate();                      \
  HandleScope scope(isolate);                                  \
  CHECK(isolate != NULL);                                      \
  byte* buf = new byte[buf_size];                              \
  MacroAssembler masm(isolate, buf, buf_size,                  \
                      v8::internal::CodeObjectRequired::kYes); \
  Decoder<DispatchingDecoderVisitor>* decoder =                \
      new Decoder<DispatchingDecoderVisitor>();                \
  Simulator simulator(decoder);                                \
  RegisterDump core;

// Reset the assembler and simulator, so that instructions can be generated,
// but don't actually emit any code. This can be used by tests that need to
// emit instructions at the start of the buffer. Note that START_AFTER_RESET
// must be called before any callee-saved register is modified, and before an
// END is encountered.
//
// Most tests should call START, rather than call RESET directly.
#define RESET() \
  __ Reset();   \
  simulator.ResetState();

#define START_AFTER_RESET()      \
  __ SetStackPointer(csp);       \
  __ PushCalleeSavedRegisters(); \
  __ Debug("Start test.", __LINE__, TRACE_ENABLE | LOG_ALL);

#define START() \
  RESET();      \
  START_AFTER_RESET();

#define RUN() simulator.RunFrom(reinterpret_cast<Instruction*>(buf))

#define END()                                               \
  __ Debug("End test.", __LINE__, TRACE_DISABLE | LOG_ALL); \
  core.Dump(&masm);                                         \
  __ PopCalleeSavedRegisters();                             \
  __ Ret();                                                 \
  __ GetCode(NULL);

#define TEARDOWN() delete[] buf;

#else  // ifdef USE_SIMULATOR.
// Run the test on real hardware or models.
#define SETUP_SIZE(buf_size)                                   \
  Isolate* isolate = CcTest::i_isolate();                      \
  HandleScope scope(isolate);                                  \
  CHECK(isolate != NULL);                                      \
  size_t actual_size;                                          \
  byte* buf = static_cast<byte*>(                              \
      v8::base::OS::Allocate(buf_size, &actual_size, true));   \
  MacroAssembler masm(isolate, buf, actual_size,               \
                      v8::internal::CodeObjectRequired::kYes); \
  RegisterDump core;

#define RESET()                                                \
  __ Reset();                                                  \
  /* Reset the machine state (like simulator.ResetState()). */ \
  __ Msr(NZCV, xzr);                                           \
  __ Msr(FPCR, xzr);

#define START_AFTER_RESET() \
  __ SetStackPointer(csp);  \
  __ PushCalleeSavedRegisters();

#define START() \
  RESET();      \
  START_AFTER_RESET();

#define RUN()                                                       \
  Assembler::FlushICache(isolate, buf, masm.SizeOfGeneratedCode()); \
  {                                                                 \
    void (*test_function)(void);                                    \
    memcpy(&test_function, &buf, sizeof(buf));                      \
    test_function();                                                \
  }

#define END()                   \
  core.Dump(&masm);             \
  __ PopCalleeSavedRegisters(); \
  __ Ret();                     \
  __ GetCode(NULL);

#define TEARDOWN() v8::base::OS::Free(buf, actual_size);

#endif  // ifdef USE_SIMULATOR.

#define CHECK_EQUAL_NZCV(expected) CHECK(EqualNzcv(expected, core.flags_nzcv()))

#define CHECK_EQUAL_REGISTERS(expected) CHECK(EqualRegisters(&expected, &core))

#define CHECK_EQUAL_32(expected, result) \
  CHECK(Equal32(static_cast<uint32_t>(expected), &core, result))

#define CHECK_EQUAL_FP32(expected, result) \
  CHECK(EqualFP32(expected, &core, result))

#define CHECK_EQUAL_64(expected, result) CHECK(Equal64(expected, &core, result))

#define CHECK_EQUAL_FP64(expected, result) \
  CHECK(EqualFP64(expected, &core, result))

#ifdef DEBUG
#define CHECK_LITERAL_POOL_SIZE(expected) \
  CHECK((expected) == (__ LiteralPoolSize()))
#else
#define CHECK_LITERAL_POOL_SIZE(expected) ((void)0)
#endif

// The maximum number of errors to report in detail for each test.
static const unsigned kErrorReportLimit = 8;

typedef void (MacroAssembler::*Test1OpNEONHelper_t)(const VRegister& vd,
                                                    const VRegister& vn);
typedef void (MacroAssembler::*Test2OpNEONHelper_t)(const VRegister& vd,
                                                    const VRegister& vn,
                                                    const VRegister& vm);
typedef void (MacroAssembler::*TestByElementNEONHelper_t)(const VRegister& vd,
                                                          const VRegister& vn,
                                                          const VRegister& vm,
                                                          int vm_index);
typedef void (MacroAssembler::*TestOpImmOpImmVdUpdateNEONHelper_t)(
    const VRegister& vd, int imm1, const VRegister& vn, int imm2);

// This helps using the same typename for both the function pointer
// and the array of immediates passed to helper routines.
template <typename T>
class Test2OpImmediateNEONHelper_t {
 public:
  typedef void (MacroAssembler::*mnemonic)(const VRegister& vd,
                                           const VRegister& vn, T imm);
};

namespace {

// Maximum number of hex characters required to represent values of either
// templated type.
template <typename Ta, typename Tb>
unsigned MaxHexCharCount() {
  unsigned count = static_cast<unsigned>(std::max(sizeof(Ta), sizeof(Tb)));
  return (count * 8) / 4;
}

// ==== Tests for instructions of the form <INST> VReg, VReg. ====

void Test1OpNEON_Helper(Test1OpNEONHelper_t helper, uintptr_t inputs_n,
                        unsigned inputs_n_length, uintptr_t results,
                        VectorFormat vd_form, VectorFormat vn_form) {
  DCHECK_NE(vd_form, kFormatUndefined);
  DCHECK_NE(vn_form, kFormatUndefined);

  SETUP();
  START();

  // Roll up the loop to keep the code size down.
  Label loop_n;

  Register out = x0;
  Register inputs_n_base = x1;
  Register inputs_n_last_16bytes = x3;
  Register index_n = x5;

  const unsigned vd_bits = RegisterSizeInBitsFromFormat(vd_form);
  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);

  const unsigned vn_bits = RegisterSizeInBitsFromFormat(vn_form);
  const unsigned vn_lane_count = LaneCountFromFormat(vn_form);
  const unsigned vn_lane_bytes = LaneSizeInBytesFromFormat(vn_form);
  const unsigned vn_lane_bytes_log2 = LaneSizeInBytesLog2FromFormat(vn_form);
  const unsigned vn_lane_bits = LaneSizeInBitsFromFormat(vn_form);

  // These will be either a D- or a Q-register form, with a single lane
  // (for use in scalar load and store operations).
  VRegister vd = VRegister::Create(0, vd_bits);
  VRegister vn = v1.V16B();
  VRegister vntmp = v3.V16B();

  // These will have the correct format for use when calling 'helper'.
  VRegister vd_helper = VRegister::Create(0, vd_bits, vd_lane_count);
  VRegister vn_helper = VRegister::Create(1, vn_bits, vn_lane_count);

  // 'v*tmp_single' will be either 'Vt.B', 'Vt.H', 'Vt.S' or 'Vt.D'.
  VRegister vntmp_single = VRegister::Create(3, vn_lane_bits);

  __ Mov(out, results);

  __ Mov(inputs_n_base, inputs_n);
  __ Mov(inputs_n_last_16bytes,
         inputs_n + (vn_lane_bytes * inputs_n_length) - 16);

  __ Ldr(vn, MemOperand(inputs_n_last_16bytes));

  __ Mov(index_n, 0);
  __ Bind(&loop_n);

  __ Ldr(vntmp_single,
         MemOperand(inputs_n_base, index_n, LSL, vn_lane_bytes_log2));
  __ Ext(vn, vn, vntmp, vn_lane_bytes);

  // Set the destination to zero.

  // TODO(all): Setting the destination to values other than zero might be a
  // better test for instructions such as sqxtn2 which may leave parts of V
  // registers unchanged.
  __ Movi(vd.V16B(), 0);

  (masm.*helper)(vd_helper, vn_helper);

  __ Str(vd, MemOperand(out, vd.SizeInBytes(), PostIndex));

  __ Add(index_n, index_n, 1);
  __ Cmp(index_n, inputs_n_length);
  __ B(lo, &loop_n);

  END();
  RUN();
  TEARDOWN();
}

// Test NEON instructions. The inputs_*[] and expected[] arrays should be
// arrays of rawbit representation of input values. This ensures that
// exact bit comparisons can be performed.
template <typename Td, typename Tn>
void Test1OpNEON(const char* name, Test1OpNEONHelper_t helper,
                 const Tn inputs_n[], unsigned inputs_n_length,
                 const Td expected[], unsigned expected_length,
                 VectorFormat vd_form, VectorFormat vn_form) {
  DCHECK_GT(inputs_n_length, 0U);

  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);
  const unsigned vn_lane_bytes = LaneSizeInBytesFromFormat(vn_form);
  const unsigned vn_lane_count = LaneCountFromFormat(vn_form);

  const unsigned results_length = inputs_n_length;
  std::vector<Td> results(results_length * vd_lane_count, 0);
  const unsigned lane_len_in_hex = MaxHexCharCount<Td, Tn>();

  Test1OpNEON_Helper(
      helper, reinterpret_cast<uintptr_t>(inputs_n), inputs_n_length,
      reinterpret_cast<uintptr_t>(results.data()), vd_form, vn_form);

  // Check the results.
  CHECK(expected_length == results_length);
  unsigned error_count = 0;
  unsigned d = 0;
  const char* padding = "                    ";
  DCHECK_GE(strlen(padding), (lane_len_in_hex + 1));
  for (unsigned n = 0; n < inputs_n_length; n++, d++) {
    bool error_in_vector = false;

    for (unsigned lane = 0; lane < vd_lane_count; lane++) {
      unsigned output_index = (n * vd_lane_count) + lane;

      if (results[output_index] != expected[output_index]) {
        error_in_vector = true;
        break;
      }
    }

    if (error_in_vector && (++error_count <= kErrorReportLimit)) {
      printf("%s\n", name);
      printf(" Vn%.*s| Vd%.*s| Expected\n", lane_len_in_hex + 1, padding,
             lane_len_in_hex + 1, padding);

      const unsigned first_index_n =
          inputs_n_length - (16 / vn_lane_bytes) + n + 1;

      for (unsigned lane = 0; lane < std::max(vd_lane_count, vn_lane_count);
           lane++) {
        unsigned output_index = (n * vd_lane_count) + lane;
        unsigned input_index_n = (first_index_n + lane) % inputs_n_length;

        printf("%c0x%0*" PRIx64 " | 0x%0*" PRIx64
               " "
               "| 0x%0*" PRIx64 "\n",
               results[output_index] != expected[output_index] ? '*' : ' ',
               lane_len_in_hex, static_cast<uint64_t>(inputs_n[input_index_n]),
               lane_len_in_hex, static_cast<uint64_t>(results[output_index]),
               lane_len_in_hex, static_cast<uint64_t>(expected[output_index]));
      }
    }
  }
  DCHECK_EQ(d, expected_length);
  if (error_count > kErrorReportLimit) {
    printf("%u other errors follow.\n", error_count - kErrorReportLimit);
  }
  DCHECK_EQ(error_count, 0U);
}

// ==== Tests for instructions of the form <mnemonic> <V><d>, <Vn>.<T> ====
//      where <V> is one of B, H, S or D registers.
//      e.g. saddlv H1, v0.8B

// TODO(all): Change tests to store all lanes of the resulting V register.
//            Some tests store all 128 bits of the resulting V register to
//            check the simulator's behaviour on the rest of the register.
//            This is better than storing the affected lanes only.
//            Change any tests such as the 'Across' template to do the same.

void Test1OpAcrossNEON_Helper(Test1OpNEONHelper_t helper, uintptr_t inputs_n,
                              unsigned inputs_n_length, uintptr_t results,
                              VectorFormat vd_form, VectorFormat vn_form) {
  DCHECK_NE(vd_form, kFormatUndefined);
  DCHECK_NE(vn_form, kFormatUndefined);

  SETUP();
  START();

  // Roll up the loop to keep the code size down.
  Label loop_n;

  Register out = x0;
  Register inputs_n_base = x1;
  Register inputs_n_last_vector = x3;
  Register index_n = x5;

  const unsigned vd_bits = RegisterSizeInBitsFromFormat(vd_form);
  const unsigned vn_bits = RegisterSizeInBitsFromFormat(vn_form);
  const unsigned vn_lane_count = LaneCountFromFormat(vn_form);
  const unsigned vn_lane_bytes = LaneSizeInBytesFromFormat(vn_form);
  const unsigned vn_lane_bytes_log2 = LaneSizeInBytesLog2FromFormat(vn_form);
  const unsigned vn_lane_bits = LaneSizeInBitsFromFormat(vn_form);

  // Test destructive operations by (arbitrarily) using the same register for
  // B and S lane sizes.
  bool destructive = (vd_bits == kBRegSize) || (vd_bits == kSRegSize);

  // These will be either a D- or a Q-register form, with a single lane
  // (for use in scalar load and store operations).
  // Create two aliases for v8; the first is the destination for the tested
  // instruction, the second, the whole Q register to check the results.
  VRegister vd = VRegister::Create(0, vd_bits);
  VRegister vdstr = VRegister::Create(0, kQRegSizeInBits);

  VRegister vn = VRegister::Create(1, vn_bits);
  VRegister vntmp = VRegister::Create(3, vn_bits);

  // These will have the correct format for use when calling 'helper'.
  VRegister vd_helper = VRegister::Create(0, vn_bits, vn_lane_count);
  VRegister vn_helper = VRegister::Create(1, vn_bits, vn_lane_count);

  // 'v*tmp_single' will be either 'Vt.B', 'Vt.H', 'Vt.S' or 'Vt.D'.
  VRegister vntmp_single = VRegister::Create(3, vn_lane_bits);

  // Same registers for use in the 'ext' instructions.
  VRegister vn_ext = (kDRegSizeInBits == vn_bits) ? vn.V8B() : vn.V16B();
  VRegister vntmp_ext =
      (kDRegSizeInBits == vn_bits) ? vntmp.V8B() : vntmp.V16B();

  __ Mov(out, results);

  __ Mov(inputs_n_base, inputs_n);
  __ Mov(inputs_n_last_vector,
         inputs_n + vn_lane_bytes * (inputs_n_length - vn_lane_count));

  __ Ldr(vn, MemOperand(inputs_n_last_vector));

  __ Mov(index_n, 0);
  __ Bind(&loop_n);

  __ Ldr(vntmp_single,
         MemOperand(inputs_n_base, index_n, LSL, vn_lane_bytes_log2));
  __ Ext(vn_ext, vn_ext, vntmp_ext, vn_lane_bytes);

  if (destructive) {
    __ Mov(vd_helper, vn_helper);
    (masm.*helper)(vd, vd_helper);
  } else {
    (masm.*helper)(vd, vn_helper);
  }

  __ Str(vdstr, MemOperand(out, kQRegSize, PostIndex));

  __ Add(index_n, index_n, 1);
  __ Cmp(index_n, inputs_n_length);
  __ B(lo, &loop_n);

  END();
  RUN();
  TEARDOWN();
}

// Test NEON instructions. The inputs_*[] and expected[] arrays should be
// arrays of rawbit representation of input values. This ensures that
// exact bit comparisons can be performed.
template <typename Td, typename Tn>
void Test1OpAcrossNEON(const char* name, Test1OpNEONHelper_t helper,
                       const Tn inputs_n[], unsigned inputs_n_length,
                       const Td expected[], unsigned expected_length,
                       VectorFormat vd_form, VectorFormat vn_form) {
  DCHECK_GT(inputs_n_length, 0U);

  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);
  const unsigned vd_lanes_per_q = MaxLaneCountFromFormat(vd_form);

  const unsigned results_length = inputs_n_length;
  std::vector<Td> results(results_length * vd_lanes_per_q, 0);
  const unsigned lane_len_in_hex = MaxHexCharCount<Td, Tn>();

  Test1OpAcrossNEON_Helper(
      helper, reinterpret_cast<uintptr_t>(inputs_n), inputs_n_length,
      reinterpret_cast<uintptr_t>(results.data()), vd_form, vn_form);

  // Check the results.
  DCHECK_EQ(expected_length, results_length);
  unsigned error_count = 0;
  unsigned d = 0;
  const char* padding = "                    ";
  DCHECK_GE(strlen(padding), (lane_len_in_hex + 1));
  for (unsigned n = 0; n < inputs_n_length; n++, d++) {
    bool error_in_vector = false;

    for (unsigned lane = 0; lane < vd_lane_count; lane++) {
      unsigned expected_index = (n * vd_lane_count) + lane;
      unsigned results_index = (n * vd_lanes_per_q) + lane;

      if (results[results_index] != expected[expected_index]) {
        error_in_vector = true;
        break;
      }

      // For across operations, the remaining lanes should be zero.
      for (unsigned lane = vd_lane_count; lane < vd_lanes_per_q; lane++) {
        unsigned results_index = (n * vd_lanes_per_q) + lane;
        if (results[results_index] != 0) {
          error_in_vector = true;
          break;
        }
      }
    }

    if (error_in_vector && (++error_count <= kErrorReportLimit)) {
      const unsigned vn_lane_count = LaneCountFromFormat(vn_form);

      printf("%s\n", name);
      printf(" Vn%.*s| Vd%.*s| Expected\n", lane_len_in_hex + 1, padding,
             lane_len_in_hex + 1, padding);

      for (unsigned lane = 0; lane < vn_lane_count; lane++) {
        unsigned results_index =
            (n * vd_lanes_per_q) + ((vn_lane_count - 1) - lane);
        unsigned input_index_n =
            (inputs_n_length - vn_lane_count + n + 1 + lane) % inputs_n_length;

        Td expect = 0;
        if ((vn_lane_count - 1) == lane) {
          // This is the last lane to be printed, ie. the least-significant
          // lane, so use the expected value; any other lane should be zero.
          unsigned expected_index = n * vd_lane_count;
          expect = expected[expected_index];
        }
        printf("%c0x%0*" PRIx64 " | 0x%0*" PRIx64 " | 0x%0*" PRIx64 "\n",
               results[results_index] != expect ? '*' : ' ', lane_len_in_hex,
               static_cast<uint64_t>(inputs_n[input_index_n]), lane_len_in_hex,
               static_cast<uint64_t>(results[results_index]), lane_len_in_hex,
               static_cast<uint64_t>(expect));
      }
    }
  }
  DCHECK_EQ(d, expected_length);
  if (error_count > kErrorReportLimit) {
    printf("%u other errors follow.\n", error_count - kErrorReportLimit);
  }
  DCHECK_EQ(error_count, 0U);
}

// ==== Tests for instructions of the form <INST> VReg, VReg, VReg. ====

void Test2OpNEON_Helper(Test2OpNEONHelper_t helper, uintptr_t inputs_d,
                        uintptr_t inputs_n, unsigned inputs_n_length,
                        uintptr_t inputs_m, unsigned inputs_m_length,
                        uintptr_t results, VectorFormat vd_form,
                        VectorFormat vn_form, VectorFormat vm_form) {
  DCHECK_NE(vd_form, kFormatUndefined);
  DCHECK_NE(vn_form, kFormatUndefined);
  DCHECK_NE(vm_form, kFormatUndefined);

  SETUP();
  START();

  // Roll up the loop to keep the code size down.
  Label loop_n, loop_m;

  Register out = x0;
  Register inputs_n_base = x1;
  Register inputs_m_base = x2;
  Register inputs_d_base = x3;
  Register inputs_n_last_16bytes = x4;
  Register inputs_m_last_16bytes = x5;
  Register index_n = x6;
  Register index_m = x7;

  const unsigned vd_bits = RegisterSizeInBitsFromFormat(vd_form);
  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);

  const unsigned vn_bits = RegisterSizeInBitsFromFormat(vn_form);
  const unsigned vn_lane_count = LaneCountFromFormat(vn_form);
  const unsigned vn_lane_bytes = LaneSizeInBytesFromFormat(vn_form);
  const unsigned vn_lane_bytes_log2 = LaneSizeInBytesLog2FromFormat(vn_form);
  const unsigned vn_lane_bits = LaneSizeInBitsFromFormat(vn_form);

  const unsigned vm_bits = RegisterSizeInBitsFromFormat(vm_form);
  const unsigned vm_lane_count = LaneCountFromFormat(vm_form);
  const unsigned vm_lane_bytes = LaneSizeInBytesFromFormat(vm_form);
  const unsigned vm_lane_bytes_log2 = LaneSizeInBytesLog2FromFormat(vm_form);
  const unsigned vm_lane_bits = LaneSizeInBitsFromFormat(vm_form);

  // Always load and store 128 bits regardless of the format.
  VRegister vd = v0.V16B();
  VRegister vn = v1.V16B();
  VRegister vm = v2.V16B();
  VRegister vntmp = v3.V16B();
  VRegister vmtmp = v4.V16B();
  VRegister vres = v5.V16B();

  // These will have the correct format for calling the 'helper'.
  VRegister vn_helper = VRegister::Create(1, vn_bits, vn_lane_count);
  VRegister vm_helper = VRegister::Create(2, vm_bits, vm_lane_count);
  VRegister vres_helper = VRegister::Create(5, vd_bits, vd_lane_count);

  // 'v*tmp_single' will be either 'Vt.B', 'Vt.H', 'Vt.S' or 'Vt.D'.
  VRegister vntmp_single = VRegister::Create(3, vn_lane_bits);
  VRegister vmtmp_single = VRegister::Create(4, vm_lane_bits);

  __ Mov(out, results);

  __ Mov(inputs_d_base, inputs_d);

  __ Mov(inputs_n_base, inputs_n);
  __ Mov(inputs_n_last_16bytes, inputs_n + (inputs_n_length - 16));
  __ Mov(inputs_m_base, inputs_m);
  __ Mov(inputs_m_last_16bytes, inputs_m + (inputs_m_length - 16));

  __ Ldr(vd, MemOperand(inputs_d_base));
  __ Ldr(vn, MemOperand(inputs_n_last_16bytes));
  __ Ldr(vm, MemOperand(inputs_m_last_16bytes));

  __ Mov(index_n, 0);
  __ Bind(&loop_n);

  __ Ldr(vntmp_single,
         MemOperand(inputs_n_base, index_n, LSL, vn_lane_bytes_log2));
  __ Ext(vn, vn, vntmp, vn_lane_bytes);

  __ Mov(index_m, 0);
  __ Bind(&loop_m);

  __ Ldr(vmtmp_single,
         MemOperand(inputs_m_base, index_m, LSL, vm_lane_bytes_log2));
  __ Ext(vm, vm, vmtmp, vm_lane_bytes);

  __ Mov(vres, vd);

  (masm.*helper)(vres_helper, vn_helper, vm_helper);

  __ Str(vres, MemOperand(out, vd.SizeInBytes(), PostIndex));

  __ Add(index_m, index_m, 1);
  __ Cmp(index_m, inputs_m_length);
  __ B(lo, &loop_m);

  __ Add(index_n, index_n, 1);
  __ Cmp(index_n, inputs_n_length);
  __ B(lo, &loop_n);

  END();
  RUN();
  TEARDOWN();
}

// Test NEON instructions. The inputs_*[] and expected[] arrays should be
// arrays of rawbit representation of input values. This ensures that
// exact bit comparisons can be performed.
template <typename Td, typename Tn, typename Tm>
void Test2OpNEON(const char* name, Test2OpNEONHelper_t helper,
                 const Td inputs_d[], const Tn inputs_n[],
                 unsigned inputs_n_length, const Tm inputs_m[],
                 unsigned inputs_m_length, const Td expected[],
                 unsigned expected_length, VectorFormat vd_form,
                 VectorFormat vn_form, VectorFormat vm_form) {
  DCHECK(inputs_n_length > 0 && inputs_m_length > 0);

  const unsigned vd_lane_count = MaxLaneCountFromFormat(vd_form);

  const unsigned results_length = inputs_n_length * inputs_m_length;
  std::vector<Td> results(results_length * vd_lane_count);
  const unsigned lane_len_in_hex =
      static_cast<unsigned>(std::max(sizeof(Td), sizeof(Tm)) * 8) / 4;

  Test2OpNEON_Helper(helper, reinterpret_cast<uintptr_t>(inputs_d),
                     reinterpret_cast<uintptr_t>(inputs_n), inputs_n_length,
                     reinterpret_cast<uintptr_t>(inputs_m), inputs_m_length,
                     reinterpret_cast<uintptr_t>(results.data()), vd_form,
                     vn_form, vm_form);

  // Check the results.
  CHECK(expected_length == results_length);
  unsigned error_count = 0;
  unsigned d = 0;
  const char* padding = "                    ";
  DCHECK_GE(strlen(padding), lane_len_in_hex + 1);
  for (unsigned n = 0; n < inputs_n_length; n++) {
    for (unsigned m = 0; m < inputs_m_length; m++, d++) {
      bool error_in_vector = false;

      for (unsigned lane = 0; lane < vd_lane_count; lane++) {
        unsigned output_index =
            (n * inputs_m_length * vd_lane_count) + (m * vd_lane_count) + lane;

        if (results[output_index] != expected[output_index]) {
          error_in_vector = true;
          break;
        }
      }

      if (error_in_vector && (++error_count <= kErrorReportLimit)) {
        printf("%s\n", name);
        printf(" Vd%.*s| Vn%.*s| Vm%.*s| Vd%.*s| Expected\n",
               lane_len_in_hex + 1, padding, lane_len_in_hex + 1, padding,
               lane_len_in_hex + 1, padding, lane_len_in_hex + 1, padding);

        for (unsigned lane = 0; lane < vd_lane_count; lane++) {
          unsigned output_index = (n * inputs_m_length * vd_lane_count) +
                                  (m * vd_lane_count) + lane;
          unsigned input_index_n =
              (inputs_n_length - vd_lane_count + n + 1 + lane) %
              inputs_n_length;
          unsigned input_index_m =
              (inputs_m_length - vd_lane_count + m + 1 + lane) %
              inputs_m_length;

          printf(
              "%c0x%0*" PRIx64 " | 0x%0*" PRIx64 " | 0x%0*" PRIx64
              " "
              "| 0x%0*" PRIx64 " | 0x%0*" PRIx64 "\n",
              results[output_index] != expected[output_index] ? '*' : ' ',
              lane_len_in_hex, static_cast<uint64_t>(inputs_d[lane]),
              lane_len_in_hex, static_cast<uint64_t>(inputs_n[input_index_n]),
              lane_len_in_hex, static_cast<uint64_t>(inputs_m[input_index_m]),
              lane_len_in_hex, static_cast<uint64_t>(results[output_index]),
              lane_len_in_hex, static_cast<uint64_t>(expected[output_index]));
        }
      }
    }
  }
  DCHECK_EQ(d, expected_length);
  if (error_count > kErrorReportLimit) {
    printf("%u other errors follow.\n", error_count - kErrorReportLimit);
  }
  DCHECK_EQ(error_count, 0U);
}

// ==== Tests for instructions of the form <INST> Vd, Vn, Vm[<#index>]. ====

void TestByElementNEON_Helper(TestByElementNEONHelper_t helper,
                              uintptr_t inputs_d, uintptr_t inputs_n,
                              unsigned inputs_n_length, uintptr_t inputs_m,
                              unsigned inputs_m_length, const int indices[],
                              unsigned indices_length, uintptr_t results,
                              VectorFormat vd_form, VectorFormat vn_form,
                              VectorFormat vm_form) {
  DCHECK_NE(vd_form, kFormatUndefined);
  DCHECK_NE(vn_form, kFormatUndefined);
  DCHECK_NE(vm_form, kFormatUndefined);

  SETUP();
  START();

  // Roll up the loop to keep the code size down.
  Label loop_n, loop_m;

  Register out = x0;
  Register inputs_n_base = x1;
  Register inputs_m_base = x2;
  Register inputs_d_base = x3;
  Register inputs_n_last_16bytes = x4;
  Register inputs_m_last_16bytes = x5;
  Register index_n = x6;
  Register index_m = x7;

  const unsigned vd_bits = RegisterSizeInBitsFromFormat(vd_form);
  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);

  const unsigned vn_bits = RegisterSizeInBitsFromFormat(vn_form);
  const unsigned vn_lane_count = LaneCountFromFormat(vn_form);
  const unsigned vn_lane_bytes = LaneSizeInBytesFromFormat(vn_form);
  const unsigned vn_lane_bytes_log2 = LaneSizeInBytesLog2FromFormat(vn_form);
  const unsigned vn_lane_bits = LaneSizeInBitsFromFormat(vn_form);

  const unsigned vm_bits = RegisterSizeInBitsFromFormat(vm_form);
  const unsigned vm_lane_count = LaneCountFromFormat(vm_form);
  const unsigned vm_lane_bytes = LaneSizeInBytesFromFormat(vm_form);
  const unsigned vm_lane_bytes_log2 = LaneSizeInBytesLog2FromFormat(vm_form);
  const unsigned vm_lane_bits = LaneSizeInBitsFromFormat(vm_form);

  // Always load and store 128 bits regardless of the format.
  VRegister vd = v0.V16B();
  VRegister vn = v1.V16B();
  VRegister vm = v2.V16B();
  VRegister vntmp = v3.V16B();
  VRegister vmtmp = v4.V16B();
  VRegister vres = v5.V16B();

  // These will have the correct format for calling the 'helper'.
  VRegister vn_helper = VRegister::Create(1, vn_bits, vn_lane_count);
  VRegister vm_helper = VRegister::Create(2, vm_bits, vm_lane_count);
  VRegister vres_helper = VRegister::Create(5, vd_bits, vd_lane_count);

  // 'v*tmp_single' will be either 'Vt.B', 'Vt.H', 'Vt.S' or 'Vt.D'.
  VRegister vntmp_single = VRegister::Create(3, vn_lane_bits);
  VRegister vmtmp_single = VRegister::Create(4, vm_lane_bits);

  __ Mov(out, results);

  __ Mov(inputs_d_base, inputs_d);

  __ Mov(inputs_n_base, inputs_n);
  __ Mov(inputs_n_last_16bytes, inputs_n + (inputs_n_length - 16));
  __ Mov(inputs_m_base, inputs_m);
  __ Mov(inputs_m_last_16bytes, inputs_m + (inputs_m_length - 16));

  __ Ldr(vd, MemOperand(inputs_d_base));
  __ Ldr(vn, MemOperand(inputs_n_last_16bytes));
  __ Ldr(vm, MemOperand(inputs_m_last_16bytes));

  __ Mov(index_n, 0);
  __ Bind(&loop_n);

  __ Ldr(vntmp_single,
         MemOperand(inputs_n_base, index_n, LSL, vn_lane_bytes_log2));
  __ Ext(vn, vn, vntmp, vn_lane_bytes);

  __ Mov(index_m, 0);
  __ Bind(&loop_m);

  __ Ldr(vmtmp_single,
         MemOperand(inputs_m_base, index_m, LSL, vm_lane_bytes_log2));
  __ Ext(vm, vm, vmtmp, vm_lane_bytes);

  __ Mov(vres, vd);
  {
    for (unsigned i = 0; i < indices_length; i++) {
      (masm.*helper)(vres_helper, vn_helper, vm_helper, indices[i]);
      __ Str(vres, MemOperand(out, vd.SizeInBytes(), PostIndex));
    }
  }

  __ Add(index_m, index_m, 1);
  __ Cmp(index_m, inputs_m_length);
  __ B(lo, &loop_m);

  __ Add(index_n, index_n, 1);
  __ Cmp(index_n, inputs_n_length);
  __ B(lo, &loop_n);

  END();
  RUN();
  TEARDOWN();
}

// Test NEON instructions. The inputs_*[] and expected[] arrays should be
// arrays of rawbit representation of input values. This ensures that
// exact bit comparisons can be performed.
template <typename Td, typename Tn, typename Tm>
void TestByElementNEON(const char* name, TestByElementNEONHelper_t helper,
                       const Td inputs_d[], const Tn inputs_n[],
                       unsigned inputs_n_length, const Tm inputs_m[],
                       unsigned inputs_m_length, const int indices[],
                       unsigned indices_length, const Td expected[],
                       unsigned expected_length, VectorFormat vd_form,
                       VectorFormat vn_form, VectorFormat vm_form) {
  DCHECK_GT(inputs_n_length, 0U);
  DCHECK_GT(inputs_m_length, 0U);
  DCHECK_GT(indices_length, 0U);

  const unsigned vd_lane_count = MaxLaneCountFromFormat(vd_form);

  const unsigned results_length =
      inputs_n_length * inputs_m_length * indices_length;
  std::vector<Td> results(results_length * vd_lane_count, 0);
  const unsigned lane_len_in_hex = MaxHexCharCount<Td, Tm>();

  TestByElementNEON_Helper(
      helper, reinterpret_cast<uintptr_t>(inputs_d),
      reinterpret_cast<uintptr_t>(inputs_n), inputs_n_length,
      reinterpret_cast<uintptr_t>(inputs_m), inputs_m_length, indices,
      indices_length, reinterpret_cast<uintptr_t>(results.data()), vd_form,
      vn_form, vm_form);

  // Check the results.
  CHECK(expected_length == results_length);
  unsigned error_count = 0;
  unsigned d = 0;
  const char* padding = "                    ";
  DCHECK_GE(strlen(padding), lane_len_in_hex + 1);
  for (unsigned n = 0; n < inputs_n_length; n++) {
    for (unsigned m = 0; m < inputs_m_length; m++) {
      for (unsigned index = 0; index < indices_length; index++, d++) {
        bool error_in_vector = false;

        for (unsigned lane = 0; lane < vd_lane_count; lane++) {
          unsigned output_index =
              (n * inputs_m_length * indices_length * vd_lane_count) +
              (m * indices_length * vd_lane_count) + (index * vd_lane_count) +
              lane;

          if (results[output_index] != expected[output_index]) {
            error_in_vector = true;
            break;
          }
        }

        if (error_in_vector && (++error_count <= kErrorReportLimit)) {
          printf("%s\n", name);
          printf(" Vd%.*s| Vn%.*s| Vm%.*s| Index | Vd%.*s| Expected\n",
                 lane_len_in_hex + 1, padding, lane_len_in_hex + 1, padding,
                 lane_len_in_hex + 1, padding, lane_len_in_hex + 1, padding);

          for (unsigned lane = 0; lane < vd_lane_count; lane++) {
            unsigned output_index =
                (n * inputs_m_length * indices_length * vd_lane_count) +
                (m * indices_length * vd_lane_count) + (index * vd_lane_count) +
                lane;
            unsigned input_index_n =
                (inputs_n_length - vd_lane_count + n + 1 + lane) %
                inputs_n_length;
            unsigned input_index_m =
                (inputs_m_length - vd_lane_count + m + 1 + lane) %
                inputs_m_length;

            printf(
                "%c0x%0*" PRIx64 " | 0x%0*" PRIx64 " | 0x%0*" PRIx64
                " "
                "| [%3d] | 0x%0*" PRIx64 " | 0x%0*" PRIx64 "\n",
                results[output_index] != expected[output_index] ? '*' : ' ',
                lane_len_in_hex, static_cast<uint64_t>(inputs_d[lane]),
                lane_len_in_hex, static_cast<uint64_t>(inputs_n[input_index_n]),
                lane_len_in_hex, static_cast<uint64_t>(inputs_m[input_index_m]),
                indices[index], lane_len_in_hex,
                static_cast<uint64_t>(results[output_index]), lane_len_in_hex,
                static_cast<uint64_t>(expected[output_index]));
          }
        }
      }
    }
  }
  DCHECK_EQ(d, expected_length);
  if (error_count > kErrorReportLimit) {
    printf("%u other errors follow.\n", error_count - kErrorReportLimit);
  }
  CHECK(error_count == 0);
}

// ==== Tests for instructions of the form <INST> VReg, VReg, #Immediate. ====

template <typename Tm>
void Test2OpImmNEON_Helper(
    typename Test2OpImmediateNEONHelper_t<Tm>::mnemonic helper,
    uintptr_t inputs_n, unsigned inputs_n_length, const Tm inputs_m[],
    unsigned inputs_m_length, uintptr_t results, VectorFormat vd_form,
    VectorFormat vn_form) {
  DCHECK(vd_form != kFormatUndefined && vn_form != kFormatUndefined);

  SETUP();
  START();

  // Roll up the loop to keep the code size down.
  Label loop_n;

  Register out = x0;
  Register inputs_n_base = x1;
  Register inputs_n_last_16bytes = x3;
  Register index_n = x5;

  const unsigned vd_bits = RegisterSizeInBitsFromFormat(vd_form);
  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);

  const unsigned vn_bits = RegisterSizeInBitsFromFormat(vn_form);
  const unsigned vn_lane_count = LaneCountFromFormat(vn_form);
  const unsigned vn_lane_bytes = LaneSizeInBytesFromFormat(vn_form);
  const unsigned vn_lane_bytes_log2 = LaneSizeInBytesLog2FromFormat(vn_form);
  const unsigned vn_lane_bits = LaneSizeInBitsFromFormat(vn_form);

  // These will be either a D- or a Q-register form, with a single lane
  // (for use in scalar load and store operations).
  VRegister vd = VRegister::Create(0, vd_bits);
  VRegister vn = v1.V16B();
  VRegister vntmp = v3.V16B();

  // These will have the correct format for use when calling 'helper'.
  VRegister vd_helper = VRegister::Create(0, vd_bits, vd_lane_count);
  VRegister vn_helper = VRegister::Create(1, vn_bits, vn_lane_count);

  // 'v*tmp_single' will be either 'Vt.B', 'Vt.H', 'Vt.S' or 'Vt.D'.
  VRegister vntmp_single = VRegister::Create(3, vn_lane_bits);

  __ Mov(out, results);

  __ Mov(inputs_n_base, inputs_n);
  __ Mov(inputs_n_last_16bytes,
         inputs_n + (vn_lane_bytes * inputs_n_length) - 16);

  __ Ldr(vn, MemOperand(inputs_n_last_16bytes));

  __ Mov(index_n, 0);
  __ Bind(&loop_n);

  __ Ldr(vntmp_single,
         MemOperand(inputs_n_base, index_n, LSL, vn_lane_bytes_log2));
  __ Ext(vn, vn, vntmp, vn_lane_bytes);

  // Set the destination to zero for tests such as '[r]shrn2'.
  // TODO(all): Setting the destination to values other than zero might be a
  // better test for shift and accumulate instructions (srsra/ssra/usra/ursra).
  __ Movi(vd.V16B(), 0);

  {
    for (unsigned i = 0; i < inputs_m_length; i++) {
      (masm.*helper)(vd_helper, vn_helper, inputs_m[i]);
      __ Str(vd, MemOperand(out, vd.SizeInBytes(), PostIndex));
    }
  }

  __ Add(index_n, index_n, 1);
  __ Cmp(index_n, inputs_n_length);
  __ B(lo, &loop_n);

  END();
  RUN();
  TEARDOWN();
}

// Test NEON instructions. The inputs_*[] and expected[] arrays should be
// arrays of rawbit representation of input values. This ensures that
// exact bit comparisons can be performed.
template <typename Td, typename Tn, typename Tm>
void Test2OpImmNEON(const char* name,
                    typename Test2OpImmediateNEONHelper_t<Tm>::mnemonic helper,
                    const Tn inputs_n[], unsigned inputs_n_length,
                    const Tm inputs_m[], unsigned inputs_m_length,
                    const Td expected[], unsigned expected_length,
                    VectorFormat vd_form, VectorFormat vn_form) {
  DCHECK(inputs_n_length > 0 && inputs_m_length > 0);

  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);
  const unsigned vn_lane_bytes = LaneSizeInBytesFromFormat(vn_form);
  const unsigned vn_lane_count = LaneCountFromFormat(vn_form);

  const unsigned results_length = inputs_n_length * inputs_m_length;
  std::vector<Td> results(results_length * vd_lane_count, 0);
  const unsigned lane_len_in_hex = MaxHexCharCount<Td, Tn>();

  Test2OpImmNEON_Helper(helper, reinterpret_cast<uintptr_t>(inputs_n),
                        inputs_n_length, inputs_m, inputs_m_length,
                        reinterpret_cast<uintptr_t>(results.data()), vd_form,
                        vn_form);

  // Check the results.
  CHECK(expected_length == results_length);
  unsigned error_count = 0;
  unsigned d = 0;
  const char* padding = "                    ";
  DCHECK_GE(strlen(padding), lane_len_in_hex + 1);
  for (unsigned n = 0; n < inputs_n_length; n++) {
    for (unsigned m = 0; m < inputs_m_length; m++, d++) {
      bool error_in_vector = false;

      for (unsigned lane = 0; lane < vd_lane_count; lane++) {
        unsigned output_index =
            (n * inputs_m_length * vd_lane_count) + (m * vd_lane_count) + lane;

        if (results[output_index] != expected[output_index]) {
          error_in_vector = true;
          break;
        }
      }

      if (error_in_vector && (++error_count <= kErrorReportLimit)) {
        printf("%s\n", name);
        printf(" Vn%.*s| Imm%.*s| Vd%.*s| Expected\n", lane_len_in_hex + 1,
               padding, lane_len_in_hex, padding, lane_len_in_hex + 1, padding);

        const unsigned first_index_n =
            inputs_n_length - (16 / vn_lane_bytes) + n + 1;

        for (unsigned lane = 0; lane < std::max(vd_lane_count, vn_lane_count);
             lane++) {
          unsigned output_index = (n * inputs_m_length * vd_lane_count) +
                                  (m * vd_lane_count) + lane;
          unsigned input_index_n = (first_index_n + lane) % inputs_n_length;
          unsigned input_index_m = m;

          printf(
              "%c0x%0*" PRIx64 " | 0x%0*" PRIx64
              " "
              "| 0x%0*" PRIx64 " | 0x%0*" PRIx64 "\n",
              results[output_index] != expected[output_index] ? '*' : ' ',
              lane_len_in_hex, static_cast<uint64_t>(inputs_n[input_index_n]),
              lane_len_in_hex, static_cast<uint64_t>(inputs_m[input_index_m]),
              lane_len_in_hex, static_cast<uint64_t>(results[output_index]),
              lane_len_in_hex, static_cast<uint64_t>(expected[output_index]));
        }
      }
    }
  }
  DCHECK_EQ(d, expected_length);
  if (error_count > kErrorReportLimit) {
    printf("%u other errors follow.\n", error_count - kErrorReportLimit);
  }
  CHECK(error_count == 0);
}

// ==== Tests for instructions of the form <INST> VReg, #Imm, VReg, #Imm. ====

void TestOpImmOpImmNEON_Helper(TestOpImmOpImmVdUpdateNEONHelper_t helper,
                               uintptr_t inputs_d, const int inputs_imm1[],
                               unsigned inputs_imm1_length, uintptr_t inputs_n,
                               unsigned inputs_n_length,
                               const int inputs_imm2[],
                               unsigned inputs_imm2_length, uintptr_t results,
                               VectorFormat vd_form, VectorFormat vn_form) {
  DCHECK_NE(vd_form, kFormatUndefined);
  DCHECK_NE(vn_form, kFormatUndefined);

  SETUP();
  START();

  // Roll up the loop to keep the code size down.
  Label loop_n;

  Register out = x0;
  Register inputs_d_base = x1;
  Register inputs_n_base = x2;
  Register inputs_n_last_vector = x4;
  Register index_n = x6;

  const unsigned vd_bits = RegisterSizeInBitsFromFormat(vd_form);
  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);

  const unsigned vn_bits = RegisterSizeInBitsFromFormat(vn_form);
  const unsigned vn_lane_count = LaneCountFromFormat(vn_form);
  const unsigned vn_lane_bytes = LaneSizeInBytesFromFormat(vn_form);
  const unsigned vn_lane_bytes_log2 = LaneSizeInBytesLog2FromFormat(vn_form);
  const unsigned vn_lane_bits = LaneSizeInBitsFromFormat(vn_form);

  // These will be either a D- or a Q-register form, with a single lane
  // (for use in scalar load and store operations).
  VRegister vd = VRegister::Create(0, vd_bits);
  VRegister vn = VRegister::Create(1, vn_bits);
  VRegister vntmp = VRegister::Create(4, vn_bits);
  VRegister vres = VRegister::Create(5, vn_bits);

  VRegister vn_helper = VRegister::Create(1, vn_bits, vn_lane_count);
  VRegister vres_helper = VRegister::Create(5, vd_bits, vd_lane_count);

  // 'v*tmp_single' will be either 'Vt.B', 'Vt.H', 'Vt.S' or 'Vt.D'.
  VRegister vntmp_single = VRegister::Create(4, vn_lane_bits);

  // Same registers for use in the 'ext' instructions.
  VRegister vn_ext = (kDRegSize == vn_bits) ? vn.V8B() : vn.V16B();
  VRegister vntmp_ext = (kDRegSize == vn_bits) ? vntmp.V8B() : vntmp.V16B();

  __ Mov(out, results);

  __ Mov(inputs_d_base, inputs_d);

  __ Mov(inputs_n_base, inputs_n);
  __ Mov(inputs_n_last_vector,
         inputs_n + vn_lane_bytes * (inputs_n_length - vn_lane_count));

  __ Ldr(vd, MemOperand(inputs_d_base));

  __ Ldr(vn, MemOperand(inputs_n_last_vector));

  __ Mov(index_n, 0);
  __ Bind(&loop_n);

  __ Ldr(vntmp_single,
         MemOperand(inputs_n_base, index_n, LSL, vn_lane_bytes_log2));
  __ Ext(vn_ext, vn_ext, vntmp_ext, vn_lane_bytes);

  for (unsigned i = 0; i < inputs_imm1_length; i++) {
    for (unsigned j = 0; j < inputs_imm2_length; j++) {
      __ Mov(vres, vd);
      (masm.*helper)(vres_helper, inputs_imm1[i], vn_helper, inputs_imm2[j]);
      __ Str(vres, MemOperand(out, vd.SizeInBytes(), PostIndex));
    }
  }

  __ Add(index_n, index_n, 1);
  __ Cmp(index_n, inputs_n_length);
  __ B(lo, &loop_n);

  END();
  RUN();
  TEARDOWN();
}

// Test NEON instructions. The inputs_*[] and expected[] arrays should be
// arrays of rawbit representation of input values. This ensures that
// exact bit comparisons can be performed.
template <typename Td, typename Tn>
void TestOpImmOpImmNEON(const char* name,
                        TestOpImmOpImmVdUpdateNEONHelper_t helper,
                        const Td inputs_d[], const int inputs_imm1[],
                        unsigned inputs_imm1_length, const Tn inputs_n[],
                        unsigned inputs_n_length, const int inputs_imm2[],
                        unsigned inputs_imm2_length, const Td expected[],
                        unsigned expected_length, VectorFormat vd_form,
                        VectorFormat vn_form) {
  DCHECK_GT(inputs_n_length, 0U);
  DCHECK_GT(inputs_imm1_length, 0U);
  DCHECK_GT(inputs_imm2_length, 0U);

  const unsigned vd_lane_count = LaneCountFromFormat(vd_form);

  const unsigned results_length =
      inputs_n_length * inputs_imm1_length * inputs_imm2_length;

  std::vector<Td> results(results_length * vd_lane_count, 0);
  const unsigned lane_len_in_hex = MaxHexCharCount<Td, Tn>();

  TestOpImmOpImmNEON_Helper(
      helper, reinterpret_cast<uintptr_t>(inputs_d), inputs_imm1,
      inputs_imm1_length, reinterpret_cast<uintptr_t>(inputs_n),
      inputs_n_length, inputs_imm2, inputs_imm2_length,
      reinterpret_cast<uintptr_t>(results.data()), vd_form, vn_form);

  // Check the results.
  CHECK(expected_length == results_length);
  unsigned error_count = 0;
  unsigned counted_length = 0;
  const char* padding = "                    ";
  DCHECK(strlen(padding) >= (lane_len_in_hex + 1));
  for (unsigned n = 0; n < inputs_n_length; n++) {
    for (unsigned imm1 = 0; imm1 < inputs_imm1_length; imm1++) {
      for (unsigned imm2 = 0; imm2 < inputs_imm2_length; imm2++) {
        bool error_in_vector = false;

        counted_length++;

        for (unsigned lane = 0; lane < vd_lane_count; lane++) {
          unsigned output_index =
              (n * inputs_imm1_length * inputs_imm2_length * vd_lane_count) +
              (imm1 * inputs_imm2_length * vd_lane_count) +
              (imm2 * vd_lane_count) + lane;

          if (results[output_index] != expected[output_index]) {
            error_in_vector = true;
            break;
          }
        }

        if (error_in_vector && (++error_count <= kErrorReportLimit)) {
          printf("%s\n", name);
          printf(" Vd%.*s| Imm%.*s| Vn%.*s| Imm%.*s| Vd%.*s| Expected\n",
                 lane_len_in_hex + 1, padding, lane_len_in_hex, padding,
                 lane_len_in_hex + 1, padding, lane_len_in_hex, padding,
                 lane_len_in_hex + 1, padding);

          for (unsigned lane = 0; lane < vd_lane_count; lane++) {
            unsigned output_index =
                (n * inputs_imm1_length * inputs_imm2_length * vd_lane_count) +
                (imm1 * inputs_imm2_length * vd_lane_count) +
                (imm2 * vd_lane_count) + lane;
            unsigned input_index_n =
                (inputs_n_length - vd_lane_count + n + 1 + lane) %
                inputs_n_length;
            unsigned input_index_imm1 = imm1;
            unsigned input_index_imm2 = imm2;

            printf(
                "%c0x%0*" PRIx64 " | 0x%0*" PRIx64 " | 0x%0*" PRIx64
                " "
                "| 0x%0*" PRIx64 " | 0x%0*" PRIx64 " | 0x%0*" PRIx64 "\n",
                results[output_index] != expected[output_index] ? '*' : ' ',
                lane_len_in_hex, static_cast<uint64_t>(inputs_d[lane]),
                lane_len_in_hex,
                static_cast<uint64_t>(inputs_imm1[input_index_imm1]),
                lane_len_in_hex, static_cast<uint64_t>(inputs_n[input_index_n]),
                lane_len_in_hex,
                static_cast<uint64_t>(inputs_imm2[input_index_imm2]),
                lane_len_in_hex, static_cast<uint64_t>(results[output_index]),
                lane_len_in_hex, static_cast<uint64_t>(expected[output_index]));
          }
        }
      }
    }
  }
  DCHECK_EQ(counted_length, expected_length);
  if (error_count > kErrorReportLimit) {
    printf("%u other errors follow.\n", error_count - kErrorReportLimit);
  }
  CHECK(error_count == 0);
}

}  // anonymous namespace

// ==== NEON Tests. ====

// clang-format off

#define CALL_TEST_NEON_HELPER_1Op(mnemonic, vdform, vnform, input_n)      \
  Test1OpNEON(STRINGIFY(mnemonic) "_" STRINGIFY(vdform),                  \
              &MacroAssembler::mnemonic, input_n,                         \
              (sizeof(input_n) / sizeof(input_n[0])),                     \
              kExpected_NEON_##mnemonic##_##vdform,                       \
              kExpectedCount_NEON_##mnemonic##_##vdform, kFormat##vdform, \
              kFormat##vnform)

#define CALL_TEST_NEON_HELPER_1OpAcross(mnemonic, vdform, vnform, input_n)   \
  Test1OpAcrossNEON(                                                         \
      STRINGIFY(mnemonic) "_" STRINGIFY(vdform) "_" STRINGIFY(vnform),       \
      &MacroAssembler::mnemonic, input_n,                                    \
      (sizeof(input_n) / sizeof(input_n[0])),                                \
      kExpected_NEON_##mnemonic##_##vdform##_##vnform,                       \
      kExpectedCount_NEON_##mnemonic##_##vdform##_##vnform, kFormat##vdform, \
      kFormat##vnform)

#define CALL_TEST_NEON_HELPER_2Op(mnemonic, vdform, vnform, vmform, input_d, \
                                  input_n, input_m)                          \
  Test2OpNEON(STRINGIFY(mnemonic) "_" STRINGIFY(vdform),                     \
              &MacroAssembler::mnemonic, input_d, input_n,                   \
              (sizeof(input_n) / sizeof(input_n[0])), input_m,               \
              (sizeof(input_m) / sizeof(input_m[0])),                        \
              kExpected_NEON_##mnemonic##_##vdform,                          \
              kExpectedCount_NEON_##mnemonic##_##vdform, kFormat##vdform,    \
              kFormat##vnform, kFormat##vmform)

#define CALL_TEST_NEON_HELPER_2OpImm(mnemonic, vdform, vnform, input_n, \
                                     input_m)                           \
  Test2OpImmNEON(STRINGIFY(mnemonic) "_" STRINGIFY(vdform) "_2OPIMM",   \
                 &MacroAssembler::mnemonic, input_n,                    \
                 (sizeof(input_n) / sizeof(input_n[0])), input_m,       \
                 (sizeof(input_m) / sizeof(input_m[0])),                \
                 kExpected_NEON_##mnemonic##_##vdform##_2OPIMM,         \
                 kExpectedCount_NEON_##mnemonic##_##vdform##_2OPIMM,    \
                 kFormat##vdform, kFormat##vnform)

#define CALL_TEST_NEON_HELPER_ByElement(mnemonic, vdform, vnform, vmform,   \
                                        input_d, input_n, input_m, indices) \
  TestByElementNEON(                                                        \
      STRINGIFY(mnemonic) "_" STRINGIFY(vdform) "_" STRINGIFY(              \
          vnform) "_" STRINGIFY(vmform),                                    \
      &MacroAssembler::mnemonic, input_d, input_n,                          \
      (sizeof(input_n) / sizeof(input_n[0])), input_m,                      \
      (sizeof(input_m) / sizeof(input_m[0])), indices,                      \
      (sizeof(indices) / sizeof(indices[0])),                               \
      kExpected_NEON_##mnemonic##_##vdform##_##vnform##_##vmform,           \
      kExpectedCount_NEON_##mnemonic##_##vdform##_##vnform##_##vmform,      \
      kFormat##vdform, kFormat##vnform, kFormat##vmform)

#define CALL_TEST_NEON_HELPER_OpImmOpImm(helper, mnemonic, vdform, vnform,  \
                                         input_d, input_imm1, input_n,      \
                                         input_imm2)                        \
  TestOpImmOpImmNEON(STRINGIFY(mnemonic) "_" STRINGIFY(vdform), helper,     \
                     input_d, input_imm1,                                   \
                     (sizeof(input_imm1) / sizeof(input_imm1[0])), input_n, \
                     (sizeof(input_n) / sizeof(input_n[0])), input_imm2,    \
                     (sizeof(input_imm2) / sizeof(input_imm2[0])),          \
                     kExpected_NEON_##mnemonic##_##vdform,                  \
                     kExpectedCount_NEON_##mnemonic##_##vdform,             \
                     kFormat##vdform, kFormat##vnform)

#define CALL_TEST_NEON_HELPER_2SAME(mnemonic, variant, input) \
  CALL_TEST_NEON_HELPER_1Op(mnemonic, variant, variant, input)

#define DEFINE_TEST_NEON_2SAME_8B_16B(mnemonic, input)              \
  SIMTEST(mnemonic##_8B) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 8B, kInput8bits##input);  \
  }                                                                 \
  SIMTEST(mnemonic##_16B) {                                         \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 16B, kInput8bits##input); \
  }

#define DEFINE_TEST_NEON_2SAME_4H_8H(mnemonic, input)               \
  SIMTEST(mnemonic##_4H) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 4H, kInput16bits##input); \
  }                                                                 \
  SIMTEST(mnemonic##_8H) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 8H, kInput16bits##input); \
  }

#define DEFINE_TEST_NEON_2SAME_2S_4S(mnemonic, input)               \
  SIMTEST(mnemonic##_2S) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 2S, kInput32bits##input); \
  }                                                                 \
  SIMTEST(mnemonic##_4S) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 4S, kInput32bits##input); \
  }

#define DEFINE_TEST_NEON_2SAME_BH(mnemonic, input) \
  DEFINE_TEST_NEON_2SAME_8B_16B(mnemonic, input)   \
  DEFINE_TEST_NEON_2SAME_4H_8H(mnemonic, input)

#define DEFINE_TEST_NEON_2SAME_NO2D(mnemonic, input) \
  DEFINE_TEST_NEON_2SAME_BH(mnemonic, input)         \
  DEFINE_TEST_NEON_2SAME_2S_4S(mnemonic, input)

#define DEFINE_TEST_NEON_2SAME(mnemonic, input)                     \
  DEFINE_TEST_NEON_2SAME_NO2D(mnemonic, input)                      \
  SIMTEST(mnemonic##_2D) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 2D, kInput64bits##input); \
  }
#define DEFINE_TEST_NEON_2SAME_SD(mnemonic, input)                  \
  DEFINE_TEST_NEON_2SAME_2S_4S(mnemonic, input)                     \
  SIMTEST(mnemonic##_2D) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 2D, kInput64bits##input); \
  }

#define DEFINE_TEST_NEON_2SAME_FP(mnemonic, input)                  \
  SIMTEST(mnemonic##_2S) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 2S, kInputFloat##input);  \
  }                                                                 \
  SIMTEST(mnemonic##_4S) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 4S, kInputFloat##input);  \
  }                                                                 \
  SIMTEST(mnemonic##_2D) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, 2D, kInputDouble##input); \
  }

#define DEFINE_TEST_NEON_2SAME_FP_SCALAR(mnemonic, input)          \
  SIMTEST(mnemonic##_S) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, S, kInputFloat##input);  \
  }                                                                \
  SIMTEST(mnemonic##_D) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, D, kInputDouble##input); \
  }

#define DEFINE_TEST_NEON_2SAME_SCALAR_B(mnemonic, input)          \
  SIMTEST(mnemonic##_B) {                                         \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, B, kInput8bits##input); \
  }
#define DEFINE_TEST_NEON_2SAME_SCALAR_H(mnemonic, input)           \
  SIMTEST(mnemonic##_H) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, H, kInput16bits##input); \
  }
#define DEFINE_TEST_NEON_2SAME_SCALAR_S(mnemonic, input)           \
  SIMTEST(mnemonic##_S) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, S, kInput32bits##input); \
  }
#define DEFINE_TEST_NEON_2SAME_SCALAR_D(mnemonic, input)           \
  SIMTEST(mnemonic##_D) {                                          \
    CALL_TEST_NEON_HELPER_2SAME(mnemonic, D, kInput64bits##input); \
  }

#define DEFINE_TEST_NEON_2SAME_SCALAR(mnemonic, input) \
  DEFINE_TEST_NEON_2SAME_SCALAR_B(mnemonic, input)     \
  DEFINE_TEST_NEON_2SAME_SCALAR_H(mnemonic, input)     \
  DEFINE_TEST_NEON_2SAME_SCALAR_S(mnemonic, input)     \
  DEFINE_TEST_NEON_2SAME_SCALAR_D(mnemonic, input)

#define DEFINE_TEST_NEON_2SAME_SCALAR_SD(mnemonic, input) \
  DEFINE_TEST_NEON_2SAME_SCALAR_S(mnemonic, input)        \
  DEFINE_TEST_NEON_2SAME_SCALAR_D(mnemonic, input)

#define CALL_TEST_NEON_HELPER_ACROSS(mnemonic, vd_form, vn_form, input_n) \
  CALL_TEST_NEON_HELPER_1OpAcross(mnemonic, vd_form, vn_form, input_n)

#define DEFINE_TEST_NEON_ACROSS(mnemonic, input)                        \
  SIMTEST(mnemonic##_B_8B) {                                            \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, B, 8B, kInput8bits##input);  \
  }                                                                     \
  SIMTEST(mnemonic##_B_16B) {                                           \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, B, 16B, kInput8bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_H_4H) {                                            \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, H, 4H, kInput16bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_H_8H) {                                            \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, H, 8H, kInput16bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_S_4S) {                                            \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, S, 4S, kInput32bits##input); \
  }

#define DEFINE_TEST_NEON_ACROSS_LONG(mnemonic, input)                   \
  SIMTEST(mnemonic##_H_8B) {                                            \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, H, 8B, kInput8bits##input);  \
  }                                                                     \
  SIMTEST(mnemonic##_H_16B) {                                           \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, H, 16B, kInput8bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_S_4H) {                                            \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, S, 4H, kInput16bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_S_8H) {                                            \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, S, 8H, kInput16bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_D_4S) {                                            \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, D, 4S, kInput32bits##input); \
  }

#define DEFINE_TEST_NEON_ACROSS_FP(mnemonic, input)                    \
  SIMTEST(mnemonic##_S_4S) {                                           \
    CALL_TEST_NEON_HELPER_ACROSS(mnemonic, S, 4S, kInputFloat##input); \
  }

#define CALL_TEST_NEON_HELPER_2DIFF(mnemonic, vdform, vnform, input_n) \
  CALL_TEST_NEON_HELPER_1Op(mnemonic, vdform, vnform, input_n)

#define DEFINE_TEST_NEON_2DIFF_LONG(mnemonic, input)                    \
  SIMTEST(mnemonic##_4H) {                                              \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 4H, 8B, kInput8bits##input);  \
  }                                                                     \
  SIMTEST(mnemonic##_8H) {                                              \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 8H, 16B, kInput8bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_2S) {                                              \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 2S, 4H, kInput16bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_4S) {                                              \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 4S, 8H, kInput16bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_1D) {                                              \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 1D, 2S, kInput32bits##input); \
  }                                                                     \
  SIMTEST(mnemonic##_2D) {                                              \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 2D, 4S, kInput32bits##input); \
  }

#define DEFINE_TEST_NEON_2DIFF_NARROW(mnemonic, input)                      \
  SIMTEST(mnemonic##_8B) {                                                  \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 8B, 8H, kInput16bits##input);     \
  }                                                                         \
  SIMTEST(mnemonic##_4H) {                                                  \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 4H, 4S, kInput32bits##input);     \
  }                                                                         \
  SIMTEST(mnemonic##_2S) {                                                  \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 2S, 2D, kInput64bits##input);     \
  }                                                                         \
  SIMTEST(mnemonic##2_16B) {                                                \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic##2, 16B, 8H, kInput16bits##input); \
  }                                                                         \
  SIMTEST(mnemonic##2_8H) {                                                 \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic##2, 8H, 4S, kInput32bits##input);  \
  }                                                                         \
  SIMTEST(mnemonic##2_4S) {                                                 \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic##2, 4S, 2D, kInput64bits##input);  \
  }

#define DEFINE_TEST_NEON_2DIFF_FP_LONG(mnemonic, input)                     \
  SIMTEST(mnemonic##_4S) {                                                  \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 4S, 4H, kInputFloat16##input);    \
  }                                                                         \
  SIMTEST(mnemonic##_2D) {                                                  \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 2D, 2S, kInputFloat##input);      \
  }                                                                         \
  SIMTEST(mnemonic##2_4S) {                                                 \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic##2, 4S, 8H, kInputFloat16##input); \
  }                                                                         \
  SIMTEST(mnemonic##2_2D) {                                                 \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic##2, 2D, 4S, kInputFloat##input);   \
  }

#define DEFINE_TEST_NEON_2DIFF_FP_NARROW(mnemonic, input)                  \
  SIMTEST(mnemonic##_4H) {                                                 \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 4H, 4S, kInputFloat##input);     \
  }                                                                        \
  SIMTEST(mnemonic##_2S) {                                                 \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 2S, 2D, kInputDouble##input);    \
  }                                                                        \
  SIMTEST(mnemonic##2_8H) {                                                \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic##2, 8H, 4S, kInputFloat##input);  \
  }                                                                        \
  SIMTEST(mnemonic##2_4S) {                                                \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic##2, 4S, 2D, kInputDouble##input); \
  }

#define DEFINE_TEST_NEON_2DIFF_FP_NARROW_2S(mnemonic, input)               \
  SIMTEST(mnemonic##_2S) {                                                 \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, 2S, 2D, kInputDouble##input);    \
  }                                                                        \
  SIMTEST(mnemonic##2_4S) {                                                \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic##2, 4S, 2D, kInputDouble##input); \
  }

#define DEFINE_TEST_NEON_2DIFF_SCALAR_NARROW(mnemonic, input)         \
  SIMTEST(mnemonic##_B) {                                             \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, B, H, kInput16bits##input); \
  }                                                                   \
  SIMTEST(mnemonic##_H) {                                             \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, H, S, kInput32bits##input); \
  }                                                                   \
  SIMTEST(mnemonic##_S) {                                             \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, S, D, kInput64bits##input); \
  }

#define DEFINE_TEST_NEON_2DIFF_FP_SCALAR_SD(mnemonic, input)           \
  SIMTEST(mnemonic##_S) {                                              \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, S, 2S, kInputFloat##input);  \
  }                                                                    \
  SIMTEST(mnemonic##_D) {                                              \
    CALL_TEST_NEON_HELPER_2DIFF(mnemonic, D, 2D, kInputDouble##input); \
  }

#define CALL_TEST_NEON_HELPER_3SAME(mnemonic, variant, input_d, input_nm)   \
  {                                                                         \
    CALL_TEST_NEON_HELPER_2Op(mnemonic, variant, variant, variant, input_d, \
                              input_nm, input_nm);                          \
  }

#define DEFINE_TEST_NEON_3SAME_8B_16B(mnemonic, input)                    \
  SIMTEST(mnemonic##_8B) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 8B, kInput8bitsAccDestination,  \
                                kInput8bits##input);                      \
  }                                                                       \
  SIMTEST(mnemonic##_16B) {                                               \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 16B, kInput8bitsAccDestination, \
                                kInput8bits##input);                      \
  }

#define DEFINE_TEST_NEON_3SAME_HS(mnemonic, input)                        \
  SIMTEST(mnemonic##_4H) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 4H, kInput16bitsAccDestination, \
                                kInput16bits##input);                     \
  }                                                                       \
  SIMTEST(mnemonic##_8H) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 8H, kInput16bitsAccDestination, \
                                kInput16bits##input);                     \
  }                                                                       \
  SIMTEST(mnemonic##_2S) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 2S, kInput32bitsAccDestination, \
                                kInput32bits##input);                     \
  }                                                                       \
  SIMTEST(mnemonic##_4S) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 4S, kInput32bitsAccDestination, \
                                kInput32bits##input);                     \
  }

#define DEFINE_TEST_NEON_3SAME_NO2D(mnemonic, input) \
  DEFINE_TEST_NEON_3SAME_8B_16B(mnemonic, input)     \
  DEFINE_TEST_NEON_3SAME_HS(mnemonic, input)

#define DEFINE_TEST_NEON_3SAME(mnemonic, input)                           \
  DEFINE_TEST_NEON_3SAME_NO2D(mnemonic, input)                            \
  SIMTEST(mnemonic##_2D) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 2D, kInput64bitsAccDestination, \
                                kInput64bits##input);                     \
  }

#define DEFINE_TEST_NEON_3SAME_FP(mnemonic, input)                        \
  SIMTEST(mnemonic##_2S) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 2S, kInputFloatAccDestination,  \
                                kInputFloat##input);                      \
  }                                                                       \
  SIMTEST(mnemonic##_4S) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 4S, kInputFloatAccDestination,  \
                                kInputFloat##input);                      \
  }                                                                       \
  SIMTEST(mnemonic##_2D) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, 2D, kInputDoubleAccDestination, \
                                kInputDouble##input);                     \
  }

#define DEFINE_TEST_NEON_3SAME_SCALAR_D(mnemonic, input)                 \
  SIMTEST(mnemonic##_D) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, D, kInput64bitsAccDestination, \
                                kInput64bits##input);                    \
  }

#define DEFINE_TEST_NEON_3SAME_SCALAR_HS(mnemonic, input)                \
  SIMTEST(mnemonic##_H) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, H, kInput16bitsAccDestination, \
                                kInput16bits##input);                    \
  }                                                                      \
  SIMTEST(mnemonic##_S) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, S, kInput32bitsAccDestination, \
                                kInput32bits##input);                    \
  }

#define DEFINE_TEST_NEON_3SAME_SCALAR(mnemonic, input)                   \
  SIMTEST(mnemonic##_B) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, B, kInput8bitsAccDestination,  \
                                kInput8bits##input);                     \
  }                                                                      \
  SIMTEST(mnemonic##_H) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, H, kInput16bitsAccDestination, \
                                kInput16bits##input);                    \
  }                                                                      \
  SIMTEST(mnemonic##_S) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, S, kInput32bitsAccDestination, \
                                kInput32bits##input);                    \
  }                                                                      \
  SIMTEST(mnemonic##_D) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, D, kInput64bitsAccDestination, \
                                kInput64bits##input);                    \
  }

#define DEFINE_TEST_NEON_3SAME_FP_SCALAR(mnemonic, input)                \
  SIMTEST(mnemonic##_S) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, S, kInputFloatAccDestination,  \
                                kInputFloat##input);                     \
  }                                                                      \
  SIMTEST(mnemonic##_D) {                                                \
    CALL_TEST_NEON_HELPER_3SAME(mnemonic, D, kInputDoubleAccDestination, \
                                kInputDouble##input);                    \
  }

#define CALL_TEST_NEON_HELPER_3DIFF(mnemonic, vdform, vnform, vmform, input_d, \
                                    input_n, input_m)                          \
  {                                                                            \
    CALL_TEST_NEON_HELPER_2Op(mnemonic, vdform, vnform, vmform, input_d,       \
                              input_n, input_m);                               \
  }

#define DEFINE_TEST_NEON_3DIFF_LONG_8H(mnemonic, input)                  \
  SIMTEST(mnemonic##_8H) {                                               \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 8H, 8B, 8B,                    \
                                kInput16bitsAccDestination,              \
                                kInput8bits##input, kInput8bits##input); \
  }                                                                      \
  SIMTEST(mnemonic##2_8H) {                                              \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 8H, 16B, 16B,               \
                                kInput16bitsAccDestination,              \
                                kInput8bits##input, kInput8bits##input); \
  }

#define DEFINE_TEST_NEON_3DIFF_LONG_4S(mnemonic, input)                    \
  SIMTEST(mnemonic##_4S) {                                                 \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 4S, 4H, 4H,                      \
                                kInput32bitsAccDestination,                \
                                kInput16bits##input, kInput16bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##2_4S) {                                                \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 4S, 8H, 8H,                   \
                                kInput32bitsAccDestination,                \
                                kInput16bits##input, kInput16bits##input); \
  }

#define DEFINE_TEST_NEON_3DIFF_LONG_2D(mnemonic, input)                    \
  SIMTEST(mnemonic##_2D) {                                                 \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 2D, 2S, 2S,                      \
                                kInput64bitsAccDestination,                \
                                kInput32bits##input, kInput32bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##2_2D) {                                                \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 2D, 4S, 4S,                   \
                                kInput64bitsAccDestination,                \
                                kInput32bits##input, kInput32bits##input); \
  }

#define DEFINE_TEST_NEON_3DIFF_LONG_SD(mnemonic, input) \
  DEFINE_TEST_NEON_3DIFF_LONG_4S(mnemonic, input)       \
  DEFINE_TEST_NEON_3DIFF_LONG_2D(mnemonic, input)

#define DEFINE_TEST_NEON_3DIFF_LONG(mnemonic, input) \
  DEFINE_TEST_NEON_3DIFF_LONG_8H(mnemonic, input)    \
  DEFINE_TEST_NEON_3DIFF_LONG_4S(mnemonic, input)    \
  DEFINE_TEST_NEON_3DIFF_LONG_2D(mnemonic, input)

#define DEFINE_TEST_NEON_3DIFF_SCALAR_LONG_S(mnemonic, input)                  \
  SIMTEST(mnemonic##_S) {                                                      \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, S, H, H, kInput32bitsAccDestination, \
                                kInput16bits##input, kInput16bits##input);     \
  }

#define DEFINE_TEST_NEON_3DIFF_SCALAR_LONG_D(mnemonic, input)                  \
  SIMTEST(mnemonic##_D) {                                                      \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, D, S, S, kInput64bitsAccDestination, \
                                kInput32bits##input, kInput32bits##input);     \
  }

#define DEFINE_TEST_NEON_3DIFF_SCALAR_LONG_SD(mnemonic, input) \
  DEFINE_TEST_NEON_3DIFF_SCALAR_LONG_S(mnemonic, input)        \
  DEFINE_TEST_NEON_3DIFF_SCALAR_LONG_D(mnemonic, input)

#define DEFINE_TEST_NEON_3DIFF_WIDE(mnemonic, input)                       \
  SIMTEST(mnemonic##_8H) {                                                 \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 8H, 8H, 8B,                      \
                                kInput16bitsAccDestination,                \
                                kInput16bits##input, kInput8bits##input);  \
  }                                                                        \
  SIMTEST(mnemonic##_4S) {                                                 \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 4S, 4S, 4H,                      \
                                kInput32bitsAccDestination,                \
                                kInput32bits##input, kInput16bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##_2D) {                                                 \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 2D, 2D, 2S,                      \
                                kInput64bitsAccDestination,                \
                                kInput64bits##input, kInput32bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##2_8H) {                                                \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 8H, 8H, 16B,                  \
                                kInput16bitsAccDestination,                \
                                kInput16bits##input, kInput8bits##input);  \
  }                                                                        \
  SIMTEST(mnemonic##2_4S) {                                                \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 4S, 4S, 8H,                   \
                                kInput32bitsAccDestination,                \
                                kInput32bits##input, kInput16bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##2_2D) {                                                \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 2D, 2D, 4S,                   \
                                kInput64bitsAccDestination,                \
                                kInput64bits##input, kInput32bits##input); \
  }

#define DEFINE_TEST_NEON_3DIFF_NARROW(mnemonic, input)                     \
  SIMTEST(mnemonic##_8B) {                                                 \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 8B, 8H, 8H,                      \
                                kInput8bitsAccDestination,                 \
                                kInput16bits##input, kInput16bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##_4H) {                                                 \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 4H, 4S, 4S,                      \
                                kInput16bitsAccDestination,                \
                                kInput32bits##input, kInput32bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##_2S) {                                                 \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic, 2S, 2D, 2D,                      \
                                kInput32bitsAccDestination,                \
                                kInput64bits##input, kInput64bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##2_16B) {                                               \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 16B, 8H, 8H,                  \
                                kInput8bitsAccDestination,                 \
                                kInput16bits##input, kInput16bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##2_8H) {                                                \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 8H, 4S, 4S,                   \
                                kInput16bitsAccDestination,                \
                                kInput32bits##input, kInput32bits##input); \
  }                                                                        \
  SIMTEST(mnemonic##2_4S) {                                                \
    CALL_TEST_NEON_HELPER_3DIFF(mnemonic##2, 4S, 2D, 2D,                   \
                                kInput32bitsAccDestination,                \
                                kInput64bits##input, kInput64bits##input); \
  }

#define CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, vdform, vnform, input_n, \
                                     input_imm)                         \
  {                                                                     \
    CALL_TEST_NEON_HELPER_2OpImm(mnemonic, vdform, vnform, input_n,     \
                                 input_imm);                            \
  }

#define DEFINE_TEST_NEON_2OPIMM(mnemonic, input, input_imm)              \
  SIMTEST(mnemonic##_8B_2OPIMM) {                                        \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 8B, 8B, kInput8bits##input,   \
                                 kInput8bitsImm##input_imm);             \
  }                                                                      \
  SIMTEST(mnemonic##_16B_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 16B, 16B, kInput8bits##input, \
                                 kInput8bitsImm##input_imm);             \
  }                                                                      \
  SIMTEST(mnemonic##_4H_2OPIMM) {                                        \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4H, 4H, kInput16bits##input,  \
                                 kInput16bitsImm##input_imm);            \
  }                                                                      \
  SIMTEST(mnemonic##_8H_2OPIMM) {                                        \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 8H, 8H, kInput16bits##input,  \
                                 kInput16bitsImm##input_imm);            \
  }                                                                      \
  SIMTEST(mnemonic##_2S_2OPIMM) {                                        \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2S, 2S, kInput32bits##input,  \
                                 kInput32bitsImm##input_imm);            \
  }                                                                      \
  SIMTEST(mnemonic##_4S_2OPIMM) {                                        \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4S, 4S, kInput32bits##input,  \
                                 kInput32bitsImm##input_imm);            \
  }                                                                      \
  SIMTEST(mnemonic##_2D_2OPIMM) {                                        \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2D, 2D, kInput64bits##input,  \
                                 kInput64bitsImm##input_imm);            \
  }

#define DEFINE_TEST_NEON_2OPIMM_COPY(mnemonic, input, input_imm)       \
  SIMTEST(mnemonic##_8B_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 8B, B, kInput8bits##input,  \
                                 kInput8bitsImm##input_imm);           \
  }                                                                    \
  SIMTEST(mnemonic##_16B_2OPIMM) {                                     \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 16B, B, kInput8bits##input, \
                                 kInput8bitsImm##input_imm);           \
  }                                                                    \
  SIMTEST(mnemonic##_4H_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4H, H, kInput16bits##input, \
                                 kInput16bitsImm##input_imm);          \
  }                                                                    \
  SIMTEST(mnemonic##_8H_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 8H, H, kInput16bits##input, \
                                 kInput16bitsImm##input_imm);          \
  }                                                                    \
  SIMTEST(mnemonic##_2S_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2S, S, kInput32bits##input, \
                                 kInput32bitsImm##input_imm);          \
  }                                                                    \
  SIMTEST(mnemonic##_4S_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4S, S, kInput32bits##input, \
                                 kInput32bitsImm##input_imm);          \
  }                                                                    \
  SIMTEST(mnemonic##_2D_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2D, D, kInput64bits##input, \
                                 kInput64bitsImm##input_imm);          \
  }

#define DEFINE_TEST_NEON_2OPIMM_NARROW(mnemonic, input, input_imm)          \
  SIMTEST(mnemonic##_8B_2OPIMM) {                                           \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 8B, 8H, kInput16bits##input,     \
                                 kInput8bitsImm##input_imm);                \
  }                                                                         \
  SIMTEST(mnemonic##_4H_2OPIMM) {                                           \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4H, 4S, kInput32bits##input,     \
                                 kInput16bitsImm##input_imm);               \
  }                                                                         \
  SIMTEST(mnemonic##_2S_2OPIMM) {                                           \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2S, 2D, kInput64bits##input,     \
                                 kInput32bitsImm##input_imm);               \
  }                                                                         \
  SIMTEST(mnemonic##2_16B_2OPIMM) {                                         \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic##2, 16B, 8H, kInput16bits##input, \
                                 kInput8bitsImm##input_imm);                \
  }                                                                         \
  SIMTEST(mnemonic##2_8H_2OPIMM) {                                          \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic##2, 8H, 4S, kInput32bits##input,  \
                                 kInput16bitsImm##input_imm);               \
  }                                                                         \
  SIMTEST(mnemonic##2_4S_2OPIMM) {                                          \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic##2, 4S, 2D, kInput64bits##input,  \
                                 kInput32bitsImm##input_imm);               \
  }

#define DEFINE_TEST_NEON_2OPIMM_SCALAR_NARROW(mnemonic, input, input_imm) \
  SIMTEST(mnemonic##_B_2OPIMM) {                                          \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, B, H, kInput16bits##input,     \
                                 kInput8bitsImm##input_imm);              \
  }                                                                       \
  SIMTEST(mnemonic##_H_2OPIMM) {                                          \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, H, S, kInput32bits##input,     \
                                 kInput16bitsImm##input_imm);             \
  }                                                                       \
  SIMTEST(mnemonic##_S_2OPIMM) {                                          \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, S, D, kInput64bits##input,     \
                                 kInput32bitsImm##input_imm);             \
  }

#define DEFINE_TEST_NEON_2OPIMM_FCMP_ZERO(mnemonic, input, input_imm)   \
  SIMTEST(mnemonic##_2S_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2S, 2S, kInputFloat##Basic,  \
                                 kInputDoubleImm##input_imm)            \
  }                                                                     \
  SIMTEST(mnemonic##_4S_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4S, 4S, kInputFloat##input,  \
                                 kInputDoubleImm##input_imm);           \
  }                                                                     \
  SIMTEST(mnemonic##_2D_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2D, 2D, kInputDouble##input, \
                                 kInputDoubleImm##input_imm);           \
  }

#define DEFINE_TEST_NEON_2OPIMM_FP(mnemonic, input, input_imm)          \
  SIMTEST(mnemonic##_2S_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2S, 2S, kInputFloat##Basic,  \
                                 kInput32bitsImm##input_imm)            \
  }                                                                     \
  SIMTEST(mnemonic##_4S_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4S, 4S, kInputFloat##input,  \
                                 kInput32bitsImm##input_imm)            \
  }                                                                     \
  SIMTEST(mnemonic##_2D_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2D, 2D, kInputDouble##input, \
                                 kInput64bitsImm##input_imm)            \
  }

#define DEFINE_TEST_NEON_2OPIMM_FP_SCALAR(mnemonic, input, input_imm) \
  SIMTEST(mnemonic##_S_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, S, S, kInputFloat##Basic,  \
                                 kInput32bitsImm##input_imm)          \
  }                                                                   \
  SIMTEST(mnemonic##_D_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, D, D, kInputDouble##input, \
                                 kInput64bitsImm##input_imm)          \
  }

#define DEFINE_TEST_NEON_2OPIMM_SD(mnemonic, input, input_imm)          \
  SIMTEST(mnemonic##_2S_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2S, 2S, kInput32bits##input, \
                                 kInput32bitsImm##input_imm);           \
  }                                                                     \
  SIMTEST(mnemonic##_4S_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4S, 4S, kInput32bits##input, \
                                 kInput32bitsImm##input_imm);           \
  }                                                                     \
  SIMTEST(mnemonic##_2D_2OPIMM) {                                       \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2D, 2D, kInput64bits##input, \
                                 kInput64bitsImm##input_imm);           \
  }

#define DEFINE_TEST_NEON_2OPIMM_SCALAR_D(mnemonic, input, input_imm)  \
  SIMTEST(mnemonic##_D_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, D, D, kInput64bits##input, \
                                 kInput64bitsImm##input_imm);         \
  }

#define DEFINE_TEST_NEON_2OPIMM_SCALAR_SD(mnemonic, input, input_imm) \
  SIMTEST(mnemonic##_S_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, S, S, kInput32bits##input, \
                                 kInput32bitsImm##input_imm);         \
  }                                                                   \
  DEFINE_TEST_NEON_2OPIMM_SCALAR_D(mnemonic, input, input_imm)

#define DEFINE_TEST_NEON_2OPIMM_FP_SCALAR_D(mnemonic, input, input_imm) \
  SIMTEST(mnemonic##_D_2OPIMM) {                                        \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, D, D, kInputDouble##input,   \
                                 kInputDoubleImm##input_imm);           \
  }

#define DEFINE_TEST_NEON_2OPIMM_FP_SCALAR_SD(mnemonic, input, input_imm) \
  SIMTEST(mnemonic##_S_2OPIMM) {                                         \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, S, S, kInputFloat##input,     \
                                 kInputDoubleImm##input_imm);            \
  }                                                                      \
  DEFINE_TEST_NEON_2OPIMM_FP_SCALAR_D(mnemonic, input, input_imm)

#define DEFINE_TEST_NEON_2OPIMM_SCALAR(mnemonic, input, input_imm)    \
  SIMTEST(mnemonic##_B_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, B, B, kInput8bits##input,  \
                                 kInput8bitsImm##input_imm);          \
  }                                                                   \
  SIMTEST(mnemonic##_H_2OPIMM) {                                      \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, H, H, kInput16bits##input, \
                                 kInput16bitsImm##input_imm);         \
  }                                                                   \
  DEFINE_TEST_NEON_2OPIMM_SCALAR_SD(mnemonic, input, input_imm)

#define DEFINE_TEST_NEON_2OPIMM_LONG(mnemonic, input, input_imm)           \
  SIMTEST(mnemonic##_8H_2OPIMM) {                                          \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 8H, 8B, kInput8bits##input,     \
                                 kInput8bitsImm##input_imm);               \
  }                                                                        \
  SIMTEST(mnemonic##_4S_2OPIMM) {                                          \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 4S, 4H, kInput16bits##input,    \
                                 kInput16bitsImm##input_imm);              \
  }                                                                        \
  SIMTEST(mnemonic##_2D_2OPIMM) {                                          \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic, 2D, 2S, kInput32bits##input,    \
                                 kInput32bitsImm##input_imm);              \
  }                                                                        \
  SIMTEST(mnemonic##2_8H_2OPIMM) {                                         \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic##2, 8H, 16B, kInput8bits##input, \
                                 kInput8bitsImm##input_imm);               \
  }                                                                        \
  SIMTEST(mnemonic##2_4S_2OPIMM) {                                         \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic##2, 4S, 8H, kInput16bits##input, \
                                 kInput16bitsImm##input_imm);              \
  }                                                                        \
  SIMTEST(mnemonic##2_2D_2OPIMM) {                                         \
    CALL_TEST_NEON_HELPER_2OPIMM(mnemonic##2, 2D, 4S, kInput32bits##input, \
                                 kInput32bitsImm##input_imm);              \
  }

#define CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, vdform, vnform, vmform,      \
                                        input_d, input_n, input_m, indices)    \
  {                                                                            \
    CALL_TEST_NEON_HELPER_ByElement(mnemonic, vdform, vnform, vmform, input_d, \
                                    input_n, input_m, indices);                \
  }

#define DEFINE_TEST_NEON_BYELEMENT(mnemonic, input_d, input_n, input_m)    \
  SIMTEST(mnemonic##_4H_4H_H) {                                            \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                       \
        mnemonic, 4H, 4H, H, kInput16bits##input_d, kInput16bits##input_n, \
        kInput16bits##input_m, kInputHIndices);                            \
  }                                                                        \
  SIMTEST(mnemonic##_8H_8H_H) {                                            \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                       \
        mnemonic, 8H, 8H, H, kInput16bits##input_d, kInput16bits##input_n, \
        kInput16bits##input_m, kInputHIndices);                            \
  }                                                                        \
  SIMTEST(mnemonic##_2S_2S_S) {                                            \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                       \
        mnemonic, 2S, 2S, S, kInput32bits##input_d, kInput32bits##input_n, \
        kInput32bits##input_m, kInputSIndices);                            \
  }                                                                        \
  SIMTEST(mnemonic##_4S_4S_S) {                                            \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                       \
        mnemonic, 4S, 4S, S, kInput32bits##input_d, kInput32bits##input_n, \
        kInput32bits##input_m, kInputSIndices);                            \
  }

#define DEFINE_TEST_NEON_BYELEMENT_SCALAR(mnemonic, input_d, input_n, input_m) \
  SIMTEST(mnemonic##_H_H_H) {                                                  \
    CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, H, H, H, kInput16bits##input_d,  \
                                    kInput16bits##input_n,                     \
                                    kInput16bits##input_m, kInputHIndices);    \
  }                                                                            \
  SIMTEST(mnemonic##_S_S_S) {                                                  \
    CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, S, S, S, kInput32bits##input_d,  \
                                    kInput32bits##input_n,                     \
                                    kInput32bits##input_m, kInputSIndices);    \
  }

#define DEFINE_TEST_NEON_FP_BYELEMENT(mnemonic, input_d, input_n, input_m)     \
  SIMTEST(mnemonic##_2S_2S_S) {                                                \
    CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, 2S, 2S, S, kInputFloat##input_d, \
                                    kInputFloat##input_n,                      \
                                    kInputFloat##input_m, kInputSIndices);     \
  }                                                                            \
  SIMTEST(mnemonic##_4S_4S_S) {                                                \
    CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, 4S, 4S, S, kInputFloat##input_d, \
                                    kInputFloat##input_n,                      \
                                    kInputFloat##input_m, kInputSIndices);     \
  }                                                                            \
  SIMTEST(mnemonic##_2D_2D_D) {                                                \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                           \
        mnemonic, 2D, 2D, D, kInputDouble##input_d, kInputDouble##input_n,     \
        kInputDouble##input_m, kInputDIndices);                                \
  }

#define DEFINE_TEST_NEON_FP_BYELEMENT_SCALAR(mnemonic, inp_d, inp_n, inp_m)   \
  SIMTEST(mnemonic##_S_S_S) {                                                 \
    CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, S, S, S, kInputFloat##inp_d,    \
                                    kInputFloat##inp_n, kInputFloat##inp_m,   \
                                    kInputSIndices);                          \
  }                                                                           \
  SIMTEST(mnemonic##_D_D_D) {                                                 \
    CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, D, D, D, kInputDouble##inp_d,   \
                                    kInputDouble##inp_n, kInputDouble##inp_m, \
                                    kInputDIndices);                          \
  }

#define DEFINE_TEST_NEON_BYELEMENT_DIFF(mnemonic, input_d, input_n, input_m)  \
  SIMTEST(mnemonic##_4S_4H_H) {                                               \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                          \
        mnemonic, 4S, 4H, H, kInput32bits##input_d, kInput16bits##input_n,    \
        kInput16bits##input_m, kInputHIndices);                               \
  }                                                                           \
  SIMTEST(mnemonic##2_4S_8H_H) {                                              \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                          \
        mnemonic##2, 4S, 8H, H, kInput32bits##input_d, kInput16bits##input_n, \
        kInput16bits##input_m, kInputHIndices);                               \
  }                                                                           \
  SIMTEST(mnemonic##_2D_2S_S) {                                               \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                          \
        mnemonic, 2D, 2S, S, kInput64bits##input_d, kInput32bits##input_n,    \
        kInput32bits##input_m, kInputSIndices);                               \
  }                                                                           \
  SIMTEST(mnemonic##2_2D_4S_S) {                                              \
    CALL_TEST_NEON_HELPER_BYELEMENT(                                          \
        mnemonic##2, 2D, 4S, S, kInput64bits##input_d, kInput32bits##input_n, \
        kInput32bits##input_m, kInputSIndices);                               \
  }

#define DEFINE_TEST_NEON_BYELEMENT_DIFF_SCALAR(mnemonic, input_d, input_n,    \
                                               input_m)                       \
  SIMTEST(mnemonic##_S_H_H) {                                                 \
    CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, S, H, H, kInput32bits##input_d, \
                                    kInput16bits##input_n,                    \
                                    kInput16bits##input_m, kInputHIndices);   \
  }                                                                           \
  SIMTEST(mnemonic##_D_S_S) {                                                 \
    CALL_TEST_NEON_HELPER_BYELEMENT(mnemonic, D, S, S, kInput64bits##input_d, \
                                    kInput32bits##input_n,                    \
                                    kInput32bits##input_m, kInputSIndices);   \
  }

#define CALL_TEST_NEON_HELPER_2OP2IMM(mnemonic, variant, input_d, input_imm1, \
                                      input_n, input_imm2)                    \
  {                                                                           \
    CALL_TEST_NEON_HELPER_OpImmOpImm(&MacroAssembler::mnemonic, mnemonic,     \
                                     variant, variant, input_d, input_imm1,   \
                                     input_n, input_imm2);                    \
  }

#define DEFINE_TEST_NEON_2OP2IMM(mnemonic, input_d, input_imm1, input_n,  \
                                 input_imm2)                              \
  SIMTEST(mnemonic##_B) {                                                 \
    CALL_TEST_NEON_HELPER_2OP2IMM(                                        \
        mnemonic, 16B, kInput8bits##input_d, kInput8bitsImm##input_imm1,  \
        kInput8bits##input_n, kInput8bitsImm##input_imm2);                \
  }                                                                       \
  SIMTEST(mnemonic##_H) {                                                 \
    CALL_TEST_NEON_HELPER_2OP2IMM(                                        \
        mnemonic, 8H, kInput16bits##input_d, kInput16bitsImm##input_imm1, \
        kInput16bits##input_n, kInput16bitsImm##input_imm2);              \
  }                                                                       \
  SIMTEST(mnemonic##_S) {                                                 \
    CALL_TEST_NEON_HELPER_2OP2IMM(                                        \
        mnemonic, 4S, kInput32bits##input_d, kInput32bitsImm##input_imm1, \
        kInput32bits##input_n, kInput32bitsImm##input_imm2);              \
  }                                                                       \
  SIMTEST(mnemonic##_D) {                                                 \
    CALL_TEST_NEON_HELPER_2OP2IMM(                                        \
        mnemonic, 2D, kInput64bits##input_d, kInput64bitsImm##input_imm1, \
        kInput64bits##input_n, kInput64bitsImm##input_imm2);              \
  }

// clang-format on

// Advanced SIMD copy.
DEFINE_TEST_NEON_2OP2IMM(ins, Basic, LaneCountFromZero, Basic,
                         LaneCountFromZero)
DEFINE_TEST_NEON_2OPIMM_COPY(dup, Basic, LaneCountFromZero)

// Advanced SIMD scalar copy.
DEFINE_TEST_NEON_2OPIMM_SCALAR(dup, Basic, LaneCountFromZero)

// Advanced SIMD three same.
DEFINE_TEST_NEON_3SAME_NO2D(shadd, Basic)
DEFINE_TEST_NEON_3SAME(sqadd, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(srhadd, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(shsub, Basic)
DEFINE_TEST_NEON_3SAME(sqsub, Basic)
DEFINE_TEST_NEON_3SAME(cmgt, Basic)
DEFINE_TEST_NEON_3SAME(cmge, Basic)
DEFINE_TEST_NEON_3SAME(sshl, Basic)
DEFINE_TEST_NEON_3SAME(sqshl, Basic)
DEFINE_TEST_NEON_3SAME(srshl, Basic)
DEFINE_TEST_NEON_3SAME(sqrshl, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(smax, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(smin, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(sabd, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(saba, Basic)
DEFINE_TEST_NEON_3SAME(add, Basic)
DEFINE_TEST_NEON_3SAME(cmtst, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(mla, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(mul, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(smaxp, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(sminp, Basic)
DEFINE_TEST_NEON_3SAME_HS(sqdmulh, Basic)
DEFINE_TEST_NEON_3SAME(addp, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmaxnm, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmla, Basic)
DEFINE_TEST_NEON_3SAME_FP(fadd, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmulx, Basic)
DEFINE_TEST_NEON_3SAME_FP(fcmeq, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmax, Basic)
DEFINE_TEST_NEON_3SAME_FP(frecps, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(and_, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(bic, Basic)
DEFINE_TEST_NEON_3SAME_FP(fminnm, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmls, Basic)
DEFINE_TEST_NEON_3SAME_FP(fsub, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmin, Basic)
DEFINE_TEST_NEON_3SAME_FP(frsqrts, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(orr, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(orn, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(uhadd, Basic)
DEFINE_TEST_NEON_3SAME(uqadd, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(urhadd, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(uhsub, Basic)
DEFINE_TEST_NEON_3SAME(uqsub, Basic)
DEFINE_TEST_NEON_3SAME(cmhi, Basic)
DEFINE_TEST_NEON_3SAME(cmhs, Basic)
DEFINE_TEST_NEON_3SAME(ushl, Basic)
DEFINE_TEST_NEON_3SAME(uqshl, Basic)
DEFINE_TEST_NEON_3SAME(urshl, Basic)
DEFINE_TEST_NEON_3SAME(uqrshl, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(umax, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(umin, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(uabd, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(uaba, Basic)
DEFINE_TEST_NEON_3SAME(sub, Basic)
DEFINE_TEST_NEON_3SAME(cmeq, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(mls, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(pmul, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(uminp, Basic)
DEFINE_TEST_NEON_3SAME_NO2D(umaxp, Basic)
DEFINE_TEST_NEON_3SAME_HS(sqrdmulh, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmaxnmp, Basic)
DEFINE_TEST_NEON_3SAME_FP(faddp, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmul, Basic)
DEFINE_TEST_NEON_3SAME_FP(fcmge, Basic)
DEFINE_TEST_NEON_3SAME_FP(facge, Basic)
DEFINE_TEST_NEON_3SAME_FP(fmaxp, Basic)
DEFINE_TEST_NEON_3SAME_FP(fdiv, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(eor, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(bsl, Basic)
DEFINE_TEST_NEON_3SAME_FP(fminnmp, Basic)
DEFINE_TEST_NEON_3SAME_FP(fabd, Basic)
DEFINE_TEST_NEON_3SAME_FP(fcmgt, Basic)
DEFINE_TEST_NEON_3SAME_FP(facgt, Basic)
DEFINE_TEST_NEON_3SAME_FP(fminp, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(bit, Basic)
DEFINE_TEST_NEON_3SAME_8B_16B(bif, Basic)

// Advanced SIMD scalar three same.
DEFINE_TEST_NEON_3SAME_SCALAR(sqadd, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR(sqsub, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(cmgt, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(cmge, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(sshl, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR(sqshl, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(srshl, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR(sqrshl, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(add, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(cmtst, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_HS(sqdmulh, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(fmulx, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(fcmeq, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(frecps, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(frsqrts, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(uqadd, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(uqsub, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(cmhi, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(cmhs, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(ushl, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR(uqshl, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(urshl, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR(uqrshl, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(sub, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_D(cmeq, Basic)
DEFINE_TEST_NEON_3SAME_SCALAR_HS(sqrdmulh, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(fcmge, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(facge, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(fabd, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(fcmgt, Basic)
DEFINE_TEST_NEON_3SAME_FP_SCALAR(facgt, Basic)

// Advanced SIMD three different.
DEFINE_TEST_NEON_3DIFF_LONG(saddl, Basic)
DEFINE_TEST_NEON_3DIFF_WIDE(saddw, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(ssubl, Basic)
DEFINE_TEST_NEON_3DIFF_WIDE(ssubw, Basic)
DEFINE_TEST_NEON_3DIFF_NARROW(addhn, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(sabal, Basic)
DEFINE_TEST_NEON_3DIFF_NARROW(subhn, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(sabdl, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(smlal, Basic)
DEFINE_TEST_NEON_3DIFF_LONG_SD(sqdmlal, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(smlsl, Basic)
DEFINE_TEST_NEON_3DIFF_LONG_SD(sqdmlsl, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(smull, Basic)
DEFINE_TEST_NEON_3DIFF_LONG_SD(sqdmull, Basic)
DEFINE_TEST_NEON_3DIFF_LONG_8H(pmull, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(uaddl, Basic)
DEFINE_TEST_NEON_3DIFF_WIDE(uaddw, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(usubl, Basic)
DEFINE_TEST_NEON_3DIFF_WIDE(usubw, Basic)
DEFINE_TEST_NEON_3DIFF_NARROW(raddhn, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(uabal, Basic)
DEFINE_TEST_NEON_3DIFF_NARROW(rsubhn, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(uabdl, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(umlal, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(umlsl, Basic)
DEFINE_TEST_NEON_3DIFF_LONG(umull, Basic)

// Advanced SIMD scalar three different.
DEFINE_TEST_NEON_3DIFF_SCALAR_LONG_SD(sqdmlal, Basic)
DEFINE_TEST_NEON_3DIFF_SCALAR_LONG_SD(sqdmlsl, Basic)
DEFINE_TEST_NEON_3DIFF_SCALAR_LONG_SD(sqdmull, Basic)

// Advanced SIMD scalar pairwise.
SIMTEST(addp_SCALAR) {
  CALL_TEST_NEON_HELPER_2DIFF(addp, D, 2D, kInput64bitsBasic);
}
DEFINE_TEST_NEON_2DIFF_FP_SCALAR_SD(fmaxnmp, Basic)
DEFINE_TEST_NEON_2DIFF_FP_SCALAR_SD(faddp, Basic)
DEFINE_TEST_NEON_2DIFF_FP_SCALAR_SD(fmaxp, Basic)
DEFINE_TEST_NEON_2DIFF_FP_SCALAR_SD(fminnmp, Basic)
DEFINE_TEST_NEON_2DIFF_FP_SCALAR_SD(fminp, Basic)

// Advanced SIMD shift by immediate.
DEFINE_TEST_NEON_2OPIMM(sshr, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(ssra, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(srshr, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(srsra, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(shl, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM(sqshl, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_NARROW(shrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_NARROW(rshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_NARROW(sqshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_NARROW(sqrshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_LONG(sshll, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_SD(scvtf, FixedPointConversions,
                           TypeWidthFromZeroToWidth)
DEFINE_TEST_NEON_2OPIMM_FP(fcvtzs, Conversions, TypeWidthFromZeroToWidth)
DEFINE_TEST_NEON_2OPIMM(ushr, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(usra, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(urshr, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(ursra, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(sri, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM(sli, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM(sqshlu, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM(uqshl, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_NARROW(sqshrun, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_NARROW(sqrshrun, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_NARROW(uqshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_NARROW(uqrshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_LONG(ushll, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_SD(ucvtf, FixedPointConversions,
                           TypeWidthFromZeroToWidth)
DEFINE_TEST_NEON_2OPIMM_FP(fcvtzu, Conversions, TypeWidthFromZeroToWidth)

// Advanced SIMD scalar shift by immediate..
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(sshr, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(ssra, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(srshr, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(srsra, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(shl, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_SCALAR(sqshl, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_SCALAR_NARROW(sqshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_NARROW(sqrshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_SD(scvtf, FixedPointConversions,
                                  TypeWidthFromZeroToWidth)
DEFINE_TEST_NEON_2OPIMM_FP_SCALAR(fcvtzs, Conversions, TypeWidthFromZeroToWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(ushr, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(usra, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(urshr, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(ursra, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(sri, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(sli, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_SCALAR(sqshlu, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_SCALAR(uqshl, Basic, TypeWidthFromZero)
DEFINE_TEST_NEON_2OPIMM_SCALAR_NARROW(sqshrun, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_NARROW(sqrshrun, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_NARROW(uqshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_NARROW(uqrshrn, Basic, TypeWidth)
DEFINE_TEST_NEON_2OPIMM_SCALAR_SD(ucvtf, FixedPointConversions,
                                  TypeWidthFromZeroToWidth)
DEFINE_TEST_NEON_2OPIMM_FP_SCALAR(fcvtzu, Conversions, TypeWidthFromZeroToWidth)

// Advanced SIMD two-register miscellaneous.
DEFINE_TEST_NEON_2SAME_NO2D(rev64, Basic)
DEFINE_TEST_NEON_2SAME_8B_16B(rev16, Basic)
DEFINE_TEST_NEON_2DIFF_LONG(saddlp, Basic)
DEFINE_TEST_NEON_2SAME(suqadd, Basic)
DEFINE_TEST_NEON_2SAME_NO2D(cls, Basic)
DEFINE_TEST_NEON_2SAME_8B_16B(cnt, Basic)
DEFINE_TEST_NEON_2DIFF_LONG(sadalp, Basic)
DEFINE_TEST_NEON_2SAME(sqabs, Basic)
DEFINE_TEST_NEON_2OPIMM(cmgt, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM(cmeq, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM(cmlt, Basic, Zero)
DEFINE_TEST_NEON_2SAME(abs, Basic)
DEFINE_TEST_NEON_2DIFF_NARROW(xtn, Basic)
DEFINE_TEST_NEON_2DIFF_NARROW(sqxtn, Basic)
DEFINE_TEST_NEON_2DIFF_FP_NARROW(fcvtn, Conversions)
DEFINE_TEST_NEON_2DIFF_FP_LONG(fcvtl, Conversions)
DEFINE_TEST_NEON_2SAME_FP(frintn, Conversions)
DEFINE_TEST_NEON_2SAME_FP(frintm, Conversions)
DEFINE_TEST_NEON_2SAME_FP(fcvtns, Conversions)
DEFINE_TEST_NEON_2SAME_FP(fcvtms, Conversions)
DEFINE_TEST_NEON_2SAME_FP(fcvtas, Conversions)
// SCVTF (vector, integer) covered by SCVTF(vector, fixed point) with fbits 0.
DEFINE_TEST_NEON_2OPIMM_FCMP_ZERO(fcmgt, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_FCMP_ZERO(fcmeq, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_FCMP_ZERO(fcmlt, Basic, Zero)
DEFINE_TEST_NEON_2SAME_FP(fabs, Basic)
DEFINE_TEST_NEON_2SAME_FP(frintp, Conversions)
DEFINE_TEST_NEON_2SAME_FP(frintz, Conversions)
DEFINE_TEST_NEON_2SAME_FP(fcvtps, Conversions)
// FCVTZS(vector, integer) covered by FCVTZS(vector, fixed point) with fbits 0.
DEFINE_TEST_NEON_2SAME_2S_4S(urecpe, Basic)
DEFINE_TEST_NEON_2SAME_FP(frecpe, Basic)
DEFINE_TEST_NEON_2SAME_BH(rev32, Basic)
DEFINE_TEST_NEON_2DIFF_LONG(uaddlp, Basic)
DEFINE_TEST_NEON_2SAME(usqadd, Basic)
DEFINE_TEST_NEON_2SAME_NO2D(clz, Basic)
DEFINE_TEST_NEON_2DIFF_LONG(uadalp, Basic)
DEFINE_TEST_NEON_2SAME(sqneg, Basic)
DEFINE_TEST_NEON_2OPIMM(cmge, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM(cmle, Basic, Zero)
DEFINE_TEST_NEON_2SAME(neg, Basic)
DEFINE_TEST_NEON_2DIFF_NARROW(sqxtun, Basic)
DEFINE_TEST_NEON_2OPIMM_LONG(shll, Basic, SHLL)
DEFINE_TEST_NEON_2DIFF_NARROW(uqxtn, Basic)
DEFINE_TEST_NEON_2DIFF_FP_NARROW_2S(fcvtxn, Conversions)
DEFINE_TEST_NEON_2SAME_FP(frinta, Conversions)
DEFINE_TEST_NEON_2SAME_FP(frintx, Conversions)
DEFINE_TEST_NEON_2SAME_FP(fcvtnu, Conversions)
DEFINE_TEST_NEON_2SAME_FP(fcvtmu, Conversions)
DEFINE_TEST_NEON_2SAME_FP(fcvtau, Conversions)
// UCVTF (vector, integer) covered by UCVTF(vector, fixed point) with fbits 0.
DEFINE_TEST_NEON_2SAME_8B_16B(not_, Basic)
DEFINE_TEST_NEON_2SAME_8B_16B(rbit, Basic)
DEFINE_TEST_NEON_2OPIMM_FCMP_ZERO(fcmge, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_FCMP_ZERO(fcmle, Basic, Zero)
DEFINE_TEST_NEON_2SAME_FP(fneg, Basic)
DEFINE_TEST_NEON_2SAME_FP(frinti, Conversions)
DEFINE_TEST_NEON_2SAME_FP(fcvtpu, Conversions)
// FCVTZU(vector, integer) covered by FCVTZU(vector, fixed point) with fbits 0.
DEFINE_TEST_NEON_2SAME_2S_4S(ursqrte, Basic)
DEFINE_TEST_NEON_2SAME_FP(frsqrte, Basic)
DEFINE_TEST_NEON_2SAME_FP(fsqrt, Basic)

// Advanced SIMD scalar two-register miscellaneous.
DEFINE_TEST_NEON_2SAME_SCALAR(suqadd, Basic)
DEFINE_TEST_NEON_2SAME_SCALAR(sqabs, Basic)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(cmgt, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(cmeq, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(cmlt, Basic, Zero)
DEFINE_TEST_NEON_2SAME_SCALAR_D(abs, Basic)
DEFINE_TEST_NEON_2DIFF_SCALAR_NARROW(sqxtn, Basic)
DEFINE_TEST_NEON_2SAME_FP_SCALAR(fcvtns, Conversions)
DEFINE_TEST_NEON_2SAME_FP_SCALAR(fcvtms, Conversions)
DEFINE_TEST_NEON_2SAME_FP_SCALAR(fcvtas, Conversions)
// SCVTF (vector, integer) covered by SCVTF(vector, fixed point) with fbits 0.
DEFINE_TEST_NEON_2OPIMM_FP_SCALAR_SD(fcmgt, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_FP_SCALAR_SD(fcmeq, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_FP_SCALAR_SD(fcmlt, Basic, Zero)
DEFINE_TEST_NEON_2SAME_FP_SCALAR(fcvtps, Conversions)
// FCVTZS(vector, integer) covered by FCVTZS(vector, fixed point) with fbits 0.
DEFINE_TEST_NEON_2SAME_FP_SCALAR(frecpe, Basic)
DEFINE_TEST_NEON_2SAME_FP_SCALAR(frecpx, Basic)
DEFINE_TEST_NEON_2SAME_SCALAR(usqadd, Basic)
DEFINE_TEST_NEON_2SAME_SCALAR(sqneg, Basic)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(cmge, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_SCALAR_D(cmle, Basic, Zero)
DEFINE_TEST_NEON_2SAME_SCALAR_D(neg, Basic)
DEFINE_TEST_NEON_2DIFF_SCALAR_NARROW(sqxtun, Basic)
DEFINE_TEST_NEON_2DIFF_SCALAR_NARROW(uqxtn, Basic)
SIMTEST(fcvtxn_SCALAR) {
  CALL_TEST_NEON_HELPER_2DIFF(fcvtxn, S, D, kInputDoubleConversions);
}
DEFINE_TEST_NEON_2SAME_FP_SCALAR(fcvtnu, Conversions)
DEFINE_TEST_NEON_2SAME_FP_SCALAR(fcvtmu, Conversions)
DEFINE_TEST_NEON_2SAME_FP_SCALAR(fcvtau, Conversions)
// UCVTF (vector, integer) covered by UCVTF(vector, fixed point) with fbits 0.
DEFINE_TEST_NEON_2OPIMM_FP_SCALAR_SD(fcmge, Basic, Zero)
DEFINE_TEST_NEON_2OPIMM_FP_SCALAR_SD(fcmle, Basic, Zero)
DEFINE_TEST_NEON_2SAME_FP_SCALAR(fcvtpu, Conversions)
// FCVTZU(vector, integer) covered by FCVTZU(vector, fixed point) with fbits 0.
DEFINE_TEST_NEON_2SAME_FP_SCALAR(frsqrte, Basic)

// Advanced SIMD across lanes.
DEFINE_TEST_NEON_ACROSS_LONG(saddlv, Basic)
DEFINE_TEST_NEON_ACROSS(smaxv, Basic)
DEFINE_TEST_NEON_ACROSS(sminv, Basic)
DEFINE_TEST_NEON_ACROSS(addv, Basic)
DEFINE_TEST_NEON_ACROSS_LONG(uaddlv, Basic)
DEFINE_TEST_NEON_ACROSS(umaxv, Basic)
DEFINE_TEST_NEON_ACROSS(uminv, Basic)
DEFINE_TEST_NEON_ACROSS_FP(fmaxnmv, Basic)
DEFINE_TEST_NEON_ACROSS_FP(fmaxv, Basic)
DEFINE_TEST_NEON_ACROSS_FP(fminnmv, Basic)
DEFINE_TEST_NEON_ACROSS_FP(fminv, Basic)

// Advanced SIMD permute.
DEFINE_TEST_NEON_3SAME(uzp1, Basic)
DEFINE_TEST_NEON_3SAME(trn1, Basic)
DEFINE_TEST_NEON_3SAME(zip1, Basic)
DEFINE_TEST_NEON_3SAME(uzp2, Basic)
DEFINE_TEST_NEON_3SAME(trn2, Basic)
DEFINE_TEST_NEON_3SAME(zip2, Basic)

// Advanced SIMD vector x indexed element.
DEFINE_TEST_NEON_BYELEMENT_DIFF(smlal, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF(sqdmlal, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF(smlsl, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF(sqdmlsl, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT(mul, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF(smull, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF(sqdmull, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT(sqdmulh, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT(sqrdmulh, Basic, Basic, Basic)
DEFINE_TEST_NEON_FP_BYELEMENT(fmla, Basic, Basic, Basic)
DEFINE_TEST_NEON_FP_BYELEMENT(fmls, Basic, Basic, Basic)
DEFINE_TEST_NEON_FP_BYELEMENT(fmul, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT(mla, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF(umlal, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT(mls, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF(umlsl, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF(umull, Basic, Basic, Basic)
DEFINE_TEST_NEON_FP_BYELEMENT(fmulx, Basic, Basic, Basic)

// Advanced SIMD scalar x indexed element.
DEFINE_TEST_NEON_BYELEMENT_DIFF_SCALAR(sqdmlal, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF_SCALAR(sqdmlsl, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_DIFF_SCALAR(sqdmull, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_SCALAR(sqdmulh, Basic, Basic, Basic)
DEFINE_TEST_NEON_BYELEMENT_SCALAR(sqrdmulh, Basic, Basic, Basic)
DEFINE_TEST_NEON_FP_BYELEMENT_SCALAR(fmla, Basic, Basic, Basic)
DEFINE_TEST_NEON_FP_BYELEMENT_SCALAR(fmls, Basic, Basic, Basic)
DEFINE_TEST_NEON_FP_BYELEMENT_SCALAR(fmul, Basic, Basic, Basic)
DEFINE_TEST_NEON_FP_BYELEMENT_SCALAR(fmulx, Basic, Basic, Basic)
