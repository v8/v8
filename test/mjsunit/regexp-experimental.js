// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --enable-experimental-regexp-engine

function Test(regexp, subject, expectedResult, expectedLastIndex) {
  assertEquals(%RegexpTypeTag(regexp), "EXPERIMENTAL");
  var result = regexp.exec(subject);
  assertArrayEquals(expectedResult, result);
  assertEquals(expectedLastIndex, regexp.lastIndex);
}

// The empty regexp.
Test(new RegExp(""), "asdf", [""], 0);

// Plain patterns without special operators.
Test(/asdf1/, "123asdf1xyz", ["asdf1"], 0);
// Escaped operators, otherwise plain string:
Test(/\*\.\(\[\]\?/, "123*.([]?123", ["*.([]?"], 0);
// Some two byte values:
Test(/쁰d섊/, "123쁰d섊abc", ["쁰d섊"], 0);
// A pattern with surrogates but without unicode flag:
Test(/💩f/, "123💩f", ["💩f"], 0);

// Disjunctions.
Test(/asdf|123/, "xyz123asdf", ["123"], 0);
Test(/asdf|123|fj|f|a/, "da123", ["a"], 0);
Test(/|123/, "123", [""], 0);

// Character ranges.
Test(/[abc]/, "123asdf", ["a"], 0);
Test(/[0-9]/, "asdf123xyz", ["1"], 0);
Test(/[^0-9]/, "123!xyz", ["!"], 0);
Test(/\w\d/, "?a??a3!!!", ["a3"], 0);
// [💩] without unicode flag is a character range matching one of the two
// surrogate characters that make up 💩.  The leading surrogate is 0xD83D.
Test(/[💩]/, "f💩", [String.fromCodePoint(0xD83D)], 0);

// Greedy quantifier for 0 or more matches.
Test(/x*/, "asdfxk", [""], 0);
Test(/xx*a/, "xxa", ["xxa"], 0);
Test(/asdf*/, "aasdfffk", ["asdfff"], 0);

// Non-capturing groups and nested operators.
Test(/(?:)/, "asdf", [""], 0);
Test(/(?:asdf)/, "123asdfxyz", ["asdf"], 0);
Test(/(?:asdf)|123/, "xyz123asdf", ["123"], 0);
Test(/asdf(?:[0-9]|(?:xy|x)*)*/, "kkkasdf5xyx8xyyky", ["asdf5xyx8xy"], 0);

// The global flag.
Test(/asdf/g, "fjasdfkkasdf", ["asdf"], 6);
