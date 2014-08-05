// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/test-isolate.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::NotNull;

namespace v8 {
namespace internal {

IsolateTest::IsolateTest() : isolate_(v8::Isolate::New()) {
  ASSERT_THAT(isolate_, NotNull());
  isolate_->Enter();
}


IsolateTest::~IsolateTest() {
  ASSERT_THAT(isolate_, NotNull());
  isolate_->Exit();
  isolate_->Dispose();
  isolate_ = NULL;
}


v8::Isolate* IsolateTest::isolate() const {
  EXPECT_THAT(isolate_, NotNull());
  return isolate_;
}


TEST_F(IsolateTest, GetCurrent) {
  EXPECT_THAT(v8::Isolate::GetCurrent(), NotNull());
  EXPECT_EQ(v8::Isolate::GetCurrent(), isolate());
}

}  // namespace internal
}  // namespace v8
