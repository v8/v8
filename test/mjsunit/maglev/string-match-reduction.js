// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --turbolev-regexp-quick-check

// String.prototype.match with a fast regexp lowers to RegExpMatchFast. The
// regexp's quick-check filters are tested inline first, and a subject they
// reject yields null without a call.

// The warmup calls also compile the regexp, which is what builds its filters.
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

(function testLiteralBitset() {
  function f(s) { return s.match(/^[a-c]x*/); }
  optimize(f, ["a"], ["zzz"]);
  assertQuickCheckRejects(/^[a-c]x*/, "d");
  assertEquals(["ax"], [...f("axy")]);
  assertEquals(0, f("axy").index);
  assertEquals("axy", f("axy").input);
  assertNull(f("dx"));
  assertNull(f(""));
  assertNull(f("ሴa"));
  assertEquals(["b"], [...f("bሴ")]);
  assertNull(f("zሴ"));
})();

(function testLiteralMask() {
  function f(s) { return s.match(/^abcd/); }
  optimize(f, ["abcd"], ["zzzz"]);
  assertQuickCheckRejects(/^abcd/, "x");
  assertEquals(["abcd"], [...f("abcdefgh")]);
  assertNull(f("abcx"));
  assertNull(f("xbcd"));
  assertNull(f("abc"));
  assertNull(f("a"));
  assertNull(f(""));
  assertEquals(["abcd"], [...f(slice("abcd------------"))]);
  assertNull(f(slice("abcx------------")));
  assertNull(f(slice("abc-abcd--------").slice(0, 3)));
})();

(function testSubjectShapes() {
  function f(s) { return s.match(/^[a-c]+/); }
  optimize(f, ["a"], ["zzz"]);
  assertQuickCheckRejects(/^[a-c]+/, "-");
  const long = "abcabcabcabcabcabcabcabc";
  // Sliced, one-byte.
  assertEquals(["cabcabc"], [...f(slice("cabcabc-----------"))]);
  assertNull(f(slice("-cabcabc-----------")));
  // Sliced, two-byte parent.
  assertEquals(["abcabc"], [...f(("ሴሴ" + long + "ሴ").slice(20))]);
  assertNull(f(("ሴሴ" + long + "ሴ").slice(1)));
  // Cons, flat and not.
  const cons = long + "-" + long;
  assertEquals([long], [...f(cons)]);
  assertEquals([long], [...f(cons)]);
  assertNull(f("-" + long + long));
  // Thin.
  const thin = "c" + long + "-";
  const o = {};
  o[thin] = 1;
  assertEquals(["c" + long], [...f(thin)]);
  const thin2 = "-" + long + "c";
  o[thin2] = 1;
  assertNull(f(thin2));
})();

(function testTwoByteSubjectIsNotFiltered() {
  // The filters come from the one-byte code, which drops alternatives that
  // need a two-byte character. They say nothing about two-byte subjects.
  function f(s) { return s.match(/^(aሴ|b)/); }
  optimize(f, ["b"], ["a"], ["zzz"]);
  assertNull(f("a"));
  assertNull(f("ax"));
  assertEquals("aሴ", f("aሴ")[0]);
  assertEquals("aሴ", f(("----------------aሴ-----").slice(16))[0]);
  assertEquals("b", f("bሴ")[0]);
})();

(function testOneByteFiltersOfTwoByteOnlyPattern() {
  // No one-byte subject can match, so the filters reject every character.
  function f(s) { return s.match(/^[\u0100-\u01ff]x/); }
  optimize(f, ["\u0161x"], ["ax"]);
  assertNull(f("ax"));
  assertEquals("\u0161x", f("\u0161x")[0]);
  assertEquals("\u0161x", f(slice("\u0161x--------------"))[0]);
})();

(function testNoArguments() {
  function f(s) { return s.match(); }
  optimize(f, ["a"], ["b"]);
  assertEquals([""], [...f("a")]);
})();

(function testCaseInsensitive() {
  function f(s) { return s.match(/^k/i); }
  optimize(f, ["k"], ["zzz"]);
  assertEquals("K", f("K")[0]);
  assertNull(f("j"));
  // KELVIN SIGN folds to k only in unicode mode.
  assertNull(f(String.fromCharCode(0x212a)));
  function u(s) { return s.match(/^k/iu); }
  optimize(u, ["k"], ["zzz"]);
  assertEquals("K", u("K")[0]);
  assertEquals(0x212a, u(String.fromCharCode(0x212a))[0].charCodeAt(0));
  assertNull(u("j"));
})();

(function testUnanchoredLiteral() {
  function f(s) { return s.match(/b+/); }
  optimize(f, ["abb"], ["zzz"]);
  assertEquals(["bb"], [...f("abb")]);
  assertEquals(1, f("abb").index);
  assertNull(f("a"));
})();

(function testGlobalAndStickyLiterals() {
  function g(s) { return s.match(/^a|b/g); }
  optimize(g, ["ab"], ["zzz"]);
  assertEquals(["a", "b", "b"], g("abb"));
  assertEquals(["b"], g("xb"));
  assertNull(g("x"));
  function y(s) { return s.match(/a/y); }
  optimize(y, ["a"], ["zzz"]);
  assertEquals(["a"], [...y("ab")]);
  assertNull(y("ba"));
})();

(function testLiteralThatWasRecompiled() {
  function f(s) {
    const re = /^a/;
    re.compile("^b");
    return s.match(re);
  }
  optimize(f, ["a"], ["b"]);
  assertNull(f("a"));
  assertEquals(["b"], [...f("b")]);
})();

