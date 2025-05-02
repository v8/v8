// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
  ASSERT_TRUE(SandboxHardwareSupport::InitializeBeforeThreadCreation());
  base::VirtualAddressSpace vas;
  Sandbox sandbox;
  sandbox.Initialize(&vas);
  ASSERT_TRUE(SandboxHardwareSupport::IsEnabled());
  sandbox.TearDown();
}

TEST(SandboxHardwareSupportTest, BlockAccessScope) {
  // Skip this test if hardware sandboxing support cannot be enabled (likely
  // because the system doesn't support PKEYs, see the Initialization test).
  if (!SandboxHardwareSupport::InitializeBeforeThreadCreation()) return;

  base::VirtualAddressSpace global_vas;

  Sandbox sandbox;
  sandbox.Initialize(&global_vas);
  ASSERT_TRUE(SandboxHardwareSupport::IsEnabled());

  VirtualAddressSpace* sandbox_vas = sandbox.address_space();
  size_t size = sandbox_vas->allocation_granularity();
  size_t alignment = sandbox_vas->allocation_granularity();
  Address ptr =
      sandbox_vas->AllocatePages(VirtualAddressSpace::kNoHint, size, alignment,
                                 PagePermissions::kReadWrite);
  EXPECT_NE(ptr, kNullAddress);
  EXPECT_TRUE(sandbox.Contains(ptr));

  int* in_sandbox_ptr = reinterpret_cast<int*>(ptr);

  // Accessing in-sandbox memory should be possible.
  EXPECT_EQ(*in_sandbox_ptr, 0);

  {
    SandboxHardwareSupport::BlockAccessScope no_sandbox_access =
        SandboxHardwareSupport::MaybeBlockAccess();

    // Any (read) access to the sandbox address space should now crash.
    // This is useful to ensure that a given piece of code cannot be influenced
    // by attacker-controlled data inside the sandbox.
    int value = 0;
    ASSERT_DEATH_IF_SUPPORTED(value = *in_sandbox_ptr, "");
    EXPECT_EQ(value, 0);
  }

  // Access should be possible again now.
  EXPECT_EQ(*in_sandbox_ptr, 0);

  sandbox.TearDown();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_SANDBOX_HARDWARE_SUPPORT
