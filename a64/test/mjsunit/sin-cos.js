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

// Test Math.sin and Math.cos.

function sinTest() {
  assertEquals(0, Math.sin(0));
  assertEquals(1, Math.sin(Math.PI / 2));
}

function cosTest() {
  assertEquals(1, Math.cos(0));
  assertEquals(-1, Math.cos(Math.PI));
}

sinTest();
cosTest();

// By accident, the slow case for sine and cosine were both sine at
// some point.  This is a regression test for that issue.
var x = Math.pow(2, 30);
assertTrue(Math.sin(x) != Math.cos(x));

// Ensure that sine and log are not the same.
x = 0.5;
assertTrue(Math.sin(x) != Math.log(x));

// Test against approximation by series.
var factorial = [1];
var accuracy = 50;
for (var i = 1; i < accuracy; i++) {
  factorial[i] = factorial[i-1] * i;
}

// We sum up in the reverse order for higher precision, as we expect the terms
// to grow smaller for x reasonably close to 0.
function precision_sum(array) {
  var result = 0;
  while (array.length > 0) {
    result += array.pop();
  }
  return result;
}

function sin(x) {
  var sign = 1;
  var x2 = x*x;
  var terms = [];
  for (var i = 1; i < accuracy; i += 2) {
    terms.push(sign * x / factorial[i]);
    x *= x2;
    sign *= -1;
  }
  return precision_sum(terms);
}

function cos(x) {
  var sign = -1;
  var x2 = x*x;
  x = x2;
  var terms = [1];
  for (var i = 2; i < accuracy; i += 2) {
    terms.push(sign * x / factorial[i]);
    x *= x2;
    sign *= -1;
  }
  return precision_sum(terms);
}

function abs_error(fun, ref, x) {
  return Math.abs(ref(x) - fun(x));
}

var test_inputs = [];
for (var i = -10000; i < 10000; i += 177) test_inputs.push(i/1257);
var epsilon = 0.000001;

test_inputs.push(0);
test_inputs.push(0 + epsilon);
test_inputs.push(0 - epsilon);
test_inputs.push(Math.PI/2);
test_inputs.push(Math.PI/2 + epsilon);
test_inputs.push(Math.PI/2 - epsilon);
test_inputs.push(Math.PI);
test_inputs.push(Math.PI + epsilon);
test_inputs.push(Math.PI - epsilon);
test_inputs.push(- 2*Math.PI);
test_inputs.push(- 2*Math.PI + epsilon);
test_inputs.push(- 2*Math.PI - epsilon);

var squares = [];
for (var i = 0; i < test_inputs.length; i++) {
  var x = test_inputs[i];
  var err_sin = abs_error(Math.sin, sin, x);
  var err_cos = abs_error(Math.cos, cos, x)
  assertTrue(err_sin < 1E-13);
  assertTrue(err_cos < 1E-13);
  squares.push(err_sin*err_sin + err_cos*err_cos);
}

// Sum squares up by adding them pairwise, to avoid losing precision.
while (squares.length > 1) {
  var reduced = [];
  if (squares.length % 2 == 1) reduced.push(squares.pop());
  // Remaining number of elements is even.
  while(squares.length > 1) reduced.push(squares.pop() + squares.pop());
  squares = reduced;
}

var err_rms = Math.sqrt(squares[0] / test_inputs.length / 2);
assertTrue(err_rms < 1E-14);

assertEquals(-1, Math.cos({ valueOf: function() { return Math.PI; } }));
assertEquals(0, Math.sin("0x00000"));
assertEquals(1, Math.cos("0x00000"));
assertTrue(isNaN(Math.sin(Infinity)));
assertTrue(isNaN(Math.cos("-Infinity")));
assertEquals("Infinity", String(Math.tan(Math.PI/2)));
assertEquals("-Infinity", String(Math.tan(-Math.PI/2)));
