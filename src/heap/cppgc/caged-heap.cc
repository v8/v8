// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "v8config.h"  // NOLINT(build/include_directory)

#if !defined(CPPGC_CAGED_HEAP)
#error "Must be compiled with caged heap enabled"
#endif

#include "include/cppgc/internal/caged-heap-local-data.h"
#include "include/cppgc/member.h"
#include "include/cppgc/platform.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/caged-heap.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/member.h"

namespace cppgc {
namespace internal {

static_assert(api_constants::kCagedHeapReservationSize ==
              kCagedHeapReservationSize);
static_assert(api_constants::kCagedHeapReservationAlignment ==
              kCagedHeapReservationAlignment);

namespace {

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

  new (reserved_area_.address())
      CagedHeapLocalData(heap_base, platform_allocator);

  const CagedAddress caged_heap_start =
      RoundUp(reinterpret_cast<CagedAddress>(reserved_area_.address()) +
                  sizeof(CagedHeapLocalData),
              kPageSize);
  const size_t local_data_size_with_padding =
      caged_heap_start -
      reinterpret_cast<CagedAddress>(reserved_area_.address());

  bounded_allocator_ = std::make_unique<v8::base::BoundedPageAllocator>(
      &platform_allocator, caged_heap_start,
      reserved_area_.size() - local_data_size_with_padding, kPageSize,
      v8::base::PageInitializationMode::kAllocatedPagesMustBeZeroInitialized,
      v8::base::PageFreeingMode::kMakeInaccessible);
}

CagedHeap::~CagedHeap() {
#if defined(CPPGC_POINTER_COMPRESSION)
  CHECK_EQ(reinterpret_cast<uintptr_t>(reserved_area_.address()),
           CageBaseGlobalUpdater::GetCageBase());
  CageBaseGlobalUpdater::UpdateCageBase(0u);
#endif  // defined(CPPGC_POINTER_COMPRESSION)
}

#if defined(CPPGC_YOUNG_GENERATION)
void CagedHeap::EnableGenerationalGC() {
  local_data().is_young_generation_enabled = true;
}
#endif  // defined(CPPGC_YOUNG_GENERATION)

}  // namespace internal
}  // namespace cppgc
