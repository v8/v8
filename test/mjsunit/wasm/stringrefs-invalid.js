// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --experimental-wasm-eh --experimental-wasm-gc

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function assertInvalid(fn, message) {
  let builder = new WasmModuleBuilder();
  fn(builder);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
               `WebAssembly.Module(): ${message}`);
}

let enableMessage = 'enable with --experimental-wasm-stringref'
assertInvalid(builder => builder.addLiteralStringRef("foo"),
              `unexpected section <StringRef> (${enableMessage}) @+10`);

for (let [name, code] of [['string', kWasmStringRef],
                          ['stringview_wtf8', kWasmStringViewWtf8],
                          ['stringview_wtf16', kWasmStringViewWtf16],
                          ['stringview_iter', kWasmStringViewIter]]) {
  let message = `invalid value type '${name}ref', ${enableMessage}`;
  let default_init = WasmInitExpr.RefNull(code);

  assertInvalid(b => b.addType(makeSig([code], [])),
                `${message} @+13`);
  assertInvalid(b => b.addStruct([makeField(code, true)]),
                `${message} @+13`);
  assertInvalid(b => b.addArray(code, true),
                `${message} @+12`);
  assertInvalid(b => b.addType(makeSig([], [code])),
                `${message} @+14`);
  assertInvalid(b => b.addGlobal(code, true, default_init),
                `${message} @+11`);
  assertInvalid(b => b.addTable(code, 0),
                `${message} @+11`);
  assertInvalid(b => b.addPassiveElementSegment([default_init], code),
                `${message} @+12`);
  assertInvalid(b => b.addTag(makeSig([code], [])),
                `${message} @+13`);
  assertInvalid(b => b.addFunction(undefined, kSig_v_v).addLocals(code, 1),
                `Compiling function #0 failed: ${message} @+24`);
}
