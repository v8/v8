// Copyright 2011 the V8 project authors. All rights reserved.
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

// Flags: --allow-natives-syntax

// This is a regression test for overlapping key and value registers.
function f(a) {
  a[0] = 0;
  a[1] = 0;
}

var a = new Int32Array(2);
for (var i = 0; i < 5; i++) {
  f(a);
}
%OptimizeFunctionOnNextCall(f);
f(a);

assertEquals(0, a[0]);
assertEquals(0, a[1]);

// Test the correct behavior of the |length| property (which is read-only).
a = new Int32Array(42);
assertEquals(42, a.length);
a.length = 2;
assertEquals(42, a.length);
assertTrue(delete a.length);
a.length = 2
assertEquals(2, a.length);

// Test the correct behavior of the |BYTES_PER_ELEMENT| property (which is
// "constant", but not read-only).
a = new Int32Array(2);
assertEquals(4, a.BYTES_PER_ELEMENT);
a.BYTES_PER_ELEMENT = 42;
assertEquals(42, a.BYTES_PER_ELEMENT);
a = new Uint8Array(2);
assertEquals(1, a.BYTES_PER_ELEMENT);
a = new Int16Array(2);
assertEquals(2, a.BYTES_PER_ELEMENT);

