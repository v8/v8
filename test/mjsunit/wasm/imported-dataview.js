// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-wasm-gc --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// We use "r" for nullable "externref", and "e" for non-nullable "ref extern".
let kSig_d_r = makeSig([kWasmExternRef], [kWasmF64]);
let kSig_l_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI64]);
let kSig_i_ri = makeSig([kWasmExternRef, kWasmI32], [kWasmI32]);
let kSig_i_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]);
let kSig_f_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmF32]);
let kSig_d_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmF64]);
let kSig_v_rili = makeSig([kWasmExternRef, kWasmI32, kWasmI64, kWasmI32], []);
let kSig_v_rii = makeSig([kWasmExternRef, kWasmI32, kWasmI32], []);
let kSig_v_riii = makeSig([kWasmExternRef, kWasmI32, kWasmI32, kWasmI32], []);
let kSig_v_rifi = makeSig([kWasmExternRef, kWasmI32, kWasmF32, kWasmI32], []);
let kSig_v_ridi = makeSig([kWasmExternRef, kWasmI32, kWasmF64, kWasmI32], []);

let kImports = {
  DataView: {
    getBigInt64Import:
        Function.prototype.call.bind(DataView.prototype.getBigInt64),
    getBigUint64Import:
        Function.prototype.call.bind(DataView.prototype.getBigUint64),
    getFloat32Import:
        Function.prototype.call.bind(DataView.prototype.getFloat32),
    getFloat64Import:
        Function.prototype.call.bind(DataView.prototype.getFloat64),
    getInt8Import: Function.prototype.call.bind(DataView.prototype.getInt8),
    getInt16Import: Function.prototype.call.bind(DataView.prototype.getInt16),
    getInt32Import: Function.prototype.call.bind(DataView.prototype.getInt32),
    getUint8Import: Function.prototype.call.bind(DataView.prototype.getUint8),
    getUint16Import: Function.prototype.call.bind(DataView.prototype.getUint16),
    getUint32Import: Function.prototype.call.bind(DataView.prototype.getUint32),
    setBigInt64Import:
        Function.prototype.call.bind(DataView.prototype.setBigInt64),
    setBigUint64Import:
        Function.prototype.call.bind(DataView.prototype.setBigUint64),
    setFloat32Import:
        Function.prototype.call.bind(DataView.prototype.setFloat32),
    setFloat64Import:
        Function.prototype.call.bind(DataView.prototype.setFloat64),
    setInt8Import: Function.prototype.call.bind(DataView.prototype.setInt8),
    setInt16Import: Function.prototype.call.bind(DataView.prototype.setInt16),
    setInt32Import: Function.prototype.call.bind(DataView.prototype.setInt32),
    setUint8Import: Function.prototype.call.bind(DataView.prototype.setUint8),
    setUint16Import: Function.prototype.call.bind(DataView.prototype.setUint16),
    setUint32Import: Function.prototype.call.bind(DataView.prototype.setUint32),
    byteLengthImport: Function.prototype.call.bind(
        Object.getOwnPropertyDescriptor(DataView.prototype, 'byteLength').get)
  },
};

function ExportedDataViewGetterBody(dataview_op) {
  return [
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, dataview_op,
  ]
}

function ExportedDataViewSetterBody(dataview_op) {
  return [
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprLocalGet, 3,
    kExprCallFunction, dataview_op,
  ];
}

