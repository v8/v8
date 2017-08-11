// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdlib.h>
#include <string.h>

#include "src/v8.h"

#include "test/cctest/cctest.h"

// Sanitizers aren't configured to return NULL on allocation failure.
#if !defined(V8_USE_ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER) && \
    !defined(THREAD_SANITIZER)

using v8::internal::AccountingAllocator;

using v8::IdleTask;
using v8::Isolate;
using v8::Task;

#include "src/allocation.h"
#include "src/zone/accounting-allocator.h"

namespace {

// Implementation of v8::Platform that can register OOM callbacks.
class AllocationPlatform : public TestPlatform {
 public:
  AllocationPlatform() {
    current_platform = this;
    // Now that it's completely constructed, make this the current platform.
    i::V8::SetPlatformForTesting(this);
  }
  virtual ~AllocationPlatform() = default;

  void OnCriticalMemoryPressure() override { oom_callback_called = true; }

  static AllocationPlatform* current_platform;
  bool oom_callback_called = false;
};

AllocationPlatform* AllocationPlatform::current_platform = nullptr;

bool DidCallOnCriticalMemoryPressure() {
  return AllocationPlatform::current_platform &&
         AllocationPlatform::current_platform->oom_callback_called;
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

// Grow the size using the alloc'ed address. This exhausts the address space
// quickly so tests reliably generate alloc failures.
size_t GrowSize(size_t size, void* address) {
  return size | bit_cast<size_t>(address);
}

static const int kLargeishAllocation = 16 * 1024 * 1024;

}  // namespace

TEST(AccountingAllocatorOOM) {
  AllocationPlatform platform;
  v8::internal::AccountingAllocator allocator;
  CHECK(!platform.oom_callback_called);
  // Start with an allocation that should succeed on all platforms, and grow it
  // until we get a failure.
  size_t size = kLargeishAllocation;
  while (true) {
    v8::internal::Segment* result = allocator.GetSegment(size);
    if (result == nullptr) {
      CHECK(platform.oom_callback_called);
      break;
    }
    size = GrowSize(size, result);
    // Leak the allocation.
  }
}

TEST(MallocedOperatorNewOOM) {
  AllocationPlatform platform;
  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnMallocedOperatorNewOOM);
  // Start with an allocation that should succeed on all platforms, and grow it
  // until we get a failure.
  size_t size = kLargeishAllocation;
  // On failure, Malloced::New won't return.
  // Callback behavior is checked in OnMallocedOperatorNewOOM before exit.
  while (true) {
    void* result = v8::internal::Malloced::New(size);
    CHECK_NOT_NULL(result);
    size = GrowSize(size, result);
    // Leak the allocation.
  }
}

TEST(NewArrayOOM) {
  AllocationPlatform platform;
  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnNewArrayOOM);
  // Start with an allocation that should succeed on all platforms, and grow it
  // until we get a failure.
  size_t size = kLargeishAllocation;
  // On failure, this won't return, since a NewArray failure is fatal.
  // Callback behavior is checked in OnNewArrayOOM before exit.
  while (true) {
    int8_t* result = v8::internal::NewArray<int8_t>(size);
    CHECK_NOT_NULL(result);
    size = GrowSize(size, result);
    // Leak the allocation.
  }
}

TEST(AlignedAllocOOM) {
  AllocationPlatform platform;
  CHECK(!platform.oom_callback_called);
  CcTest::isolate()->SetFatalErrorHandler(OnAlignedAllocOOM);
  // Start with an allocation that should succeed on all platforms, and grow it
  // until we get a failure.
  size_t size = kLargeishAllocation;
  // On failure, this won't return, since an AlignedAlloc failure is fatal.
  // Callback behavior is checked in OnAlignedAllocOOM before exit.
  while (true) {
    void* result =
        v8::internal::AlignedAlloc(size, v8::base::OS::AllocateAlignment());
    CHECK_NOT_NULL(result);
    size = GrowSize(size, result);
    // Leak the allocation.
  }
}

TEST(AllocVirtualMemoryOOM) {
  AllocationPlatform platform;
  CHECK(!platform.oom_callback_called);
  // Start with an allocation that should succeed on all platforms, and grow it
  // until we get a failure.
  size_t size = kLargeishAllocation;
  while (true) {
    auto* result = new v8::base::VirtualMemory();
    bool success = v8::internal::AllocVirtualMemory(size, nullptr, result);
    if (!success) {
      CHECK(!result->IsReserved());
      CHECK(platform.oom_callback_called);
      break;
    }
    size = GrowSize(size, result->address());
    // Leak the allocation.
  }
}

TEST(AlignedAllocVirtualMemoryOOM) {
  AllocationPlatform platform;
  CHECK(!platform.oom_callback_called);
  // Start with an allocation that should succeed on all platforms, and grow it
  // until we get a failure.
  size_t size = kLargeishAllocation;
  while (true) {
    auto* result = new v8::base::VirtualMemory();
    bool success = v8::internal::AlignedAllocVirtualMemory(
        size, v8::base::OS::AllocateAlignment(), nullptr, result);
    if (!success) {
      CHECK(!result->IsReserved());
      CHECK(platform.oom_callback_called);
      break;
    }
    size = GrowSize(size, result->address());
    // Leak the allocation.
  }
}

#endif  // !defined(V8_USE_ADDRESS_SANITIZER) && !defined(MEMORY_SANITIZER) &&
        // !defined(THREAD_SANITIZER)
