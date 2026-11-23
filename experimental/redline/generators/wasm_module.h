// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXPERIMENTAL_REDLINE_GENERATORS_WASM_MODULE_H_
#define V8_EXPERIMENTAL_REDLINE_GENERATORS_WASM_MODULE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "experimental/redline/core/random.h"

namespace redline {

// Value types of the generated modules with their binary encodings.
enum class WasmType : uint8_t {
  kI32 = 0x7F,
  kI64 = 0x7E,
  kF32 = 0x7D,
  kF64 = 0x7C,
};

struct WasmSignature {
  std::vector<WasmType> params;
  std::vector<WasmType> results;  // Empty or one value.
};

// Random WebAssembly module that is valid by construction, plus metadata that
// the JavaScript caller needs.
struct WasmModule {
  std::vector<uint8_t> bytes;
  // Imported functions `m.f0`, `m.f1`, ... with their signatures.
  std::vector<WasmSignature> imports;
  // Exported functions `e0`, `e1`, ... with their signatures.
  std::vector<WasmSignature> functions;
  // Exported mutable globals `g0`, `g1`, ... with their types.
  std::vector<WasmType> globals;
  bool memory = false;  // Exported as `mem`.
  bool table = false;   // Exported as `tab`.
  // Number of `funcref` slots in `tab`.
  uint32_t table_size = 0;
  // Number of callees in `tab` that share the `call_indirect` signature.
  uint32_t table_callees = 0;
  // Index in `functions` of an export that executes `call_indirect` with
  // parameter 0 as table slot, or -1 if none exists.
  int indirect_caller = -1;
};

// Builds a spec-valid WebAssembly MVP module with types, imports, globals,
// memory, and a function table. Function bodies use masked loads and stores,
// `call_indirect`, conversions, `select`, `br_if`, `if`/`else`, and counted
// `loop` blocks.
WasmModule RandomWasmModule(Random& r);

}  // namespace redline

#endif  // V8_EXPERIMENTAL_REDLINE_GENERATORS_WASM_MODULE_H_
