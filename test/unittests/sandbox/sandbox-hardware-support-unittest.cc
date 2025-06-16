// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/trusted-range.h"
#include "src/sandbox/hardware-support.h"
#include "src/sandbox/sandbox.h"
#include "test/unittests/test-utils.h"

#ifdef V8_ENABLE_SANDBOX_HARDWARE_SUPPORT

namespace v8 {
namespace internal {

TEST(SandboxHardwareSupportTest, Initialization) {
  if (!base::MemoryProtectionKey::HasMemoryProtectionKeyAPIs() ||
      !base::MemoryProtectionKey::TestKeyAllocation())
    return;

  // If PKEYs are supported at runtime (and V8_ENABLE_SANDBOX_HARDWARE_SUPPORT
  // is enabled at compile-time) we expect hardware sandbox support to work.
  ASSERT_TRUE(SandboxHardwareSupport::TryActivateBeforeThreadCreation());
  base::VirtualAddressSpace vas;
  Sandbox sandbox;
  sandbox.Initialize(&vas);
  ASSERT_TRUE(SandboxHardwareSupport::IsActive());
  sandbox.TearDown();
}

// The ASSERT_DEATH_IF_SUPPORTED macro is somewhat complicated and for example
// performs heap allocations. As such, we cannot run that macro while in
// sandboxed mode. Instead, we have to enter (and exit) sandboxed mode as part
// of the operation performed within ASSERT_DEATH_IF_SUPPORTED.
#define RUN_SANDBOXED(stmt) \
  {                         \
    EnterSandbox();         \
    stmt;                   \
    ExitSandbox();          \
  }

TEST(SandboxHardwareSupportTest, SimpleSandboxedCPPCode) {
  // Skip this test if hardware sandboxing support cannot be enabled (likely
  // because the system doesn't support PKEYs, see the Initialization test).
  if (!SandboxHardwareSupport::TryActivateBeforeThreadCreation()) return;

  base::VirtualAddressSpace global_vas;

  Sandbox sandbox;
  sandbox.Initialize(&global_vas);
  ASSERT_TRUE(SandboxHardwareSupport::IsActive());

  size_t size = global_vas.allocation_granularity();
  size_t alignment = global_vas.allocation_granularity();

  Address page_outside_sandbox =
      global_vas.AllocatePages(VirtualAddressSpace::kNoHint, size, alignment,
                               PagePermissions::kReadWrite);
  EXPECT_NE(page_outside_sandbox, kNullAddress);
  SandboxHardwareSupport::RegisterOutOfSandboxMemory(page_outside_sandbox, size,
                                                     PageAllocator::kReadWrite);

  VirtualAddressSpace* sandbox_vas = sandbox.address_space();
  Address page_in_sandbox =
      sandbox_vas->AllocatePages(VirtualAddressSpace::kNoHint, size, alignment,
                                 PagePermissions::kReadWrite);
  EXPECT_NE(page_in_sandbox, kNullAddress);
  EXPECT_TRUE(sandbox.Contains(page_in_sandbox));

  // TODO(saelo): in the future, we should be able to use "regular" memory here
  // (e.g. malloc allocations, global memory, etc.). But for now, that memory
  // is not yet automatically tagged as out-of-sandbox memory.
  volatile int* out_of_sandbox_ptr =
      reinterpret_cast<int*>(page_outside_sandbox);
  volatile int* in_sandbox_ptr = reinterpret_cast<int*>(page_in_sandbox);

  // Out-of-sandbox memory cannot be written to in sandboxed mode.
  ASSERT_DEATH_IF_SUPPORTED(RUN_SANDBOXED(*out_of_sandbox_ptr = 42), "");
  // In-sandbox memory on the other hand can be written to.
  RUN_SANDBOXED(*in_sandbox_ptr = 43);
}

TEST(SandboxHardwareSupportTest, SandboxedCodeNoWriteAccessToTrustedSpace) {
  // Skip this test if hardware sandboxing support cannot be enabled (likely
  // because the system doesn't support PKEYs, see the Initialization test).
  if (!SandboxHardwareSupport::TryActivateBeforeThreadCreation()) return;

  // TODO(saelo): we should instead use the TestWithPlatform mixin for this
  // test, but currently we still need to manually activate sandbox hardware
  // support before that. Once sandbox hardware support has matured further, we
  // should be able to activate it as part of V8's initialization, and then we
  // can switch to the TestWithPlatform mixin here.
  auto platform = v8::platform::NewDefaultPlatform(
      0, v8::platform::IdleTaskSupport::kEnabled);
  i::V8::InitializePlatformForTesting(platform.get());

  auto trusted_range =
      TrustedRange::EnsureProcessWideTrustedRange(kMinimumTrustedRangeSize);
  auto trusted_space_allocator = trusted_range->page_allocator();
  size_t size = trusted_space_allocator->AllocatePageSize();
  void* page_in_trusted_space = trusted_space_allocator->AllocatePages(
      nullptr, size, size, PageAllocator::kReadWrite);
  CHECK_NE(page_in_trusted_space, nullptr);

  volatile int* trusted_space_ptr =
      reinterpret_cast<int*>(page_in_trusted_space);

  // Trusted space memory can be written to from (normal) C++ code...
  *trusted_space_ptr = 42;
  // ... but not from sandboxed code.
  ASSERT_DEATH_IF_SUPPORTED(RUN_SANDBOXED(*trusted_space_ptr = 43), "");
}

TEST(SandboxHardwareSupportTest, DisallowSandboxAccess) {
  // DisallowSandboxAccess is only enforced in DEBUG builds.
  if (!DEBUG_BOOL) return;

  // Skip this test if hardware sandboxing support cannot be enabled (likely
  // because the system doesn't support PKEYs, see the Initialization test).
  if (!SandboxHardwareSupport::TryActivateBeforeThreadCreation()) return;

  base::VirtualAddressSpace global_vas;

  Sandbox sandbox;
  sandbox.Initialize(&global_vas);
  ASSERT_TRUE(SandboxHardwareSupport::IsActive());

  VirtualAddressSpace* sandbox_vas = sandbox.address_space();
  size_t size = sandbox_vas->allocation_granularity();
  size_t alignment = sandbox_vas->allocation_granularity();
  Address ptr =
      sandbox_vas->AllocatePages(VirtualAddressSpace::kNoHint, size, alignment,
                                 PagePermissions::kReadWrite);
  EXPECT_NE(ptr, kNullAddress);
  EXPECT_TRUE(sandbox.Contains(ptr));

  volatile int* in_sandbox_ptr = reinterpret_cast<int*>(ptr);

  // Accessing in-sandbox memory should be possible.
  int value = *in_sandbox_ptr;

  // In debug builds, any (read) access to the sandbox address space should
  // crash while a DisallowSandboxAccess scope is active. This is useful to
  // ensure that a given piece of code cannot be influenced by (potentially)
  // attacker-controlled data inside the sandbox.
  {
    DisallowSandboxAccess no_sandbox_access;
    ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
    {
      // Also test that nesting of DisallowSandboxAccess scopes works correctly.
      DisallowSandboxAccess nested_no_sandbox_access;
      ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
    }
    ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
  }
  // Access should be possible again now.
  value += *in_sandbox_ptr;

  // Simple example involving a heap-allocated DisallowSandboxAccess object.
  DisallowSandboxAccess* heap_no_sandbox_access = new DisallowSandboxAccess;
  ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
  delete heap_no_sandbox_access;
  value += *in_sandbox_ptr;

  // Somewhat more elaborate example that involves a mix of stack- and
  // heap-allocated DisallowSandboxAccess objects.
  {
    DisallowSandboxAccess no_sandbox_access;
    heap_no_sandbox_access = new DisallowSandboxAccess;
    ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
  }
  // Heap-allocated DisallowSandboxAccess scope is still active.
  ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
  {
    DisallowSandboxAccess no_sandbox_access;
    delete heap_no_sandbox_access;
    ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
  }
  value += *in_sandbox_ptr;

  // Mostly just needed to force a use of |value|.
  EXPECT_EQ(value, 0);

  sandbox.TearDown();
}

TEST(SandboxHardwareSupportTest, AllowSandboxAccess) {
  // DisallowSandboxAccess/AllowSandboxAccess is only enforced in DEBUG builds.
  if (!DEBUG_BOOL) return;

  // Skip this test if hardware sandboxing support cannot be enabled (likely
  // because the system doesn't support PKEYs, see the Initialization test).
  if (!SandboxHardwareSupport::TryActivateBeforeThreadCreation()) return;

  base::VirtualAddressSpace global_vas;

  Sandbox sandbox;
  sandbox.Initialize(&global_vas);
  ASSERT_TRUE(SandboxHardwareSupport::IsActive());

  VirtualAddressSpace* sandbox_vas = sandbox.address_space();
  size_t size = sandbox_vas->allocation_granularity();
  size_t alignment = sandbox_vas->allocation_granularity();
  Address ptr =
      sandbox_vas->AllocatePages(VirtualAddressSpace::kNoHint, size, alignment,
                                 PagePermissions::kReadWrite);
  EXPECT_NE(ptr, kNullAddress);
  EXPECT_TRUE(sandbox.Contains(ptr));

  volatile int* in_sandbox_ptr = reinterpret_cast<int*>(ptr);

  // Accessing in-sandbox memory should be possible.
  int value = *in_sandbox_ptr;

  // AllowSandboxAccess can be used to temporarily enable sandbox access in the
  // presence of a DisallowSandboxAccess scope.
  {
    DisallowSandboxAccess no_sandbox_access;
    ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
    {
      AllowSandboxAccess temporary_sandbox_access;
      value += *in_sandbox_ptr;
    }
    ASSERT_DEATH_IF_SUPPORTED(value += *in_sandbox_ptr, "");
  }

  // AllowSandboxAccess scopes cannot be nested. They should only be used for
  // short sequences of code that read/write some data from/to the sandbox.
  {
    DisallowSandboxAccess no_sandbox_access;
    {
      AllowSandboxAccess temporary_sandbox_access;
      {
        ASSERT_DEATH_IF_SUPPORTED(new AllowSandboxAccess(), "");
      }
    }
  }

  // AllowSandboxAccess scopes can be used even if there is no active
  // DisallowSandboxAccess, in which case they do nothing.
  AllowSandboxAccess no_op_sandbox_access;

  // Mostly just needed to force a use of |value|.
  EXPECT_EQ(value, 0);

  sandbox.TearDown();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX_HARDWARE_SUPPORT
