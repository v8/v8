// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(scottmg): Temporary during 3-sided roll, see https://crbug.com/765754.
#if defined(CHROMIUM_ROLLING_MAGENTA_TO_ZIRCON)
#include <zircon/process.h>
#include <zircon/syscalls.h>
#define MX_OK ZX_OK
#define MX_PROP_NAME ZX_PROP_NAME
#define MX_VM_FLAG_PERM_EXECUTE ZX_VM_FLAG_PERM_EXECUTE
#define MX_VM_FLAG_PERM_READ ZX_VM_FLAG_PERM_READ
#define MX_VM_FLAG_PERM_WRITE ZX_VM_FLAG_PERM_WRITE
#define mx_handle_close zx_handle_close
#define mx_handle_t zx_handle_t
#define mx_object_set_property zx_object_set_property
#define mx_status_t zx_status_t
#define mx_vmar_map zx_vmar_map
#define mx_vmar_protect zx_vmar_protect
#define mx_vmar_root_self zx_vmar_root_self
#define mx_vmar_unmap zx_vmar_unmap
#define mx_vmo_create zx_vmo_create
#else
#include <magenta/process.h>
#include <magenta/syscalls.h>
#endif

#include "src/base/macros.h"
#include "src/base/platform/platform-posix-time.h"
#include "src/base/platform/platform-posix.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace base {

TimezoneCache* OS::CreateTimezoneCache() {
  return new PosixDefaultTimezoneCache();
}

// static
void* OS::Allocate(const size_t requested, size_t* allocated,
                   OS::MemoryPermission access, void* hint) {
  CHECK(false);  // TODO(scottmg): Port, https://crbug.com/731217.
  return nullptr;
}

// static
bool OS::Guard(void* address, size_t size) {
  return mx_vmar_protect(mx_vmar_root_self(),
                         reinterpret_cast<uintptr_t>(address), size,
                         0 /*no permissions*/) == MX_OK;
}

// static
void* OS::ReserveRegion(size_t size, void* hint) {
  mx_handle_t vmo;
  if (mx_vmo_create(size, 0, &vmo) != MX_OK) return nullptr;
  uintptr_t result;
  mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size,
                                   0 /*no permissions*/, &result);
  mx_handle_close(vmo);
  if (status != MX_OK) return nullptr;
  return reinterpret_cast<void*>(result);
}

// static
void* OS::ReserveAlignedRegion(size_t size, size_t alignment, void* hint,
                               size_t* allocated) {
  DCHECK((alignment % OS::AllocateAlignment()) == 0);
  hint = AlignedAddress(hint, alignment);
  size_t request_size =
      RoundUp(size + alignment, static_cast<intptr_t>(OS::AllocateAlignment()));

  mx_handle_t vmo;
  if (mx_vmo_create(request_size, 0, &vmo) != MX_OK) return;
  static const char kVirtualMemoryName[] = "v8-virtualmem";
  mx_object_set_property(vmo, MX_PROP_NAME, kVirtualMemoryName,
                         strlen(kVirtualMemoryName));
  uintptr_t reservation;
  mx_status_t status = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, request_size,
                                   0 /*no permissions*/, &reservation);
  // Either the vmo is now referenced by the vmar, or we failed and are bailing,
  // so close the vmo either way.
  mx_handle_close(vmo);
  if (status != MX_OK) {
    *allocated = 0;
    return nullptr;
  }

  uint8_t* base = reinterpret_cast<uint8_t*>(reservation);
  uint8_t* aligned_base = RoundUp(base, alignment);
  DCHECK_LE(base, aligned_base);

  // Unmap extra memory reserved before and after the desired block.
  if (aligned_base != base) {
    size_t prefix_size = static_cast<size_t>(aligned_base - base);
    mx_vmar_unmap(mx_vmar_root_self(), reinterpret_cast<uintptr_t>(base),
                  prefix_size);
    request_size -= prefix_size;
  }

  size_t aligned_size = RoundUp(size, OS::AllocateAlignment());
  DCHECK_LE(aligned_size, request_size);

  if (aligned_size != request_size) {
    size_t suffix_size = request_size - aligned_size;
    mx_vmar_unmap(mx_vmar_root_self(),
                  reinterpret_cast<uintptr_t>(aligned_base + aligned_size),
                  suffix_size);
    request_size -= suffix_size;
  }

  DCHECK(aligned_size == request_size);

  *allocated = aligned_size;
  return static_cast<void*>(aligned_base);
}

// static
bool OS::CommitRegion(void* address, size_t size, bool is_executable) {
  uint32_t prot = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
                  (is_executable ? MX_VM_FLAG_PERM_EXECUTE : 0);
  return mx_vmar_protect(mx_vmar_root_self(),
                         reinterpret_cast<uintptr_t>(address), size,
                         prot) == MX_OK;
}

// static
bool OS::UncommitRegion(void* address, size_t size) {
  return mx_vmar_protect(mx_vmar_root_self(),
                         reinterpret_cast<uintptr_t>(address), size,
                         0 /*no permissions*/) == MX_OK;
}

// static
bool OS::ReleasePartialRegion(void* address, size_t size, void* free_start,
                              size_t free_size) {
  return mx_vmar_unmap(mx_vmar_root_self(),
                       reinterpret_cast<uintptr_t>(free_start),
                       free_size) == MX_OK;
}

// static
bool OS::ReleaseRegion(void* address, size_t size) {
  return mx_vmar_unmap(mx_vmar_root_self(),
                       reinterpret_cast<uintptr_t>(address), size) == MX_OK;
}

// static
bool OS::HasLazyCommits() {
  // TODO(scottmg): Port, https://crbug.com/731217.
  return false;
}

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  CHECK(false);  // TODO(scottmg): Port, https://crbug.com/731217.
  return std::vector<SharedLibraryAddress>();
}

void OS::SignalCodeMovingGC() {
  CHECK(false);  // TODO(scottmg): Port, https://crbug.com/731217.
}

}  // namespace base
}  // namespace v8

#if defined(CHROMIUM_ROLLING_MAGENTA_TO_ZIRCON)
#undef MX_OK ZX_OK
#undef MX_PROP_NAME ZX_PROP_NAME
#undef MX_VM_FLAG_PERM_EXECUTE ZX_VM_FLAG_PERM_EXECUTE
#undef MX_VM_FLAG_PERM_READ ZX_VM_FLAG_PERM_READ
#undef MX_VM_FLAG_PERM_WRITE ZX_VM_FLAG_PERM_WRITE
#undef mx_handle_close zx_handle_close
#undef mx_handle_t zx_handle_t
#undef mx_object_set_property zx_object_set_property
#undef mx_status_t zx_status_t
#undef mx_vmar_map zx_vmar_map
#undef mx_vmar_protect zx_vmar_protect
#undef mx_vmar_root_self zx_vmar_root_self
#undef mx_vmar_unmap zx_vmar_unmap
#undef mx_vmo_create zx_vmo_create
#endif
