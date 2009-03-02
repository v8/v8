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

function CheckMatch(re, str, matches) {
  assertEquals(matches.length > 0, re.test(str));
  var result = str.match(re);
  if (matches.length > 0) {
    assertEquals(matches.length, result.length);
    for (idx in matches) {
      var from = matches[idx][0];
      var length = matches[idx][1];
      var expected = str.substr(from, length);
      assertEquals(expected, result[idx]);
    }
    assertEquals(expected, RegExp.lastMatch);
    assertEquals(str.substr(0, from), RegExp.leftContext);
    assertEquals(str.substr(from + length), RegExp.rightContext);
  } else {
    assertTrue(result === null);
  }
}

CheckMatch(/abc/, "xxxabcxxxabcxxx", [[3, 3]]);
CheckMatch(/abc/g, "xxxabcxxxabcxxx", [[3, 3], [9, 3]]);
CheckMatch(/abc/, "xxxabababcxxxabcxxx", [[7, 3]]);
CheckMatch(/abc/g, "abcabcabc", [[0, 3], [3, 3], [6, 3]]);
CheckMatch(/aba/g, "ababababa", [[0, 3], [4, 3]]);
CheckMatch(/foo/g, "ofooofoooofofooofo", [[1, 3], [5, 3], [12, 3]]);
CheckMatch(/foobarbaz/, "xx", []);
CheckMatch(new RegExp(""), "xxx", [[0, 0]]);
CheckMatch(/abc/, "abababa", []);

assertEquals("xxxdefxxxdefxxx", "xxxabcxxxabcxxx".replace(/abc/g, "def"));
assertEquals("o-o-oofo-ofo", "ofooofoooofofooofo".replace(/foo/g, "-"));
assertEquals("deded", "deded".replace(/x/g, "-"));
assertEquals("-a-b-c-d-e-f-", "abcdef".replace(new RegExp("", "g"), "-"));
