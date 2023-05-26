// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/code-memory-access-inl.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

ThreadIsolationData g_thread_isolation_data;

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT
thread_local int RwxMemoryWriteScope::code_space_write_nesting_level_ = 0;
#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT

#if V8_HAS_PKU_JIT_WRITE_PROTECT

bool RwxMemoryWriteScope::IsPKUWritable() {
  DCHECK(g_thread_isolation_data.initialized);
  return base::MemoryProtectionKey::GetKeyPermission(
             g_thread_isolation_data.pkey) ==
         base::MemoryProtectionKey::kNoRestrictions;
}

void RwxMemoryWriteScope::SetDefaultPermissionsForSignalHandler() {
  DCHECK(g_thread_isolation_data.initialized);
  if (!RwxMemoryWriteScope::IsSupported()) return;
  base::MemoryProtectionKey::SetPermissionsForKey(
      g_thread_isolation_data.pkey, base::MemoryProtectionKey::kDisableWrite);
}

#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

RwxMemoryWriteScopeForTesting::RwxMemoryWriteScopeForTesting()
    : RwxMemoryWriteScope("For Testing") {}

RwxMemoryWriteScopeForTesting::~RwxMemoryWriteScopeForTesting() {}

void ThreadIsolationData::Initialize(
    ThreadIsolatedAllocator* thread_isolated_allocator) {
#if DEBUG
  initialized = true;
#endif

  if (!thread_isolated_allocator) {
    return;
  }

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  if (!base::MemoryProtectionKey::InitializeMemoryProtectionKeySupport()) {
    return;
  }
#endif

  // Check that our compile time assumed page size that we use for padding was
  // large enough.
  CHECK_GE(THREAD_ISOLATION_ALIGN_SZ,
           GetPlatformPageAllocator()->CommitPageSize());

  allocator = thread_isolated_allocator;

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  pkey = allocator->Pkey();
  base::MemoryProtectionKey::SetPermissionsAndKey(
      GetPlatformPageAllocator(),
      {reinterpret_cast<Address>(this), sizeof(*this)},
      v8::PageAllocator::Permission::kRead, pkey);
#endif
}

}  // namespace internal
}  // namespace v8
