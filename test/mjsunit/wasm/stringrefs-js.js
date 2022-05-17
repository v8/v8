// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-stringref

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function assertInvalid(fn, message) {
  let builder = new WasmModuleBuilder();
  fn(builder);
  assertThrows(() => builder.toModule(), WebAssembly.CompileError,
               `WebAssembly.Module(): ${message}`);
}

(function TestPassthrough() {
  let kSig_w_w = makeSig([kWasmStringRef], [kWasmStringRef]);
  let builder = new WasmModuleBuilder();

  builder.addFunction("passthrough", kSig_w_w)
    .exportFunc()
    .addBody([
      kExprLocalGet, 0,
    ]);

  let instance = builder.instantiate()

  assertEquals('foo', instance.exports.passthrough('foo'));
  assertEquals(null, instance.exports.passthrough(null));

  assertThrows(()=>instance.exports.passthrough(3),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.passthrough({}),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.passthrough({valueOf: ()=>'foo'}),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.passthrough(undefined),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.passthrough(),
               TypeError, "type incompatibility when transforming from/to JS");
})();

(function TestSwap() {
  let kSig_ww_ww = makeSig([kWasmStringRef, kWasmStringRef],
                           [kWasmStringRef, kWasmStringRef]);
  let builder = new WasmModuleBuilder();

  builder.addFunction("swap", kSig_ww_ww)
    .exportFunc()
    .addBody([
      kExprLocalGet, 1,
      kExprLocalGet, 0,
    ]);

  let instance = builder.instantiate()

  assertArrayEquals(['bar', 'foo'], instance.exports.swap('foo', 'bar'));
  assertArrayEquals(['bar', null], instance.exports.swap(null, 'bar'));
  assertArrayEquals([null, 'foo'], instance.exports.swap('foo', null));
})();

(function TestViewsUnsupported() {
  let kSig_x_v = makeSig([], [kWasmStringViewWtf8]);
  let kSig_y_v = makeSig([], [kWasmStringViewWtf16]);
  let kSig_z_v = makeSig([], [kWasmStringViewIter]);
  let builder = new WasmModuleBuilder();

  builder.addFunction("stringview_wtf8", kSig_x_v)
    .exportFunc()
    .addLocals(kWasmStringViewWtf8, 1)
    .addBody([
      kExprLocalGet, 0,
    ]);
  builder.addFunction("stringview_wtf16", kSig_y_v)
    .exportFunc()
    .addLocals(kWasmStringViewWtf16, 1)
    .addBody([
      kExprLocalGet, 0,
    ]);
  builder.addFunction("stringview_iter", kSig_z_v)
    .exportFunc()
    .addLocals(kWasmStringViewIter, 1)
    .addBody([
      kExprLocalGet, 0,
    ]);

  let instance = builder.instantiate()

  assertThrows(()=>instance.exports.stringview_wtf8(),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.stringview_wtf16(),
               TypeError, "type incompatibility when transforming from/to JS");
  assertThrows(()=>instance.exports.stringview_iter(),
               TypeError, "type incompatibility when transforming from/to JS");
})();

// TODO(wingo): Test stringref-valued globals (defined and imported).
// TODO(wingo): Test calls from wasm to JS.
// TODO(wingo): Test stringrefs in tables.
