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

// static
SandboxHardwareSupport::BlockAccessScope
SandboxHardwareSupport::MaybeBlockAccess() {
  return BlockAccessScope(pkey_);
}

SandboxHardwareSupport::BlockAccessScope::BlockAccessScope(int pkey)
    : pkey_(pkey) {
  if (pkey_ != base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    base::MemoryProtectionKey::SetPermissionsForKey(
        pkey_, base::MemoryProtectionKey::Permission::kDisableAccess);
  }
}

SandboxHardwareSupport::BlockAccessScope::~BlockAccessScope() {
  if (pkey_ != base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    base::MemoryProtectionKey::SetPermissionsForKey(
        pkey_, base::MemoryProtectionKey::Permission::kNoRestrictions);
  }
}

#else  // V8_ENABLE_SANDBOX_HARDWARE_SUPPORT

// static
bool SandboxHardwareSupport::InitializeBeforeThreadCreation() { return false; }

// static
bool SandboxHardwareSupport::TryEnable(Address addr, size_t size) {
  return false;
}

// static
bool SandboxHardwareSupport::IsEnabled() { return false; }

// static
void SandboxHardwareSupport::SetDefaultPermissionsForSignalHandler() {}

// static
void SandboxHardwareSupport::NotifyReadOnlyPageCreated(
    Address addr, size_t size, PageAllocator::Permission perm) {}

// static
SandboxHardwareSupport::BlockAccessScope
SandboxHardwareSupport::MaybeBlockAccess() {
  return BlockAccessScope();
}

#endif

}  // namespace internal
}  // namespace v8
