// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax
// Also enable all features that are used in the randomly generated module. This
// should stay in sync with `EnableExperimentalWasmFeatures` in
// `wasm-fuzzer-common.cc`.
// Flags: --wasm-staging

(function TestGenerateRandomModule() {
  print(arguments.callee.name);
  // TODO(v8:14642): Always generate a valid module, then reenable this.
  // assertInstanceof(%WasmGenerateRandomModule(), WebAssembly.Module);
  assertInstanceof(%WasmGenerateRandomModule(4), WebAssembly.Module);
  assertInstanceof(%WasmGenerateRandomModule(4.2), WebAssembly.Module);
  assertInstanceof(
      %WasmGenerateRandomModule(new Uint8Array([11, 27])), WebAssembly.Module);
  assertInstanceof(
      %WasmGenerateRandomModule(new Uint8Array([11, 27]).buffer),
      WebAssembly.Module);
})();
