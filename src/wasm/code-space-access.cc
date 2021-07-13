// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/code-space-access.h"

#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-engine.h"

namespace v8 {
namespace internal {
namespace wasm {

thread_local int CodeSpaceWriteScope::code_space_write_nesting_level_ = 0;

// TODO(jkummerow): Background threads could permanently stay in
// writable mode; only the main thread has to switch back and forth.
#if defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)
CodeSpaceWriteScope::CodeSpaceWriteScope(NativeModule*) {
#else
CodeSpaceWriteScope::CodeSpaceWriteScope(NativeModule* native_module)
    : native_module_(native_module) {
#endif
  if (code_space_write_nesting_level_ == 0) SetWritable();
  code_space_write_nesting_level_++;
}

CodeSpaceWriteScope::~CodeSpaceWriteScope() {
  code_space_write_nesting_level_--;
  if (code_space_write_nesting_level_ == 0) SetExecutable();
}

#if defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)

void CodeSpaceWriteScope::SetWritable() const {
  SwitchMemoryPermissionsToWritable();
}

void CodeSpaceWriteScope::SetExecutable() const {
  SwitchMemoryPermissionsToExecutable();
}

#else  // Not Mac-on-arm64.

void CodeSpaceWriteScope::SetWritable() const {
  DCHECK_NOT_NULL(native_module_);
  auto* code_manager = GetWasmCodeManager();
  if (code_manager->HasMemoryProtectionKeySupport()) {
    DCHECK(FLAG_wasm_memory_protection_keys);
    code_manager->SetThreadWritable(true);
  } else if (FLAG_wasm_write_protect_code_memory) {
    CHECK(native_module_->SetWritable(true));
  }
}

void CodeSpaceWriteScope::SetExecutable() const {
  auto* code_manager = GetWasmCodeManager();
  if (code_manager->HasMemoryProtectionKeySupport()) {
    DCHECK(FLAG_wasm_memory_protection_keys);
    code_manager->SetThreadWritable(false);
  } else if (FLAG_wasm_write_protect_code_memory) {
    CHECK(native_module_->SetWritable(false));
  }
}

#endif  // defined(V8_OS_MACOSX) && defined(V8_HOST_ARCH_ARM64)

}  // namespace wasm
}  // namespace internal
}  // namespace v8
