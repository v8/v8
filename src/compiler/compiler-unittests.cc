// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libplatform/libplatform.h"
#include "src/compiler/compiler-unittests.h"
#include "src/isolate-inl.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::IsNull;
using testing::NotNull;

namespace v8 {
namespace internal {
namespace compiler {

// static
v8::Isolate* CompilerTest::isolate_ = NULL;


CompilerTest::CompilerTest()
    : isolate_scope_(isolate_),
      handle_scope_(isolate_),
      context_scope_(v8::Context::New(isolate_)),
      zone_(isolate()) {}


CompilerTest::~CompilerTest() {}


Factory* CompilerTest::factory() const { return isolate()->factory(); }


// static
void CompilerTest::SetUpTestCase() {
  Test::SetUpTestCase();
  EXPECT_THAT(isolate_, IsNull());
  isolate_ = v8::Isolate::New();
  ASSERT_THAT(isolate_, NotNull());
}


// static
void CompilerTest::TearDownTestCase() {
  ASSERT_THAT(isolate_, NotNull());
  isolate_->Dispose();
  isolate_ = NULL;
  Test::TearDownTestCase();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8


namespace {

class CompilerTestEnvironment FINAL : public ::testing::Environment {
 public:
  CompilerTestEnvironment() : platform_(NULL) {}
  ~CompilerTestEnvironment() {}

  virtual void SetUp() OVERRIDE {
    EXPECT_THAT(platform_, IsNull());
    platform_ = v8::platform::CreateDefaultPlatform();
    v8::V8::InitializePlatform(platform_);
    ASSERT_TRUE(v8::V8::Initialize());
  }

  virtual void TearDown() OVERRIDE {
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
  testing::AddGlobalTestEnvironment(new CompilerTestEnvironment);
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
  return RUN_ALL_TESTS();
}
