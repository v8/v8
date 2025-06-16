// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/hardware-support.h"

#include "src/base/platform/memory-protection-key.h"
#include "src/base/platform/platform.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX_HARDWARE_SUPPORT

int SandboxHardwareSupport::sandbox_pkey_ =
    base::MemoryProtectionKey::kNoMemoryProtectionKey;
int SandboxHardwareSupport::out_of_sandbox_pkey_ =
    base::MemoryProtectionKey::kNoMemoryProtectionKey;
int SandboxHardwareSupport::extension_pkey_ =
    base::MemoryProtectionKey::kNoMemoryProtectionKey;
uint32_t SandboxHardwareSupport::sandboxed_mode_pkey_mask_ = 0;

// static
bool SandboxHardwareSupport::TryActivateBeforeThreadCreation() {
  bool success = TryActivate();
  CHECK_IMPLIES(v8_flags.force_memory_protection_keys, success);
  return success;
}

// static
bool SandboxHardwareSupport::IsActive() {
  return sandbox_pkey_ != base::MemoryProtectionKey::kNoMemoryProtectionKey;
}

// static
void SandboxHardwareSupport::RegisterSandboxMemory(Address addr, size_t size) {
  if (!IsActive()) return;

  CHECK(base::MemoryProtectionKey::SetPermissionsAndKey(
      {addr, size}, v8::PageAllocator::Permission::kNoAccess, sandbox_pkey_));
}

void SandboxHardwareSupport::RegisterOutOfSandboxMemory(
    Address addr, size_t size, PageAllocator::Permission page_permission) {
  if (!IsActive()) return;

  CHECK(base::MemoryProtectionKey::SetPermissionsAndKey(
      {addr, size}, page_permission, out_of_sandbox_pkey_));
}

void SandboxHardwareSupport::RegisterUnsafeSandboxExtensionMemory(Address addr,
                                                                  size_t size) {
  if (!IsActive()) return;

  CHECK(base::MemoryProtectionKey::SetPermissionsAndKey(
      {addr, size}, v8::PageAllocator::Permission::kReadWrite,
      extension_pkey_));
}

// static
void SandboxHardwareSupport::RegisterReadOnlyMemoryInsideSandbox(
    Address addr, size_t size, PageAllocator::Permission perm) {
  if (!IsActive()) return;

  // Reset the pkey of the read-only page to the default pkey, since some
  // SBXCHECKs will safely read read-only data from the heap.
  CHECK(base::MemoryProtectionKey::SetPermissionsAndKey(
      {addr, size}, perm, base::MemoryProtectionKey::kDefaultProtectionKey));
}

// static
void SandboxHardwareSupport::EnterSandboxedExecutionModeForCurrentThread() {
  if (!IsActive()) return;

  DCHECK_EQ(CurrentSandboxingMode(), CodeSandboxingMode::kUnsandboxed);
  base::MemoryProtectionKey::SetPermissionsForKey(
      out_of_sandbox_pkey_,
      base::MemoryProtectionKey::Permission::kDisableWrite);
}

// static
void SandboxHardwareSupport::ExitSandboxedExecutionModeForCurrentThread() {
  if (!IsActive()) return;

  DCHECK_EQ(CurrentSandboxingMode(), CodeSandboxingMode::kSandboxed);
  base::MemoryProtectionKey::SetPermissionsForKey(
      out_of_sandbox_pkey_,
      base::MemoryProtectionKey::Permission::kNoRestrictions);
}

// static
CodeSandboxingMode SandboxHardwareSupport::CurrentSandboxingMode() {
  if (!IsActive()) return CodeSandboxingMode::kUnsandboxed;

  auto key_permissions =
      base::MemoryProtectionKey::GetKeyPermission(out_of_sandbox_pkey_);
  if (key_permissions == base::MemoryProtectionKey::Permission::kDisableWrite) {
    return CodeSandboxingMode::kSandboxed;
  } else {
    DCHECK_EQ(key_permissions,
              base::MemoryProtectionKey::Permission::kNoRestrictions);
    return CodeSandboxingMode::kUnsandboxed;
  }
}

// static
bool SandboxHardwareSupport::CurrentSandboxingModeIs(
    CodeSandboxingMode expected_mode) {
  if (!IsActive()) return true;
  return CurrentSandboxingMode() == expected_mode;
}

