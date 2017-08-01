// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdlib.h>
#include <string.h>

#include "src/v8.h"

#include "test/cctest/cctest.h"

using v8::internal::AccountingAllocator;

using v8::IdleTask;
using v8::Isolate;
using v8::Task;

#include "src/allocation.h"
#include "src/zone/accounting-allocator.h"

// ASAN isn't configured to return NULL, so skip all of these tests.
#if !defined(V8_USE_ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER) && \
    !defined(THREAD_SANITIZER)

namespace {

// Minimal implementation of platform that can receive OOM callbacks.
class MockAllocationPlatform : public v8::Platform {
 public:
  MockAllocationPlatform() { current_platform = this; }
  virtual ~MockAllocationPlatform() {}

  void OnCriticalMemoryPressure() override { oom_callback_called = true; }

  void CallOnBackgroundThread(Task* task,
                              ExpectedRuntime expected_runtime) override {}

  void CallOnForegroundThread(Isolate* isolate, Task* task) override {}

  void CallDelayedOnForegroundThread(Isolate* isolate, Task* task,
                                     double delay_in_seconds) override {}

  double MonotonicallyIncreasingTime() override { return 0.0; }

  void CallIdleOnForegroundThread(Isolate* isolate, IdleTask* task) override {}

  bool IdleTasksEnabled(Isolate* isolate) override { return false; }

  v8::TracingController* GetTracingController() override {
    return &tracing_controller_;
  }

  bool PendingIdleTask() { return false; }

  void PerformIdleTask(double idle_time_in_seconds) {}

  bool PendingDelayedTask() { return false; }

  void PerformDelayedTask() {}

  static MockAllocationPlatform* current_platform;
  bool oom_callback_called = false;

 private:
  v8::TracingController tracing_controller_;

  DISALLOW_COPY_AND_ASSIGN(MockAllocationPlatform);
};

MockAllocationPlatform* MockAllocationPlatform::current_platform = nullptr;

bool DidCallOnCriticalMemoryPressure() {
  return MockAllocationPlatform::current_platform &&
         MockAllocationPlatform::current_platform->oom_callback_called;
}

// No OS should be able to malloc/new this number of bytes. Generate enough
// random values in the address space to get a very large fraction of it. Using
// even larger values is that overflow from rounding or padding can cause the
// allocations to succeed somehow.
size_t GetHugeMemoryAmount() {
  static size_t huge_memory = 0;
  if (!huge_memory) {
    for (int i = 0; i < 100; i++) {
      huge_memory |= bit_cast<size_t>(v8::base::OS::GetRandomMmapAddr());
    }
    // Make it larger than the available address space.
    huge_memory *= 2;
    CHECK_NE(0, huge_memory);
  }
  return huge_memory;
}

void OnMallocedOperatorNewOOM(const char* location, const char* message) {
  // exit(0) if the OOM callback was called and location matches expectation.
  if (DidCallOnCriticalMemoryPressure())
    exit(strcmp(location, "Malloced operator new"));
  exit(1);
}

void OnNewArrayOOM(const char* location, const char* message) {
  // exit(0) if the OOM callback was called and location matches expectation.
  if (DidCallOnCriticalMemoryPressure()) exit(strcmp(location, "NewArray"));
  exit(1);
}

void OnAlignedAllocOOM(const char* location, const char* message) {
  // exit(0) if the OOM callback was called and location matches expectation.
  if (DidCallOnCriticalMemoryPressure()) exit(strcmp(location, "AlignedAlloc"));
  exit(1);
}

}  // namespace

TEST(AccountingAllocatorOOM) {
  // TODO(bbudge) Implement a TemporaryPlatformScope to simplify test code.
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  MockAllocationPlatform platform;
  i::V8::SetPlatformForTesting(&platform);

  v8::internal::AccountingAllocator allocator;
  CHECK(!platform.oom_callback_called);
  v8::internal::Segment* result = allocator.GetSegment(GetHugeMemoryAmount());
  // On a few systems, allocation somehow succeeds.
  CHECK_EQ(result == nullptr, platform.oom_callback_called);

  i::V8::SetPlatformForTesting(old_platform);
}

TEST(MallocedOperatorNewOOM) {
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  MockAllocationPlatform platform;
  i::V8::SetPlatformForTesting(&platform);

  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnMallocedOperatorNewOOM);
  // On failure, this won't return, since a Malloced::New failure is fatal.
  // In that case, behavior is checked in OnMallocedOperatorNewOOM before exit.
  void* result = v8::internal::Malloced::New(GetHugeMemoryAmount());
  // On a few systems, allocation somehow succeeds.
  CHECK_EQ(result == nullptr, platform.oom_callback_called);

  i::V8::SetPlatformForTesting(old_platform);
}

TEST(NewArrayOOM) {
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  MockAllocationPlatform platform;
  i::V8::SetPlatformForTesting(&platform);

  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnNewArrayOOM);
  // On failure, this won't return, since a NewArray failure is fatal.
  // In that case, behavior is checked in OnNewArrayOOM before exit.
  int8_t* result = v8::internal::NewArray<int8_t>(GetHugeMemoryAmount());
  // On a few systems, allocation somehow succeeds.
  CHECK_EQ(result == nullptr, platform.oom_callback_called);

  i::V8::SetPlatformForTesting(old_platform);
}

TEST(AlignedAllocOOM) {
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  MockAllocationPlatform platform;
  i::V8::SetPlatformForTesting(&platform);

  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnAlignedAllocOOM);
  // On failure, this won't return, since an AlignedAlloc failure is fatal.
  // In that case, behavior is checked in OnAlignedAllocOOM before exit.
  void* result = v8::internal::AlignedAlloc(GetHugeMemoryAmount(),
                                            v8::base::OS::AllocateAlignment());
  // On a few systems, allocation somehow succeeds.
  CHECK_EQ(result == nullptr, platform.oom_callback_called);

  i::V8::SetPlatformForTesting(old_platform);
}

TEST(AllocVirtualMemoryOOM) {
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  MockAllocationPlatform platform;
  i::V8::SetPlatformForTesting(&platform);

  CHECK(!platform.oom_callback_called);
  v8::base::VirtualMemory result;
  bool success =
      v8::internal::AllocVirtualMemory(GetHugeMemoryAmount(), nullptr, &result);
  // On a few systems, allocation somehow succeeds.
  CHECK_IMPLIES(success, result.IsReserved());
  CHECK_IMPLIES(!success, !result.IsReserved() && platform.oom_callback_called);

  i::V8::SetPlatformForTesting(old_platform);
}

TEST(AlignedAllocVirtualMemoryOOM) {
  v8::Platform* old_platform = i::V8::GetCurrentPlatform();
  MockAllocationPlatform platform;
  i::V8::SetPlatformForTesting(&platform);

  CHECK(!platform.oom_callback_called);
  v8::base::VirtualMemory result;
  bool success = v8::internal::AlignedAllocVirtualMemory(
      GetHugeMemoryAmount(), v8::base::OS::AllocateAlignment(), nullptr,
      &result);
  // On a few systems, allocation somehow succeeds.
  CHECK_IMPLIES(success, result.IsReserved());
  CHECK_IMPLIES(!success, !result.IsReserved() && platform.oom_callback_called);

  i::V8::SetPlatformForTesting(old_platform);
}

#endif  // !defined(V8_USE_ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER) &&
        // !defined(THREAD_SANITIZER)
