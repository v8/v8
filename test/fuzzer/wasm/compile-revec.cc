// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/vector.h"
#include "src/wasm/fuzzing/random-module-generation.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/zone/zone.h"
#include "test/fuzzer/fuzzer-support.h"
#include "test/fuzzer/wasm/fuzzer-common.h"

namespace v8::internal::wasm::fuzzing {

// Fuzzer that generates SIMD expressions which may be revectorized.
class WasmCompileRevecFuzzer : public WasmExecutionFuzzer {
  bool GenerateModule(Isolate* isolate, Zone* zone,
                      base::Vector<const uint8_t> data,
                      ZoneBuffer* buffer) override {
    base::Vector<const uint8_t> wire_bytes =
        GenerateWasmModuleForRevec(zone, data);
    if (wire_bytes.empty()) return false;
    buffer->write(wire_bytes.data(), wire_bytes.size());
    return true;
  }
};

V8_SYMBOL_USED extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  v8_fuzzer::FuzzerSupport::InitializeFuzzerSupport(argc, argv);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  constexpr bool kRequireValid = true;
  return WasmCompileRevecFuzzer().FuzzWasmModule({data, size}, kRequireValid);
}

}  // namespace v8::internal::wasm::fuzzing