// static
bool SandboxHardwareSupport::TryActivate() {
  DCHECK(!IsActive());

  if (!base::MemoryProtectionKey::HasMemoryProtectionKeyAPIs()) {
    return false;
  }

  sandbox_pkey_ = base::MemoryProtectionKey::AllocateKey();
  if (sandbox_pkey_ == base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    return false;
  }

  // Ideally, this would be the default protection key.
  // See the comment at the declaration of these keys for more information
  // about why that currently isn't the case.
  out_of_sandbox_pkey_ = base::MemoryProtectionKey::AllocateKey();
  if (out_of_sandbox_pkey_ ==
      base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    base::MemoryProtectionKey::FreeKey(sandbox_pkey_);
    sandbox_pkey_ = base::MemoryProtectionKey::kNoMemoryProtectionKey;
    return false;
  }

  extension_pkey_ = base::MemoryProtectionKey::AllocateKey();
  if (extension_pkey_ == base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    base::MemoryProtectionKey::FreeKey(sandbox_pkey_);
    base::MemoryProtectionKey::FreeKey(out_of_sandbox_pkey_);
    sandbox_pkey_ = base::MemoryProtectionKey::kNoMemoryProtectionKey;
    out_of_sandbox_pkey_ = base::MemoryProtectionKey::kNoMemoryProtectionKey;
    return false;
  }

  // Compute the PKEY mask for entering sandboxed execution mode. For that, we
  // simply need to remove write access for the out-of-sandbox pkey.
  sandboxed_mode_pkey_mask_ =
      base::MemoryProtectionKey::ComputeRegisterMaskForPermissionSwitch(
          out_of_sandbox_pkey_,
          base::MemoryProtectionKey::Permission::kDisableWrite);
  // We use zero to indicate that sandbox hardware support is inactive.
  CHECK_NE(sandboxed_mode_pkey_mask_, 0);

  CHECK(IsActive());
  return true;
}

#ifdef DEBUG
// DisallowSandboxAccess scopes can be arbitrarily nested and even attached to
// heap-allocated objects (so their lifetime isn't necessarily tied to a stack
// frame). For that to work correctly, we need to track the activation count in
// a per-thread global variable.
thread_local unsigned disallow_sandbox_access_activation_counter_ = 0;
// AllowSandboxAccess scopes on the other hand cannot be nested. There must be
// at most a single one active at any point in time. These are supposed to only
// be used for short sequences of code that's otherwise running with an active
// DisallowSandboxAccess scope.
thread_local bool has_active_allow_sandbox_access_scope_ = false;

DisallowSandboxAccess::DisallowSandboxAccess() {
  pkey_ = SandboxHardwareSupport::sandbox_pkey_;
  if (pkey_ == base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    return;
  }

  // Using a DisallowSandboxAccess inside an AllowSandboxAccess isn't currently
  // allowed, but we could add support for that in the future if needed.
  DCHECK_WITH_MSG(!has_active_allow_sandbox_access_scope_,
                  "DisallowSandboxAccess cannot currently be nested inside an "
                  "AllowSandboxAccess");

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

AllowSandboxAccess::AllowSandboxAccess() {
  if (disallow_sandbox_access_activation_counter_ == 0) {
    // This means that either scope enforcement is disabled due to a lack of
    // PKEY support on the system or that there is no active
    // DisallowSandboxAccess. In both cases, we don't need to do anything here
    // and this scope object is just a no-op.
    pkey_ = base::MemoryProtectionKey::kNoMemoryProtectionKey;
    return;
  }

  DCHECK_WITH_MSG(!has_active_allow_sandbox_access_scope_,
                  "AllowSandboxAccess scopes cannot be nested");
  has_active_allow_sandbox_access_scope_ = true;

  pkey_ = SandboxHardwareSupport::sandbox_pkey_;
  // We must have an active DisallowSandboxAccess so PKEYs must be supported.
  DCHECK_NE(pkey_, base::MemoryProtectionKey::kNoMemoryProtectionKey);

  DCHECK_EQ(base::MemoryProtectionKey::GetKeyPermission(pkey_),
            base::MemoryProtectionKey::Permission::kDisableAccess);
  base::MemoryProtectionKey::SetPermissionsForKey(
      pkey_, base::MemoryProtectionKey::Permission::kNoRestrictions);
}

AllowSandboxAccess::~AllowSandboxAccess() {
  if (pkey_ == base::MemoryProtectionKey::kNoMemoryProtectionKey) {
    // There was no DisallowSandboxAccess scope active when this
    // AllowSandboxAccess scope was created, and we don't expect one to have
    // been created in the meantime.
    DCHECK_EQ(disallow_sandbox_access_activation_counter_, 0);
    return;
  }

  // There was an active DisallowSandboxAccess scope when this
  // AllowSandboxAccess scope was created, and we expect it to still be there.
  DCHECK_GT(disallow_sandbox_access_activation_counter_, 0);

  DCHECK(has_active_allow_sandbox_access_scope_);
  has_active_allow_sandbox_access_scope_ = false;

  DCHECK_EQ(base::MemoryProtectionKey::GetKeyPermission(pkey_),
            base::MemoryProtectionKey::Permission::kNoRestrictions);
  base::MemoryProtectionKey::SetPermissionsForKey(
      pkey_, base::MemoryProtectionKey::Permission::kDisableAccess);
}
#endif  // DEBUG

#endif  // V8_ENABLE_SANDBOX_HARDWARE_SUPPORT

}  // namespace internal
}  // namespace v8
