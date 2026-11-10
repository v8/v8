// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Demo fuzz tests used to probe-test the FuzzTest and Centipede integration
// with Clusterfuzz.

#include "test/unittests/fuzztest.h"

#include <iostream>
#include <string>
#include <vector>

#include "src/base/logging.h"

#if defined(V8_ENABLE_FUZZTEST) && defined(__linux__) &&                 \
    !defined(__ANDROID__) && defined(__arm__) &&                         \
    !defined(__USING_SJLJ_EXCEPTIONS__) && !defined(__ARM_DWARF_EH__) && \
    !defined(__SEH__)
#include <unwind.h>

// On 32-bit ARM Linux (ARM EHABI), Clang's builtin <unwind.h> declares
// _Unwind_GetIPInfo as an external function, whereas GCC's libgcc_s.so.1 only
// exports _Unwind_VRS_Get and defines _Unwind_GetIPInfo as a macro in
// unwind-arm-common.h. Provide a weak definition matching GCC's macro so
// FuzzTest's sanitizer_interface.cc can link against libgcc_s.
extern "C" __attribute__((weak)) _Unwind_Word
_Unwind_GetIPInfo(struct _Unwind_Context* context, int* ip_before_insn) {
  *ip_before_insn = 0;
  return _Unwind_GetIP(context);
}
#endif

namespace v8::internal {

#ifdef V8_ENABLE_FUZZTEST

static void ManyConditions(std::vector<int> input, int failure) {
  int i = 0;

  if (input.size() > 4) {
    if (input[0] >= 1500 && input[0] <= 2000) {
      i++;
    }
    if (input[1] >= 250 && input[1] <= 500) {
      i++;
    }
    if (input[2] >= 200 && input[2] <= 250) {
      i++;
    }
    if (input[3] >= 150 && input[3] <= 200) {
      i++;
    }
    if (input[4] >= 3 && input[4] <= 20) {
      i++;
    }
  }

  if (i >= 4) {
    switch (failure) {
      case 0:
        ASSERT_LT(failure, 0);
        break;
      case 1:
        CHECK_WITH_MSG(false, "Fake fuzz-test check failure");
        break;
      case 2:
        // The message here needs to be different enough from the one above to
        // avoid grouping the resulting test cases.
        DCHECK_WITH_MSG(false,
                        "A very different fake f-u-z-z-t-e-s-t dcheck failure");
        break;
      case 3:
        // Fake Heap-buffer-overflow.
        int* adr = new int(3);
        std::cout << *(adr + 3);
        break;
    }
  }
}

V8_FUZZ_TEST(SmokeTest, ManyConditions)
    .WithDomains(fuzztest::VectorOf(fuzztest::InRange(1, 2000)),
                 fuzztest::InRange(0, 3));

static void SingleString(std::string input) { ASSERT_NE(input, "V8"); }

V8_FUZZ_TEST(SmokeTest, SingleString);

#endif  // V8_ENABLE_FUZZTEST
}  // namespace v8::internal
