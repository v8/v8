// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/hardware-support.h"

#include "src/base/platform/memory-protection-key.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX_HARDWARE_SUPPORT

int SandboxHardwareSupport::pkey_ =
    base::MemoryProtectionKey::kNoMemoryProtectionKey;

// static
bool SandboxHardwareSupport::InitializeBeforeThreadCreation() {
  DCHECK_EQ(pkey_, base::MemoryProtectionKey::kNoMemoryProtectionKey);
  pkey_ = base::MemoryProtectionKey::AllocateKey();
  return pkey_ != base::MemoryProtectionKey::kNoMemoryProtectionKey;
}

// static
bool SandboxHardwareSupport::TryEnable(Address addr, size_t size) {
  if (pkey_ == base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    return false;
  }

  // If we have a valid PKEY, we expect this to always succeed.
  CHECK(base::MemoryProtectionKey::SetPermissionsAndKey(
      {addr, size}, v8::PageAllocator::Permission::kNoAccess, pkey_));
  return true;
}

// static
bool SandboxHardwareSupport::IsEnabled() {
  return pkey_ != base::MemoryProtectionKey::kNoMemoryProtectionKey;
}

// static
void SandboxHardwareSupport::SetDefaultPermissionsForSignalHandler() {
  if (!IsEnabled()) return;

  base::MemoryProtectionKey::SetPermissionsForKey(
      pkey_, base::MemoryProtectionKey::Permission::kNoRestrictions);
}

// static
void SandboxHardwareSupport::NotifyReadOnlyPageCreated(
    Address addr, size_t size, PageAllocator::Permission perm) {
  if (!IsEnabled()) return;

  // Reset the pkey of the read-only page to the default pkey, since some
  // SBXCHECKs will safely read read-only data from the heap.
  CHECK(base::MemoryProtectionKey::SetPermissionsAndKey(
      {addr, size}, perm, base::MemoryProtectionKey::kDefaultProtectionKey));
}

#ifdef DEBUG
// DisallowSandboxAccess scopes can be arbitrarily nested and even attached to
// heap-allocated objects (so their lifetime isn't necessarily tied to a stack
// frame). For that to work correctly, we need to track the activation count in
// a per-thread global variable.
thread_local unsigned disallow_sandbox_access_activation_counter_ = 0;

DisallowSandboxAccess::DisallowSandboxAccess() {
  pkey_ = SandboxHardwareSupport::pkey_;
  if (pkey_ == base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    return;
  }

  if (disallow_sandbox_access_activation_counter_ == 0) {
    DCHECK_EQ(base::MemoryProtectionKey::GetKeyPermission(pkey_),
              base::MemoryProtectionKey::Permission::kNoRestrictions);
    base::MemoryProtectionKey::SetPermissionsForKey(
        pkey_, base::MemoryProtectionKey::Permission::kDisableAccess);
  }
  disallow_sandbox_access_activation_counter_ += 1;
}

DisallowSandboxAccess::~DisallowSandboxAccess() {
  if (pkey_ == base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    return;
  }

  DCHECK_NE(disallow_sandbox_access_activation_counter_, 0);
  disallow_sandbox_access_activation_counter_ -= 1;
  if (disallow_sandbox_access_activation_counter_ == 0) {
    DCHECK_EQ(base::MemoryProtectionKey::GetKeyPermission(pkey_),
              base::MemoryProtectionKey::Permission::kDisableAccess);
    base::MemoryProtectionKey::SetPermissionsForKey(
        pkey_, base::MemoryProtectionKey::Permission::kNoRestrictions);
  }
}
#endif  // DEBUG

#endif  // V8_ENABLE_SANDBOX_HARDWARE_SUPPORT

}  // namespace internal
}  // namespace v8
