// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --expose-externalize-string --turbolev --turbolev-regexp-quick-check

// The inline regexp quick check of global and sticky regexps: exec and test
// start at lastIndex, as does match unless the regexp is global. Sequential
// and cached external one-byte subjects are read inline, also through a slice
// or a ThinString.

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
  const sliced = ("0123456789ABCDEF" + s).slice(16);
  assertEquals(s, sliced);
  return sliced;
}

// Externalizes a copy. A string literal would be externalized in place, and
// with it every other use of that literal.
function external(s) {
  const ext =
      createExternalizableString(%FlattenString(s.slice(0, 1) + s.slice(1)));
  externalizeString(ext);
  return ext;
}

function externalSlice(s) {
  const sliced = external("0123456789ABCDEF" + s).slice(16);
  assertEquals(s, sliced);
  return sliced;
}

function externalTwoByte(s) {
  const ext = createExternalizableTwoByteString(s);
  externalizeString(ext);
  return ext;
}

// A ThinString that forwards to {s}.
function thin(s) {
  return %ConstructThinString(s);
}

// A SlicedString whose parent has become a ThinString.
function thinSlice(s) {
  const padded = "0123456789ABCDEF" + s;
  const copy = () => %FlattenString(padded.slice(0, 1) + padded.slice(1));
  const parent = copy();
  const sliced = parent.slice(16);
  // Of two equal strings, the second to be internalized forwards to the
  // first.
  %InternalizeString(copy());
  %InternalizeString(parent);
  assertEquals(s, sliced);
  return sliced;
}

const kSubjects = [
  ["seq", (s) => s],
  ["sliced", slice],
  ["external", external],
  ["external sliced", externalSlice],
  ["thin", thin],
  ["thin sliced", thinSlice],
];

function exec(re, s) {
  return re.exec(s);
}

function test(re, s) {
  return re.test(s);
}

function match(re, s) {
  return s.match(re);
}

(function testStickyExec() {
  // The mask covers four characters, the bitset the first one.
  for (const source of ["abcd", "a|b"]) {
    const re = new RegExp(source, "y");
    optimize(exec, [re, "xabcd"], [re, "zzzzz"]);
    assertQuickCheckRejects(re, "z");
    for (const [name, make] of kSubjects) {
      re.lastIndex = 1;
      assertEquals(1, exec(re, make("xabcd--------------")).index, name);
      assertEquals(source.length == 4 ? 5 : 2, re.lastIndex, name);
      re.lastIndex = 1;
      assertNull(exec(re, make("xzbcd--------------")), name);
      assertEquals(0, re.lastIndex, name);
      // Fewer than four characters are left.
      re.lastIndex = 17;
      assertNull(exec(re, make("-----------------zb")), name);
      assertEquals(0, re.lastIndex, name);
    }
  }
})();

(function testGlobalExec() {
  const re = /ab/g;
  optimize(exec, [re, "xab"], [re, "zzz"]);
  for (const [name, make] of kSubjects) {
    re.lastIndex = 0;
    assertEquals(17, exec(re, make("-----------------ab")).index, name);
    assertEquals(19, re.lastIndex, name);
    re.lastIndex = 18;
    assertNull(exec(re, make("-----------------ab")), name);
    assertEquals(0, re.lastIndex, name);
  }
})();

