// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/libplatform/libplatform.h"
#include "include/v8.h"
#include "src/base/compiler-specific.h"
#include "testing/gmock/include/gmock/gmock.h"

#ifdef V8_USE_EXTERNAL_STARTUP_DATA
#include "src/startup-data-util.h"
#endif  // V8_USE_EXTERNAL_STARTUP_DATA

namespace {

class ArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

class DefaultPlatformEnvironment final : public ::testing::Environment {
 public:
  DefaultPlatformEnvironment() : platform_(NULL) {}
  ~DefaultPlatformEnvironment() {}

  virtual void SetUp() override {
    EXPECT_EQ(NULL, platform_);
    platform_ = v8::platform::CreateDefaultPlatform();
    ASSERT_TRUE(platform_ != NULL);
    v8::V8::InitializePlatform(platform_);
    v8::V8::SetArrayBufferAllocator(&array_buffer_allocator_);
    ASSERT_TRUE(v8::V8::Initialize());
  }

  virtual void TearDown() override {
    ASSERT_TRUE(platform_ != NULL);
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    delete platform_;
    platform_ = NULL;
  }

 private:
  v8::Platform* platform_;
  ArrayBufferAllocator array_buffer_allocator_;
};

}  // namespace


int main(int argc, char** argv) {
  testing::InitGoogleMock(&argc, argv);
  testing::AddGlobalTestEnvironment(new DefaultPlatformEnvironment);
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);
#ifdef V8_USE_EXTERNAL_STARTUP_DATA
  v8::StartupDataHandler startup_data(argv[0], NULL, NULL);
#endif
  return RUN_ALL_TESTS();
}
