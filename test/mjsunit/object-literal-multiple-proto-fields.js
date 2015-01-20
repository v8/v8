// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var p1 = {};
var p2 = {};
var p3 = {};
var x = 0;
var y = 0;
var z = 0;
var o = {
  __proto__: (x++, p1),
  __proto__: (y++, p2),
  __proto__: (z++, p3)
};
assertEquals(1, x);
assertEquals(1, y);
assertEquals(1, z);
assertEquals(Object.getPrototypeOf(o), p3);