function MakeInstance() {
  let builder = new WasmModuleBuilder();

  let kDataViewGetBigInt64 =
      builder.addImport('DataView', 'getBigInt64Import', kSig_l_rii);
  let kDataViewGetBigUint64 =
      builder.addImport('DataView', 'getBigUint64Import', kSig_l_rii);
  let kDataViewGetFloat32 =
      builder.addImport('DataView', 'getFloat32Import', kSig_f_rii);
  let kDataViewGetFloat64 =
      builder.addImport('DataView', 'getFloat64Import', kSig_d_rii);
  let kDataViewGetInt8 =
      builder.addImport('DataView', 'getInt8Import', kSig_i_ri);
  let kDataViewGetInt16 =
      builder.addImport('DataView', 'getInt16Import', kSig_i_rii);
  let kDataViewGetInt32 =
      builder.addImport('DataView', 'getInt32Import', kSig_i_rii);
  let kDataViewGetUint8 =
      builder.addImport('DataView', 'getUint8Import', kSig_i_ri);
  let kDataViewGetUint16 =
      builder.addImport('DataView', 'getUint16Import', kSig_i_rii);
  let kDataViewGetUint32 =
      builder.addImport('DataView', 'getUint32Import', kSig_i_rii);
  let kDataViewSetBigInt64 =
      builder.addImport('DataView', 'setBigInt64Import', kSig_v_rili);
  let kDataViewSetBigUint64 =
      builder.addImport('DataView', 'setBigUint64Import', kSig_v_rili);
  let kDataViewSetFloat32 =
      builder.addImport('DataView', 'setFloat32Import', kSig_v_rifi);
  let kDataViewSetFloat64 =
      builder.addImport('DataView', 'setFloat64Import', kSig_v_ridi);
  let kDataViewSetInt8 =
      builder.addImport('DataView', 'setInt8Import', kSig_v_rii);
  let kDataViewSetInt16 =
      builder.addImport('DataView', 'setInt16Import', kSig_v_riii);
  let kDataViewSetInt32 =
      builder.addImport('DataView', 'setInt32Import', kSig_v_riii);
  let kDataViewSetUint8 =
      builder.addImport('DataView', 'setUint8Import', kSig_v_rii);
  let kDataViewSetUint16 =
      builder.addImport('DataView', 'setUint16Import', kSig_v_riii);
  let kDataViewSetUint32 =
      builder.addImport('DataView', 'setUint32Import', kSig_v_riii);
  let kDataViewByteLength =
      builder.addImport('DataView', 'byteLengthImport', kSig_d_r);

  builder.addFunction('getBigInt64', kSig_l_rii)
      .exportFunc()
      .addBody(ExportedDataViewGetterBody(kDataViewGetBigInt64));
  builder.addFunction('getBigUint64', kSig_l_rii)
      .exportFunc()
      .addBody(ExportedDataViewGetterBody(kDataViewGetBigUint64));
  builder.addFunction('getFloat32', kSig_f_rii)
      .exportFunc()
      .addBody(ExportedDataViewGetterBody(kDataViewGetFloat32));
  builder.addFunction('getFloat64', kSig_d_rii)
      .exportFunc()
      .addBody(ExportedDataViewGetterBody(kDataViewGetFloat64));
  builder.addFunction('getInt8', kSig_i_ri).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallFunction, kDataViewGetInt8,
  ]);
  builder.addFunction('getInt16', kSig_i_rii)
      .exportFunc()
      .addBody(ExportedDataViewGetterBody(kDataViewGetInt16));
  builder.addFunction('getInt32', kSig_i_rii)
      .exportFunc()
      .addBody(ExportedDataViewGetterBody(kDataViewGetInt32));
  builder.addFunction('getUint8', kSig_i_ri).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprCallFunction, kDataViewGetUint8,
  ]);
  builder.addFunction('getUint16', kSig_i_rii)
      .exportFunc()
      .addBody(ExportedDataViewGetterBody(kDataViewGetUint16));
  builder.addFunction('getUint32', kSig_i_rii)
      .exportFunc()
      .addBody(ExportedDataViewGetterBody(kDataViewGetUint32));
  builder.addFunction('setBigInt64', kSig_v_rili)
      .exportFunc()
      .addBody(ExportedDataViewSetterBody(kDataViewSetBigInt64));
  builder.addFunction('setBigUint64', kSig_v_rili)
      .exportFunc()
      .addBody(ExportedDataViewSetterBody(kDataViewSetBigUint64));
  builder.addFunction('setFloat32', kSig_v_rifi)
      .exportFunc()
      .addBody(ExportedDataViewSetterBody(kDataViewSetFloat32));
  builder.addFunction('setFloat64', kSig_v_ridi)
      .exportFunc()
      .addBody(ExportedDataViewSetterBody(kDataViewSetFloat64));
  builder.addFunction('setInt8', kSig_v_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewSetInt8,
  ]);
  builder.addFunction('setInt16', kSig_v_riii)
      .exportFunc()
      .addBody(ExportedDataViewSetterBody(kDataViewSetInt16));
  builder.addFunction('setInt32', kSig_v_riii)
      .exportFunc()
      .addBody(ExportedDataViewSetterBody(kDataViewSetInt32));
  builder.addFunction('setUint8', kSig_v_rii).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprLocalGet, 1,
    kExprLocalGet, 2,
    kExprCallFunction, kDataViewSetUint8,
  ]);
  builder.addFunction('setUint16', kSig_v_riii)
      .exportFunc()
      .addBody(ExportedDataViewSetterBody(kDataViewSetUint16));
  builder.addFunction('setUint32', kSig_v_riii)
      .exportFunc()
      .addBody(ExportedDataViewSetterBody(kDataViewSetUint32));
  builder.addFunction('byteLength', kSig_d_r).exportFunc().addBody([
    kExprLocalGet, 0,
    kExprCallFunction, kDataViewByteLength,
  ]);
  return builder.instantiate(kImports);
}

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

