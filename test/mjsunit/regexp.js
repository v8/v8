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
assertTrue(/\c[a/]/.test( "\x1ba/]" ));

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
