// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_LIMITS_H_
#define V8_WASM_WASM_LIMITS_H_

namespace v8 {
namespace internal {
namespace wasm {

const size_t kV8MaxWasmSignatures = 10000000;
const size_t kV8MaxWasmFunctions = 10000000;
const size_t kV8MaxWasmMemoryPages = 16384;  // = 1 GiB
const size_t kV8MaxWasmStringSize = 256;
const size_t kV8MaxWasmModuleSize = 1024 * 1024 * 1024;  // = 1 GiB
const size_t kV8MaxWasmFunctionSize = 128 * 1024;
const size_t kV8MaxWasmFunctionLocals = 50000;
const size_t kV8MaxWasmTableSize = 16 * 1024 * 1024;

const size_t kSpecMaxWasmMemoryPages = 65536;

const uint64_t kWasmMaxHeapOffset =
    static_cast<uint64_t>(
        std::numeric_limits<uint32_t>::max())  // maximum base value
    + std::numeric_limits<uint32_t>::max();    // maximum index value

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_LIMITS_H_