function CheckDataViewErrorMessages(dataview_op, array, set_to_value) {
  let dataview = new DataView(array.buffer);

  // Incompatible receiver (number).
  let params = [123, 0];
  if (set_to_value != undefined) params.push(set_to_value);
  CheckStackTrace(
      () => instance.exports[dataview_op](...params),
      () => DataView.prototype[dataview_op].call(...params), dataview_op);

  // Incompatible receiver (string).
  params = ['test_string', 0];
  if (set_to_value != undefined) params.push(set_to_value);
  CheckStackTrace(
      () => instance.exports[dataview_op](...params),
      () => DataView.prototype[dataview_op].call(...params), dataview_op);

  // Offset bounds check.
  params = [dataview, -1];
  if (set_to_value != undefined) params.push(set_to_value);
  CheckStackTrace(
      () => instance.exports[dataview_op](...params),
      () => DataView.prototype[dataview_op].call(...params), dataview_op);

  // Dataview bounds check (expect the buffer index 100 is out of bounds).
  params = [dataview, 100];
  if (set_to_value != undefined) params.push(set_to_value);
  CheckStackTrace(
      () => instance.exports[dataview_op](...params),
      () => DataView.prototype[dataview_op].call(...params), dataview_op);

  // Detached buffer.
  params = [dataview, 0];
  if (set_to_value != undefined) params.push(set_to_value);
  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports[dataview_op](...params),
      () => DataView.prototype[dataview_op].call(...params), dataview_op);
}

let instance = MakeInstance();

(function TestDataViewOutOfBounds() {
  print(arguments.callee.name);
  let array = new BigInt64Array(2);
  array[0] = 0x7FFFFFFFFFFFFFFFn;
  array[1] = 0x12345678n;
  let dataview = new DataView(array.buffer);

  // Dataview bounds check:
  CheckStackTrace(
      () => instance.exports.getBigInt64(dataview, 15, 1),
      () => DataView.prototype.getBigInt64.call(dataview, 15, 1),
      'getBigInt64');
  // Offset with element size exactly out of bounds.
  CheckStackTrace(
      () => instance.exports.getBigInt64(dataview, 16, 1),
      () => DataView.prototype.getBigInt64.call(dataview, 16, 1),
      'getBigInt64');
  // Offset without element size already out of bounds.
  CheckStackTrace(
      () => instance.exports.getBigInt64(dataview, 17, 1),
      () => DataView.prototype.getBigInt64.call(dataview, 17, 1),
      'getBigInt64');
})();

(function TestDataViewSharedArrayBuffer() {
  print(arguments.callee.name);
  let buffer = new SharedArrayBuffer(16);
  let array = new Int32Array(buffer);
  let dataview = new DataView(buffer);

  instance.exports.setInt32(dataview, 0, 100, 1);
  assertEquals(100, array[0]);
  assertEquals(100, instance.exports.getInt32(dataview, 0, 1));

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
      () => instance.exports.setInt32(dataview, 16, 100, 1),
      () => DataView.prototype.setInt32.call(dataview, 16, 100, 1), 'setInt32');

  // Note: `SharedArrayBuffer` cannot become detached.
})();

(function TestGetBigInt64() {
  print(arguments.callee.name);
  let array = new BigInt64Array(2);
  array[0] = 0x7FFFFFFFFFFFFFFFn;
  array[1] = 0x12345678n;
  let dataview = new DataView(array.buffer);

  assertEquals(
      0x7FFFFFFFFFFFFFFFn, instance.exports.getBigInt64(dataview, 0, 1));
  assertEquals(0x12345678n, instance.exports.getBigInt64(dataview, 8, 1));
  assertEquals(
      0x7856341200000000n, instance.exports.getBigInt64(dataview, 8, 0));

  CheckDataViewErrorMessages('getBigInt64', array);
})();

(function TestGetBigUint64() {
  print(arguments.callee.name);
  let array = new BigUint64Array(2);
  array[0] = 0x7FFFFFFFFFFFFFFFn;
  array[1] = 0x12345678n;
  let dataview = new DataView(array.buffer);

  assertEquals(
      0x7FFFFFFFFFFFFFFFn, instance.exports.getBigUint64(dataview, 0, 1));
  assertEquals(
      0x7856341200000000n, instance.exports.getBigUint64(dataview, 8, 0));

  CheckDataViewErrorMessages('getBigUint64', array);
})();

