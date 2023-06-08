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
bool ThreadIsolation::Enabled() {
#if V8_HEAP_USE_PKU_JIT_WRITE_PROTECT
  return allocator() != nullptr;
#else
  return false;
#endif
}

// static
template <typename T, typename... Args>
void ThreadIsolation::ConstructNew(T** ptr, Args&&... args) {
  *ptr = reinterpret_cast<T*>(trusted_data_.allocator->Allocate(sizeof(T)));
  if (!*ptr) return;
  new (*ptr) T(std::forward<Args>(args)...);
}

// static
template <typename T>
void ThreadIsolation::Delete(T* ptr) {
  ptr->~T();
  trusted_data_.allocator->Free(ptr);
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
ThreadIsolation::JitPageReference ThreadIsolation::LookupJitPageLocked(
    Address page) {
  trusted_data_.jit_pages_mutex_->AssertHeld();
  auto it = trusted_data_.jit_pages_->find(page);
  CHECK_NE(it, trusted_data_.jit_pages_->end());
  return JitPageReference(it->second);
}

namespace {

size_t GetSize(ThreadIsolation::JitPage* jit_page) {
  return ThreadIsolation::JitPageReference(jit_page).Size();
}

size_t GetSize(ThreadIsolation::JitAllocation allocation) {
  return allocation.Size();
}

template <class T>
void CheckForRegionOverlap(const T& map, Address addr, size_t size) {
  // The data is untrusted from the pov of CFI, so we check that there's no
  // overlaps with existing regions etc.
  CHECK_GE(addr + size, addr);

  // Find an entry in the map with key > addr
  auto it = map.upper_bound(addr);
  bool is_begin = it == map.begin();
  bool is_end = it == map.end();

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
    const typename T::value_type::second_type& prev_entry = it->second;
    Address offset = addr - prev_addr;
    CHECK_LE(GetSize(prev_entry), offset);
  }
}

}  // namespace

ThreadIsolation::JitPage::~JitPage() {
  // TODO(sroettger): check that the page is not in use (scan shadow stacks).
}

ThreadIsolation::JitPageReference::JitPageReference(JitPage* jit_page)
    : page_lock_(&jit_page->mutex_), jit_page_(jit_page) {}

size_t ThreadIsolation::JitPageReference::Size() const {
  return jit_page_->size_;
}

bool ThreadIsolation::JitPageReference::Empty() const {
  return jit_page_->allocations_.empty();
}

void ThreadIsolation::JitPageReference::RegisterAllocation(Address addr,
                                                           size_t size) {
  // The data is untrusted from the pov of CFI, so the checks are security
  // sensitive.
  CHECK_GE(addr, jit_page_->address_);
  Address offset = addr - jit_page_->address_;
  Address end_offset = offset + size;
  CHECK_GT(end_offset, offset);
  CHECK_GT(jit_page_->size_, offset);
  CHECK_GT(jit_page_->size_, end_offset);

  CheckForRegionOverlap(jit_page_->allocations_, addr, size);
  jit_page_->allocations_.emplace(addr, JitAllocation(size));
}

void ThreadIsolation::JitPageReference::UnregisterAllocationsExcept(
    const std::vector<Address>& keep) {
  // TODO(sroettger): check that the page is not in use (scan shadow stacks).
  JitPage::AllocationMap keep_allocations;
  auto keep_iterator = keep.begin();
  for (const auto allocation : jit_page_->allocations_) {
    if (keep_iterator == keep.end()) break;
    if (allocation.first == *keep_iterator) {
      keep_allocations.emplace_hint(keep_allocations.end(), allocation.first,
                                    allocation.second);
      keep_iterator++;
    }
  }
  CHECK_EQ(keep_iterator, keep.end());
  jit_page_->allocations_.swap(keep_allocations);
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
    CheckForRegionOverlap(*trusted_data_.jit_pages_, address, size);
    JitPage* jit_page;
    ConstructNew(&jit_page, address, size);
    trusted_data_.jit_pages_->emplace(address, jit_page);
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

  JitPage* jit_page;

  RwxMemoryWriteScope write_scope("Removing executable memory.");
  {
    base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
    auto it = trusted_data_.jit_pages_->find(address);
    CHECK_NE(it, trusted_data_.jit_pages_->end());
    jit_page = it->second;
    // Lock the mutex to ensure no other thread holds a reference to the object.
    // Acquiring a reference is guarded behind jit_pages_mutex_ above.
    jit_page->mutex_.Lock();
    trusted_data_.jit_pages_->erase(it);
  }
  jit_page->mutex_.Unlock();
  Delete(jit_page);
}

// static
void ThreadIsolation::RegisterJitAllocation(Address page, Address obj,
                                            size_t size) {
  // private, so we skip the Enabled() check.
  base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
  LookupJitPageLocked(page).RegisterAllocation(obj, size);
}

// static
void ThreadIsolation::RegisterInstructionStreamAllocation(Address addr,
                                                          size_t size) {
  if (!Enabled()) return;

  return RegisterJitAllocation(JitPageAddressFromInstructionStream(addr), addr,
                               size);
}

// static
Address ThreadIsolation::JitPageAddressFromInstructionStream(Address addr) {
  return MemoryChunk::FromAddress(addr)->address() +
         MemoryChunkLayout::ObjectPageOffsetInCodePage();
}

// static
void ThreadIsolation::UnregisterInstructionStreamsInPageExcept(
    MemoryChunk* chunk, const std::vector<Address>& keep) {
  if (!Enabled()) return;

  base::MutexGuard guard(trusted_data_.jit_pages_mutex_);
  LookupJitPageLocked(chunk->address() +
                      MemoryChunkLayout::ObjectPageOffsetInCodePage())
      .UnregisterAllocationsExcept(keep);
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
