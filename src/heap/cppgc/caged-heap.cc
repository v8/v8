// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/internal/caged-heap.h"

#include <map>

#include "v8config.h"  // NOLINT(build/include_directory)

#if !defined(CPPGC_CAGED_HEAP)
#error "Must be compiled with caged heap enabled"
#endif

#include "include/cppgc/internal/caged-heap-local-data.h"
#include "include/cppgc/member.h"
#include "include/cppgc/platform.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/lazy-instance.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/caged-heap.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/member.h"

namespace cppgc {
namespace internal {

static_assert(api_constants::kCagedHeapReservationSize ==
              kCagedHeapReservationSize);
static_assert(api_constants::kCagedHeapReservationAlignment ==
              kCagedHeapReservationAlignment);
static_assert(api_constants::kCagedHeapNormalPageReservationSize ==
              kCagedHeapNormalPageReservationSize);

namespace {

// TODO(v8:12231): Remove once shared cage is there. Currently it's only used
// for large pages lookup in the write barrier.
using Cages = std::map<uintptr_t /*cage_base*/, HeapBase*>;

static Cages& global_cages() {
  static v8::base::LeakyObject<Cages> instance;
  return *instance.get();
}

VirtualMemory ReserveCagedHeap(PageAllocator& platform_allocator) {
  DCHECK_EQ(0u,
            kCagedHeapReservationSize % platform_allocator.AllocatePageSize());

  static constexpr size_t kAllocationTries = 4;
  for (size_t i = 0; i < kAllocationTries; ++i) {
    void* hint = reinterpret_cast<void*>(RoundDown(
        reinterpret_cast<uintptr_t>(platform_allocator.GetRandomMmapAddr()),
        kCagedHeapReservationAlignment));

    VirtualMemory memory(&platform_allocator, kCagedHeapReservationSize,
                         kCagedHeapReservationAlignment, hint);
    if (memory.IsReserved()) return memory;
  }

  FATAL("Fatal process out of memory: Failed to reserve memory for caged heap");
  UNREACHABLE();
}

}  // namespace

CagedHeap::CagedHeap(HeapBase& heap_base, PageAllocator& platform_allocator)
    : reserved_area_(ReserveCagedHeap(platform_allocator)) {
  using CagedAddress = CagedHeap::AllocatorType::Address;

#if defined(CPPGC_POINTER_COMPRESSION)
  // With pointer compression only single heap per thread is allowed.
  CHECK(!CageBaseGlobal::IsSet());
  CageBaseGlobalUpdater::UpdateCageBase(
      reinterpret_cast<uintptr_t>(reserved_area_.address()));
#endif  // defined(CPPGC_POINTER_COMPRESSION)

  const bool is_not_oom = platform_allocator.SetPermissions(
      reserved_area_.address(),
      RoundUp(sizeof(CagedHeapLocalData), platform_allocator.CommitPageSize()),
      PageAllocator::kReadWrite);
  // Failing to commit the reservation means that we are out of memory.
  CHECK(is_not_oom);

  new (reserved_area_.address()) CagedHeapLocalData(platform_allocator);

  const CagedAddress caged_heap_start =
      RoundUp(reinterpret_cast<CagedAddress>(reserved_area_.address()) +
                  sizeof(CagedHeapLocalData),
              kPageSize);
  const size_t local_data_size_with_padding =
      caged_heap_start -
      reinterpret_cast<CagedAddress>(reserved_area_.address());

  normal_page_bounded_allocator_ = std::make_unique<
      v8::base::BoundedPageAllocator>(
      &platform_allocator, caged_heap_start,
      kCagedHeapNormalPageReservationSize - local_data_size_with_padding,
      kPageSize,
      v8::base::PageInitializationMode::kAllocatedPagesMustBeZeroInitialized,
      v8::base::PageFreeingMode::kMakeInaccessible);

  large_page_bounded_allocator_ = std::make_unique<
      v8::base::BoundedPageAllocator>(
      &platform_allocator,
      reinterpret_cast<uintptr_t>(reserved_area_.address()) +
          kCagedHeapNormalPageReservationSize,
      kCagedHeapNormalPageReservationSize, kPageSize,
      v8::base::PageInitializationMode::kAllocatedPagesMustBeZeroInitialized,
      v8::base::PageFreeingMode::kMakeInaccessible);

  auto is_inserted = global_cages().emplace(
      reinterpret_cast<uintptr_t>(reserved_area_.address()), &heap_base);
  CHECK(is_inserted.second);
}

CagedHeap::~CagedHeap() {
#if defined(CPPGC_POINTER_COMPRESSION)
  CHECK_EQ(reinterpret_cast<uintptr_t>(reserved_area_.address()),
           CageBaseGlobalUpdater::GetCageBase());
  CageBaseGlobalUpdater::UpdateCageBase(0u);
#endif  // defined(CPPGC_POINTER_COMPRESSION)
}

void CagedHeap::NotifyLargePageCreated(LargePage* page) {
  DCHECK(page);
  auto result = large_pages_.insert(page);
  USE(result);
  DCHECK(result.second);
}

void CagedHeap::NotifyLargePageDestroyed(LargePage* page) {
  DCHECK(page);
  auto size = large_pages_.erase(page);
  USE(size);
  DCHECK_EQ(1u, size);
}

BasePage* CagedHeap::LookupPageFromInnerPointer(void* ptr) const {
  DCHECK(IsOnHeap(ptr));
  if (V8_LIKELY(IsWithinNormalPageReservation(ptr))) {
    return NormalPage::FromPayload(ptr);
  } else {
    return LookupLargePageFromInnerPointer(ptr);
  }
}

LargePage* CagedHeap::LookupLargePageFromInnerPointer(void* ptr) const {
  auto it = large_pages_.upper_bound(static_cast<LargePage*>(ptr));
  DCHECK_NE(large_pages_.begin(), it);
  auto* page = *std::next(it, -1);
  DCHECK(page);
  DCHECK(page->PayloadContains(static_cast<ConstAddress>(ptr)));
  return page;
}

// static
BasePageHandle* CagedHeapBase::LookupLargePageFromInnerPointer(
    uintptr_t heap_base, void* address) {
  DCHECK_EQ(0, heap_base & (kCagedHeapReservationAlignment - 1));

  auto it = global_cages().find(heap_base);
  DCHECK_NE(global_cages().end(), it);

  return it->second->caged_heap().LookupLargePageFromInnerPointer(address);
}

}  // namespace internal
}  // namespace cppgc
