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


  var sum = 0;
  for(var {z} = { z : 3 }; z != 0; z--) {
    sum += z;
  }
  assertEquals(6, sum);
}());


(function TestObjectLiteralPatternLexical() {
  'use strict';
  let { x : x, y : y } = { x : 1, y : 2 };
  assertEquals(1, x);
  assertEquals(2, y);

  let {z} = { z : 3 };
  assertEquals(3, z);


  let sum = 0;
  for(let {x, z} = { x : 0, z : 3 }; z != 0; z--) {
    assertEquals(0, x);
    sum += z;
  }
  assertEquals(6, sum);
}());


(function TestObjectLiteralPatternLexicalConst() {
  'use strict';
  const { x : x, y : y } = { x : 1, y : 2 };
  assertEquals(1, x);
  assertEquals(2, y);

  const {z} = { z : 3 };
  assertEquals(3, z);


  for(const {x, z} = { x : 0, z : 3 }; z != 3 || x != 0;) {
    assertTrue(false);
  }
}());
