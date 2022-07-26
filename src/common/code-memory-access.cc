// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/code-memory-access-inl.h"

namespace v8 {
namespace internal {

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT
thread_local int RwxMemoryWriteScope::code_space_write_nesting_level_ = 0;
#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT

#if V8_HAS_PKU_JIT_WRITE_PROTECT
int RwxMemoryWriteScope::memory_protection_key_ =
    base::MemoryProtectionKey::kNoMemoryProtectionKey;

#if DEBUG
bool RwxMemoryWriteScope::pkey_initialized = false;
#endif

void RwxMemoryWriteScope::InitializeMemoryProtectionKey() {
  // Flip {pkey_initialized} (in debug mode) and check the new value.
  DCHECK_EQ(true, pkey_initialized = !pkey_initialized);
  memory_protection_key_ = base::MemoryProtectionKey::AllocateKey();
  DCHECK(memory_protection_key_ > 0 ||
         memory_protection_key_ ==
             base::MemoryProtectionKey::kNoMemoryProtectionKey);
}

bool RwxMemoryWriteScope::IsPKUWritable() {
  DCHECK(pkey_initialized);
  return base::MemoryProtectionKey::GetKeyPermission(memory_protection_key_) ==
         base::MemoryProtectionKey::kNoRestrictions;
}

ResetPKUPermissionsForThreadSpawning::ResetPKUPermissionsForThreadSpawning() {
  if (!RwxMemoryWriteScope::IsSupported()) return;
  int pkey = RwxMemoryWriteScope::memory_protection_key();
  was_writable_ = base::MemoryProtectionKey::GetKeyPermission(pkey) ==
                  base::MemoryProtectionKey::kNoRestrictions;
  if (was_writable_) {
    base::MemoryProtectionKey::SetPermissionsForKey(
        pkey, base::MemoryProtectionKey::kDisableWrite);
  }
}

ResetPKUPermissionsForThreadSpawning::~ResetPKUPermissionsForThreadSpawning() {
  if (!RwxMemoryWriteScope::IsSupported()) return;
  int pkey = RwxMemoryWriteScope::memory_protection_key();
  if (was_writable_) {
    base::MemoryProtectionKey::SetPermissionsForKey(
        pkey, base::MemoryProtectionKey::kNoRestrictions);
  }
}
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

RwxMemoryWriteScopeForTesting::RwxMemoryWriteScopeForTesting()
    : RwxMemoryWriteScope("For Testing") {}

RwxMemoryWriteScopeForTesting::~RwxMemoryWriteScopeForTesting() {}

}  // namespace internal
}  // namespace v8
