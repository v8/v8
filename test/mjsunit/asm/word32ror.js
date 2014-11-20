// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var stdlib = {};
var foreign = {};
var heap = new ArrayBuffer(64 * 1024);

var rol = (function Module(stdlib, foreign, heap) {
  "use asm";
  function rol(x, y) {
    x = x | 0;
    y = y | 0;
    return (x << y) | (x >>> (32 - y));
  }
  return { rol: rol };
})(stdlib, foreign, heap).rol;

assertEquals(10, rol(10, 0));
assertEquals(2, rol(1, 1));
assertEquals(0x40000000, rol(1, 30));
assertEquals(-0x80000000, rol(1, 31));
