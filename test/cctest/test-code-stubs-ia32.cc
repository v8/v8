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

#include <limits>

#include "v8.h"

#include "cctest.h"
#include "code-stubs.h"
#include "factory.h"
#include "macro-assembler.h"
#include "platform.h"

#if __GNUC__
#define STDCALL  __attribute__((stdcall))
#else
#define STDCALL  __stdcall
#endif

using namespace v8::internal;


typedef int32_t STDCALL ConvertDToIFuncType(double input);
typedef ConvertDToIFuncType* ConvertDToIFunc;


int STDCALL ConvertDToICVersion(double d) {
  Address double_ptr = reinterpret_cast<Address>(&d);
  uint32_t exponent_bits = Memory::uint32_at(double_ptr + kDoubleSize / 2);
  int32_t shifted_mask = static_cast<int32_t>(Double::kExponentMask >> 32);
  int32_t exponent = (((exponent_bits & shifted_mask) >>
                       (Double::kPhysicalSignificandSize - 32)) -
                      HeapNumber::kExponentBias);
  uint32_t unsigned_exponent = static_cast<uint32_t>(exponent);
  int result = 0;
  uint32_t max_exponent =
    static_cast<uint32_t>(Double::kPhysicalSignificandSize);
  if (unsigned_exponent >= max_exponent) {
    if ((exponent - Double::kPhysicalSignificandSize) < 32) {
      result = Memory::uint32_at(double_ptr) <<
        (exponent - Double::kPhysicalSignificandSize);
    }
  } else {
    uint64_t big_result =
        (BitCast<uint64_t>(d) & Double::kSignificandMask) | Double::kHiddenBit;
    big_result = big_result >> (Double::kPhysicalSignificandSize - exponent);
    result = static_cast<uint32_t>(big_result);
  }
  if (static_cast<int32_t>(exponent_bits) < 0) {
    return (0 - result);
  } else {
    return result;
  }
}


void RunOneTruncationTestWithTest(ConvertDToIFunc func,
                                  double from,
                                  double raw) {
  uint64_t to = static_cast<int64_t>(raw);
  int result = (*func)(from);
  CHECK_EQ(static_cast<int>(to), result);
}


// #define NaN and Infinity so that it's possible to cut-and-paste these tests
// directly to a .js file and run them.
#define NaN (OS::nan_value())
#define Infinity (std::numeric_limits<double>::infinity())
#define RunOneTruncationTest(p1, p2) RunOneTruncationTestWithTest(func, p1, p2)

void RunAllTruncationTests(ConvertDToIFunc func) {
  RunOneTruncationTest(0, 0);
  RunOneTruncationTest(0.5, 0);
  RunOneTruncationTest(-0.5, 0);
  RunOneTruncationTest(1.5, 1);
  RunOneTruncationTest(-1.5, -1);
  RunOneTruncationTest(5.5, 5);
  RunOneTruncationTest(-5.0, -5);
  RunOneTruncationTest(NaN, 0);
  RunOneTruncationTest(Infinity, 0);
  RunOneTruncationTest(-NaN, 0);
  RunOneTruncationTest(-Infinity, 0);

  RunOneTruncationTest(4.5036e+15, 0x1635E000);
  RunOneTruncationTest(-4.5036e+15, -372629504);

  RunOneTruncationTest(4503603922337791.0, -1);
  RunOneTruncationTest(-4503603922337791.0, 1);
  RunOneTruncationTest(4503601774854143.0, 2147483647);
  RunOneTruncationTest(-4503601774854143.0, -2147483647);
  RunOneTruncationTest(9007207844675582.0, -2);
  RunOneTruncationTest(-9007207844675582.0, 2);
  RunOneTruncationTest(2.4178527921507624e+24, -536870912);
  RunOneTruncationTest(-2.4178527921507624e+24, 536870912);
  RunOneTruncationTest(2.417853945072267e+24, -536870912);
  RunOneTruncationTest(-2.417853945072267e+24, 536870912);

  RunOneTruncationTest(4.8357055843015248e+24, -1073741824);
  RunOneTruncationTest(-4.8357055843015248e+24, 1073741824);
  RunOneTruncationTest(4.8357078901445341e+24, -1073741824);
  RunOneTruncationTest(-4.8357078901445341e+24, 1073741824);

  RunOneTruncationTest(9.6714111686030497e+24, -2147483648.0);
  RunOneTruncationTest(-9.6714111686030497e+24, -2147483648.0);
  RunOneTruncationTest(9.6714157802890681e+24, -2147483648.0);
  RunOneTruncationTest(-9.6714157802890681e+24, -2147483648.0);
}

