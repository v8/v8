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

// Flags: --allow-natives-syntax --opt --no-always-opt

// Test references and assignments to global variables.

var g = 0;

// Test compilation of a global variable store.
assertEquals(1, eval('g = 1'));
// Test that the store worked.
assertEquals(1, g);

// Test that patching the IC in the compiled code works.
assertEquals(1, eval('g = 1'));
assertEquals(1, g);
assertEquals(1, eval('g = 1'));
assertEquals(1, g);

// Test a second store.
assertEquals("2", eval('g = "2"'));
assertEquals("2", g);

// Test a load.
assertEquals("2", eval('g'));

// Test that patching the IC in the compiled code works.
assertEquals("2", eval('g'));
assertEquals("2", eval('g'));

// Test a second load.
g = 3;
assertEquals(3, eval('g'));

// Test postfix count operation
var t;
t = g++;
assertEquals(3, t);
assertEquals(4, g);

code = "g--; 1";
assertEquals(1, eval(code));
assertEquals(3, g);

// Test simple assignment to non-deletable and deletable globals.
var glo1 = 0;
function f1(x) { glo1 = x; }
f1(42);
assertEquals(glo1, 42);

glo2 = 0;
function f2(x) { glo2 = x; }
f2(42);
assertEquals(42, glo2);


//////////////////////////////////////////////////////////////////////////////


// Test Constant cell value going from writable to read-only.
{
  glo4 = 4;

  function write_glo4(x) { glo4 = x }

  %PrepareFunctionForOptimization(write_glo4);
  write_glo4(4);
  assertEquals(4, glo4);

  // At this point, glo4 has cell type Constant.

  %OptimizeFunctionOnNextCall(write_glo4);
  write_glo4(4);
  assertEquals(4, glo4);
  assertOptimized(write_glo4);

  Object.defineProperty(this, 'glo4', {writable: false});
  assertUnoptimized(write_glo4);
  write_glo4(2);
  assertEquals(4, glo4);
}


// Test ConstantType cell value going from writable to read-only.
{
  glo5 = 5;

  function write_glo5(x) { glo5 = x }

  %PrepareFunctionForOptimization(write_glo5);
  write_glo5(0);
  assertEquals(0, glo5);

  // At this point, glo5 has cell type ConstantType.

  %OptimizeFunctionOnNextCall(write_glo5);
  write_glo5(5);
  assertEquals(5, glo5);
  assertOptimized(write_glo5);

  Object.defineProperty(this, 'glo5', {writable: false});
  assertUnoptimized(write_glo5);
  write_glo5(2);
  assertEquals(5, glo5);
}


// Test Mutable cell value going from writable to read-only.
{
  glo6 = 6;

  function write_glo6(x) { glo6 = x }

  %PrepareFunctionForOptimization(write_glo6);
  write_glo6({});
  write_glo6(3);
  assertEquals(3, glo6);

  // At this point, glo6 has cell type Mutable.

  %OptimizeFunctionOnNextCall(write_glo6);
  write_glo6(6);
  assertEquals(6, glo6);
  assertOptimized(write_glo6);

  Object.defineProperty(this, 'glo6', {writable: false});
  assertUnoptimized(write_glo6);
  write_glo6(2);
  assertEquals(6, glo6);
}


// Test Constant cell value going from read-only to writable.
{
  glo7 = 7;
  Object.defineProperty(this, 'glo7', {writable: false});

  function read_glo7() { return glo7 }

  %PrepareFunctionForOptimization(read_glo7);
  assertEquals(7, read_glo7());

  // At this point, glo7 has cell type Constant.

  %OptimizeFunctionOnNextCall(read_glo7);
  assertEquals(7, read_glo7());

  Object.defineProperty(this, 'glo7', {writable: true});
  assertEquals(7, read_glo7());
  assertOptimized(read_glo7);
}


// Test ConstantType cell value going from read-only to writable.
{
  glo8 = 0;
  glo8 = 8;
  Object.defineProperty(this, 'glo8', {writable: false});

  function read_glo8() { return glo8 }

  %PrepareFunctionForOptimization(read_glo8);
  assertEquals(8, read_glo8());

  // At this point, glo8 has cell type ConstantType.

  %OptimizeFunctionOnNextCall(read_glo8);
  assertEquals(8, read_glo8());

  Object.defineProperty(this, 'glo8', {writable: true});
  assertEquals(8, read_glo8());
  assertOptimized(read_glo8);
}


// Test Mutable cell value going from read-only to writable.
{
  glo9 = {};
  glo9 = 9;
  Object.defineProperty(this, 'glo9', {writable: false});

  function read_glo9() { return glo9 }

  %PrepareFunctionForOptimization(read_glo9);
  assertEquals(9, read_glo9());

  // At this point, glo9 has cell type Mutable.

  %OptimizeFunctionOnNextCall(read_glo9);
  assertEquals(9, read_glo9());

  Object.defineProperty(this, 'glo9', {writable: true});
  assertEquals(9, read_glo9());
  assertOptimized(read_glo9);
}
