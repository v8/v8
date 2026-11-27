// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbolev-regexp-quick-check

// RegExp.prototype.test on a receiver with the initial RegExp map and an
// untouched RegExp.prototype.exec lowers to RegExpPrototypeTestFast. lastIndex
// semantics, argument coercion and a replaced exec must all be preserved.

function test(re, s) {
  return re.test(s);
}

(function testNonGlobalIgnoresLastIndex() {
  const re = /ab/;
  %PrepareFunctionForOptimization(test);
  assertTrue(test(re, "xxab"));
  assertFalse(test(re, "zz"));
  %OptimizeFunctionOnNextCall(test);
  re.lastIndex = 5;
  assertTrue(test(re, "xxab"));
  assertEquals(5, re.lastIndex);
  assertFalse(test(re, "zz"));
  assertEquals(5, re.lastIndex);
})();

(function testGlobalAdvancesAndResetsLastIndex() {
  const re = /a/g;
  const f = (re, s) => re.test(s);
  %PrepareFunctionForOptimization(f);
  re.lastIndex = 0;
  f(re, "aa");
  %OptimizeFunctionOnNextCall(f);
  re.lastIndex = 0;
  assertTrue(f(re, "aa"));
  assertEquals(1, re.lastIndex);
  assertTrue(f(re, "aa"));
  assertEquals(2, re.lastIndex);
  assertFalse(f(re, "aa"));
  assertEquals(0, re.lastIndex);
})();

