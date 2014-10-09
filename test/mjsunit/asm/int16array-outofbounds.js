// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function Module(stdlib, foreign, heap) {
  "use asm";
  var MEM16 = new stdlib.Int16Array(heap);
  function load(i) {
    i = i|0;
    i = MEM16[i >> 1] | 0;
    return i;
  }
  function store(i, v) {
    i = i|0;
    v = v|0;
    MEM16[i >> 1] = v;
  }
  return { load: load, store: store };
}

var m = Module(this, {}, new ArrayBuffer(2));

m.store(0, 32767);
for (var i = 1; i < 64; ++i) {
  m.store(i * 2 * 32 * 1024, i);
}
assertEquals(32767, m.load(0));
for (var i = 1; i < 64; ++i) {
  assertEquals(0, m.load(i * 2 * 32 * 1024));
}
