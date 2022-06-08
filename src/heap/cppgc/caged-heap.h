// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_CAGED_HEAP_H_
#define V8_HEAP_CPPGC_CAGED_HEAP_H_

#include <limits>
#include <memory>
#include <set>

#include "include/cppgc/platform.h"
#include "src/base/bounded-page-allocator.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/virtual-memory.h"

namespace cppgc {
namespace internal {

struct CagedHeapLocalData;
class HeapBase;
class BasePage;
class LargePage;

class CagedHeap final {
 public:
  using AllocatorType = v8::base::BoundedPageAllocator;

  template <typename RetType = uintptr_t>
  static RetType OffsetFromAddress(const void* address) {
    static_assert(
        std::numeric_limits<RetType>::max() >= (kCagedHeapReservationSize - 1),
        "The return type should be large enough");
    return reinterpret_cast<uintptr_t>(address) &
           (kCagedHeapReservationAlignment - 1);
  }

  static uintptr_t BaseFromAddress(const void* address) {
    return reinterpret_cast<uintptr_t>(address) &
           ~(kCagedHeapReservationAlignment - 1);
  }

  static bool IsWithinNormalPageReservation(const void* address) {
    return OffsetFromAddress(address) < kCagedHeapNormalPageReservationSize;
  }

  CagedHeap(HeapBase& heap, PageAllocator& platform_allocator);
  ~CagedHeap();

  CagedHeap(const CagedHeap&) = delete;
  CagedHeap& operator=(const CagedHeap&) = delete;

  AllocatorType& normal_page_allocator() {
    return *normal_page_bounded_allocator_;
  }
  const AllocatorType& normal_page_allocator() const {
    return *normal_page_bounded_allocator_;
  }

  AllocatorType& large_page_allocator() {
    return *large_page_bounded_allocator_;
  }
  const AllocatorType& large_page_allocator() const {
    return *large_page_bounded_allocator_;
  }

  void NotifyLargePageCreated(LargePage* page);
  void NotifyLargePageDestroyed(LargePage* page);

  BasePage* LookupPageFromInnerPointer(void* ptr) const;
  LargePage* LookupLargePageFromInnerPointer(void* ptr) const;

  CagedHeapLocalData& local_data() {
    return *static_cast<CagedHeapLocalData*>(reserved_area_.address());
  }
  const CagedHeapLocalData& local_data() const {
    return *static_cast<CagedHeapLocalData*>(reserved_area_.address());
  }

  bool IsOnHeap(const void* address) const {
    return reinterpret_cast<void*>(BaseFromAddress(address)) ==
           reserved_area_.address();
  }

  void* base() const { return reserved_area_.address(); }

 private:
  const VirtualMemory reserved_area_;
  std::unique_ptr<AllocatorType> normal_page_bounded_allocator_;
  std::unique_ptr<AllocatorType> large_page_bounded_allocator_;
  std::set<LargePage*> large_pages_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_CAGED_HEAP_H_
