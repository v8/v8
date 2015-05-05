// Copyright 2015 the V8 project authors. All rights reserved.
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

// Flags: --allow-natives-syntax --noalways-opt

// Test that a -0 result doesn't cause deopt loops
function noDeoptLoop(x) {
  return Math.round(x);
}
assertEquals(0, noDeoptLoop(0.4));
assertEquals(0, noDeoptLoop(0.4));
assertEquals(0, noDeoptLoop(0.4));
%OptimizeFunctionOnNextCall(noDeoptLoop);
assertEquals(0, noDeoptLoop(0.4));
assertEquals(0, noDeoptLoop(0.4));
assertEquals(-Infinity, 1/noDeoptLoop(-0.4));
assertUnoptimized(noDeoptLoop);
assertEquals(-Infinity, 1/noDeoptLoop(-0.4));
assertEquals(-Infinity, 1/noDeoptLoop(-0.0));
assertEquals(-Infinity, 1/noDeoptLoop(-0.5));
assertEquals(-1, noDeoptLoop(-1));
assertEquals(-1, noDeoptLoop(-0.51));
%OptimizeFunctionOnNextCall(noDeoptLoop);
assertEquals(-Infinity, 1/noDeoptLoop(-0.4));
assertEquals(-Infinity, 1/noDeoptLoop(-0.4));
assertOptimized(noDeoptLoop);
%ClearFunctionTypeFeedback(noDeoptLoop);
%DeoptimizeFunction(noDeoptLoop);

// Test that round that goes megamorphic is handled correctly.
function notRound(x) {
  return -x;
}
function testMega(f, x) {
  return f(x);
}
assertEquals(7, testMega(Math.round, 7.4));
assertEquals(7, testMega(Math.round, 7.4));
assertEquals(7, testMega(Math.round, 7.4));
assertEquals(-7.4, testMega(notRound, 7.4));

// Make sure that we can learn about round specialization from Cranskhaft, which
// doesn't insert soft deopts for CallICs.
function crankRoundLearn(x) {
  return Math.round(x);
}
%OptimizeFunctionOnNextCall(crankRoundLearn);
assertEquals(12, crankRoundLearn(12.3));
assertOptimized(crankRoundLearn);
assertEquals(-Infinity, 1/crankRoundLearn(-0.4));
assertOptimized(crankRoundLearn);
assertEquals(-Infinity, 1/crankRoundLearn(-0.4));
