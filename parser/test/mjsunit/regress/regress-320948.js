// Copyright 2013 the V8 project authors. All rights reserved.
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

// Flags: --allow-natives-syntax --debug-code

var one_byte = %NewString(10, true);
var two_byte = %NewString(10, false);

function foo1(s, arg1, arg2) {
  return %_OneByteSeqStringSetChar(s, arg1, arg2)
}
foo1(one_byte, 0, 0);
assertThrows("{ foo1(4, 0, 0); }");
assertThrows("{ foo1(one_byte, new Object(), 0); }");
assertThrows("{ foo1(one_byte, 0, new Object()); }");
assertThrows("{ foo1(one_byte, 100000, 100; }");
assertThrows("{ foo1(one_byte, -1, 100; }");

function bar1(s, arg1, arg2) {
  return %_OneByteSeqStringSetChar(s, arg1, arg2)
}

bar1(one_byte, 0, 0);
bar1(one_byte, 0, 0);
bar1(one_byte, 0, 0);
%OptimizeFunctionOnNextCall(bar1);
bar1(one_byte, 0, 0);
assertThrows("{ bar1(4, 0, 0); }");
assertThrows("{ bar1(one_byte, new Object(), 0); }");
assertThrows("{ bar1(one_byte, 0, new Object()); }");
assertThrows("{ bar1(one_byte, 100000, 100; }");
assertThrows("{ bar1(one_byte, -1, 100; }");

function foo2(s, arg1, arg2) {
  return %_TwoByteSeqStringSetChar(s, arg1, arg2)
}
foo2(two_byte, 0, 0);
assertThrows("{ foo2(4, 0, 0); }");
assertThrows("{ foo2(two_byte, new Object(), 0); }");
assertThrows("{ foo2(two_byte, 0, new Object()); }");
assertThrows("{ foo2(two_byte, 100000, 100; }");
assertThrows("{ foo2(two_byte, -1, 100; }");

function bar2(s, arg1, arg2) {
  return %_TwoByteSeqStringSetChar(s, arg1, arg2)
}

bar2(two_byte, 0, 0);
bar2(two_byte, 0, 0);
bar2(two_byte, 0, 0);
%OptimizeFunctionOnNextCall(bar2);
bar2(two_byte, 0, 0);
assertThrows("{ bar2(4, 0, 0); }");
assertThrows("{ bar2(two_byte, new Object(), 0); }");
assertThrows("{ bar2(two_byte, 0, new Object()); }");
assertThrows("{ bar2(two_byte, 100000, 100; }");
assertThrows("{ bar2(two_byte, -1, 100; }");
