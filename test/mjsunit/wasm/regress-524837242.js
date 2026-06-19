// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Verifies that rejecting a promise inside a large Wasm module (>512MB wire
// byte position) does not crash or fail DCHECKs.

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function addHugeCustomSection(wasmBuffer, targetSize) {
  let name = "padding";
  let nameBytes = [];
  for (let i = 0; i < name.length; i++) nameBytes.push(name.charCodeAt(i));

  let payloadSize = targetSize - 6;
  let customDataSize = payloadSize - 1 - name.length;

  let customSectionHeader = [];
  customSectionHeader.push(0); // Section code

  let temp = payloadSize;
  for (let i = 0; i < 5; i++) {
    let byte = temp & 0x7f;
    temp >>>= 7;
    if (i < 4) byte |= 0x80;
    customSectionHeader.push(byte);
  }

  customSectionHeader.push(name.length);
  for (let b of nameBytes) customSectionHeader.push(b);

  let totalLength =
      8 + customSectionHeader.length + customDataSize + (wasmBuffer.length - 8);
  let finalBuffer = new Uint8Array(totalLength);

  finalBuffer.set(wasmBuffer.subarray(0, 8), 0);
  finalBuffer.set(customSectionHeader, 8);
  // customData is left as 0 by default.
  finalBuffer.set(
      wasmBuffer.subarray(8),
      8 + customSectionHeader.length + customDataSize);

  return finalBuffer;
}

const builder = new WasmModuleBuilder();
let my_sig = builder.addType(makeSig([], []));
let imp_idx = builder.addImport("ext", "callback", my_sig);

builder.addFunction("main", my_sig)
    .addBody([kExprCallFunction, imp_idx])
    .exportFunc();

let wasmBuffer = builder.toBuffer();
let paddedBuffer = addHugeCustomSection(wasmBuffer, 540000000);

let module = new WebAssembly.Module(paddedBuffer);
let instance = new WebAssembly.Instance(module, {
  ext: {
    callback: () => {
      Promise.reject(new Error("test")).catch(() => {});
    }
  }
});

instance.exports.main();
