// Copyright 2010 the V8 project authors. All rights reserved.
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

// Flags: --expose-externalize-string --expose-gc

var size = 1024;

function test() {
  var str = "";

  // Build an ascii cons string.
  for (var i = 0; i < size; i++) {
      str += String.fromCharCode(i & 0x7f);
  }
  assertTrue(isAsciiString(str));

  var twoByteExternalWithAsciiData =
      "AAAAAAAA" + (function() { return "A"; })();
  externalizeString(twoByteExternalWithAsciiData, true /* force two-byte */);
  assertFalse(isAsciiString(twoByteExternalWithAsciiData));

  var realTwoByteExternalString =
      "\u1234\u1234\u1234\u1234" + (function() { return "\u1234"; })();
  externalizeString(realTwoByteExternalString);
  assertFalse(isAsciiString(realTwoByteExternalString));

  assertTrue(isAsciiString(["a", twoByteExternalWithAsciiData].join("")));

  // Appending a two-byte string that contains only ascii chars should
  // still produce an ascii cons.
  var str1 = str + twoByteExternalWithAsciiData;
  assertTrue(isAsciiString(str1));

  // Force flattening of the string.
  var old_length = str1.length - twoByteExternalWithAsciiData.length;
  for (var i = 0; i < old_length; i++) {
    assertEquals(String.fromCharCode(i & 0x7f), str1[i]);
  }
  for (var i = old_length; i < str1.length; i++) {
    assertEquals("A", str1[i]);
  }

  // Flattened string should still be ascii.
  assertTrue(isAsciiString(str1));

  // Lower-casing an ascii string should produce ascii.
  assertTrue(isAsciiString(str1.toLowerCase()));

  assertFalse(isAsciiString(["a", realTwoByteExternalString].join("")));

  // Appending a real two-byte string should produce a two-byte cons.
  var str2 = str + realTwoByteExternalString;
  assertFalse(isAsciiString(str2));

  // Force flattening of the string.
  old_length = str2.length - realTwoByteExternalString.length;
  for (var i = 0; i < old_length; i++) {
    assertEquals(String.fromCharCode(i & 0x7f), str2[i]);
  }
  for (var i = old_length; i < str.length; i++) {
    assertEquals("\u1234", str2[i]);
  }

  // Flattened string should still be two-byte.
  assertFalse(isAsciiString(str2));

  // Test buffered external strings.
  var charat_str = new Array(5);
  charat_str[0] = "0123456789ABCDEF0123456789ABCDEF\
0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\
0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\
0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF\
0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
  charat_str[1] = "0123456789ABCDEF";
  for (var i = 0; i < 6; i++) charat_str[1] += charat_str[1];
  try {  // String can only be externalized once
    externalizeString(charat_str[0], false);
    externalizeString(charat_str[1], true);
  } catch (ex) { }
  charat_str[2] = charat_str[0].slice(0, -1);
  charat_str[3] = charat_str[1].slice(0, -1);
  charat_str[4] = charat_str[0] + charat_str[0];

  for (var i = 0; i < 5; i++) {
    assertEquals('B', charat_str[i].charAt(6*16 + 11));
    assertEquals('C', charat_str[i].charAt(6*16 + 12));
    assertEquals('A', charat_str[i].charAt(3*16 + 10));
    assertEquals('B', charat_str[i].charAt(3*16 + 11));
  }
}

// Run the test many times to ensure IC-s don't break things.
for (var i = 0; i < 10; i++) {
  test();
}

// Clean up string to make Valgrind happy.
gc();
gc();
