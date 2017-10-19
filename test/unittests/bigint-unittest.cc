// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#include "src/conversions.h"
#include "src/factory.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects/bigint-inl.h"
#include "test/unittests/test-utils.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

typedef TestWithIsolate BigIntWithIsolate;

void Compare(Handle<BigInt> x, double value, ComparisonResult expected) {
  CHECK_EQ(expected, BigInt::CompareToDouble(x, value));
}

TEST_F(BigIntWithIsolate, CompareToDouble) {
  Factory* factory = isolate()->factory();
  Handle<BigInt> zero = factory->NewBigIntFromInt(0);
  Handle<BigInt> one = factory->NewBigIntFromInt(1);
  Handle<BigInt> minus_one = factory->NewBigIntFromInt(-1);

  // Non-finite doubles.
  Compare(zero, std::nan(""), ComparisonResult::kUndefined);
  Compare(one, INFINITY, ComparisonResult::kLessThan);
  Compare(one, -INFINITY, ComparisonResult::kGreaterThan);

  // Unequal sign.
  Compare(one, -1, ComparisonResult::kGreaterThan);
  Compare(minus_one, 1, ComparisonResult::kLessThan);

  // Cases involving zero.
  Compare(zero, 0, ComparisonResult::kEqual);
  Compare(zero, -0, ComparisonResult::kEqual);
  Compare(one, 0, ComparisonResult::kGreaterThan);
  Compare(minus_one, 0, ComparisonResult::kLessThan);
  Compare(zero, 1, ComparisonResult::kLessThan);
  Compare(zero, -1, ComparisonResult::kGreaterThan);

  // Small doubles.
  Compare(zero, 0.25, ComparisonResult::kLessThan);
  Compare(one, 0.5, ComparisonResult::kGreaterThan);
  Compare(one, -0.5, ComparisonResult::kGreaterThan);
  Compare(zero, -0.25, ComparisonResult::kGreaterThan);
  Compare(minus_one, -0.5, ComparisonResult::kLessThan);

  // Different bit lengths.
  Handle<BigInt> four = factory->NewBigIntFromInt(4);
  Handle<BigInt> minus_five = factory->NewBigIntFromInt(-5);
  Compare(four, 3.9, ComparisonResult::kGreaterThan);
  Compare(four, 1.5, ComparisonResult::kGreaterThan);
  Compare(four, 8, ComparisonResult::kLessThan);
  Compare(four, 16, ComparisonResult::kLessThan);
  Compare(minus_five, -4.9, ComparisonResult::kLessThan);
  Compare(minus_five, -4, ComparisonResult::kLessThan);
  Compare(minus_five, -25, ComparisonResult::kGreaterThan);

  // Same bit length, difference in first digit.
  double big_double = 4428155326412785451008.0;
  Handle<BigInt> big =
      StringToBigInt(isolate(), "0xF10D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kGreaterThan);
  big = StringToBigInt(isolate(), "0xE00D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kLessThan);

  double other_double = -13758438578910658560.0;
  Handle<BigInt> other =
      StringToBigInt(isolate(), "-0xBEEFC1FE00000000").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kGreaterThan);
  other = StringToBigInt(isolate(), "-0xBEEFCBFE00000000").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kLessThan);

  // Same bit length, difference in non-first digit.
  big = StringToBigInt(isolate(), "0xF00D00000000000001").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kGreaterThan);
  big = StringToBigInt(isolate(), "0xF00A00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kLessThan);

  other = StringToBigInt(isolate(), "-0xBEEFCAFE00000001").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kLessThan);

  // Same bit length, difference in fractional part.
  Compare(one, 1.5, ComparisonResult::kLessThan);
  Compare(minus_one, -1.25, ComparisonResult::kGreaterThan);
  big = factory->NewBigIntFromInt(0xF00D00);
  Compare(big, 15731968.125, ComparisonResult::kLessThan);
  Compare(big, 15731967.875, ComparisonResult::kGreaterThan);
  big = StringToBigInt(isolate(), "0x123456789ab").ToHandleChecked();
  Compare(big, 1250999896491.125, ComparisonResult::kLessThan);

  // Equality!
  Compare(one, 1, ComparisonResult::kEqual);
  Compare(minus_one, -1, ComparisonResult::kEqual);
  big = StringToBigInt(isolate(), "0xF00D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kEqual);

  Handle<BigInt> two_52 =
      StringToBigInt(isolate(), "0x10000000000000").ToHandleChecked();
  Compare(two_52, 4503599627370496.0, ComparisonResult::kEqual);
}

}  // namespace internal
}  // namespace v8
