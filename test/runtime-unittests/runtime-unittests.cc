// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libplatform/libplatform.h"
#include "src/isolate-inl.h"
#include "test/runtime-unittests/runtime-unittests.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::IsNull;
using testing::NotNull;

namespace v8 {
namespace internal {

// static
v8::Isolate* RuntimeTest::isolate_ = NULL;


RuntimeTest::RuntimeTest()
    : isolate_scope_(isolate_), handle_scope_(isolate_), zone_(isolate()) {}


RuntimeTest::~RuntimeTest() {}


Factory* RuntimeTest::factory() const { return isolate()->factory(); }


Heap* RuntimeTest::heap() const { return isolate()->heap(); }


// static
void RuntimeTest::SetUpTestCase() {
  Test::SetUpTestCase();
  EXPECT_THAT(isolate_, IsNull());
  isolate_ = v8::Isolate::New();
  ASSERT_THAT(isolate_, NotNull());
}


// static
void RuntimeTest::TearDownTestCase() {
  ASSERT_THAT(isolate_, NotNull());
  isolate_->Dispose();
  isolate_ = NULL;
  Test::TearDownTestCase();
}

}  // namespace internal
}  // namespace v8


namespace {

class RuntimeTestEnvironment V8_FINAL : public ::testing::Environment {
 public:
  RuntimeTestEnvironment() : platform_(NULL) {}
  virtual ~RuntimeTestEnvironment() {}

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
  testing::AddGlobalTestEnvironment(new RuntimeTestEnvironment);
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  return RUN_ALL_TESTS();
}
