// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var typedArrayConstructors = [
  Uint8Array,
  Int8Array,
  Uint16Array,
  Int16Array,
  Uint32Array,
  Int32Array,
  Uint8ClampedArray,
  Float32Array,
  Float64Array];

for (var constructor of typedArrayConstructors) {
  assertEquals(1, constructor.prototype.fill.length);

  assertArrayEquals([], new constructor([]).fill(8));
  assertArrayEquals([8, 8, 8, 8, 8], new constructor([0, 0, 0, 0, 0]).fill(8));
  assertArrayEquals([0, 8, 8, 8, 8], new constructor([0, 0, 0, 0, 0]).fill(8, 1));
  assertArrayEquals([0, 0, 0, 0, 0], new constructor([0, 0, 0, 0, 0]).fill(8, 10));
  assertArrayEquals([8, 8, 8, 8, 8], new constructor([0, 0, 0, 0, 0]).fill(8, -5));
  assertArrayEquals([0, 8, 8, 8, 0], new constructor([0, 0, 0, 0, 0]).fill(8, 1, 4));
  assertArrayEquals([0, 8, 8, 8, 0], new constructor([0, 0, 0, 0, 0]).fill(8, 1, -1));
  assertArrayEquals([0, 8, 8, 8, 8], new constructor([0, 0, 0, 0, 0]).fill(8, 1, 42));
  assertArrayEquals([0, 0, 8, 8, 8], new constructor([0, 0, 0, 0, 0]).fill(8, -3, 42));
  assertArrayEquals([0, 0, 8, 8, 0], new constructor([0, 0, 0, 0, 0]).fill(8, -3, 4));
  assertArrayEquals([0, 0, 0, 8, 0], new constructor([0, 0, 0, 0, 0]).fill(8, -2, -1));
  assertArrayEquals([0, 0, 0, 0, 0], new constructor([0, 0, 0, 0, 0]).fill(8, -1, -3));
  assertArrayEquals([8, 8, 8, 8, 0], new constructor([0, 0, 0, 0, 0]).fill(8, 0, 4));

  assertArrayEquals([0, 0, 0, 0, 0], new constructor([0, 0, 0, 0, 0]).fill(8, Infinity));
  assertArrayEquals([8, 8, 8, 8, 8], new constructor([0, 0, 0, 0, 0]).fill(8, -Infinity));
  assertArrayEquals([8, 8, 8, 8, 8], new constructor([0, 0, 0, 0, 0]).fill(8, 0, Infinity));
  assertArrayEquals([0, 0, 0, 0, 0], new constructor([0, 0, 0, 0, 0]).fill(8, 0, -Infinity));

  // Test exceptions
  assertThrows('constructor.prototype.fill.call(null)', TypeError);
  assertThrows('constructor.prototype.fill.call(undefined)', TypeError);
  assertThrows('constructor.prototype.fill.call([])', TypeError);

  // Test ToNumber
  var s = "";
  var p = new Proxy({}, {get(t,k) { s += k.toString() + '\n'; return Reflect.get(t, k)}})
  new constructor(3).fill(p);
  assertEquals(s, `Symbol(Symbol.toPrimitive)
valueOf
toString
Symbol(Symbol.toStringTag)
Symbol(Symbol.toPrimitive)
valueOf
toString
Symbol(Symbol.toStringTag)
Symbol(Symbol.toPrimitive)
valueOf
toString
Symbol(Symbol.toStringTag)
`);

  // Shadowing length doesn't affect fill, unlike Array.prototype.fill
  var a = new constructor([2, 2]);
  Object.defineProperty(a, 'length', {value: 1});
  a.fill(3);
  assertArrayEquals([3, 3], [a[0], a[1]]);
  Array.prototype.fill.call(a, 4);
  assertArrayEquals([4, 3], [a[0], a[1]]);
}

// Empty args
assertArrayEquals([0], new Uint8Array([0]).fill());
assertArrayEquals([NaN], new Float32Array([0]).fill());

// Clamping
assertArrayEquals([0, 0, 0, 0, 0], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(-10));
assertArrayEquals([255, 255, 255, 255, 255], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(1000));

assertArrayEquals([1, 1, 1, 1, 1], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(0.50001));
assertArrayEquals([0, 0, 0, 0, 0], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(0.50000));
assertArrayEquals([0, 0, 0, 0, 0], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(0.49999));
// Check round half to even
assertArrayEquals([2, 2, 2, 2, 2], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(1.50000));
assertArrayEquals([2, 2, 2, 2, 2], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(2.50000));
assertArrayEquals([3, 3, 3, 3, 3], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(2.50001));
// Check infinity clamping.
assertArrayEquals([0, 0, 0, 0, 0], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(-Infinity));
assertArrayEquals([255, 255, 255, 255, 255], new Uint8ClampedArray([0, 0, 0, 0, 0]).fill(Infinity));