#undef NaN
#undef Infinity
#undef RunOneTruncationTest

#define __ assm.

ConvertDToIFunc MakeConvertDToIFuncTrampoline(Isolate* isolate,
                                              Register source_reg,
                                              Register destination_reg) {
  // Allocate an executable page of memory.
  size_t actual_size;
  byte* buffer = static_cast<byte*>(OS::Allocate(Assembler::kMinimalBufferSize,
                                                   &actual_size,
                                                   true));
  CHECK(buffer);
  HandleScope handles(isolate);
  MacroAssembler assm(isolate, buffer, static_cast<int>(actual_size));
  assm.set_allow_stub_calls(false);
  int offset =
    source_reg.is(esp) ? 0 : (HeapNumber::kValueOffset - kSmiTagSize);
  DoubleToIStub stub(source_reg, destination_reg, offset, true);
  byte* start = stub.GetCode(isolate)->instruction_start();

  __ push(ebx);
  __ push(ecx);
  __ push(edx);
  __ push(esi);
  __ push(edi);

  if (!source_reg.is(esp)) {
    __ lea(source_reg, MemOperand(esp, 6 * kPointerSize - offset));
  }

  int param_offset = 7 * kPointerSize;
  // Save registers make sure they don't get clobbered.
  int reg_num = 0;
  for (;reg_num < Register::NumAllocatableRegisters(); ++reg_num) {
    Register reg = Register::from_code(reg_num);
    if (!reg.is(esp) && !reg.is(ebp) && !reg.is(destination_reg)) {
      __ push(reg);
      param_offset += kPointerSize;
    }
  }

  // Re-push the double argument
  __ push(MemOperand(esp, param_offset));
  __ push(MemOperand(esp, param_offset));

  // Call through to the actual stub
  __ call(start, RelocInfo::EXTERNAL_REFERENCE);

  __ add(esp, Immediate(kDoubleSize));

  // Make sure no registers have been unexpectedly clobbered
  for (--reg_num; reg_num >= 0; --reg_num) {
    Register reg = Register::from_code(reg_num);
    if (!reg.is(esp) && !reg.is(ebp) && !reg.is(destination_reg)) {
      __ cmp(reg, MemOperand(esp, 0));
      __ Assert(equal, "register was clobbered");
      __ add(esp, Immediate(kPointerSize));
    }
  }

  __ mov(eax, destination_reg);

  __ pop(edi);
  __ pop(esi);
  __ pop(edx);
  __ pop(ecx);
  __ pop(ebx);

  __ ret(kDoubleSize);

  CodeDesc desc;
  assm.GetCode(&desc);
  return reinterpret_cast<ConvertDToIFunc>(
      reinterpret_cast<intptr_t>(buffer));
}

#undef __


static Isolate* GetIsolateFrom(LocalContext* context) {
  return reinterpret_cast<Isolate*>((*context)->GetIsolate());
}


TEST(ConvertDToI) {
  CcTest::InitializeVM();
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  HandleScope scope(isolate);

#if DEBUG
  // Verify that the tests actually work with the C version. In the release
  // code, the compiler optimizes it away because it's all constant, but does it
  // wrong, triggering an assert on gcc.
  RunAllTruncationTests(&ConvertDToICVersion);
#endif

  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esp, eax));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esp, ebx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esp, ecx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esp, edx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esp, edi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esp, esi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, eax, eax));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, eax, ebx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, eax, ecx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, eax, edx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, eax, edi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, eax, esi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ebx, eax));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ebx, ebx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ebx, ecx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ebx, edx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ebx, edi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ebx, esi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ecx, eax));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ecx, ebx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ecx, ecx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ecx, edx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ecx, edi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, ecx, esi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edx, eax));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edx, ebx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edx, ecx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edx, edx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edx, edi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edx, esi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esi, eax));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esi, ebx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esi, ecx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esi, edx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esi, edi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, esi, esi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edi, eax));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edi, ebx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edi, ecx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edi, edx));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edi, edi));
  RunAllTruncationTests(MakeConvertDToIFuncTrampoline(isolate, edi, esi));
}
