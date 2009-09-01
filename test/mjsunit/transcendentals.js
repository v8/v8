// Copyright 2009 the V8 project authors. All rights reserved.
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


// Two fp numbers that have the same hash value (see TranscendentalCache
// in heap.h).
var x = 0x123456789ABCD;
var y = 0x1134567899BCD;

assertEquals(-0.5582508193778007, Math.sin(x));
assertEquals(-0.7367701055966746, Math.sin(y));

assertEquals(-0.8296722380940645, Math.cos(x));
assertEquals(-0.6761433365042245, Math.cos(y));

assertEquals(0.6728570557696649, Math.tan(x));
assertEquals(1.0896655573149632, Math.tan(y));

assertEquals(33.400141709152514, Math.log(x));
assertEquals(33.343643692997280, Math.log(y));

// These also have the same hash value but they are < 1 so they can be
// used for the asin and other functions.
x = 0x123456789ABCD / 0x2000000000000;
y = 0x1134567899BCD / 0x2000000000000;

assertEquals(0.6051541873165459, Math.asin(x));
assertEquals(0.5676343396849298, Math.asin(y));

assertEquals(0.9656421394783508, Math.acos(x));
assertEquals(1.0031619871099668, Math.acos(y));

assertEquals(0.5172294898564562, Math.atan(x));
assertEquals(0.4933034078249788, Math.atan(y));

assertEquals(1.7663034013841883, Math.exp(x));
assertEquals(1.7119599587777090, Math.exp(y));

print("OK");
