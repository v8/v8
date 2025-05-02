// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_HARDWARE_SUPPORT_H_
#define V8_SANDBOX_HARDWARE_SUPPORT_H_

#include "include/v8-platform.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE SandboxHardwareSupport {
 public:
  // Allocates a pkey that will be used to optionally block sandbox access. This
  // function should be called once before any threads are created so that new
  // threads inherit access to the new pkey.
  // This function returns true on success, false otherwise. It can generally
  // fail for one of two reasons:
  //   1. the current system doesn't support memory protection keys, or
  //   2. there are no allocatable memory protection keys left.
  static bool InitializeBeforeThreadCreation();

  // Try to set up hardware permissions to the sandbox address space. If
  // successful, future calls to MaybeBlockAccess will block the current thread
  // from accessing the memory.
  // This will only fail if InitializeBeforeThreadCreation was unable to
  // allocate a memory protection key.
  static bool TryEnable(Address addr, size_t size);

  // Returns true if hardware sandboxing is enabled.
  static bool IsEnabled();

  class V8_NODISCARD V8_ALLOW_UNUSED BlockAccessScope {
   public:
#ifdef V8_ENABLE_SANDBOX_HARDWARE_SUPPORT
    explicit BlockAccessScope(int pkey);
    ~BlockAccessScope();

   private:
    int pkey_;
#else
    BlockAccessScope() = default;
#endif
  };

  // If V8_ENABLE_SANDBOX_HARDWARE_SUPPORT is enabled, this function will
  // prevent any access (read or write) to all sandbox memory on the current
  // thread, as long as the returned Scope object is valid. The only exception
  // are read-only pages, which will still be readable.
  static BlockAccessScope MaybeBlockAccess();

  // Removes the pkey from read only pages, so that MaybeBlockAccess will still
  // allow read access.
  static void NotifyReadOnlyPageCreated(
      Address addr, size_t size, PageAllocator::Permission current_permissions);

  // This function should only be called by
  // `ThreadIsolatedAllocator::SetDefaultPermissionsForSignalHandler`.
  static void SetDefaultPermissionsForSignalHandler();

 private:
#ifdef V8_ENABLE_SANDBOX_HARDWARE_SUPPORT
  static int pkey_;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_HARDWARE_SUPPORT_H_
