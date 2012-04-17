// Copyright 2012 the V8 project authors. All rights reserved.
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

function oneMatch(re) {
  "abcd".replace(re, function() { });
  assertEquals("abcd", RegExp.input);
  assertEquals("a", RegExp.leftContext);
  assertEquals("b", RegExp.lastMatch);
  assertEquals("", RegExp.lastParen);
  assertEquals(undefined, RegExp.lastIndex);
  assertEquals(undefined, RegExp.index);
  assertEquals("cd", RegExp.rightContext);
  for (var i = 1; i < 10; i++) {
    assertEquals("", RegExp['$' + i]);
  }
}

oneMatch(/b/);
oneMatch(/b/g);

"abcdabcd".replace(/b/g, function() { });
assertEquals("abcdabcd", RegExp.input);
assertEquals("abcda", RegExp.leftContext);
assertEquals("b", RegExp.lastMatch);
assertEquals("", RegExp.lastParen);
assertEquals(undefined, RegExp.lastIndex);
assertEquals(undefined, RegExp.index);
assertEquals("cd", RegExp.rightContext);
for (var i = 1; i < 10; i++) {
  assertEquals("", RegExp['$' + i]);
}

function captureMatch(re) {
  "abcd".replace(re, function() { });
  assertEquals("abcd", RegExp.input);
  assertEquals("a", RegExp.leftContext);
  assertEquals("bc", RegExp.lastMatch);
  assertEquals("c", RegExp.lastParen);
  assertEquals(undefined, RegExp.lastIndex);
  assertEquals(undefined, RegExp.index);
  assertEquals("d", RegExp.rightContext);
  assertEquals('b', RegExp.$1);
  assertEquals('c', RegExp.$2);
  for (var i = 3; i < 10; i++) {
    assertEquals("", RegExp['$' + i]);
  }
}

captureMatch(/(b)(c)/);
captureMatch(/(b)(c)/g);

"abcdabcd".replace(/(b)(c)/g, function() { });
assertEquals("abcdabcd", RegExp.input);
assertEquals("abcda", RegExp.leftContext);
assertEquals("bc", RegExp.lastMatch);
assertEquals("c", RegExp.lastParen);
assertEquals(undefined, RegExp.lastIndex);
assertEquals(undefined, RegExp.index);
assertEquals("d", RegExp.rightContext);
assertEquals('b', RegExp.$1);
assertEquals('c', RegExp.$2);
for (var i = 3; i < 10; i++) {
  assertEquals("", RegExp['$' + i]);
}
