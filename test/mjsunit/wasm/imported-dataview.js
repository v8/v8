// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// We use "r" for nullable "externref", and "e" for non-nullable "ref extern".
let kSig_i_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]);
let kSig_v_riii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32], []);

let kDataViewGetInt32;
let kDataViewSetInt32;

function MakeBuilder() {
  let builder = new WasmModuleBuilder();

  kDataViewGetInt32 =
      builder.addImport('DataView', 'getInt32Import', kSig_i_rii);
  kDataViewSetInt32 =
      builder.addImport('DataView', 'setInt32Import', kSig_v_riii);

  return builder;
}

let kImports = {
  DataView: {
    getInt32Import: Function.prototype.call.bind(DataView.prototype.getInt32),
    setInt32Import: Function.prototype.call.bind(DataView.prototype.setInt32),
  },
};

function CheckStackTrace(thrower, reference, topmost_wasm_func) {
  let reference_exception;
  let actual_exception;
  try {
    thrower();
    assertUnreachable();
  } catch (e) {
    actual_exception = e;
  }
  try {
    reference();
    assertUnreachable();
  } catch (e) {
    reference_exception = e;
  }
  assertInstanceof(actual_exception, reference_exception.constructor);
  let actual_stack = actual_exception.stack.split('\n');
  let reference_stack = reference_exception.stack.split('\n');
  assertEquals(reference_stack[0], actual_stack[0]);
  assertEquals(reference_stack[1], actual_stack[1]);

  let str_stack_msg = `    at ${topmost_wasm_func} (wasm://wasm/`;
  if (!actual_stack[2].startsWith(str_stack_msg)) {
    console.log(
        `expected starts with:\n${str_stack_msg}\nfound:\n${actual_stack[2]}`);
    assertUnreachable();
  }
}

(function TestGetInt32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction("getInt32", kSig_i_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewGetInt32,
    ]);
  let instance = builder.instantiate(kImports);
  let array = new Int32Array(2);
  array[0] = 42;
  array[1] = 0x12345678;

  let dataview = new DataView(array.buffer);

  assertEquals(42, instance.exports.getInt32(dataview, 0, 1));
  assertEquals(0x12345678, instance.exports.getInt32(dataview, 4, 1));
  assertEquals(0x78563412, instance.exports.getInt32(dataview, 4, 0));

  // Incompatible receiver.
  assertThrows(
      () => {instance.exports.getInt32('test_string', 0, 1)}, TypeError);
  CheckStackTrace(
      () => instance.exports.getInt32('test_string', 0, 1),
      () => DataView.prototype.getInt32.call('test_string', 0, 1), 'getInt32');

  // Offset bounds check.
  assertThrows(() => {instance.exports.getInt32(dataview, -1, 1)}, RangeError);
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, -1, 1),
      () => DataView.prototype.getInt32.call(dataview, -1, 1), 'getInt32');

  // Dataview bounds check.
  assertThrows(() => {instance.exports.getInt32(dataview, 8, 1)}, RangeError);
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, 7, 1),
      () => DataView.prototype.getInt32.call(dataview, 7, 1), 'getInt32');
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, 8, 1),
      () => DataView.prototype.getInt32.call(dataview, 8, 1), 'getInt32');
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, 0x7FFF_FFFF, 1),
      () => DataView.prototype.getInt32.call(dataview, 0x7FFF_FFFF, 1), 'getInt32');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  assertThrows(() => {instance.exports.getInt32(dataview, 0, 1)}, TypeError);
  CheckStackTrace(
      () => instance.exports.getInt32(dataview, 0, 1),
      () => DataView.prototype.getInt32.call(dataview, 0, 1), 'getInt32');
})();

(function TestSetInt32() {
  print(arguments.callee.name);
  let builder = MakeBuilder();
  builder.addFunction("setInt32", kSig_v_riii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, kDataViewSetInt32,
    ]);
  let instance = builder.instantiate(kImports);
  let array = new Int32Array(2);
  array[0] = 42;
  array[1] = 3;

  let dataview = new DataView(array.buffer);

  instance.exports.setInt32(dataview, 0, 50, 1);
  assertEquals(50, array[0]);
  instance.exports.setInt32(dataview, 4, 100, 1);
  assertEquals(100, array[1]);
  instance.exports.setInt32(dataview, 0, 0x12345678, 0);
  assertEquals(0x78563412, array[0]);

  // Incompatible receiver.
  CheckStackTrace(
      () => instance.exports.setInt32('test_string', 0, 100, 1),
      () => DataView.prototype.setInt32.call('test_string', 0, 100, 1),
      'setInt32');

  // Offset bounds check.
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, -1, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, -1, 100, 1), 'setInt32');

  // Dataview bounds check.
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, 7, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 7, 100, 1), 'setInt32');
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, 8, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 8, 100, 1), 'setInt32');
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, 9, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 9, 100, 1), 'setInt32');

  // Detached buffer.
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.setInt32(dataview, 0, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 0, 100, 1), 'setInt32');
})();