(function testLiteralThatWasRecompiledInALoop() {
  function f(s, n) {
    const re = /^a/;
    let result;
    for (let i = 0; i < n; i++) {
      result = s.match(re);
      re.compile("^b");
    }
    return result;
  }
  optimize(f, ["a", 2], ["b", 2]);
  assertEquals(["a"], [...f("a", 1)]);
  assertNull(f("a", 2));
  assertEquals(["b"], [...f("b", 2)]);
})();

(function testLiteralWithLastIndexStore() {
  let calls = 0;
  const index = { valueOf() { calls++; return 0; } };
  function f(s) {
    const re = /^a/;
    re.lastIndex = index;
    return s.match(re);
  }
  optimize(f, ["a"], ["b"]);
  calls = 0;
  assertNull(f("b"));
  assertEquals(1, calls);
  assertEquals(["a"], [...f("a")]);
  assertEquals(2, calls);
})();

(function testLiteralWithSmiLastIndexStore() {
  function f(s) {
    const re = /a/y;
    re.lastIndex = 1;
    return s.match(re);
  }
  optimize(f, ["ba"], ["ab"]);
  assertEquals(["a"], [...f("ba")]);
  assertNull(f("ab"));
})();

(function testNonLiteralLastIndex() {
  function f(s, re) { return s.match(re); }
  const re = /^a/;
  optimize(f, ["a", re], ["b", re]);
  re.lastIndex = 5;
  assertEquals(["a"], [...f("a", re)]);
  assertNull(f("b", re));
  assertEquals(5, re.lastIndex);
  let calls = 0;
  re.lastIndex = { valueOf() { calls++; return 0; } };
  assertNull(f("b", re));
  assertEquals(1, calls);
})();

(function testNonStringReceiver() {
  function f(s) { return String.prototype.match.call(s, /^1/); }
  optimize(f, ["1"], ["2"]);
  assertEquals(["1"], [...f(12)]);
  assertNull(f(2));
  assertEquals(["1"], [...f({ toString() { return "1x"; } })]);
  assertThrows(() => f(null), TypeError);
  assertThrows(() => f(undefined), TypeError);
})();

(function testNonRegExpArgument() {
  function f(s, re) { return s.match(re); }
  optimize(f, ["a", /^a/], ["b", /^a/]);
  assertEquals(["a"], [...f("a", "a")]);
  assertEquals([""], [...f("a", undefined)]);
  assertEquals("custom", f("a", { [Symbol.match]() { return "custom"; } }));
  class R extends RegExp {}
  assertEquals(["a"], [...f("a", new R("^a"))]);
  const own = /^a/;
  own.exec = () => null;
  assertNull(f("a", own));
})();

(function testNonLiteral() {
  function f(s, re) { return s.match(re); }
  const re = /^[a-c]x*/;
  optimize(f, ["a", re], ["zzz", re]);
  assertQuickCheckRejects(re, "d");
  assertEquals(["ax"], [...f("axy", re)]);
  assertNull(f("dx", re));
  assertNull(f("", re));
  assertEquals(["cx"], [...f(slice("cx---------------"), re)]);
  assertNull(f(slice("-cx---------------"), re));
  assertEquals(["b"], [...f("b\u1234", re)]);
  assertNull(f("z\u1234", re));
  // A regexp that was never executed has no filters yet.
  assertNull(f("zzz", /^[a-c]x*/));
  assertEquals(["a"], [...f("a", /^[a-c]x*/)]);
  // Neither has an atom.
  assertNull(f("zzz", /abc/));
  assertEquals(["abc"], [...f("zabc", /abc/)]);
})();

(function testConstantRegExp() {
  const re = /^[a-c]x*/;
  function f(s) { return s.match(re); }
  optimize(f, ["a"], ["zzz"]);
  assertEquals(["ax"], [...f("axy")]);
  assertNull(f("dx"));
  re.compile("^d");
  assertEquals(["d"], [...f("dx")]);
  assertNull(f("ax"));
})();

(function testConstantNonRegExp() {
  const pattern = "a";
  function f(s) { return s.match(pattern); }
  optimize(f, ["a"], ["zzz"]);
  assertEquals(["a"], [...f("ba")]);
  assertNull(f("b"));
})();

(function testNonLiteralGlobal() {
  function f(s, re) { return s.match(re); }
  const re = /^a/g;
  optimize(f, ["a", re], ["b", re]);
  assertQuickCheckRejects(re, "b");
  // A global match resets lastIndex even if nothing matches.
  re.lastIndex = 3;
  assertNull(f("b", re));
  assertEquals(0, re.lastIndex);
  assertEquals(["a"], f("ab", re));
})();

(function testNonLiteralSticky() {
  function f(s, re) { return s.match(re); }
  const re = /a/y;
  optimize(f, ["a", re], ["b", re]);
  assertQuickCheckRejects(re, "b");
  // A sticky match starts at lastIndex.
  re.lastIndex = 1;
  assertEquals(["a"], [...f("ba", re)]);
  assertEquals(2, re.lastIndex);
  re.lastIndex = 1;
  assertNull(f("ab", re));
  assertEquals(0, re.lastIndex);
})();

(function testNonLiteralRecompiled() {
  function f(s, re) { return s.match(re); }
  const re = /^a/;
  optimize(f, ["a", re], ["b", re]);
  assertQuickCheckRejects(re, "b");
  assertNull(f("b", re));
  re.compile("^b");
  assertEquals(["b"], [...f("b", re)]);
  assertNull(f("a", re));
  re.compile("a", "y");
  re.lastIndex = 1;
  assertEquals(["a"], [...f("ba", re)]);
  re.compile("^a", "g");
  re.lastIndex = 3;
  assertNull(f("b", re));
  assertEquals(0, re.lastIndex);
})();
