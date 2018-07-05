// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt --no-always-opt

var buffer = new ArrayBuffer(64);
var dataview = new DataView(buffer, 8, 24);

var values = [-1, 2, -3, 42];

function readUint8(offset) {
  return dataview.getUint8(offset);
}

function readInt8Handled(offset) {
  try {
    return dataview.getInt8(offset);
  } catch (e) {
    return e;
  }
}

function readUint16(offset, little_endian) {
  return dataview.getUint16(offset, little_endian);
}

function readInt16Handled(offset, little_endian) {
  try {
    return dataview.getInt16(offset, little_endian);
  } catch (e) {
    return e;
  }
}

function warmup(f) {
  f(0);
  f(1);
  %OptimizeFunctionOnNextCall(f);
  f(2);
  f(3);
}

// TurboFan valid getInt8.
for (var i = 0; i < values.length; i++) {
  dataview.setInt8(i, values[i]);
}
warmup(readInt8Handled);
assertOptimized(readInt8Handled);
assertEquals(values[0], readInt8Handled(0));
assertEquals(values[1], readInt8Handled(1));
assertEquals(values[2], readInt8Handled(2));
assertEquals(values[3], readInt8Handled(3));

// TurboFan valid getUint8.
dataview.setUint32(4, 0xdeadbeef);
warmup(readUint8);
assertOptimized(readUint8);
assertEquals(0xde, readUint8(4));
assertEquals(0xad, readUint8(5));
assertEquals(0xbe, readUint8(6));
assertEquals(0xef, readUint8(7));

// TurboFan valid getUint16.
dataview.setUint16(8, 0xabcd);
warmup(readUint16);
assertOptimized(readUint16);
assertEquals(0xabcd, readUint16(8));
assertEquals(0xcdab, readUint16(8, true));

// TurboFan valid getInt16.
dataview.setInt16(10, -0x1234);
warmup(readInt16Handled);
assertEquals(-0x1234, readInt16Handled(10));
dataview.setInt16(10, -0x1234, true);
assertEquals(-0x1234, readInt16Handled(10, true));

// TurboFan out of bounds read, throw with exception handler.
assertOptimized(readInt8Handled);
assertInstanceof(readInt8Handled(24), RangeError);
assertOptimized(readInt8Handled);
assertOptimized(readInt16Handled);
assertInstanceof(readInt16Handled(23), RangeError);
assertOptimized(readInt16Handled);

// Without exception handler.
assertOptimized(readUint8);
assertThrows(() => readUint8(64));
assertOptimized(readUint8);

// TurboFan deoptimizations.
assertOptimized(readInt8Handled);
assertInstanceof(readInt8Handled(-1), RangeError); // Negative Smi deopts.
assertUnoptimized(readInt8Handled);

warmup(readInt8Handled);
assertOptimized(readInt8Handled);
assertEquals(values[3], readInt8Handled(3.14)); // Non-Smi index deopts.
assertUnoptimized(readInt8Handled);

// None of the stores wrote out of bounds.
var bytes = new Uint8Array(buffer);
for (var i = 0; i < 8; i++) assertEquals(0, bytes[i]);
for (var i = 24; i < 64; i++) assertEquals(0, bytes[i]);

// TurboFan neutered buffer.
warmup(readInt8Handled);
assertOptimized(readInt8Handled);
%ArrayBufferNeuter(buffer);
assertInstanceof(readInt8Handled(0), TypeError);
assertOptimized(readInt8Handled);
