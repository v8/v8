// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbolev-regexp-quick-check

// RegExp.prototype.exec on a receiver with the initial RegExp map lowers to
// RegExpPrototypeExecFast, behind the inline quick check of the regexp.

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

function exec(re, s) {
  return re.exec(s);
}

(function testResult() {
  const re = /(?<x>b)c/;
  optimize(exec, [re, "abc"], [re, "zz"]);
  const m = exec(re, "abc");
  assertEquals(["bc", "b"], [...m]);
  assertEquals(1, m.index);
  assertEquals("abc", m.input);
  assertEquals("b", m.groups.x);
  assertNull(exec(re, "zz"));
  assertOptimized(exec);
})();

(function testHasIndices() {
  const re = /b/d;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "ab"], [re, "zz"]);
  assertEquals([1, 2], f(re, "ab").indices[0]);
  assertNull(f(re, "zz"));
  assertOptimized(f);
})();

(function testNonGlobalIgnoresLastIndex() {
  const re = /ab/;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "xxab"], [re, "zz"]);
  re.lastIndex = 5;
  assertEquals(2, f(re, "xxab").index);
  assertEquals(5, re.lastIndex);
  assertNull(f(re, "zz"));
  assertEquals(5, re.lastIndex);
})();

(function testGlobalAdvancesAndResetsLastIndex() {
  const re = /a/g;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "aa"]);
  re.lastIndex = 0;
  assertEquals(0, f(re, "aa").index);
  assertEquals(1, re.lastIndex);
  assertEquals(1, f(re, "aa").index);
  assertEquals(2, re.lastIndex);
  assertNull(f(re, "aa"));
  assertEquals(0, re.lastIndex);
})();

(function testSticky() {
  const re = /a/y;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "ba"]);
  re.lastIndex = 0;
  assertNull(f(re, "ba"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 1;
  assertEquals(1, f(re, "ba").index);
  assertEquals(2, re.lastIndex);
})();

(function testNonSmiAndNegativeLastIndex() {
  const re = /a/g;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "aa"]);
  re.lastIndex = -1;
  assertEquals(0, f(re, "aa").index);
  re.lastIndex = 1.5;
  assertEquals(1, f(re, "aa").index);
})();

(function testArgumentIsCoercedToString() {
  const re = /12/;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "12"]);
  assertEquals("123", f(re, 123).input);
  assertNull(f(re, undefined));
})();

(function testMissingArgumentIsCoercedToUndefined() {
  const re = /undefined/;
  const f = (re) => re.exec();
  optimize(f, [re]);
  assertEquals("undefined", f(re)[0]);
})();

(function testQuickCheckLiteral() {
  function f(s) { return /^abcd/.exec(s); }
  optimize(f, ["abcd"], ["zzzz"]);
  assertQuickCheckRejects(/^abcd/, "z");
  assertEquals("abcd", f("abcdefgh")[0]);
  assertNull(f("abcx"));
  assertNull(f(""));
  assertOptimized(f);
})();

(function testQuickCheckGlobalAndStickyLiterals() {
  function g(s) { return /^a/g.exec(s); }
  optimize(g, ["a"], ["zzz"]);
  assertEquals("a", g("ab")[0]);
  assertNull(g("ba"));
  function y(s) {
    const re = /a/y;
    re.lastIndex = 1;
    return re.exec(s);
  }
  optimize(y, ["ba"], ["ab"]);
  assertEquals(1, y("ba").index);
  assertNull(y("ab"));
})();

(function testQuickCheckNonLiteral() {
  const re = /^a/;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "a"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  assertEquals("a", f(re, "ab")[0]);
  assertNull(f(re, "ba"));
  re.compile("^b");
  assertEquals("b", f(re, "ba")[0]);
  assertNull(f(re, "ab"));
})();

(function testQuickCheckNonLiteralGlobal() {
  const re = /^a/g;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "b"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  // A failed global exec resets lastIndex.
  re.lastIndex = 3;
  assertNull(f(re, "b"));
  assertEquals(0, re.lastIndex);
})();

(function testSubclass() {
  class MyRe extends RegExp {}
  const re = new MyRe("a");
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "a"], [re, "b"]);
  assertEquals("a", f(re, "a")[0]);
  assertNull(f(re, "b"));
})();

// A global or sticky regexp starts at lastIndex, which is where the quick
// check looks, and a failed exec resets lastIndex.
(function testQuickCheckAtLastIndex() {
  const re = /ab/y;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "xab"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  re.lastIndex = 1;
  assertEquals(1, f(re, "xab").index);
  assertEquals(3, re.lastIndex);
  re.lastIndex = 1;
  assertNull(f(re, "abb"));
  assertEquals(0, re.lastIndex);
  // At the end, nothing is left to look at, and past it the exec fails.
  re.lastIndex = 3;
  assertNull(f(re, "xab"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 4;
  assertNull(f(re, "xab"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 2;
  assertEquals(2, f(re, slice("--ab------------")).index);
  re.lastIndex = 2;
  assertNull(f(re, slice("--xb------------")));
  assertEquals(0, re.lastIndex);
  // Subjects the inline check does not read are left to the builtin.
  re.lastIndex = 1;
  assertEquals(1, f(re, "\u1234ab").index);
  re.lastIndex = 1;
  assertEquals(1, f(re, %ConstructConsString("xabcdefghijklm", "nopqrstuvwxyz")).index);
  re.lastIndex = 1;
  assertNull(f(re, %ConstructConsString("xzbcdefghijklm", "nopqrstuvwxyz")));
  assertEquals(0, re.lastIndex);
  assertOptimized(f);
})();

(function testQuickCheckAnchoredGlobalAtLastIndex() {
  const re = /^a/g;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "a"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  re.lastIndex = 1;
  assertNull(f(re, "aa"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 1;
  assertNull(f(re, "ab"));
  assertEquals(0, re.lastIndex);
  assertEquals(0, f(re, "ab").index);
  assertEquals(1, re.lastIndex);
})();

(function testQuickCheckNonGlobalIgnoresLastIndex() {
  const re = /^a/;
  const f = (re, s) => re.exec(s);
  optimize(f, [re, "a"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  re.lastIndex = 1;
  assertEquals(0, f(re, "ab").index);
  assertNull(f(re, "ba"));
  assertEquals(1, re.lastIndex);
})();
