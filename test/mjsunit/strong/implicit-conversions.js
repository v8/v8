// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode --allow-natives-syntax

'use strict';

// TODO(conradw): Implement other strong operators
let strong_arith = [
    "-",
    "*",
    "/",
    "%"
]

let nonnumber_values = [
    "{}",
    "'foo'",
    "(function(){})",
    "[]",
    "'0'",
    "'NaN'",
    "(class Foo {})"
]

let number_values = [
    "0",
    "(-0)",
    "1",
    "0.79",
    "(-0.79)",
    "4294967295",
    "4294967296",
    "(-4294967295)",
    "(-4294967296)",
    "9999999999999",
    "(-9999999999999)",
    "1.5e10",
    "(-1.5e10)",
    "0xFFF",
    "(-0xFFF)",
    "NaN",
    "Infinity",
    "(-Infinity)"
]

function sub_strong(x, y) {
  "use strong";
  let v = x - y;
  return v;
}

function mul_strong(x, y) {
  "use strong";
  let v = x * y;
  return v;
}

function div_strong(x, y) {
  "use strong";
  let v = x / y;
  return v;
}

function mod_strong(x, y) {
  "use strong";
  let v = x % y;
  return v;
}

let strong_funcs = [sub_strong, mul_strong, div_strong, mod_strong];

function inline_sub_strong(x, y) {
  "use strong";
  let v = x - y;
  return v;
}

function inline_outer(x, y) {
  return inline_sub_strong(x, y);
}

function inline_sub(x, y) {
  let v = x - y;
  return v;
}

function inline_outer_strong(x, y) {
  "use strong";
  return inline_sub(x, y);
}

for (let op of strong_arith) {
  for (let left of number_values) {
    for (let right of number_values) {
      let expr = "(" + left + op + right + ")";
      assertEquals(eval(expr), eval("'use strong';" + expr));
      assertDoesNotThrow("'use strong'; " + expr + ";");
      assertDoesNotThrow("'use strong'; let v = " + expr + ";");
    }
  }
  for (let left of number_values) {
    for (let right of nonnumber_values) {
      let expr = "(" + left + op + right + ")";
      assertDoesNotThrow("'use strict'; " + expr + ";");
      assertDoesNotThrow("'use strict'; let v = " + expr + ";");
      assertThrows("'use strong'; " + expr + ";", TypeError);
      assertThrows("'use strong'; let v = " + expr + ";", TypeError);
    }
  }
  for (let left of nonnumber_values) {
    for (let right of number_values.concat(nonnumber_values)) {
      let expr = "(" + left + op + right + ")";
      assertDoesNotThrow("'use strict'; " + expr + ";");
      assertDoesNotThrow("'use strict'; let v = " + expr + ";");
      assertThrows("'use strong'; " + expr + ";", TypeError);
      assertThrows("'use strong'; let v = " + expr + ";", TypeError);
    }
  }
}

for (let func of strong_funcs) {
  let a = func(4, 5);
  let b = func(4, 5);
  assertTrue(a === b);
  %OptimizeFunctionOnNextCall(func);
  let c = func(4, 5);
  assertOptimized(func);
  assertTrue(b === c);
  %DeoptimizeFunction(func);
  let d = func(4, 5);
  assertTrue(c === d);
  %DeoptimizeFunction(func);
  %ClearFunctionTypeFeedback(func);
}

for (let func of strong_funcs) {
  try {
    let a = func(2, 3);
    let b = func(2, 3);
    assertTrue(a === b);
    %OptimizeFunctionOnNextCall(func);
    let c = func(2, "foo");
    assertUnreachable();
  } catch(e) {
    assertTrue(e instanceof TypeError);
    assertUnoptimized(func);
    assertThrows(function(){func(2, "foo");}, TypeError);
    assertDoesNotThrow(function(){func(2, 3);});
  }
}

assertThrows(function(){inline_outer(1, {})}, TypeError);
for (var i = 0; i < 100; i++) {
  inline_outer(1, 2);
}
assertThrows(function(){inline_outer(1, {})}, TypeError);

assertDoesNotThrow(function(){inline_outer_strong(1, {})});
for (var i = 0; i < 100; i++) {
  inline_outer_strong(1, 2);
}
assertDoesNotThrow(function(){inline_outer_strong(1, {})});
