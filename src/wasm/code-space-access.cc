// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/code-space-access.h"

#include "src/wasm/wasm-code-manager.h"

namespace v8 {
namespace internal {
namespace wasm {

#if defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)

thread_local int CodeSpaceWriteScope::code_space_write_nesting_level_ = 0;

// The {NativeModule} argument is unused; it is just here for a common API with
// the non-M1 implementation.
// TODO(jkummerow): Background threads could permanently stay in
// writable mode; only the main thread has to switch back and forth.
CodeSpaceWriteScope::CodeSpaceWriteScope(NativeModule*) {
  if (code_space_write_nesting_level_ == 0) {
    SwitchMemoryPermissionsToWritable();
  }
  code_space_write_nesting_level_++;
}

CodeSpaceWriteScope::~CodeSpaceWriteScope() {
  code_space_write_nesting_level_--;
  if (code_space_write_nesting_level_ == 0) {
    SwitchMemoryPermissionsToExecutable();
  }
}

#else  // Not on MacOS on ARM64 (M1 hardware): Use Intel PKU and/or mprotect.

CodeSpaceWriteScope::CodeSpaceWriteScope(NativeModule* native_module)
    : native_module_(native_module) {
  DCHECK_NOT_NULL(native_module_);
  if (FLAG_wasm_memory_protection_keys) {
    bool success = native_module_->SetThreadWritable(true);
    if (!success && FLAG_wasm_write_protect_code_memory) {
      // Fallback to mprotect-based write protection (much slower).
      success = native_module_->SetWritable(true);
      CHECK(success);
    }
  } else if (FLAG_wasm_write_protect_code_memory) {
    bool success = native_module_->SetWritable(true);
    CHECK(success);
  }
}

CodeSpaceWriteScope::~CodeSpaceWriteScope() {
  if (FLAG_wasm_memory_protection_keys) {
    bool success = native_module_->SetThreadWritable(false);
    if (!success && FLAG_wasm_write_protect_code_memory) {
      // Fallback to mprotect-based write protection (much slower).
      success = native_module_->SetWritable(false);
      CHECK(success);
    }
  } else if (FLAG_wasm_write_protect_code_memory) {
    bool success = native_module_->SetWritable(false);
    CHECK(success);
  }
}

#endif  // defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)

}  // namespace wasm
}  // namespace internal
}  // namespace v8
