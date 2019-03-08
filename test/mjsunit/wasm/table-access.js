// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-wasm --experimental-wasm-anyref

load("test/mjsunit/wasm/wasm-module-builder.js");

function addTableWithAccessors(builder, type, size, name) {
  const table = builder.addTable(type, size);
  const set_sig = makeSig([kWasmI32, type], []);
  builder.addFunction('set_' + name, set_sig)
      .addBody([kExprGetLocal, 0,
          kExprGetLocal, 1,
          kExprSetTable, table.index])
      .exportFunc();

  const get_sig = makeSig([kWasmI32], [type]);
  builder.addFunction('get_' + name, get_sig)
      .addBody([kExprGetLocal, 0, kExprGetTable, table.index])
      .exportFunc();
}

const builder = new WasmModuleBuilder();

addTableWithAccessors(builder, kWasmAnyFunc, 10, 'table_func1');
addTableWithAccessors(builder, kWasmAnyRef, 20, 'table_ref1');
addTableWithAccessors(builder, kWasmAnyRef, 9, 'table_ref2');
addTableWithAccessors(builder, kWasmAnyFunc, 12, 'table_func2');

let exports = builder.instantiate().exports;
const dummy_ref = {foo : 1, bar : 3};
const dummy_func = exports.set_table_func1;

(function testTableGetInitialValue() {
  print(arguments.callee.name);
  // Tables are initialized with `null`.
  assertSame(null, exports.get_table_func1(1));
  assertSame(null, exports.get_table_func2(2));
  assertSame(null, exports.get_table_ref1(3));
  assertSame(null, exports.get_table_ref2(4));
})();

(function testTableGetOOB() {
  print(arguments.callee.name);
  assertSame(null, exports.get_table_func2(11));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_func1(11));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_func2(21));
  assertSame(null, exports.get_table_ref1(14));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_ref2(14));
  assertTraps(kTrapTableOutOfBounds, () => exports.get_table_ref1(44));
})();

(function testTableSetOOB() {
  print(arguments.callee.name);
  exports.set_table_func2(11, dummy_func);
  assertTraps(kTrapTableOutOfBounds, () => exports.set_table_func1(11, dummy_func));
  assertTraps(kTrapTableOutOfBounds, () => exports.set_table_func2(21, dummy_func));
  exports.set_table_ref1(14, dummy_ref);
  assertTraps(kTrapTableOutOfBounds, () => exports.set_table_ref2(14, dummy_ref));
  assertTraps(kTrapTableOutOfBounds, () => exports.set_table_ref1(44, dummy_ref));
})();

(function testSetTable() {
  print(arguments.callee.name);
  assertSame(null, exports.get_table_func1(3));
  exports.set_table_func1(3, dummy_func);
  assertSame(dummy_func, exports.get_table_func1(3));
  assertSame(null, exports.get_table_func2(3));

  assertSame(null, exports.get_table_func2(7));
  exports.set_table_func2(7, dummy_func);
  assertSame(dummy_func, exports.get_table_func2(7));
  assertSame(null, exports.get_table_func1(7));

  assertSame(null, exports.get_table_ref1(3));
  exports.set_table_ref1(3, dummy_ref);
  assertSame(dummy_ref, exports.get_table_ref1(3));
  assertSame(null, exports.get_table_ref2(3));

  assertSame(null, exports.get_table_ref2(7));
  exports.set_table_ref2(7, dummy_ref);
  assertSame(dummy_ref, exports.get_table_ref2(7));
  assertSame(null, exports.get_table_ref1(7));
})();
