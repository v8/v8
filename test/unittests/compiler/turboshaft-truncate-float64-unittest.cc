// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include "src/base/numerics/safe_conversions.h"
#include "test/common/value-helper.h"
#include "test/unittests/compiler/turboshaft-codegen-tester.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::compiler::turboshaft {

namespace turboshaft_truncate_float64_unittest {

#if V8_TARGET_ARCH_64_BIT

using TurboshaftTruncateFloat64Test = TestWithIsolateAndZone;

TEST_F(TurboshaftTruncateFloat64Test,
       RunTryTruncateFloat64ToInt32RoundTowardsZero) {
  BufferedRawMachineAssemblerTester<int32_t> m(i_isolate(), zone(),
                                               MachineType::Float64());
  m.Return(m.Projection<0>(m.TryTruncateFloat64ToInt32(m.Parameter(0))));

  FOR_FLOAT64_INPUTS(i) {
    if (base::IsValueInRangeForNumericType<int32_t>(i)) {
      double input = static_cast<double>(i);
      CHECK_EQ(static_cast<int32_t>(input), m.Call(input));
    }
  }
}

TEST_F(TurboshaftTruncateFloat64Test,
       RunTryTruncateFloat64ToUint32RoundTowardsZero) {
  BufferedRawMachineAssemblerTester<uint32_t> m(i_isolate(), zone(),
                                                MachineType::Float64());
  m.Return(m.Projection<0>(m.TryTruncateFloat64ToUint32(m.Parameter(0))));

  FOR_FLOAT64_INPUTS(i) {
    if (base::IsValueInRangeForNumericType<uint32_t>(i)) {
      double input = static_cast<double>(i);
      CHECK_EQ(static_cast<uint32_t>(input), m.Call(input));
    }
  }
}

#endif  // V8_TARGET_ARCH_64_BIT

}  // namespace turboshaft_truncate_float64_unittest

}  // namespace v8::internal::compiler::turboshaft
