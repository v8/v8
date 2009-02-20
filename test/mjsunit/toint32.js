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

function toInt32(x) {
  return x | 0;
}

assertEquals(0, toInt32(Infinity));
assertEquals(0, toInt32(-Infinity));
assertEquals(0, toInt32(NaN));
assertEquals(0, toInt32(0.0));
assertEquals(0, toInt32(-0.0));

assertEquals(0, toInt32(Number.MIN_VALUE));
assertEquals(0, toInt32(-Number.MIN_VALUE));
assertEquals(0, toInt32(0.1));
assertEquals(0, toInt32(-0.1));
assertEquals(1, toInt32(1));
assertEquals(1, toInt32(1.1));
assertEquals(-1, toInt32(-1));

assertEquals(2147483647, toInt32(2147483647));
assertEquals(-2147483648, toInt32(2147483648));
assertEquals(-2147483647, toInt32(2147483649));

assertEquals(-1, toInt32(4294967295));
assertEquals(0, toInt32(4294967296));
assertEquals(1, toInt32(4294967297));

assertEquals(-2147483647, toInt32(-2147483647));
assertEquals(-2147483648, toInt32(-2147483648));
assertEquals(2147483647, toInt32(-2147483649));

assertEquals(1, toInt32(-4294967295));
assertEquals(0, toInt32(-4294967296));
assertEquals(-1, toInt32(-4294967297));

assertEquals(-2147483648, toInt32(2147483648.25));
assertEquals(-2147483648, toInt32(2147483648.5));
assertEquals(-2147483648, toInt32(2147483648.75));
assertEquals(-1, toInt32(4294967295.25));
assertEquals(-1, toInt32(4294967295.5));
assertEquals(-1, toInt32(4294967295.75));
assertEquals(-1294967296, toInt32(3000000000.25));
assertEquals(-1294967296, toInt32(3000000000.5));
assertEquals(-1294967296, toInt32(3000000000.75));

assertEquals(-2147483648, toInt32(-2147483648.25));
assertEquals(-2147483648, toInt32(-2147483648.5));
assertEquals(-2147483648, toInt32(-2147483648.75));
assertEquals(1, toInt32(-4294967295.25));
assertEquals(1, toInt32(-4294967295.5));
assertEquals(1, toInt32(-4294967295.75));
assertEquals(1294967296, toInt32(-3000000000.25));
assertEquals(1294967296, toInt32(-3000000000.5));
assertEquals(1294967296, toInt32(-3000000000.75));

var base = Math.pow(2, 64);
assertEquals(0, toInt32(base + 0));
assertEquals(0, toInt32(base + 1117));
assertEquals(4096, toInt32(base + 2234));
assertEquals(4096, toInt32(base + 3351));
assertEquals(4096, toInt32(base + 4468));
assertEquals(4096, toInt32(base + 5585));
assertEquals(8192, toInt32(base + 6702));
assertEquals(8192, toInt32(base + 7819));
assertEquals(8192, toInt32(base + 8936));
assertEquals(8192, toInt32(base + 10053));
assertEquals(12288, toInt32(base + 11170));
assertEquals(12288, toInt32(base + 12287));
assertEquals(12288, toInt32(base + 13404));
assertEquals(16384, toInt32(base + 14521));
assertEquals(16384, toInt32(base + 15638));
assertEquals(16384, toInt32(base + 16755));
assertEquals(16384, toInt32(base + 17872));
assertEquals(20480, toInt32(base + 18989));
assertEquals(20480, toInt32(base + 20106));
assertEquals(20480, toInt32(base + 21223));
assertEquals(20480, toInt32(base + 22340));
assertEquals(24576, toInt32(base + 23457));
assertEquals(24576, toInt32(base + 24574));
assertEquals(24576, toInt32(base + 25691));
assertEquals(28672, toInt32(base + 26808));
assertEquals(28672, toInt32(base + 27925));
assertEquals(28672, toInt32(base + 29042));
assertEquals(28672, toInt32(base + 30159));
assertEquals(32768, toInt32(base + 31276));
assertEquals(32768, toInt32(base + 32393));
assertEquals(32768, toInt32(base + 33510));
assertEquals(32768, toInt32(base + 34627));
assertEquals(36864, toInt32(base + 35744));
assertEquals(36864, toInt32(base + 36861));
assertEquals(36864, toInt32(base + 37978));
assertEquals(40960, toInt32(base + 39095));
assertEquals(40960, toInt32(base + 40212));
assertEquals(40960, toInt32(base + 41329));
assertEquals(40960, toInt32(base + 42446));
assertEquals(45056, toInt32(base + 43563));
assertEquals(45056, toInt32(base + 44680));
assertEquals(45056, toInt32(base + 45797));
assertEquals(45056, toInt32(base + 46914));
assertEquals(49152, toInt32(base + 48031));
assertEquals(49152, toInt32(base + 49148));
assertEquals(49152, toInt32(base + 50265));
assertEquals(53248, toInt32(base + 51382));
assertEquals(53248, toInt32(base + 52499));
assertEquals(53248, toInt32(base + 53616));
assertEquals(53248, toInt32(base + 54733));
assertEquals(57344, toInt32(base + 55850));
assertEquals(57344, toInt32(base + 56967));
assertEquals(57344, toInt32(base + 58084));
assertEquals(57344, toInt32(base + 59201));
assertEquals(61440, toInt32(base + 60318));
assertEquals(61440, toInt32(base + 61435));
assertEquals(61440, toInt32(base + 62552));
assertEquals(65536, toInt32(base + 63669));
assertEquals(65536, toInt32(base + 64786));
assertEquals(65536, toInt32(base + 65903));
assertEquals(65536, toInt32(base + 67020));
assertEquals(69632, toInt32(base + 68137));
assertEquals(69632, toInt32(base + 69254));
assertEquals(69632, toInt32(base + 70371));
assertEquals(69632, toInt32(base + 71488));
assertEquals(73728, toInt32(base + 72605));
assertEquals(73728, toInt32(base + 73722));
assertEquals(73728, toInt32(base + 74839));
assertEquals(77824, toInt32(base + 75956));
assertEquals(77824, toInt32(base + 77073));
assertEquals(77824, toInt32(base + 78190));
assertEquals(77824, toInt32(base + 79307));
assertEquals(81920, toInt32(base + 80424));
assertEquals(81920, toInt32(base + 81541));
assertEquals(81920, toInt32(base + 82658));
assertEquals(81920, toInt32(base + 83775));
assertEquals(86016, toInt32(base + 84892));
assertEquals(86016, toInt32(base + 86009));
assertEquals(86016, toInt32(base + 87126));
assertEquals(90112, toInt32(base + 88243));
assertEquals(90112, toInt32(base + 89360));
assertEquals(90112, toInt32(base + 90477));
assertEquals(90112, toInt32(base + 91594));
assertEquals(94208, toInt32(base + 92711));
assertEquals(94208, toInt32(base + 93828));
assertEquals(94208, toInt32(base + 94945));
assertEquals(94208, toInt32(base + 96062));
assertEquals(98304, toInt32(base + 97179));
assertEquals(98304, toInt32(base + 98296));
assertEquals(98304, toInt32(base + 99413));
