// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-tostring

'use strict';

function getPropertyDescriptor(object, field, expectedDepth) {
  for (var depth = 0; depth < expectedDepth; depth++) {
    assertFalse(Object.hasOwnProperty(object, field));
    object = object.__proto__;
  }
  return Object.getOwnPropertyDescriptor(object, field);
}

function assertGetterName(expected, object, name, expectedDepth) {
  var descr = getPropertyDescriptor(object, name, expectedDepth);
  assertSame(expected, descr.get.name);
}


function assertSetterName(expected, object, name, indirect) {
  var descr = getPropertyDescriptor(object, name);
  assertSame(expected, descr.set.name);
}


assertGetterName('get byteLength', ArrayBuffer.prototype, 'byteLength');
assertGetterName('get size', Set.prototype, 'size');
assertGetterName('get size', Map.prototype, 'size');


let typedArrays = [
  Uint8Array,
  Int8Array,
  Uint16Array,
  Int16Array,
  Uint32Array,
  Int32Array,
  Float32Array,
  Float64Array,
  Uint8ClampedArray
];

for (let f of typedArrays) {
  assertGetterName('get buffer', f.prototype, 'buffer', 1);
  assertGetterName('get byteOffset', f.prototype, 'byteOffset', 1);
  assertGetterName('get byteLength', f.prototype, 'byteLength', 1);
  assertGetterName('get length', f.prototype, 'length', 1);
  assertGetterName('get [Symbol.toStringTag]', f.prototype, Symbol.toStringTag, 1);
}


assertGetterName('get buffer', DataView.prototype, 'buffer');
assertGetterName('get byteOffset', DataView.prototype, 'byteOffset');
assertGetterName('get byteLength', DataView.prototype, 'byteLength');


assertGetterName('get __proto__', Object.prototype, '__proto__');
assertSetterName('set __proto__', Object.prototype, '__proto__');
