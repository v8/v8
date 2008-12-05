// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

function testEscape(str, regex) {
  assertEquals("foo:bar:baz", str.split(regex).join(":"));
}

testEscape("foo\nbar\nbaz", /\n/);
testEscape("foo bar baz", /\s/);
testEscape("foo\tbar\tbaz", /\s/);
testEscape("foo-bar-baz", /\u002D/);

// Test containing null char in regexp.
var s = '[' + String.fromCharCode(0) + ']';
var re = new RegExp(s);
assertEquals(s.match(re).length, 1);
assertEquals(s.match(re)[0], String.fromCharCode(0));

// Test strings containing all line separators
s = 'aA\nbB\rcC\r\ndD\u2028eE\u2029fF';
re = /^./gm; // any non-newline character at the beginning of a line
var result = s.match(re);
assertEquals(result.length, 6);
assertEquals(result[0], 'a');
assertEquals(result[1], 'b');
assertEquals(result[2], 'c');
assertEquals(result[3], 'd');
assertEquals(result[4], 'e');
assertEquals(result[5], 'f');

re = /.$/gm; // any non-newline character at the end of a line
result = s.match(re);
assertEquals(result.length, 6);
assertEquals(result[0], 'A');
assertEquals(result[1], 'B');
assertEquals(result[2], 'C');
assertEquals(result[3], 'D');
assertEquals(result[4], 'E');
assertEquals(result[5], 'F');

re = /^[^]/gm; // *any* character at the beginning of a line
result = s.match(re);
assertEquals(result.length, 7);
assertEquals(result[0], 'a');
assertEquals(result[1], 'b');
assertEquals(result[2], 'c');
assertEquals(result[3], '\n');
assertEquals(result[4], 'd');
assertEquals(result[5], 'e');
assertEquals(result[6], 'f');

re = /[^]$/gm; // *any* character at the end of a line
result = s.match(re);
assertEquals(result.length, 7);
assertEquals(result[0], 'A');
assertEquals(result[1], 'B');
assertEquals(result[2], 'C');
assertEquals(result[3], '\r');
assertEquals(result[4], 'D');
assertEquals(result[5], 'E');
assertEquals(result[6], 'F');

// Some tests from the Mozilla tests, where our behavior differs from
// SpiderMonkey.
// From ecma_3/RegExp/regress-334158.js
assertTrue(/\ca/.test( "\x01" ));
assertFalse(/\ca/.test( "\\ca" ));
// Passes in KJS, fails in IrregularExpressions.
// See http://code.google.com/p/v8/issues/detail?id=152
//assertTrue(/\c[a/]/.test( "\x1ba/]" ));


// Test that we handle \s and \S correctly inside some bizarre
// character classes.
re = /[\s-:]/;
assertTrue(re.test('-'));
assertTrue(re.test(':'));
assertTrue(re.test(' '));
assertTrue(re.test('\t'));
assertTrue(re.test('\n'));
assertFalse(re.test('a'));
assertFalse(re.test('Z'));

re = /[\S-:]/;
assertTrue(re.test('-'));
assertTrue(re.test(':'));
assertFalse(re.test(' '));
assertFalse(re.test('\t'));
assertFalse(re.test('\n'));
assertTrue(re.test('a'));
assertTrue(re.test('Z'));

re = /[^\s-:]/;
assertFalse(re.test('-'));
assertFalse(re.test(':'));
assertFalse(re.test(' '));
assertFalse(re.test('\t'));
assertFalse(re.test('\n'));
assertTrue(re.test('a'));
assertTrue(re.test('Z'));

re = /[^\S-:]/;
assertFalse(re.test('-'));
assertFalse(re.test(':'));
assertTrue(re.test(' '));
assertTrue(re.test('\t'));
assertTrue(re.test('\n'));
assertFalse(re.test('a'));
assertFalse(re.test('Z'));

re = /[\s]/;
assertFalse(re.test('-'));
assertFalse(re.test(':'));
assertTrue(re.test(' '));
assertTrue(re.test('\t'));
assertTrue(re.test('\n'));
assertFalse(re.test('a'));
assertFalse(re.test('Z'));

re = /[^\s]/;
assertTrue(re.test('-'));
assertTrue(re.test(':'));
assertFalse(re.test(' '));
assertFalse(re.test('\t'));
assertFalse(re.test('\n'));
assertTrue(re.test('a'));
assertTrue(re.test('Z'));

re = /[\S]/;
assertTrue(re.test('-'));
assertTrue(re.test(':'));
assertFalse(re.test(' '));
assertFalse(re.test('\t'));
assertFalse(re.test('\n'));
assertTrue(re.test('a'));
assertTrue(re.test('Z'));

re = /[^\S]/;
assertFalse(re.test('-'));
assertFalse(re.test(':'));
assertTrue(re.test(' '));
assertTrue(re.test('\t'));
assertTrue(re.test('\n'));
assertFalse(re.test('a'));
assertFalse(re.test('Z'));

re = /[\s\S]/;
assertTrue(re.test('-'));
assertTrue(re.test(':'));
assertTrue(re.test(' '));
assertTrue(re.test('\t'));
assertTrue(re.test('\n'));
assertTrue(re.test('a'));
assertTrue(re.test('Z'));

re = /[^\s\S]/;
assertFalse(re.test('-'));
assertFalse(re.test(':'));
assertFalse(re.test(' '));
assertFalse(re.test('\t'));
assertFalse(re.test('\n'));
assertFalse(re.test('a'));
assertFalse(re.test('Z'));

// Test beginning and end of line assertions with or without the
// multiline flag.
re = /^\d+/;
assertFalse(re.test("asdf\n123"));
re = /^\d+/m;
assertTrue(re.test("asdf\n123"));

