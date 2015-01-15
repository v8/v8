// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/factory.h"
#include "src/handles-inl.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

typedef TestWithIsolate FactoryTest;


TEST_F(FactoryTest, NewNumberWithMinusZero) {
  Handle<Object> minus_zero_value = factory()->minus_zero_value();
  EXPECT_TRUE(minus_zero_value.is_identical_to(factory()->NewNumber(-0.0)));
}

}  // namespace internal
}  // namespace v8
