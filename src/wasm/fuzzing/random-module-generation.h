// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_FUZZING_RANDOM_MODULE_GENERATION_H_
#define V8_WASM_FUZZING_RANDOM_MODULE_GENERATION_H_

#include "src/base/logging.h"
#include "src/base/vector.h"

namespace v8::internal {
class Zone;
}

namespace v8::internal::wasm::fuzzing {

// Generate a valid Wasm module based on the given input bytes.
// Returns an empty buffer on failure, valid module wire bytes otherwise.
// The bytes will be allocated in the zone.
#ifdef OFFICIAL_BUILD
inline base::Vector<uint8_t> GenerateRandomWasmModule(
    Zone*, base::Vector<const uint8_t> data) {
  UNIMPLEMENTED();
}

inline base::Vector<uint8_t> GenerateWasmModuleForInitExpressions(
    Zone*, base::Vector<const uint8_t> data, size_t* count) {
  UNIMPLEMENTED();
}

#else
// Defined in random-module-generation.cc.
V8_EXPORT_PRIVATE base::Vector<uint8_t> GenerateRandomWasmModule(
    Zone*, base::Vector<const uint8_t> data);

V8_EXPORT_PRIVATE base::Vector<uint8_t> GenerateWasmModuleForInitExpressions(
    Zone*, base::Vector<const uint8_t> data, size_t* count);
#endif

}  // namespace v8::internal::wasm::fuzzing

#endif  // V8_WASM_FUZZING_RANDOM_MODULE_GENERATION_H_
