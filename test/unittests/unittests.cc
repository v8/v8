// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <cstring>
#include <memory>

#include "include/libplatform/libplatform.h"
#include "test/unittests/unittests.h"
#include "testing/gmock/include/gmock/gmock.h"

using ::testing::IsNull;
using ::testing::NotNull;

namespace v8 {
namespace internal {

// static
v8::Platform* EngineTest::platform_ = NULL;


// static
void EngineTest::SetUpTestCase() {
  EXPECT_THAT(platform_, IsNull());
  platform_ = v8::platform::CreateDefaultPlatform();
  EXPECT_THAT(platform_, NotNull());
  v8::V8::InitializePlatform(platform_);
  EXPECT_TRUE(v8::V8::Initialize());
}


// static
void EngineTest::TearDownTestCase() {
  EXPECT_THAT(platform_, NotNull());
  v8::V8::Dispose();
  v8::V8::ShutdownPlatform();
  delete platform_;
  platform_ = NULL;
}

}  // namespace internal
}  // namespace v8


int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  return RUN_ALL_TESTS();
}
