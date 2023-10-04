// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/trusted-range.h"

#include "src/base/lazy-instance.h"
#include "src/base/once.h"
#include "src/heap/heap-inl.h"
#include "src/utils/allocation.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX

bool TrustedRange::InitReservation(size_t requested) {
  DCHECK_LE(requested, kMaximalTrustedRangeSize);
  DCHECK_GE(requested, kMinimumTrustedRangeSize);

  auto page_allocator = GetPlatformPageAllocator();

  const size_t kPageSize = MemoryChunk::kPageSize;
  CHECK(IsAligned(kPageSize, page_allocator->AllocatePageSize()));

  // The allocatable region must not cross a 4GB boundary so that the default
  // pointer compression scheme of truncating pointers to 32-bits still works.
  const size_t base_alignment = base::bits::RoundUpToPowerOfTwo(requested);

  const Address requested_start_hint =
      RoundDown(reinterpret_cast<Address>(page_allocator->GetRandomMmapAddr()),
                base_alignment);

  VirtualMemoryCage::ReservationParams params;
  params.page_allocator = page_allocator;
  params.reservation_size = requested;
  params.page_size = kPageSize;
  params.base_alignment = base_alignment;
  params.requested_start_hint = requested_start_hint;
  params.jit = JitPermission::kNoJit;
  return VirtualMemoryCage::InitReservation(params);
}

namespace {

TrustedRange* process_wide_trusted_range_ = nullptr;

V8_DECLARE_ONCE(init_trusted_range_once);
void InitProcessWideTrustedRange(size_t requested_size) {
  TrustedRange* trusted_range = new TrustedRange();
  if (!trusted_range->InitReservation(requested_size)) {
    V8::FatalProcessOutOfMemory(
        nullptr, "Failed to reserve virtual memory for TrustedRange");
  }
  process_wide_trusted_range_ = trusted_range;
}
}  // namespace

// static
TrustedRange* TrustedRange::EnsureProcessWideTrustedRange(
    size_t requested_size) {
  base::CallOnce(&init_trusted_range_once, InitProcessWideTrustedRange,
                 requested_size);
  return process_wide_trusted_range_;
}

// static
TrustedRange* TrustedRange::GetProcessWideTrustedRange() {
  return process_wide_trusted_range_;
}

#endif  // V8_ENABLE_SANDBOX

}  // namespace internal
}  // namespace v8
