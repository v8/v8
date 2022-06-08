// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_H_
#define INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_H_

#include <cstddef>

#include "cppgc/internal/api-constants.h"
#include "cppgc/internal/base-page-handle.h"
#include "v8config.h"  // NOLINT(build/include_directory)

#if defined(CPPGC_CAGED_HEAP)

namespace cppgc {
namespace internal {

class V8_EXPORT CagedHeapBase {
 public:
  V8_INLINE static bool IsWithinNormalPageReservation(uintptr_t heap_base,
                                                      void* address) {
    return (reinterpret_cast<uintptr_t>(address) - heap_base) <
           api_constants::kCagedHeapNormalPageReservationSize;
  }

  V8_INLINE static BasePageHandle* LookupPageFromInnerPointer(
      uintptr_t heap_base, void* ptr) {
    if (V8_LIKELY(IsWithinNormalPageReservation(heap_base, ptr)))
      return BasePageHandle::FromPayload(ptr);
    else
      return LookupLargePageFromInnerPointer(heap_base, ptr);
  }

 private:
  static BasePageHandle* LookupLargePageFromInnerPointer(uintptr_t heap_base,
                                                         void* address);
};

}  // namespace internal
}  // namespace cppgc

#endif  // defined(CPPGC_CAGED_HEAP)

#endif  // INCLUDE_CPPGC_INTERNAL_CAGED_HEAP_H_
