// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --correctness-fuzzer-suppressions --jitless

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();
builder.addFunction('f', kSig_v_v).addBody([]);
const wire_bytes = builder.toBuffer();

assertTrue(WebAssembly.validate(wire_bytes));
assertThrowsAsync(WebAssembly.compile(wire_bytes), WebAssembly.CompileError);
assertThrowsAsync(
    WebAssembly.instantiate(wire_bytes), WebAssembly.CompileError);
assertThrows(
    () => new WebAssembly.Module(wire_bytes), WebAssembly.CompileError);
