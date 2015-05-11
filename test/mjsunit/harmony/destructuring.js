// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-destructuring

(function TestObjectLiteralPattern() {
  var { x : x, y : y } = { x : 1, y : 2 };
  assertEquals(1, x);
  assertEquals(2, y);

  var {z} = { z : 3 };
  assertEquals(3, z);
}());
