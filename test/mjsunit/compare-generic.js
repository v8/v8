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

// Flags: --allow-natives-syntax

// Test the correct placement of the simulates after HCompareGenericAndBranch:
function Checker() {
  this.str = "1";
  var toStringCalled = 0;
  var toStringExpected = 0;
  this.toString = function() {
    toStringCalled++;
    return this.str;
  };
  this.check = function() {
    toStringExpected++;
    assertEquals(toStringExpected, toStringCalled);
  };
};
var left = new Checker();
var right = new Checker();

// This test compares a < b against x < y where
// x/y are objects providing a/b as toString. In the end we
// check if the observable side effects match our
// expectations, thus we make sure that we deopted to a
// simulate after the comparison was done.
function test(a,b) {
  left.str = a;
  right.str = b;
  if (left >= right) {
    assertTrue(a >= b);
  } else {
    assertFalse(a >= b);
  }
  left.check();
  right.check();
}

test("ab","abc");
test("ab","a");
%OptimizeFunctionOnNextCall(test);
test("a","ab");
test(1,"a");
test("a","ab");
%OptimizeFunctionOnNextCall(test);
test("a","ab");
test("a",1);
test("ab","a");


// Use generic compare in value, effect and test contexts

function Checker2() {
  var valueOfCalled = 0;
  this.valueOf = function() {
    return valueOfCalled++;
  }
  this.valueOfCalled = function() {
    return valueOfCalled;
  }
}

var x = new Checker2();
var y = new Checker2();

if (x < y || y < x || x <= y) {
  assertEquals(3, x.valueOfCalled());
  assertEquals(3, y.valueOfCalled());
  assertEquals(1, (x < y) + (y < x) + (x <= y))
  assertEquals(6, x.valueOfCalled());
  assertEquals(6, y.valueOfCalled());
  x < y;
  assertEquals(7, x.valueOfCalled());
  assertEquals(7, y.valueOfCalled());
  x < y;
  assertEquals(8, x.valueOfCalled());
  assertEquals(8, y.valueOfCalled());
  var res;
  if (x <= y) {
    res = 1+(x > {});
  } else {
    assertTrue(false);
    res = y <= {};
  }
  assertEquals(10, x.valueOfCalled());
  assertEquals(9, y.valueOfCalled());
  assertEquals(1, res);
  assertFalse(x < y);

  var tb = 0, fb = 0;
  var val = 0;
  for (var i = 1; i < 10; i++) {
    var res = 0;
    // uses x,y in control context
    if (x <= y) {
      res += val;
      assertTrue(x <= y);
      // adds 1 + 0, uses x in value context
      res += 1+(x > {});
      tb++;
      assertEquals(fb, tb);
    } else {
      res += val;
      assertFalse(x < y);
      // adds 1, uses y in value context, increments 2
      res += (y <= y);
      // use x in value context, increments x once to make it equal to y again
      x + 2;
      assertEquals(fb, tb);
      fb++;
    }
    assertEquals(11+(2*i)+tb+fb, x.valueOfCalled());
    assertEquals(10+(2*i)+(2*fb), y.valueOfCalled());
    assertEquals(1 + val, res);
    // Triggers deopt inside branch.
    if (i%5 == 0) val += 0.5;
  }
} else {
  assertTrue(false);
}


function t(a,b) { return (b < a) - (a < b); };
function f() {
  x = new Checker2();
  y = new Checker2();
  var tb = 0, fb = 0;
  var val = 0;
  for (var i = 1; i < 10; i++) {
    var res = 0;
    if ((x < y) + (y < x)) {
      res += val;
      res += x<0;
      fb++;
    } else {
      res += val;
      res += y<0;
      tb++;
    }
    assertEquals(0, res + 1 - res - 1);
    assertEquals((2*i)+fb, x.valueOfCalled());
    assertEquals((2*i)+tb, y.valueOfCalled());
    assertEquals(val, res);
    if (i%4 == 0) val += 0.5;
  }
}

f();
%OptimizeFunctionOnNextCall(f);
f();

var a = {valueOf: function(){this.conv++; return 1;}};
var b = {valueOf: function(){this.conv++; return 2;}};

a.conv = 0;
b.conv = 0;

function f2(a,b,d1,d2) {
  var runs = 0;
  if ((a < b) + (a < b)) {
    if (d2) { d2 += 0.2; }
    runs++;
  } else {
    assertUnreachable();
  }
  assertEquals(1, runs);
  if (a > b) {
    assertUnreachable();
  } else {
    if (d1) { d1 += 0.2; }
    runs++;
  }
  assertEquals(2, runs);
}

f2(a,b);
f2(a,b);

%OptimizeFunctionOnNextCall(f2);
f2(a,b);
f2(a,b);

f2(a,b,true);
f2(a,b);

%OptimizeFunctionOnNextCall(f2);
f2(a,b);
f2(a,b);

f2(a,b,false,true);
f2(a,b);

assertEquals(30, a.conv);
assertEquals(30, b.conv);

b.valueOf = function(){ return {}; }
try {
  f2(a,b);
} catch(e) {
  res = e.stack;
}
