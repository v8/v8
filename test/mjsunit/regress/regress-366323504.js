// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --fuzzing

// Wasm isn't supported in jitless mode.
// When fuzzing, these runtime functions just return undefined.
if (typeof WebAssembly === "undefined") {
  assertEquals(undefined, %WasmStruct());
  assertEquals(undefined, %WasmArray());
} else {
  assertTrue(typeof %WasmStruct() === "object");
  assertTrue(typeof %WasmArray() === "object");
}
