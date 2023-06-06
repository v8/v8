// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/common/code-memory-access-inl.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

ThreadIsolation::TrustedData ThreadIsolation::trusted_data_;
ThreadIsolation::UntrustedData ThreadIsolation::untrusted_data_;

#if V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT
thread_local int RwxMemoryWriteScope::code_space_write_nesting_level_ = 0;
#endif  // V8_HAS_PTHREAD_JIT_WRITE_PROTECT || V8_HAS_PKU_JIT_WRITE_PROTECT

#if V8_HAS_PKU_JIT_WRITE_PROTECT

bool RwxMemoryWriteScope::IsPKUWritable() {
  DCHECK(ThreadIsolation::initialized());
  return base::MemoryProtectionKey::GetKeyPermission(ThreadIsolation::pkey()) ==
         base::MemoryProtectionKey::kNoRestrictions;
}

void RwxMemoryWriteScope::SetDefaultPermissionsForSignalHandler() {
  DCHECK(ThreadIsolation::initialized());
  if (!RwxMemoryWriteScope::IsSupportedUntrusted()) return;
  base::MemoryProtectionKey::SetPermissionsForKey(
      ThreadIsolation::untrusted_pkey(),
      base::MemoryProtectionKey::kDisableWrite);
}

#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT

RwxMemoryWriteScopeForTesting::RwxMemoryWriteScopeForTesting()
    : RwxMemoryWriteScope("For Testing") {}

RwxMemoryWriteScopeForTesting::~RwxMemoryWriteScopeForTesting() {}

// static
bool ThreadIsolation::Enabled() { return allocator() != nullptr; }

// static
template <class T, typename... Args>
void ThreadIsolation::ConstructNew(T** ptr, Args&&... args) {
  *ptr = reinterpret_cast<T*>(trusted_data_.allocator->Allocate(sizeof(T)));
  if (!*ptr) return;
  new (*ptr) T(std::forward<Args>(args)...);
}

// static
void ThreadIsolation::Initialize(
    ThreadIsolatedAllocator* thread_isolated_allocator) {
#if DEBUG
  untrusted_data_.initialized = true;
#endif

  if (!thread_isolated_allocator) {
    return;
  }

  if (v8_flags.jitless) {
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

  trusted_data_.allocator = thread_isolated_allocator;

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  trusted_data_.pkey = trusted_data_.allocator->Pkey();
  untrusted_data_.pkey = trusted_data_.pkey;

  {
    RwxMemoryWriteScope write_scope("Initialize thread isolation.");
    ConstructNew(&trusted_data_.jit_pages_mutex_);
    ConstructNew(&trusted_data_.jit_pages_);
  }

  base::MemoryProtectionKey::SetPermissionsAndKey(
      {reinterpret_cast<Address>(&trusted_data_), sizeof(trusted_data_)},
      v8::PageAllocator::Permission::kRead, trusted_data_.pkey);
#endif
}

// static
void ThreadIsolation::CheckForRegionOverlapLocked(Address addr, size_t size) {
  // Find an entry in the map with key > addr
  auto it = trusted_data_.jit_pages_->upper_bound(addr);
  bool is_begin = it == trusted_data_.jit_pages_->begin();
  bool is_end = it == trusted_data_.jit_pages_->end();

  // Check for overlap with the next entry
  if (!is_end) {
    Address next_addr = it->first;
    Address offset = next_addr - addr;
    CHECK_LE(size, offset);
  }

  // Check the previous entry for overlap
  if (!is_begin) {
    it--;
    Address prev_addr = it->first;
    JitPage& prev_region = it->second;
    Address offset = addr - prev_addr;
    CHECK_LE(prev_region.Size(), offset);
  }
}

// static
bool ThreadIsolation::RegisterJitPageAndMakeExecutable(Address address,
                                                       size_t size) {
  DCHECK(Enabled());

  RwxMemoryWriteScope write_scope("Adding new executable memory.");

  // TODO(sroettger): need to make sure that the memory is zero-initialized.
  // maybe map over it with MAP_FIXED, or call MADV_DONTNEED, or fall back to
  // memset.

  {
    base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
    CheckForRegionOverlapLocked(address, size);
    trusted_data_.jit_pages_->emplace(address, JitPage(size));
  }

#if V8_HAS_PKU_JIT_WRITE_PROTECT
  return base::MemoryProtectionKey::SetPermissionsAndKey(
      {address, size}, PageAllocator::Permission::kReadWriteExecute, pkey());
#else   // V8_HAS_PKU_JIT_WRITE_PROTECT
  UNREACHABLE();
#endif  // V8_HAS_PKU_JIT_WRITE_PROTECT
}

void ThreadIsolation::UnregisterJitPage(Address address) {
  if (!Enabled()) return;

  RwxMemoryWriteScope write_scope("Removing executable memory.");
  base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
  CHECK_EQ(trusted_data_.jit_pages_->erase(address), 1);
}

#if DEBUG

// static
void ThreadIsolation::CheckTrackedMemoryEmpty() {
  if (!Enabled()) return;
  DCHECK(trusted_data_.jit_pages_->empty());
}

#endif  // DEBUG

}  // namespace internal
}  // namespace v8
