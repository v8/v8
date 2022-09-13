// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --wasm-gc-js-interop --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function MakeInstance() {
  let builder = new WasmModuleBuilder();
  let struct_type = builder.addStruct([makeField(kWasmI32, true)]);
  let array_type = builder.addArray(kWasmI32, true);
  builder.addFunction('MakeStruct', makeSig([], [kWasmExternRef]))
      .exportFunc()
      .addBody([
        kExprI32Const, 42,                       // --
        kGCPrefix, kExprStructNew, struct_type,  // --
        kGCPrefix, kExprExternExternalize        // --
      ]);
  builder.addFunction('MakeArray', makeSig([], [kWasmExternRef]))
      .exportFunc()
      .addBody([
        kExprI32Const, 2,                             // length
        kGCPrefix, kExprArrayNewDefault, array_type,  // --
        kGCPrefix, kExprExternExternalize             // --
      ]);

  return builder.instantiate();
}

let instance = MakeInstance();
let struct = instance.exports.MakeStruct();
let array = instance.exports.MakeArray();

function testThrowsRepeated(fn) {
  %PrepareFunctionForOptimization(fn);
  for (let i = 0; i < 5; i++) assertThrows(fn, TypeError);
  %OptimizeFunctionOnNextCall(fn);
  assertThrows(fn, TypeError);
}

// TODO: test repeated execution of functions using ICs, including optimized.
for (let wasm_obj of [struct, array]) {
  testThrowsRepeated(() => wasm_obj.foo);
  testThrowsRepeated(() => { wasm_obj.foo = 42; });
  testThrowsRepeated(() => wasm_obj[0]);
  testThrowsRepeated(() => { wasm_obj[0] = undefined; });
  assertThrows(() => wasm_obj.__proto__, TypeError);
  assertThrows(() => Object.prototype.__proto__.call(wasm_obj), TypeError);
  assertThrows(() => wasm_obj.__proto__ = null, TypeError);
  assertThrows(() => JSON.stringify(wasm_obj), TypeError);
  assertThrows(() => { for (let p in wasm_obj) { } }, TypeError);
  assertThrows(() => { for (let p of wasm_obj) { } }, TypeError);
  assertThrows(() => wasm_obj.toString(), TypeError);
  assertThrows(() => wasm_obj.valueOf(), TypeError);
  assertThrows(() => "" + wasm_obj, TypeError);
  assertThrows(() => 0 + wasm_obj, TypeError);
  assertThrows(() => { delete wasm_obj.foo; }, TypeError);
  assertThrows(() => Object.freeze(wasm_obj), TypeError);
  assertThrows(() => Object.seal(wasm_obj), TypeError);
  assertThrows(
      () => Object.prototype.__lookupGetter__.call(wasm_obj, 'foo'), TypeError);
  assertThrows(
      () => Object.prototype.__lookupSetter__.call(wasm_obj, 'foo'), TypeError);
  assertThrows(
      () => Object.prototype.__defineGetter__.call(wasm_obj, 'foo', () => 42),
      TypeError);
  assertThrows(
      () => Object.prototype.__defineSetter__.call(wasm_obj, 'foo', () => {}),
      TypeError);
  assertThrows(
      () => Object.defineProperty(wasm_obj, 'foo', {value: 42}), TypeError);

  assertEquals([], Object.getOwnPropertyNames(wasm_obj));
  assertEquals([], Object.getOwnPropertySymbols(wasm_obj));
  assertEquals({}, Object.getOwnPropertyDescriptors(wasm_obj));
  assertEquals([], Object.keys(wasm_obj));
  assertEquals([], Object.entries(wasm_obj));
  assertEquals(undefined, Object.getOwnPropertyDescriptor(wasm_obj, "foo"));
  assertEquals(false, "foo" in wasm_obj);
  assertEquals(false, Object.prototype.hasOwnProperty.call(wasm_obj, "foo"));
  assertEquals(true, Object.isSealed(wasm_obj));
  assertEquals(true, Object.isFrozen(wasm_obj));
  assertEquals(false, Object.isExtensible(wasm_obj));
  assertEquals("object", typeof wasm_obj);
  assertEquals("[object Object]", Object.prototype.toString.call(wasm_obj));

  {
    let js_obj = {};
    js_obj.foo = wasm_obj;
    assertEquals(wasm_obj, js_obj.foo);
    js_obj[0] = wasm_obj;
    assertEquals(wasm_obj, js_obj[0]);
  }

  assertEquals(42, wasm_obj ? 42 : 0);

  assertFalse(Array.isArray(wasm_obj));
}
