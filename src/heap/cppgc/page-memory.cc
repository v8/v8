// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/page-memory.h"

#include <algorithm>
#include <cstddef>

#include "src/base/lazy-instance.h"
#include "src/base/macros.h"
#include "src/base/sanitizer/asan.h"
#include "src/heap/cppgc/memory.h"
#include "src/heap/cppgc/platform.h"

namespace cppgc {
namespace internal {

NormalPageMemoryPool* NormalPageMemoryPool::g_instance_ = nullptr;

namespace {

V8_WARN_UNUSED_RESULT bool TryUnprotect(PageAllocator& allocator,
                                        const PageMemory& page_memory) {
  if (SupportsCommittingGuardPages(allocator)) {
    return allocator.SetPermissions(page_memory.writeable_region().base(),
                                    page_memory.writeable_region().size(),
                                    PageAllocator::Permission::kReadWrite);
  }
  // No protection using guard pages in case the allocator cannot commit at
  // the required granularity. Only protect if the allocator supports
  // committing at that granularity.
  //
  // The allocator needs to support committing the overall range.
  CHECK_EQ(0u,
           page_memory.overall_region().size() % allocator.CommitPageSize());
  return allocator.SetPermissions(page_memory.overall_region().base(),
                                  page_memory.overall_region().size(),
                                  PageAllocator::Permission::kReadWrite);
}

V8_WARN_UNUSED_RESULT bool TryDiscard(PageAllocator& allocator,
                                      const PageMemory& page_memory) {
  if (SupportsCommittingGuardPages(allocator)) {
    // Swap the same region, providing the OS with a chance for fast lookup and
    // change.
    return allocator.DiscardSystemPages(page_memory.writeable_region().base(),
                                        page_memory.writeable_region().size());
  }
  // See Unprotect().
  CHECK_EQ(0u,
           page_memory.overall_region().size() % allocator.CommitPageSize());
  return allocator.DiscardSystemPages(page_memory.overall_region().base(),
                                      page_memory.overall_region().size());
}

v8::base::Optional<MemoryRegion> ReserveMemoryRegion(PageAllocator& allocator,
                                                     size_t allocation_size) {
  void* region_memory =
      allocator.AllocatePages(nullptr, allocation_size, kPageSize,
                              PageAllocator::Permission::kNoAccess);
  if (!region_memory) {
    return v8::base::nullopt;
  }
  const MemoryRegion reserved_region(static_cast<Address>(region_memory),
                                     allocation_size);
  DCHECK_EQ(reserved_region.base() + allocation_size, reserved_region.end());
  return reserved_region;
}

void FreeMemoryRegion(PageAllocator& allocator,
                      const MemoryRegion& reserved_region) {
  // Make sure pages returned to OS are unpoisoned.
  ASAN_UNPOISON_MEMORY_REGION(reserved_region.base(), reserved_region.size());
  allocator.FreePages(reserved_region.base(), reserved_region.size());
}

std::unique_ptr<PageMemoryRegion> CreateNormalPageMemoryRegion(
    PageAllocator& allocator) {
  DCHECK_EQ(0u, kPageSize % allocator.AllocatePageSize());
  const auto region = ReserveMemoryRegion(allocator, kPageSize);
  if (!region) return {};
  auto result = std::unique_ptr<PageMemoryRegion>(
      new PageMemoryRegion(allocator, *region));
  return result;
}

std::unique_ptr<PageMemoryRegion> CreateLargePageMemoryRegion(
    PageAllocator& allocator, size_t length) {
  const auto region = ReserveMemoryRegion(
      allocator,
      RoundUp(length + 2 * kGuardPageSize, allocator.AllocatePageSize()));
  if (!region) return {};
  auto result = std::unique_ptr<PageMemoryRegion>(
      new PageMemoryRegion(allocator, *region));
  return result;
}

}  // namespace

PageMemoryRegion::PageMemoryRegion(PageAllocator& allocator,
                                   MemoryRegion reserved_region)
    : allocator_(allocator), reserved_region_(reserved_region) {}

PageMemoryRegion::~PageMemoryRegion() {
  FreeMemoryRegion(allocator_, reserved_region());
}

void PageMemoryRegion::UnprotectForTesting() {
  CHECK(TryUnprotect(allocator_, GetPageMemory()));
}

PageMemoryRegionTree::PageMemoryRegionTree() = default;

PageMemoryRegionTree::~PageMemoryRegionTree() = default;

void PageMemoryRegionTree::Add(PageMemoryRegion* region) {
  DCHECK(region);
  auto result = set_.emplace(region->reserved_region().base(), region);
  USE(result);
  DCHECK(result.second);
}

void PageMemoryRegionTree::Remove(PageMemoryRegion* region) {
  DCHECK(region);
  auto size = set_.erase(region->reserved_region().base());
  USE(size);
  DCHECK_EQ(1u, size);
}

NormalPageMemoryPool& NormalPageMemoryPool::Instance() {
  DCHECK_NOT_NULL(g_instance_);
  return *g_instance_;
}

void NormalPageMemoryPool::Initialize(PageAllocator& page_allocator) {
  DCHECK_NULL(g_instance_);
  static v8::base::LeakyObject<NormalPageMemoryPool> instance(page_allocator);
  g_instance_ = instance.get();
}

NormalPageMemoryPool::NormalPageMemoryPool(PageAllocator& page_allocator)
    : page_allocator_(page_allocator) {}

PageMemoryRegion* NormalPageMemoryPool::AllocatePageMemoryRegion() {
  PageMemoryRegion* result = nullptr;
  {
    v8::base::MutexGuard guard(&mutex_);
    if (!pool_.empty()) {
      result = pool_.back();
      DCHECK_NOT_NULL(result);
      DCHECK_NE(normal_page_memory_regions_.end(),
                normal_page_memory_regions_.find(result));
      pool_.pop_back();
    }
  }
  if (!result) {
    auto pmr = CreateNormalPageMemoryRegion(page_allocator_);
    if (!pmr) return nullptr;
    if (V8_UNLIKELY(!TryUnprotect(page_allocator_, pmr->GetPageMemory())))
      return nullptr;
    auto* pmr_raw = pmr.get();
    v8::base::MutexGuard guard(&mutex_);
    normal_page_memory_regions_.emplace(pmr.get(), std::move(pmr));
    return pmr_raw;
  }
  void* base = result->GetPageMemory().writeable_region().base();
  const size_t size = result->GetPageMemory().writeable_region().size();
  ASAN_UNPOISON_MEMORY_REGION(base, size);
#if DEBUG
  CheckMemoryIsZero(base, size);
#endif
  return result;
}

void NormalPageMemoryPool::FreePageMemoryRegion(
    PageMemoryRegion* pmr, FreeMemoryHandling free_memory_handling) {
  DCHECK_NOT_NULL(pmr);
  DCHECK_EQ(pmr->GetPageMemory().overall_region().size(), kPageSize);
  // Oilpan requires the pages to be zero-initialized. Either discard it or
  // memset to zero.
  if (free_memory_handling == FreeMemoryHandling::kDiscardWherePossible) {
    CHECK(TryDiscard(page_allocator_, pmr->GetPageMemory()));
  } else {
    void* base = pmr->GetPageMemory().writeable_region().base();
    const size_t size = pmr->GetPageMemory().writeable_region().size();
    AsanUnpoisonScope unpoison_for_memset(base, size);
    std::memset(base, 0, size);
  }
  v8::base::MutexGuard guard(&mutex_);
  pool_.push_back(pmr);
  DCHECK_NE(normal_page_memory_regions_.end(),
            normal_page_memory_regions_.find(pmr));
}

void NormalPageMemoryPool::DiscardPooledPages() {
  decltype(pool_) copied_pool;
  {
    v8::base::MutexGuard guard(&mutex_);
    copied_pool = pool_;
  }
  for (auto* pmr : copied_pool) {
    DCHECK_NOT_NULL(pmr);
    CHECK(TryDiscard(page_allocator_, pmr->GetPageMemory()));
  }
}

void NormalPageMemoryPool::FlushPooledPagesForTesting() {
  v8::base::MutexGuard guard(&mutex_);
  pool_.clear();
  normal_page_memory_regions_.clear();
}

PageBackend::PageBackend(PageAllocator& large_page_allocator)
    : large_page_allocator_(large_page_allocator) {}

PageBackend::~PageBackend() = default;

Address PageBackend::TryAllocateNormalPageMemory() {
  if (PageMemoryRegion* cached =
          NormalPageMemoryPool::Instance().AllocatePageMemoryRegion()) {
    const auto writeable_region = cached->GetPageMemory().writeable_region();
    v8::base::MutexGuard guard(&mutex_);
    page_memory_region_tree_.Add(cached);
    return writeable_region.base();
  }
  return nullptr;
}

void PageBackend::FreeNormalPageMemory(
    Address writeable_base, FreeMemoryHandling free_memory_handling) {
  v8::base::MutexGuard guard(&mutex_);
  auto* pmr = page_memory_region_tree_.Lookup(writeable_base);
  DCHECK_NOT_NULL(pmr);
  page_memory_region_tree_.Remove(pmr);
  NormalPageMemoryPool::Instance().FreePageMemoryRegion(pmr,
                                                        free_memory_handling);
}

Address PageBackend::TryAllocateLargePageMemory(size_t size) {
  v8::base::MutexGuard guard(&mutex_);
  auto pmr = CreateLargePageMemoryRegion(large_page_allocator_, size);
  if (!pmr) {
    return nullptr;
  }
  const PageMemory pm = pmr->GetPageMemory();
  if (V8_LIKELY(TryUnprotect(large_page_allocator_, pm))) {
    page_memory_region_tree_.Add(pmr.get());
    large_page_memory_regions_.emplace(pmr.get(), std::move(pmr));
    return pm.writeable_region().base();
  }
  return nullptr;
}

void PageBackend::FreeLargePageMemory(Address writeable_base) {
  v8::base::MutexGuard guard(&mutex_);
  PageMemoryRegion* pmr = page_memory_region_tree_.Lookup(writeable_base);
  page_memory_region_tree_.Remove(pmr);
  auto size = large_page_memory_regions_.erase(pmr);
  USE(size);
  DCHECK_EQ(1u, size);
}

}  // namespace internal
}  // namespace cppgc
