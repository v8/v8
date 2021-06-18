// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_CODE_SPACE_ACCESS_H_
#define V8_WASM_CODE_SPACE_ACCESS_H_

#include "src/base/build_config.h"
#include "src/base/macros.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

namespace wasm {

class NativeModule;

#if defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)
// Arm64 on MacOS (M1 hardware) uses CodeSpaceWriteScope to switch permissions.
// TODO(wasm): Merge NativeModuleModificationScope and CodeSpaceWriteScope.
class V8_NODISCARD NativeModuleModificationScope final {
 public:
  explicit NativeModuleModificationScope(NativeModule*) {}
};
#else
// Within the scope, the native_module is writable and not executable.
// At the scope's destruction, the native_module is executable and not writable.
// The states inside the scope and at the scope termination are irrespective of
// native_module's state when entering the scope.
// We currently mark the entire module's memory W^X:
//  - for AOT, that's as efficient as it can be.
//  - for Lazy, we don't have a heuristic for functions that may need patching,
//    and even if we did, the resulting set of pages may be fragmented.
//    Currently, we try and keep the number of syscalls low.
// -  similar argument for debug time.
class V8_NODISCARD NativeModuleModificationScope final {
 public:
  explicit NativeModuleModificationScope(NativeModule* native_module);
  ~NativeModuleModificationScope();

  // Disable copy constructor and copy-assignment operator, since this manages
  // a resource and implicit copying of the scope can yield surprising errors.
  NativeModuleModificationScope(const NativeModuleModificationScope&) = delete;
  NativeModuleModificationScope& operator=(
      const NativeModuleModificationScope&) = delete;

 private:
  NativeModule* native_module_;
};
#endif  // defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)

}  // namespace wasm

#if defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)

// Ignoring this warning is considered better than relying on
// __builtin_available.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"
inline void SwitchMemoryPermissionsToWritable() {
  pthread_jit_write_protect_np(0);
}
inline void SwitchMemoryPermissionsToExecutable() {
  pthread_jit_write_protect_np(1);
}
#pragma clang diagnostic pop

namespace wasm {

class V8_NODISCARD CodeSpaceWriteScope {
 public:
  // TODO(jkummerow): Background threads could permanently stay in
  // writable mode; only the main thread has to switch back and forth.
  CodeSpaceWriteScope() {
    if (code_space_write_nesting_level_ == 0) {
      SwitchMemoryPermissionsToWritable();
    }
    code_space_write_nesting_level_++;
  }
  ~CodeSpaceWriteScope() {
    code_space_write_nesting_level_--;
    if (code_space_write_nesting_level_ == 0) {
      SwitchMemoryPermissionsToExecutable();
    }
  }

 private:
  static thread_local int code_space_write_nesting_level_;
};

#define CODE_SPACE_WRITE_SCOPE CodeSpaceWriteScope _write_access_;

}  // namespace wasm

#else  // Not Mac-on-arm64.

// Nothing to do, we map code memory with rwx permissions.
inline void SwitchMemoryPermissionsToWritable() {}
inline void SwitchMemoryPermissionsToExecutable() {}

#define CODE_SPACE_WRITE_SCOPE

#endif  // V8_OS_MACOSX && V8_HOST_ARCH_ARM64

}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_CODE_SPACE_ACCESS_H_
