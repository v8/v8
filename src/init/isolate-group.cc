// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/init/isolate-group.h"

#include "src/base/bounded-page-allocator.h"
#include "src/base/platform/memory.h"
#include "src/common/ptr-compr-inl.h"
#include "src/execution/isolate.h"
#include "src/heap/code-range.h"
#include "src/heap/trusted-range.h"
#include "src/sandbox/sandbox.h"
#include "src/utils/memcopy.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

#ifdef V8_COMPRESS_POINTERS
struct PtrComprCageReservationParams
    : public VirtualMemoryCage::ReservationParams {
  PtrComprCageReservationParams() {
    page_allocator = GetPlatformPageAllocator();

    reservation_size = kPtrComprCageReservationSize;
    base_alignment = kPtrComprCageBaseAlignment;

    // Simplify BoundedPageAllocator's life by configuring it to use same page
    // size as the Heap will use (MemoryChunk::kPageSize).
    page_size =
        RoundUp(size_t{1} << kPageSizeBits, page_allocator->AllocatePageSize());
    requested_start_hint = RoundDown(
        reinterpret_cast<Address>(page_allocator->GetRandomMmapAddr()),
        base_alignment);

#if V8_OS_FUCHSIA && !V8_EXTERNAL_CODE_SPACE
    // If external code space is not enabled then executable pages (e.g. copied
    // builtins, and JIT pages) will fall under the pointer compression range.
    // Under Fuchsia that means the entire range must be allocated as JITtable.
    permissions = PageAllocator::Permission::kNoAccessWillJitLater;
#else
    permissions = PageAllocator::Permission::kNoAccess;
#endif
    page_initialization_mode =
        base::PageInitializationMode::kAllocatedPagesCanBeUninitialized;
    page_freeing_mode = base::PageFreeingMode::kMakeInaccessible;
  }
};
#endif  // V8_COMPRESS_POINTERS

#ifndef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
// static
IsolateGroup* IsolateGroup::GetProcessWideIsolateGroup() {
  static ::v8::base::LeakyObject<IsolateGroup> global_isolate_group_;
  return global_isolate_group_.get();
}
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES

#ifdef V8_ENABLE_SANDBOX
void IsolateGroup::Initialize(Sandbox* sandbox) {
  CHECK(sandbox->is_initialized());
  PtrComprCageReservationParams params;
  Address base = sandbox->address_space()->AllocatePages(
    sandbox->base(), params.reservation_size, params.base_alignment,
    PagePermissions::kNoAccess);
  CHECK_EQ(sandbox->base(), base);
  base::AddressRegion existing_reservation(base, params.reservation_size);
  params.page_allocator = sandbox->page_allocator();
  if (!reservation_.InitReservation(params, existing_reservation)) {
    V8::FatalProcessOutOfMemory(
      nullptr,
      "Failed to reserve virtual memory for process-wide V8 "
      "pointer compression cage");
  }
  page_allocator_ = reservation_.page_allocator();
  pointer_compression_cage_ = &reservation_;
  trusted_pointer_compression_cage_ =
      TrustedRange::EnsureProcessWideTrustedRange(kMaximalTrustedRangeSize);
}
#elif defined(V8_COMPRESS_POINTERS)
void IsolateGroup::Initialize() {
  PtrComprCageReservationParams params;
  if (!reservation_.InitReservation(params)) {
    V8::FatalProcessOutOfMemory(
        nullptr,
        "Failed to reserve virtual memory for process-wide V8 "
        "pointer compression cage");
  }
  page_allocator_ = reservation_.page_allocator();
  pointer_compression_cage_ = &reservation_;
  trusted_pointer_compression_cage_ = &reservation_;
}
#else   // !V8_COMPRESS_POINTERS
void IsolateGroup::Initialize() {
  pointer_compression_cage_ = &reservation_;
  trusted_pointer_compression_cage_ = &reservation_;
  page_allocator_ = GetPlatformPageAllocator();
}
#endif  // V8_ENABLE_SANDBOX

// static
void IsolateGroup::InitializeOncePerProcess() {
#ifndef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  IsolateGroup* group = GetProcessWideIsolateGroup();
  DCHECK(!group->reservation_.IsReserved());

#ifdef V8_ENABLE_SANDBOX
  group->Initialize(GetProcessWideSandbox());
#else
  group->Initialize();
#endif
  CHECK_NOT_NULL(group->page_allocator_);

#ifdef V8_COMPRESS_POINTERS
  V8HeapCompressionScheme::InitBase(group->GetPtrComprCageBase());
#endif  // V8_COMPRESS_POINTERS
#ifdef V8_EXTERNAL_CODE_SPACE
  // Speculatively set the code cage base to the same value in case jitless
  // mode will be used. Once the process-wide CodeRange instance is created
  // the code cage base will be set accordingly.
  ExternalCodeCompressionScheme::InitBase(V8HeapCompressionScheme::base());
#endif  // V8_EXTERNAL_CODE_SPACE
#endif  // !V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
}

// static
IsolateGroup* IsolateGroup::New() {
  IsolateGroup* group = new IsolateGroup;

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  static_assert(!V8_ENABLE_SANDBOX_BOOL);
  group->Initialize();
#else
  FATAL(
      "Creation of new isolate groups requires enabling "
      "multiple pointer compression cages at build-time");
#endif

  CHECK_NOT_NULL(group->page_allocator_);
  return group;
}

// static
IsolateGroup* IsolateGroup::AcquireGlobal() {
#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  return nullptr;
#else
  return GetProcessWideIsolateGroup()->Acquire();
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
}

// static
void IsolateGroup::ReleaseGlobal() {
#ifndef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  if (CodeRange* code_range = CodeRange::GetProcessWideCodeRange()) {
    code_range->Free();
  }

  IsolateGroup *group = GetProcessWideIsolateGroup();
  CHECK_EQ(group->reference_count_.load(), 1);
  group->page_allocator_ = nullptr;
  group->trusted_pointer_compression_cage_ = nullptr;
  group->pointer_compression_cage_ = nullptr;
  DCHECK_EQ(COMPRESS_POINTERS_BOOL, group->reservation_.IsReserved());
  if (COMPRESS_POINTERS_BOOL) group->reservation_.Free();
#endif  // V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
}

}  // namespace internal
}  // namespace v8
