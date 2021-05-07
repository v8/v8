// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/memory-protection-key.h"

#include "src/base/build_config.h"
#include "src/base/macros.h"

// Runtime-detection of PKU support with {dlsym()}.
//
// For now, we support memory protection keys/PKEYs/PKU only for Linux on x64
// based on glibc functions {pkey_alloc()}, {pkey_free()}, etc.
// Those functions are only available since glibc version 2.27:
// https://man7.org/linux/man-pages/man2/pkey_alloc.2.html
// However, if we check the glibc verison with V8_GLIBC_PREPREQ here at compile
// time, this causes two problems due to dynamic linking of glibc:
// 1) If the compiling system _has_ a new enough glibc, the binary will include
// calls to {pkey_alloc()} etc., and then the runtime system must supply a
// new enough glibc version as well. That is, this potentially breaks runtime
// compatability on older systems (e.g., Ubuntu 16.04 with glibc 2.23).
// 2) If the compiling system _does not_ have a new enough glibc, PKU support
// will not be compiled in, even though the runtime system potentially _does_
// have support for it due to a new enough Linux kernel and glibc version.
// That is, this results in non-optimal security (PKU available, but not used).
// Hence, we do _not_ check the glibc version during compilation, and instead
// only at runtime try to load {pkey_alloc()} etc. with {dlsym()}.
// TODO(dlehmann): Move this import and freestanding functions below to
// base/platform/platform.h {OS} (lower-level functions) and
// {base::PageAllocator} (exported API).
#if defined(V8_OS_LINUX) && defined(V8_HOST_ARCH_X64)
#include <dlfcn.h>
#endif

namespace v8 {
namespace internal {
namespace wasm {

int AllocateMemoryProtectionKey() {
// See comment on the import on feature testing for PKEY support.
#if defined(V8_OS_LINUX) && defined(V8_HOST_ARCH_X64)
  // Try to to find {pkey_alloc()} support in glibc.
  typedef int (*pkey_alloc_t)(unsigned int, unsigned int);
  auto pkey_alloc = bit_cast<pkey_alloc_t>(dlsym(RTLD_DEFAULT, "pkey_alloc"));
  if (pkey_alloc != nullptr) {
    // If there is support in glibc, try to allocate a new key.
    // This might still return -1, e.g., because the kernel does not support
    // PKU or because there is no more key available.
    // Different reasons for why {pkey_alloc()} failed could be checked with
    // errno, e.g., EINVAL vs ENOSPC vs ENOSYS. See manpages and glibc manual
    // (the latter is the authorative source):
    // https://www.gnu.org/software/libc/manual/html_mono/libc.html#Memory-Protection-Keys
    return pkey_alloc(/* flags, unused */ 0, kDisableAccess);
  }
#endif
  return kNoMemoryProtectionKey;
}

void FreeMemoryProtectionKey(int key) {
#if defined(V8_OS_LINUX) && defined(V8_HOST_ARCH_X64)
  // Only free the key if one was allocated.
  if (key != kNoMemoryProtectionKey) {
    typedef int (*pkey_free_t)(int);
    auto pkey_free = bit_cast<pkey_free_t>(dlsym(RTLD_DEFAULT, "pkey_free"));
    // If a key was allocated with {pkey_alloc()}, {pkey_free()} must also be
    // available.
    CHECK_NOT_NULL(pkey_free);
    CHECK_EQ(/* success */ 0, pkey_free(key));
  }
#else
  // On platforms without support even compiled in, no key should have been
  // allocated.
  CHECK_EQ(kNoMemoryProtectionKey, key);
#endif
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
