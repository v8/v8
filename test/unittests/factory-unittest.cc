// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/factory.h"
#include "src/handles-inl.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock-support.h"

using testing::BitEq;

namespace v8 {
namespace internal {

typedef TestWithIsolate FactoryTest;


namespace {

const PretenureFlag kPretenureFlags[] = {TENURED, NOT_TENURED};

}  // namespace


TEST_F(FactoryTest, MinusZeroValue) {
  Handle<Object> minus_zero_value = factory()->minus_zero_value();
  EXPECT_TRUE(minus_zero_value->IsHeapNumber());
  EXPECT_THAT(minus_zero_value->Number(), BitEq(-0.0));
}


TEST_F(FactoryTest, NewNumberWithMinusZero) {
  Handle<Object> minus_zero_value = factory()->minus_zero_value();
  TRACED_FOREACH(PretenureFlag, pretenure_flag, kPretenureFlags) {
    EXPECT_TRUE(minus_zero_value.is_identical_to(
        factory()->NewNumber(-0.0, pretenure_flag)));
  }
}


TEST_F(FactoryTest, NewHeapNumberWithMinusZero) {
  TRACED_FOREACH(PretenureFlag, pretenure_flag, kPretenureFlags) {
    Handle<Object> value =
        factory()->NewHeapNumber(-0.0, IMMUTABLE, pretenure_flag);
    EXPECT_TRUE(value->IsHeapNumber());
    EXPECT_THAT(value->Number(), BitEq(-0.0));
    EXPECT_FALSE(value.is_identical_to(factory()->minus_zero_value()));
  }
}

}  // namespace internal
}  // namespace v8
