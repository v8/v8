// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-change-array-by-copy

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function TestSurface() {
  for (let TA of ctors) {
    assertEquals(TA.prototype.toReversed.length, 0);
    assertEquals(TA.prototype.toReversed.name, "toReversed");
  }
})();

(function TestBasic() {
  for (let TA of ctors) {
    let a = new TA(4);
    for (let i = 0; i < 4; i++) {
      a[i] = i + "";
    }
    let r = a.toReversed();
    for (let i = 0; i < 4; i++) {
      assertEquals(a[i], r[4-i-1]);
    }
    assertFalse(a === r);
  }
})();

(function TestNoSpecies() {
  class MyUint8Array extends Uint8Array {
    static get [Symbol.species]() { return MyUint8Array; }
  }
  assertEquals(Uint8Array, (new MyUint8Array()).toReversed().constructor);
})();
