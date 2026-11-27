// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const realm = Realm.createAllowCrossRealmAccess();
const OtherTypeError = Realm.eval(realm, 'TypeError');
const OtherRangeError = Realm.eval(realm, 'RangeError');

const otherGetInt32 = Realm.eval(realm, 'DataView.prototype.getInt32');
const otherByteLength = Realm.eval(
    realm,
    'Object.getOwnPropertyDescriptor(DataView.prototype, "byteLength").get');
const otherNumToString = Realm.eval(realm, 'Number.prototype.toString');

const builder = new WasmModuleBuilder();
const getInt32Import = builder.addImport(
    'm', 'getInt32',
    makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]));
const byteLengthImport = builder.addImport(
    'm', 'byteLength', makeSig([kWasmExternRef], [kWasmF64]));
const intToStringImport = builder.addImport(
    'm', 'intToString', makeSig([kWasmI32, kWasmI32], [kWasmExternRef]));

builder
    .addFunction(
        'callGetInt32',
        makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
      kExprCallFunction, getInt32Import,
    ])
    .exportFunc();

builder
    .addFunction(
        'returnCallGetInt32',
        makeSig([kWasmExternRef, kWasmI32, kWasmI32], [kWasmI32]))
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprLocalGet, 2,
      kExprReturnCall, getInt32Import,
    ])
    .exportFunc();

builder
    .addFunction('callByteLength', makeSig([kWasmExternRef], [kWasmF64]))
    .addBody([kExprLocalGet, 0, kExprCallFunction, byteLengthImport])
    .exportFunc();

builder
    .addFunction(
        'callIntToString', makeSig([kWasmI32, kWasmI32], [kWasmExternRef]))
    .addBody([
      kExprLocalGet, 0, kExprLocalGet, 1, kExprCallFunction, intToStringImport,
    ])
    .exportFunc();

const instance = builder.instantiate({
  m: {
    getInt32: Function.prototype.call.bind(otherGetInt32),
    byteLength: Function.prototype.call.bind(otherByteLength),
    intToString: Function.prototype.call.bind(otherNumToString),
  },
});

const dv = new DataView(new ArrayBuffer(8));
const detachedAb = new ArrayBuffer(8);
const detachedDv = new DataView(detachedAb);
detachedAb.transfer();

function checkCrossRealmErrors() {
  assertThrows(() => instance.exports.callGetInt32({}, 0, 0), OtherTypeError);
  assertThrows(
      () => instance.exports.callGetInt32(detachedDv, 0, 0), OtherTypeError);
  assertThrows(
      () => instance.exports.callGetInt32(dv, 100, 0), OtherRangeError);
  assertThrows(
      () => instance.exports.returnCallGetInt32({}, 0, 0), OtherTypeError);
  assertThrows(() => instance.exports.callByteLength({}), OtherTypeError);
  assertThrows(() => instance.exports.callIntToString(42, 1), OtherRangeError);
}

// Baseline (Liftoff / generic Wasm-to-JS wrapper).
checkCrossRealmErrors();

// Tier up to Turboshaft: must still throw the imported builtin's realm errors.
%WasmTierUpFunction(instance.exports.callGetInt32);
%WasmTierUpFunction(instance.exports.returnCallGetInt32);
%WasmTierUpFunction(instance.exports.callByteLength);
%WasmTierUpFunction(instance.exports.callIntToString);

checkCrossRealmErrors();
