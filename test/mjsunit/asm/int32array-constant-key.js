// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function Module(stdlib, foreign, heap) {
  "use asm";
  var MEM32 = new stdlib.Int32Array(heap);
  function load0() {
    return MEM32[0];
  }
  function load4() {
    return MEM32[4];
  }
  function store0(v) {
    MEM32[0] = v;
  }
  function store4(v) {
    MEM32[4] = v;
  }
  return { load0: load0, store0: store0, load4: load4, store4: store4 };
}

var m = Module(this, {}, new ArrayBuffer(4));

assertEquals(0, m.load0());
assertEquals(undefined, m.load4());
m.store0(0x12345678);
assertEquals(0x12345678, m.load0());
assertEquals(undefined, m.load4());
m.store4(43);
assertEquals(0x12345678, m.load0());
assertEquals(undefined, m.load4());
