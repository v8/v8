// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function TestGrow() {
  let array = [0,1,2,3];
  function f(e) {
    array[4] = 42;
    return e;
  }
  assertEquals(array.flatMap(f), [0,1,2,3]);
})();

(function TestShrink() {
  let array = [0,1,2,3];
  function f(e) {
    array.length = 3;
    return e;
  }
  assertEquals(array.flatMap(f), [0,1,2]);
})();