re = /\d+$/;
assertFalse(re.test("123\nasdf"));
re = /\d+$/m;
assertTrue(re.test("123\nasdf"));

// Test that empty matches are handled correctly for multiline global
// regexps.
re = /^(.*)/mg;
assertEquals(3, "a\n\rb".match(re).length);
assertEquals("*a\n*b\r*c\n*\r*d\r*\n*e", "a\nb\rc\n\rd\r\ne".replace(re, "*$1"));

// Test that empty matches advance one character
re = new RegExp("", "g");
assertEquals("xAx", "A".replace(re, "x"));
assertEquals(3, String.fromCharCode(161).replace(re, "x").length);

// Test that we match the KJS behavior with regard to undefined constructor
// arguments:
re = new RegExp();
// KJS actually shows this as '//'.  Here we match the Firefox behavior (ie,
// giving a syntactically legal regexp literal).
assertEquals('/(?:)/', re.toString());
re = new RegExp(void 0);
assertEquals('/(?:)/', re.toString());
re.compile();
assertEquals('/(?:)/', re.toString());
re.compile(void 0);
assertEquals('/undefined/', re.toString());


// Check for lazy RegExp literal creation
function lazyLiteral(doit) {
  if (doit) return "".replace(/foo(/gi, "");
  return true;
}

assertTrue(lazyLiteral(false));
assertThrows("lazyLiteral(true)");

// Check $01 and $10
re = new RegExp("(.)(.)(.)(.)(.)(.)(.)(.)(.)(.)");
assertEquals("t", "123456789t".replace(re, "$10"), "$10");
assertEquals("15", "123456789t".replace(re, "$15"), "$10");
assertEquals("1", "123456789t".replace(re, "$01"), "$01");
assertEquals("$001", "123456789t".replace(re, "$001"), "$001");
re = new RegExp("foo(.)");
assertEquals("bar$0", "foox".replace(re, "bar$0"), "$0");
assertEquals("bar$00", "foox".replace(re, "bar$00"), "$00");
assertEquals("bar$000", "foox".replace(re, "bar$000"), "$000");
assertEquals("barx", "foox".replace(re, "bar$01"), "$01 2");
assertEquals("barx5", "foox".replace(re, "bar$15"), "$15");

assertFalse(/()foo$\1/.test("football"), "football1");
assertFalse(/foo$(?=ball)/.test("football"), "football2");
assertFalse(/foo$(?!bar)/.test("football"), "football3");
assertTrue(/()foo$\1/.test("foo"), "football4");
assertTrue(/foo$(?=(ball)?)/.test("foo"), "football5");
assertTrue(/()foo$(?!bar)/.test("foo"), "football6");
assertFalse(/(x?)foo$\1/.test("football"), "football7");
assertFalse(/foo$(?=ball)/.test("football"), "football8");
assertFalse(/foo$(?!bar)/.test("football"), "football9");
assertTrue(/(x?)foo$\1/.test("foo"), "football10");
assertTrue(/foo$(?=(ball)?)/.test("foo"), "football11");
assertTrue(/foo$(?!bar)/.test("foo"), "football12");

// Check that the back reference has two successors.  See
// BackReferenceNode::PropagateForward.
assertFalse(/f(o)\b\1/.test('foo'));
assertTrue(/f(o)\B\1/.test('foo'));

// Back-reference, ignore case:
// ASCII
assertEquals("xaAx,a", String(/x(a)\1x/i.exec("xaAx")), "\\1 ASCII");
assertFalse(/x(...)\1/i.test("xaaaaa"), "\\1 ASCII, string short");
assertTrue(/x((?:))\1\1x/i.test("xx"), "\\1 empty, ASCII");
assertTrue(/x(?:...|(...))\1x/i.test("xabcx"), "\\1 uncaptured, ASCII");
assertTrue(/x(?:...|(...))\1x/i.test("xabcABCx"), "\\1 backtrack, ASCII");
assertEquals("xaBcAbCABCx,aBc",
             String(/x(...)\1\1x/i.exec("xaBcAbCABCx")),
             "\\1\\1 ASCII");

for (var i = 0; i < 128; i++) {
  var testName = "(.)\\1 ~ " + i + "," + (i^0x20);
  var test = /^(.)\1$/i.test(String.fromCharCode(i, i ^ 0x20))
  var c = String.fromCharCode(i);
  if (('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z')) {
    assertTrue(test, testName);
  } else {
    assertFalse(test, testName);
  }
}

// UC16
// Characters used:
// "\u03a3\u03c2\u03c3\u039b\u03bb" - Sigma, final sigma, sigma, Lambda, lamda
assertEquals("x\u03a3\u03c3x,\u03a3",
              String(/x(.)\1x/i.exec("x\u03a3\u03c3x")), "\\1 UC16");
assertFalse(/x(...)\1/i.test("x\u03a3\u03c2\u03c3\u03c2\u03c3"),
            "\\1 ASCII, string short");
assertTrue(/\u03a3((?:))\1\1x/i.test("\u03c2x"), "\\1 empty, UC16");
assertTrue(/x(?:...|(...))\1x/i.test("x\u03a3\u03c2\u03c3x"),
           "\\1 uncaptured, UC16");
assertTrue(/x(?:...|(...))\1x/i.test("x\u03c2\u03c3\u039b\u03a3\u03c2\u03bbx"),
           "\\1 backtrack, UC16");
var longUC16String = "x\u03a3\u03c2\u039b\u03c2\u03c3\u03bb\u03c3\u03a3\u03bb";
assertEquals(longUC16String + "," + longUC16String.substring(1,4),
             String(/x(...)\1\1/i.exec(longUC16String)),
             "\\1\\1 UC16");

assertFalse(/f(o)$\1/.test('foo'), "backref detects at_end");