(function testLastIndexAtAndPastTheEnd() {
  const re = /a/y;
  optimize(exec, [re, "xa"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  re.lastIndex = 3;
  assertNull(exec(re, "aaa"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 4;
  assertNull(exec(re, "aaa"));
  assertEquals(0, re.lastIndex);
  re.lastIndex = 1;
  assertNull(exec(re, ""));
  assertEquals(0, re.lastIndex);
})();

(function testNonGlobalIgnoresLastIndex() {
  const re = /^ab/;
  optimize(exec, [re, "ab"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  for (const [name, make] of kSubjects) {
    re.lastIndex = 5;
    assertEquals(0, exec(re, make("ab-----------------")).index, name);
    assertNull(exec(re, make("xab----------------")), name);
    assertEquals(5, re.lastIndex, name);
  }
})();

(function testStickyTest() {
  const re = /abcd/y;
  optimize(test, [re, "xabcd"], [re, "zzzzz"]);
  assertQuickCheckRejects(re, "z");
  for (const [name, make] of kSubjects) {
    re.lastIndex = 1;
    assertTrue(test(re, make("xabcd--------------")), name);
    assertEquals(5, re.lastIndex, name);
    re.lastIndex = 1;
    assertFalse(test(re, make("xabxd--------------")), name);
    assertEquals(0, re.lastIndex, name);
  }
})();

(function testStickyMatch() {
  const re = /abcd/y;
  optimize(match, [re, "xabcd"], [re, "zzzzz"]);
  assertQuickCheckRejects(re, "z");
  for (const [name, make] of kSubjects) {
    re.lastIndex = 1;
    assertEquals(1, match(re, make("xabcd--------------")).index, name);
    assertEquals(5, re.lastIndex, name);
    re.lastIndex = 1;
    assertNull(match(re, make("xabxd--------------")), name);
    assertEquals(0, re.lastIndex, name);
    // A match at index 0 does not count.
    re.lastIndex = 1;
    assertNull(match(re, make("abcd---------------")), name);
    assertEquals(0, re.lastIndex, name);
    re.lastIndex = 20;
    assertNull(match(re, make("abcd---------------")), name);
    assertEquals(0, re.lastIndex, name);
  }
})();

// A global match starts at index 0 whatever lastIndex is, and leaves lastIndex
// at 0.
(function testGlobalMatch() {
  const re = /^abcd/g;
  optimize(match, [re, "abcd"], [re, "zzzzz"]);
  assertQuickCheckRejects(re, "z");
  for (const [name, make] of kSubjects) {
    re.lastIndex = 1;
    assertEquals(["abcd"], match(re, make("abcd---------------")), name);
    assertEquals(0, re.lastIndex, name);
    re.lastIndex = 1;
    assertNull(match(re, make("xabcd--------------")), name);
    assertEquals(0, re.lastIndex, name);
    re.lastIndex = 20;
    assertEquals(["abcd"], match(re, make("abcd---------------")), name);
    assertEquals(0, re.lastIndex, name);
    re.lastIndex = 20;
    assertNull(match(re, make("xabcd--------------")), name);
    assertEquals(0, re.lastIndex, name);
  }
})();

(function testGlobalStickyMatch() {
  const re = /ab/gy;
  optimize(match, [re, "abab"], [re, "zzzzz"]);
  assertQuickCheckRejects(re, "z");
  for (const [name, make] of kSubjects) {
    re.lastIndex = 1;
    assertEquals(["ab", "ab"], match(re, make("ababx--------------")), name);
    assertEquals(0, re.lastIndex, name);
    // The character at lastIndex passes the filters, the one at 0 does not.
    re.lastIndex = 1;
    assertNull(match(re, make("xab----------------")), name);
    assertEquals(0, re.lastIndex, name);
    re.lastIndex = 20;
    assertEquals(["ab"], match(re, make("ab-----------------")), name);
    assertEquals(0, re.lastIndex, name);
  }
})();

// A reject resets lastIndex, which a later load in the same function has to
// see.
(function testLastIndexLoadAfterReject() {
  const fs = [
    (re, s) => {
      const before = re.lastIndex;
      re.exec(s);
      return [before, re.lastIndex];
    },
    (re, s) => {
      const before = re.lastIndex;
      re.test(s);
      return [before, re.lastIndex];
    },
    (re, s) => {
      const before = re.lastIndex;
      s.match(re);
      return [before, re.lastIndex];
    },
  ];
  for (const f of fs) {
    const re = /abcd/y;
    optimize(f, [re, "abcd"], [re, "zzzz"]);
    assertQuickCheckRejects(re, "z");
    re.lastIndex = 1;
    assertEquals([1, 5], f(re, "xabcd"));
    re.lastIndex = 1;
    assertEquals([1, 0], f(re, "xzbcd"));
    assertOptimized(f);
  }
})();

// A short external string does not cache its character pointer where
// external pointers are indirect, and is left to the builtin.
(function testShortExternal() {
  const re = /ab/y;
  optimize(exec, [re, "xab"], [re, "zzz"]);
  assertQuickCheckRejects(re, "z");
  re.lastIndex = 1;
  assertEquals(1, exec(re, external("xab")).index);
  assertEquals(3, re.lastIndex);
  re.lastIndex = 1;
  assertNull(exec(re, external("xzb")));
  assertEquals(0, re.lastIndex);
})();

// A regexp literal that nothing has used yet is checked at index 0, whatever
// its flags.
(function testLiteral() {
  const f = (s) => s.match(/^abcd/);
  optimize(f, ["abcdefgh"], ["zzzzzzzz"]);
  assertQuickCheckRejects(/^abcd/, "z");
  for (const [name, make] of kSubjects) {
    assertEquals("abcd", f(make("abcd---------------"))[0], name);
    assertNull(f(make("abxd---------------")), name);
  }
  assertOptimized(f);
})();

// The one-byte filters do not hold for two-byte subjects, even where the
// character at lastIndex is one-byte.
(function testTwoByteIsNotFiltered() {
  const re = /aሴ|b/y;
  optimize(exec, [re, "xb"], [re, "xz"]);
  re.lastIndex = 1;
  assertEquals("aሴ", exec(re, "xaሴ")[0]);
  re.lastIndex = 1;
  assertEquals("aሴ", exec(re, externalTwoByte("xaሴ0123456789"))[0]);
  re.lastIndex = 1;
  assertEquals(
      "aሴ",
      exec(re, externalTwoByte("0123456789ABCDEF-aሴ--").slice(16))[0]);
})();
