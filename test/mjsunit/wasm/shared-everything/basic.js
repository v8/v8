// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-shared

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function SharedGlobal() {
  print(arguments.callee.name);
  let builder = new WasmModuleBuilder();
  builder.addGlobal(kWasmI32, true, true, [kExprI32Const, 0]);
  builder.instantiate();
})();