(function TestGetFloat32() {
  print(arguments.callee.name);
  let array = new Float32Array(2);
  array[0] = 140.125;
  array[1] = -2048.015625;
  let dataview = new DataView(array.buffer);

  assertEquals(140.125, instance.exports.getFloat32(dataview, 0, 1));
  assertEquals(
      dataview.getFloat32(4, 0), instance.exports.getFloat32(dataview, 4, 0));

  CheckDataViewErrorMessages('getFloat32', array);
})();

(function TestGetFloat64() {
  print(arguments.callee.name);
  let array = new Float64Array(2);
  array[0] = 140.125;
  array[1] = -20480000.001953125;
  let dataview = new DataView(array.buffer);

  assertEquals(140.125, instance.exports.getFloat64(dataview, 0, 1));
  assertEquals(
      dataview.getFloat64(8, 0), instance.exports.getFloat64(dataview, 8, 0));

  CheckDataViewErrorMessages('getFloat64', array);
})();

(function TestGetInt8() {
  print(arguments.callee.name);
  let array = new Int8Array(2);
  array[0] = 127;
  array[1] = -64;
  let dataview = new DataView(array.buffer);

  assertEquals(127, instance.exports.getInt8(dataview, 0));
  assertEquals(-64, instance.exports.getInt8(dataview, 1));

  CheckDataViewErrorMessages('getInt8', array);
})();

(function TestGetInt16() {
  print(arguments.callee.name);
  let array = new Int16Array(2);
  array[0] = 32767;
  array[1] = 0x1234;
  let dataview = new DataView(array.buffer);

  assertEquals(32767, instance.exports.getInt16(dataview, 0, 1));
  assertEquals(0x3412, instance.exports.getInt16(dataview, 2, 0));

  CheckDataViewErrorMessages('getInt16', array);
})();

(function TestGetInt32() {
  print(arguments.callee.name);
  let array = new Int32Array(2);
  array[0] = 42;
  array[1] = 0x12345678;
  let dataview = new DataView(array.buffer);

  assertEquals(42, instance.exports.getInt32(dataview, 0, 1));
  assertEquals(0x12345678, instance.exports.getInt32(dataview, 4, 1));
  assertEquals(0x78563412, instance.exports.getInt32(dataview, 4, 0));

  CheckDataViewErrorMessages('getInt32', array);
})();

(function TestGetUint8() {
  print(arguments.callee.name);
  let array = new Uint8Array(1);
  array[0] = 255;
  let dataview = new DataView(array.buffer);

  assertEquals(255, instance.exports.getUint8(dataview, 0));

  CheckDataViewErrorMessages('getUint8', array);
})();

(function TestGetUint16() {
  print(arguments.callee.name);
  let array = new Uint16Array(2);
  array[0] = 0xFFFF;
  array[1] = 0x1234;
  let dataview = new DataView(array.buffer);

  assertEquals(0xFFFF, instance.exports.getUint16(dataview, 0, 1));
  assertEquals(0x3412, instance.exports.getUint16(dataview, 2, 0));

  CheckDataViewErrorMessages('getUint16', array);
})();

(function TestGetUint32() {
  print(arguments.callee.name);
  let array = new Uint32Array(2);
  array[0] = 0x7FFFFFFF;
  array[1] = 0x12345678;
  let dataview = new DataView(array.buffer);

  assertEquals(0x7FFFFFFF, instance.exports.getUint32(dataview, 0, 1));
  assertEquals(0x78563412, instance.exports.getUint32(dataview, 4, 0));

  CheckDataViewErrorMessages('getUint32', array);
})();

(function TestSetBigInt64() {
  print(arguments.callee.name);
  let array = new BigInt64Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setBigInt64(dataview, 0, 0x7FFFFFFFFFFFFFFFn, 1);
  assertEquals(0x7FFFFFFFFFFFFFFFn, array[0]);
  instance.exports.setBigInt64(dataview, 0, -0x7FFFFFFFFFFFFFFFn, 1);
  assertEquals(-0x7FFFFFFFFFFFFFFFn, array[0]);
  instance.exports.setBigInt64(dataview, 8, 0x12345678n, 0);
  assertEquals(0x7856341200000000n, array[1]);

  CheckDataViewErrorMessages('setBigInt64', array, 50n);
})();

