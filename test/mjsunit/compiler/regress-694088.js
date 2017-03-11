// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function () {
  var buffer = new ArrayBuffer(8);
  var i32 = new Int32Array(buffer);
  var f64 = new Float64Array(buffer);

  function foo() {
    f64[0] = 1;
    var x = f64[0];
    return i32[0];
  }
  assertEquals(0, foo());
})();

(function () {
  var buffer = new ArrayBuffer(8);
  var i16 = new Int16Array(buffer);
  var i32 = new Int32Array(buffer);

  function foo() {
    i32[0] = 0x10001;
    var x = i32[0];
    return i16[0];
  }
  assertEquals(1, foo());
})();
