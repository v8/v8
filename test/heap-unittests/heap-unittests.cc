// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libplatform/libplatform.h"
#include "include/v8.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::IsNull;
using testing::NotNull;

namespace {

class HeapTestEnvironment V8_FINAL : public ::testing::Environment {
 public:
  HeapTestEnvironment() : platform_(NULL) {}
  virtual ~HeapTestEnvironment() {}

  virtual void SetUp() V8_OVERRIDE {
    EXPECT_THAT(platform_, IsNull());
    platform_ = v8::platform::CreateDefaultPlatform();
    v8::V8::InitializePlatform(platform_);
    ASSERT_TRUE(v8::V8::Initialize());
  }

  virtual void TearDown() V8_OVERRIDE {
    ASSERT_THAT(platform_, NotNull());
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    delete platform_;
    platform_ = NULL;
  }

 private:
  v8::Platform* platform_;
};

}  // namespace


int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  testing::AddGlobalTestEnvironment(new HeapTestEnvironment);
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  return RUN_ALL_TESTS();
}