(function TestSetBigUint64() {
  print(arguments.callee.name);
  let array = new BigUint64Array(1);
  let dataview = new DataView(array.buffer);

  instance.exports.setBigUint64(dataview, 0, 0xFFFFFFFFFFFFFFFFn, 1);
  assertEquals(0xFFFFFFFFFFFFFFFFn, array[0]);
  instance.exports.setBigUint64(dataview, 0, 0x12345678n, 0);
  assertEquals(0x7856341200000000n, array[0]);

  CheckDataViewErrorMessages('setBigUint64', array, 50n);
})();

(function TestSetFloat32() {
  print(arguments.callee.name);
  let array = new Float32Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setFloat32(dataview, 0, 50.5, 1);
  assertEquals(50.5, array[0]);
  instance.exports.setFloat32(dataview, 0, 0x1234, 0);
  dataview.setFloat32(4, 0x1234, 0);
  assertEquals(array[1], array[0]);

  CheckDataViewErrorMessages('setFloat32', array, 50.5);
})();

(function TestSetFloat64() {
  print(arguments.callee.name);
  let array = new Float64Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setFloat64(dataview, 0, 42.5, 1);
  assertEquals(42.5, array[0]);
  instance.exports.setFloat64(dataview, 0, 0x1234, 0);
  dataview.setFloat64(8, 0x1234, 0);
  assertEquals(array[1], array[0]);

  CheckDataViewErrorMessages('setFloat64', array, 50.5);
})();

(function TestSetInt8() {
  print(arguments.callee.name);
  let array = new Int8Array(1);
  let dataview = new DataView(array.buffer);

  instance.exports.setInt8(dataview, 0, 127);
  assertEquals(127, array[0]);

  CheckDataViewErrorMessages('setInt8', array, 120);
})();

(function TestSetInt16() {
  print(arguments.callee.name);
  let array = new Int16Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setInt16(dataview, 0, 0x1234, 1);
  assertEquals(0x1234, array[0]);
  instance.exports.setInt16(dataview, 2, 0x1234, 0);
  assertEquals(0x3412, array[1]);

  CheckDataViewErrorMessages('setInt16', array, 120);
})();

(function TestSetInt32() {
  print(arguments.callee.name);
  let array = new Int32Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setInt32(dataview, 0, 50, 1);
  assertEquals(50, array[0]);
  instance.exports.setInt32(dataview, 4, 100, 1);
  assertEquals(100, array[1]);
  instance.exports.setInt32(dataview, 0, 0x12345678, 0);
  assertEquals(0x78563412, array[0]);

  CheckDataViewErrorMessages('setInt32', array, 100);
})();

(function TestSetUint8() {
  print(arguments.callee.name);
  let array = new Uint8Array(1);
  let dataview = new DataView(array.buffer);

  instance.exports.setUint8(dataview, 0, 255);
  assertEquals(255, array[0]);

  CheckDataViewErrorMessages('setUint8', array, 100);
})();

(function TestSetUint16() {
  print(arguments.callee.name);
  let array = new Uint16Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setUint16(dataview, 0, 0x1234, 1);
  assertEquals(0x1234, array[0]);
  instance.exports.setUint16(dataview, 2, 0x1234, 0);
  assertEquals(0x3412, array[1]);

  CheckDataViewErrorMessages('setUint16', array, 100);
})();

(function TestSetUint32() {
  print(arguments.callee.name);
  let array = new Uint32Array(2);
  let dataview = new DataView(array.buffer);

  instance.exports.setUint32(dataview, 0, 50, 1);
  assertEquals(50, array[0]);
  instance.exports.setUint32(dataview, 4, 0x12345678, 0);
  assertEquals(0x78563412, array[1]);

  CheckDataViewErrorMessages('setUint32', array, 100);
})();

(function TestByteLength() {
  print(arguments.callee.name);
  let array = new Int32Array(2);
  let dataview = new DataView(array.buffer);

  assertEquals(8, instance.exports.byteLength(dataview));
  let dataview2 = new DataView(array.buffer, 4, 4);
  assertEquals(4, dataview2.byteLength);

  CheckStackTrace(
      () => instance.exports.byteLength('test_string'),
      () => Object.getOwnPropertyDescriptor(DataView.prototype, 'byteLength')
                .get.call('test_string'),
      'byteLength');

  %ArrayBufferDetach(array.buffer);
  CheckStackTrace(
      () => instance.exports.byteLength(dataview),
      () => Object.getOwnPropertyDescriptor(DataView.prototype, 'byteLength')
                .get.call(dataview),
      'byteLength');
})();
