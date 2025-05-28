// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-custom-descriptors --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

function StringToArray(str) {
  let result = [str.length];
  for (let c of str) result.push(c.charCodeAt(0));
  return result;
}

let builder = new WasmModuleBuilder();
builder.startRecGroup();
let $obj0 = builder.addStruct({fields: [], descriptor: 1});
let $desc0 = builder.addStruct({describes: $obj0});
builder.endRecGroup();

let kStringTest = builder.addImport('wasm:js-string', 'test', kSig_i_r);

let $g_desc0 = builder.addGlobal(wasmRefType($desc0).exact(), false, false, [
  kGCPrefix, kExprStructNewDefault, $desc0,
]);
let $g_desc1 = builder.addGlobal(wasmRefType($desc0).exact(), false, false, [
  kGCPrefix, kExprStructNewDefault, $desc0,
]);
let $g_desc2 = builder.addGlobal(wasmRefType($desc0).exact(), false, false, [
  kGCPrefix, kExprStructNewDefault, $desc0,
]);

let $make_t = builder.addType(makeSig([kWasmI32], [kWasmAnyRef]));
let make_body = (desc) => [
  kExprGlobalGet, desc.index,
  kGCPrefix, kExprStructNew, $obj0,
]
let $make0 = builder.addFunction("WasmP", $make_t).addBody(make_body($g_desc0));
let $make1 = builder.addFunction("WasmP", $make_t).addBody(make_body($g_desc1));
let $make2 = builder.addFunction("WasmP", $make_t).addBody(make_body($g_desc2));

let $foo = builder.addFunction("foo", kSig_i_v).addBody([kExprI32Const, 42]);

let e_3 = new Array(3).fill(kWasmExternRef);
let e_4 = new Array(4).fill(kWasmExternRef);
builder.addFunction("getp", makeSig(e_3, [kWasmI32])).exportFunc().addBody([
  kExprLocalGet, 1,
  kExprCallFunction, kStringTest,
]);
builder.addFunction("setp", makeSig(e_4, [kWasmI32])).exportFunc().addBody([
  kExprLocalGet, 1,
  kExprCallFunction, kStringTest,
]);
builder.addFunction("setf", makeSig(e_4, [kWasmI32])).exportFunc().addBody([
  kExprI32Const, 0,
]);

let global_entries = [
  3,  // number of entries

  $g_desc0.index,  // global index
  0,  // no parent
  0,  // no methods
  1, ...StringToArray("W0"), $make0.index,  // constructor
  0,  // no statics

  $g_desc1.index,  // global index
  0,  // no parent
  1,  // number of methods
  1, ...StringToArray("foo"), $foo.index,  // 1=getter
  1, ...StringToArray("W1"), $make1.index,  // constructor
  0,  // no statics

  $g_desc2.index,  // global index
  0,  // no parent
  0,  // no methods
  1, ...StringToArray("W2"), $make2.index,  // constructor
  0,  // no statics
];
builder.addCustomSection("experimental-descriptors", [
  0,  // version
  0,  // module name
  2,  // "GlobalEntries" subsection
  ...wasmUnsignedLeb(global_entries.length),
  ...global_entries,
]);

let instance = builder.instantiate({}, {builtins: ["js-string"]});
let wasm = instance.exports;
Object.setPrototypeOf(
    wasm.W0.prototype,
    new Proxy(Object.freeze(Object.create(null)), Object.freeze({
      get: wasm.getp,
      set: wasm.setp,
    })));
Object.freeze(wasm.W0.prototype);
Object.setPrototypeOf(
    wasm.W1.prototype,
    new Proxy(Object.freeze(Object.create(null)), Object.freeze({
      get: wasm.getp,  // can't get inlined due to prototype getter {foo}.
      set: wasm.setf,  // can't get inlined due to prototype getter {foo}.
    })));
Object.freeze(wasm.W1.prototype);
Object.setPrototypeOf(
    wasm.W2.prototype,
    new Proxy(Object.freeze(Object.create(null)), Object.freeze({
      get: wasm.getp,
      set: wasm.setf,  // returns 0, meaning {false}.
    })));
Object.freeze(wasm.W2.prototype);

let w0 = new wasm.W0();
let w1 = new wasm.W1();
let w2 = new wasm.W2();

class Test {
  constructor(body, expected, inner = null) {
    this.body = body;
    this.expected = expected;
    this.inner = inner;
  }
}

let tests = [

  new Test(function IntIsString() { return w0[0]; }, 1),

  new Test(function ObjectIsString() { return w0[{}]; }, 1),

  new Test(function StringIsString() {
    let sum = 0;
    for (let str of ["a", "bb", "ccc"]) {
      sum += w0[str];
    }
    return sum;
  }, 3),

  new Test(function NoMethodUsesProxy() {
    let result;
    for (let key of ["bar", "foo"]) result = w0[key];
    return result;
  }, 1),

  new Test(function MethodHidesProxy() {
    let result;
    for (let key of ["bar", "foo"]) result = w1[key];
    return result;
  }, 42),

  new Test(function StrictModeThrows(inner) {
    assertThrows(() => inner("foo"), TypeError,
                  /'set' on proxy: trap returned falsish/);
    assertThrows(() => inner("bar"), TypeError,
                  /'set' on proxy: trap returned falsish/);
    return 1;
  }, 1, function inner(key) {
    "use strict";
    w2[key] = 0;
  }),

  new Test(function StrictModeThrowsMethod(inner) {
    assertThrows(
      () => inner("foo"), TypeError,
      /Cannot set property foo of \[object Object\] which has only a getter/);
    assertThrows(
      () => inner("bar"), TypeError,
      /'set' on proxy: trap returned falsish/);
    return 1;
  }, 1, function inner(key) {
    "use strict";
    w1[key] = 0;
  }),

  new Test(function StrictModeCatches() {
    "use strict";
    let key = "foo";
    try {
      w2[key] = 0;
    } catch(e) {
      return "threw";
    }
    return "didn't throw";
  }, "threw"),
];

for (let test of tests) {
  console.log(`Test: ${test.body.name}...`);
  %PrepareFunctionForOptimization(test.body);
  if (test.inner) { %PrepareFunctionForOptimization(test.inner); }
  for (let i = 0; i < 3; i++) {
    assertEquals(test.expected, test.body(test.inner));
  }
  %OptimizeFunctionOnNextCall(test.body);
  if (test.inner) { %OptimizeFunctionOnNextCall(test.inner); }
  assertEquals(test.expected, test.body(test.inner));
}
