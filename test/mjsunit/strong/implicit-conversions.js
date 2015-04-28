// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode --allow-natives-syntax

"use strict";

// TODO(conradw): Implement other strong operators
let strongBinops = [
  "-",
  "*",
  "/",
  "%",
  "|",
  "&",
  "^",
  "<<",
  ">>",
  ">>>",
];

let strongUnops = [
  "~",
  "+",
  "-"
];

let nonNumberValues = [
  "{}",
  "'foo'",
  "(function(){})",
  "[]",
  "'0'",
  "'NaN'",
  "(class Foo {})"
];

let numberValues = [
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
];

function sub_strong(x, y) {
  "use strong";
  return x - y;
}

function mul_strong(x, y) {
  "use strong";
  return x * y;
}

function div_strong(x, y) {
  "use strong";
  return x / y;
}

function mod_strong(x, y) {
  "use strong";
  return x % y;
}

function or_strong(x, y) {
  "use strong";
  return x | y;
}

function and_strong(x, y) {
  "use strong";
  return x & y;
}

function xor_strong(x, y) {
  "use strong";
  return x ^ y;
}

function shl_strong(x, y) {
  "use strong";
  return x << y;
}

function shr_strong(x, y) {
  "use strong";
  return x >> y;
}

function sar_strong(x, y) {
  "use strong";
  return x >>> y;
}

function typed_sub_strong(x, y) {
  "use strong";
  return (+x) - (+y);
}

function typed_mul_strong(x, y) {
  "use strong";
  return (+x) * (+y);
}

function typed_div_strong(x, y) {
  "use strong";
  return (+x) / (+y);
}

function typed_mod_strong(x, y) {
  "use strong";
  return (+x) % (+y);
}

function typed_or_strong(x, y) {
  "use strong";
  return (+x) | (+y);
}

function typed_and_strong(x, y) {
  "use strong";
  return (+x) & (+y);
}

function typed_xor_strong(x, y) {
  "use strong";
  return (+x) ^ (+y);
}

function typed_shl_strong(x, y) {
  "use strong";
  return (+x) << (+y);
}

function typed_shr_strong(x, y) {
  "use strong";
  return (+x) >> (+y);
}

function typed_sar_strong(x, y) {
  "use strong";
  return (+x) >>> (+y);
}

let strongFuncs = [sub_strong, mul_strong, div_strong, mod_strong, or_strong,
                   and_strong, xor_strong, shl_strong, shr_strong, sar_strong,
                   typed_sub_strong, typed_mul_strong, typed_div_strong,
                   typed_mod_strong, typed_or_strong,  typed_and_strong,
                   typed_xor_strong, typed_shl_strong, typed_shr_strong,
                   typed_sar_strong];

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

function assertStrongNonThrowBehaviour(expr) {
  assertEquals(eval(expr), eval("'use strong';" + expr));
  assertDoesNotThrow("'use strong'; " + expr + ";");
  assertDoesNotThrow("'use strong'; let v = " + expr + ";");
}

function assertStrongThrowBehaviour(expr) {
  assertDoesNotThrow("'use strict'; " + expr + ";");
  assertDoesNotThrow("'use strict'; let v = " + expr + ";");
  assertThrows("'use strong'; " + expr + ";", TypeError);
  assertThrows("'use strong'; let v = " + expr + ";", TypeError);
}

for (let op of strongBinops) {
  for (let v1 of numberValues) {
    let assignExpr = "foo " + op + "= " + v1 + ";";
    for (let v2 of numberValues) {
      assertDoesNotThrow("'use strong'; let foo = " + v2 + "; " + assignExpr);
      assertStrongNonThrowBehaviour("(" + v1 + op + v2 + ")");
    }
    for (let v2 of nonNumberValues) {
      assertThrows("'use strong'; let foo = " + v2 + "; " + assignExpr,
        TypeError);
      assertStrongThrowBehaviour("(" + v1 + op + v2 + ")");
    }
  }
  for (let v1 of nonNumberValues) {
    let assignExpr = "foo " + op + "= " + v1 + ";";
    for (let v2 of numberValues.concat(nonNumberValues)) {
      assertThrows("'use strong'; let foo = " + v2 + "; " + assignExpr,
        TypeError);
      assertStrongThrowBehaviour("(" + v1 + op + v2 + ")");
    }
  }
}

for (let op of strongUnops) {
  for (let value of numberValues) {
    assertStrongNonThrowBehaviour("(" + op + value + ")");
  }
  for (let value of nonNumberValues) {
    assertStrongThrowBehaviour("(" + op + value + ")");
  }
}

for (let func of strongFuncs) {
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

for (let func of strongFuncs) {
  try {
    let a = func(2, 3);
    let b = func(2, 3);
    assertTrue(a === b);
    %OptimizeFunctionOnNextCall(func);
    let c = func(2, "foo");
    assertUnreachable();
  } catch (e) {
    assertInstanceof(e, TypeError);
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
