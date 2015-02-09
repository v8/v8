// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --use-osr

function f() {
  var sum = 0;
  for (var i = 0; i < 1000000; i++) {
    var x = i + 2;
    var y = x + 5;
    var z = y + 3;
    sum += z;
  }
  return sum;
}


for (var i = 0; i < 2; i++) {
  assertEquals(500009500000, f());
}
