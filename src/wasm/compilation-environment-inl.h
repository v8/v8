// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_COMPILATION_ENVIRONMENT_INL_H_
#define V8_WASM_COMPILATION_ENVIRONMENT_INL_H_

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#include "src/wasm/compilation-environment.h"
// Include the non-inl header before the rest of the headers.

#include "src/wasm/wasm-code-manager.h"

namespace v8::internal::wasm {

inline CompilationEnv CompilationEnv::ForModule(
    const NativeModule* native_module) {
  return CompilationEnv(
      native_module->module(), native_module->enabled_features(),
      native_module->fast_api_targets(), native_module->fast_api_signatures(),
      native_module->coverage_data());
}

inline CompilationEnv CompilationEnv::NoModuleAllFeaturesForTesting() {
  return CompilationEnv(nullptr, WasmEnabledFeatures::All(), nullptr, nullptr,
                        nullptr);
}

}  // namespace v8::internal::wasm

#endif  // V8_WASM_COMPILATION_ENVIRONMENT_INL_H_