(function testSticky() {
  const re = /a/y;
  const f = (re, s) => re.test(s);
  %PrepareFunctionForOptimization(f);
  re.lastIndex = 0;
  f(re, "ba");
  %OptimizeFunctionOnNextCall(f);
  re.lastIndex = 0;
  assertFalse(f(re, "ba"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 1;
  assertTrue(f(re, "ba"));
  assertEquals(2, re.lastIndex);
})();

(function testNonSmiAndNegativeLastIndex() {
  const re = /a/g;
  const f = (re, s) => re.test(s);
  %PrepareFunctionForOptimization(f);
  re.lastIndex = 0;
  f(re, "aa");
  %OptimizeFunctionOnNextCall(f);
  // Neither a negative nor a fractional lastIndex may take the fast path
  // silently; the results must still be correct.
  re.lastIndex = -1;
  assertTrue(f(re, "aa"));
  re.lastIndex = 1.5;
  assertTrue(f(re, "aa"));
})();

(function testArgumentIsCoercedToString() {
  const re = /12/;
  %PrepareFunctionForOptimization(test);
  assertTrue(test(re, "12"));
  %OptimizeFunctionOnNextCall(test);
  assertTrue(test(re, 123));
  assertFalse(test(re, undefined));
})();

(function testExtraArgumentsAreIgnored() {
  const re = /ab/;
  const f = (re, s, extra) => re.test(s, extra);
  %PrepareFunctionForOptimization(f);
  assertTrue(f(re, "xxab", 1));
  assertFalse(f(re, "zz", 1));
  %OptimizeFunctionOnNextCall(f);
  assertTrue(f(re, "xxab", 1));
  assertFalse(f(re, "zz", 1));
})();

(function testMissingArgumentIsCoercedToUndefined() {
  const re = /undefined/;
  const f = (re) => re.test();
  %PrepareFunctionForOptimization(f);
  assertTrue(f(re));
  %OptimizeFunctionOnNextCall(f);
  assertTrue(f(re));
})();

// The quick-check filters of the regexp are tested inline, and a subject they
// reject yields false without a call. The warmup calls compile the regexp,
// which is what builds the filters.
function optimize(f, ...warmup) {
  %PrepareFunctionForOptimization(f);
  for (const args of warmup) f(...args);
  %OptimizeFunctionOnNextCall(f);
}

// A missing filter costs performance, not correctness, so results alone cannot
// show that the inline check has anything to reject.
function assertQuickCheckRejects(re, c) {
  re.exec("");  // Compile.
  // The experimental engine does not build quick-check filters.
  if (%RegexpTypeTag(re) === "EXPERIMENTAL") return;
  assertTrue(%RegexpQuickCheckRejects(re, c), `${re} should reject '${c}'`);
}

function slice(s) {
  // A SlicedString, which needs at least SlicedString::kMinLength characters.
  const padded = "0123456789ABCDEF" + s;
  const sliced = padded.slice(16);
  assertEquals(s, sliced);
  return sliced;
}

(function testQuickCheckLiteralBitset() {
  function f(s) { return /^[a-c]x*/.test(s); }
  optimize(f, ["a"], ["zzz"]);
  assertQuickCheckRejects(/^[a-c]x*/, "d");
  assertTrue(f("axy"));
  assertFalse(f("dx"));
  assertFalse(f(""));
  assertFalse(f("\u1234a"));
  assertTrue(f("b\u1234"));
  assertFalse(f("z\u1234"));
  assertTrue(f(slice("cx--------------")));
  assertFalse(f(slice("-cx-------------")));
})();

(function testQuickCheckLiteralMask() {
  function f(s) { return /^abcd/.test(s); }
  optimize(f, ["abcd"], ["zzzz"]);
  assertQuickCheckRejects(/^abcd/, "z");
  assertTrue(f("abcdefgh"));
  assertFalse(f("abcx"));
  assertFalse(f("xbcd"));
  assertFalse(f("abc"));
  assertFalse(f(""));
  assertTrue(f(slice("abcd------------")));
  assertFalse(f(slice("abcx------------")));
})();

(function testQuickCheckGlobalAndStickyLiterals() {
  function g(s) { return /^a/g.test(s); }
  optimize(g, ["a"], ["zzz"]);
  assertTrue(g("ab"));
  assertFalse(g("ba"));
  function y(s) { return /a/y.test(s); }
  optimize(y, ["a"], ["zzz"]);
  assertTrue(y("ab"));
  assertFalse(y("ba"));
})();

(function testQuickCheckLiteralWithLastIndexStore() {
  function f(s) {
    const re = /a/y;
    re.lastIndex = 1;
    return re.test(s);
  }
  optimize(f, ["ba"], ["ab"]);
  assertTrue(f("ba"));
  assertFalse(f("ab"));
})();

(function testQuickCheckLiteralThatWasRecompiled() {
  function f(s) {
    const re = /^a/;
    re.compile("^b");
    return re.test(s);
  }
  optimize(f, ["a"], ["b"]);
  assertFalse(f("a"));
  assertTrue(f("b"));
})();

(function testQuickCheckNonLiteral() {
  const re = /^a/;
  const f = (re, s) => re.test(s);
  optimize(f, [re, "a"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  assertTrue(f(re, "ab"));
  assertFalse(f(re, "ba"));
  re.lastIndex = 5;
  assertFalse(f(re, "ba"));
  assertEquals(5, re.lastIndex);
  re.compile("^b");
  assertTrue(f(re, "ba"));
  assertFalse(f(re, "ab"));
})();

(function testQuickCheckNonLiteralGlobalAndSticky() {
  const f = (re, s) => re.test(s);
  const g = /^a/g;
  optimize(f, [g, "b"], [g, "zzz"]);
  assertQuickCheckRejects(g, "z");
  // A failed global test resets lastIndex.
  g.lastIndex = 3;
  assertFalse(f(g, "b"));
  assertEquals(0, g.lastIndex);
  const y = /a/y;
  // A sticky test starts at lastIndex.
  y.lastIndex = 1;
  assertTrue(f(y, "ba"));
  assertEquals(2, y.lastIndex);
  y.lastIndex = 1;
  assertFalse(f(y, "ab"));
  assertEquals(0, y.lastIndex);
})();

// A global or sticky regexp starts at lastIndex, which is where the quick
// check looks, and a failed test resets lastIndex.
(function testQuickCheckAtLastIndex() {
  const re = /ab/y;
  const f = (re, s) => re.test(s);
  optimize(f, [re, "xab"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  re.lastIndex = 1;
  assertTrue(f(re, "xab"));
  assertEquals(3, re.lastIndex);
  re.lastIndex = 1;
  assertFalse(f(re, "abb"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 3;
  assertFalse(f(re, "xab"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 4;
  assertFalse(f(re, "xab"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 2;
  assertFalse(f(re, slice("--xb------------")));
  assertEquals(0, re.lastIndex);
  // Subjects the inline check does not read are left to the builtin.
  re.lastIndex = 1;
  assertTrue(f(re, "\u1234ab"));
  re.lastIndex = 1;
  assertTrue(f(re, %ConstructConsString("xabcdefghijklm", "nopqrstuvwxyz")));
  re.lastIndex = 1;
  assertFalse(f(re, %ConstructConsString("xzbcdefghijklm", "nopqrstuvwxyz")));
  assertEquals(0, re.lastIndex);
  assertOptimized(f);
})();

(function testReplacedExecIsHonoured() {
  const re = /a/;
  const f = (re, s) => re.test(s);
  %PrepareFunctionForOptimization(f);
  assertTrue(f(re, "a"));
  %OptimizeFunctionOnNextCall(f);
  assertTrue(f(re, "a"));
  const originalExec = RegExp.prototype.exec;
  let called = 0;
  RegExp.prototype.exec = function (s) { called++; return null; };
  assertFalse(f(re, "a"));
  assertEquals(1, called);
  RegExp.prototype.exec = originalExec;
})();

(function testSubclassWithOwnExec() {
  class MyRe extends RegExp { exec(s) { return null; } }
  const re = new MyRe("a");
  const f = (re, s) => re.test(s);
  %PrepareFunctionForOptimization(f);
  assertFalse(f(re, "a"));
  %OptimizeFunctionOnNextCall(f);
  assertFalse(f(re, "a"));
})();
