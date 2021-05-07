// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_MEMORY_PROTECTION_KEY_H_
#define V8_WASM_MEMORY_PROTECTION_KEY_H_

namespace v8 {
namespace internal {
namespace wasm {

// TODO(dlehmann): Move this to base/platform/platform.h {OS} (lower-level API)
// and {base::PageAllocator} (higher-level, exported API) once the API is more
// stable and we have converged on a better design (e.g., typed class wrapper
// around int memory protection key).

// Sentinel value if there is no PKU support or allocation of a key failed.
// This is also the return value on an error of pkey_alloc() and has the
// benefit that calling pkey_mprotect() with -1 behaves the same as regular
// mprotect().
constexpr int kNoMemoryProtectionKey = -1;

// Permissions for memory protection keys on top of the permissions by mprotect.
// NOTE: Since there is no executable bit, the executable permission cannot be
// withdrawn by memory protection keys.
enum MemoryProtectionKeyPermission {
  kNoRestrictions = 0,
  kDisableAccess = 1,
  kDisableWrite = 2,
};

// Allocates a memory protection key on platforms with PKU support, returns
// {kNoMemoryProtectionKey} on platforms without support or when allocation
// failed at runtime.
int AllocateMemoryProtectionKey();

// Frees the given memory protection key, to make it available again for the
// next call to {AllocateMemoryProtectionKey()}. Note that this does NOT
// invalidate access rights to pages that are still tied to that key. That is,
// if the key is reused and pages with that key are still accessable, this might
// be a security issue. See
// https://www.gnu.org/software/libc/manual/html_mono/libc.html#Memory-Protection-Keys
void FreeMemoryProtectionKey(int key);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_MEMORY_PROTECTION_KEY_H_
