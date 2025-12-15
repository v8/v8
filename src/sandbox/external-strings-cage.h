// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_STRINGS_CAGE_H_
#define V8_SANDBOX_EXTERNAL_STRINGS_CAGE_H_

#include <stddef.h>

#include "src/base/address-region.h"
#include "src/base/logging.h"
#include "src/utils/allocation.h"

namespace v8::internal {

#if defined(V8_ENABLE_SANDBOX) && defined(V8_ENABLE_MEMORY_CORRUPTION_API)

// Manages a virtual memory range for hosting external string contents, with an
// extra reservation at the end in order to fit any read past a string's buffer
// end using a corrupted length.
//
// Currently only used in memory_corruption_api-enabled builds, in order to
// distinguish external string OOB reads from other issues.
//
// Note: There's an additional memory overhead per each string, since we append
// a redzone and occupy whole pages for a string at the moment.
class ExternalStringsCage final {
 public:
  // The maximum total length of strings (and additional redzones) that the cage
  // can fit. Chosen arbitrarily; increase this if it turns out to be
  // insufficient for testing of important use cases.
  static constexpr size_t kMaxContentsSize = size_t{1} << 32;
  // The size of the guard region at the end of the cage. Chosen to cover an
  // arbitrary 32-bit offset for a UTF-16 string.
  static constexpr size_t kGuardRegionSize = size_t{1} << 33;

  template <typename T>
  class Allocator final {
   public:
    using value_type = T;

    explicit Allocator(ExternalStringsCage* cage) : cage_(cage) {}

    Allocator(const Allocator&) V8_NOEXCEPT = default;
    Allocator& operator=(const Allocator&) V8_NOEXCEPT = default;

    T* allocate(size_t n) {
      CHECK_LE(n, kMaxContentsSize / sizeof(T));
      return static_cast<T*>(cage_->Allocate(n * sizeof(T)));
    }

    void deallocate(T* p, size_t n) { cage_->Free(p, n * sizeof(T)); }

   private:
    ExternalStringsCage* const cage_;
  };

  ExternalStringsCage();
  ~ExternalStringsCage();

  ExternalStringsCage(const ExternalStringsCage&) = delete;
  ExternalStringsCage& operator=(const ExternalStringsCage&) = delete;

  bool Initialize();
  void* Allocate(size_t size);
  void Free(void* ptr, size_t size);

  template <typename T>
  Allocator<T> GetAllocator() {
    return Allocator<T>(this);
  }

  base::AddressRegion reservation_region() const {
    CHECK(vm_cage_.IsReserved());
    return vm_cage_.region();
  }

 private:
  size_t GetAllocSize(size_t string_size) const;

  const size_t page_size_;
  VirtualMemoryCage vm_cage_;
};

#endif  // defined(V8_ENABLE_SANDBOX) &&
        // defined(V8_ENABLE_MEMORY_CORRUPTION_API)

}  // namespace v8::internal

#endif  // V8_SANDBOX_EXTERNAL_STRINGS_CAGE_H_
