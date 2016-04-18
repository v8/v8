// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/typegen.js");


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = default_test_size;


// Generate an appropriate context for testing the following.
let context =
    "var x = 42; " +
    "var a = [42]; " +
    "var o = {x: 42}; " +
    "function fo() { return o; }; " +
    "function fa() { return a; }; " +
    "function ff() { return fo; }; " +
    "function ffa() { return [fo]; }; " +
    "function ffo() { return {x: fo}; }; ";

function CheckValidInContext(expr) {
  CheckValid(context + expr);
}

function CheckInvalidInContext(expr) {
  CheckInvalid(context + expr);
}

function CheckInvalidReferenceInContext(expr) {
  CheckInvalid(context + expr, ReferenceError);
}


function ValidTypeAssertions(size) {
  return Generate(size, [
    new TestGen(1, ValidTypes, [
      // With primary expressions.
      t => "< " + t + " > 42",
      t => "< " + t + " > x",
      t => "< " + t + " > (x+42)",
      t => "< " + t + " > [1,2,3]",
      t => "< " + t + " > {a: 42}",
      t => "< " + t + " > function() {}",
      t => "< " + t + " > function*() {}",
      t => "< " + t + " > class C {}",
      // With expressions of lower precedence.
      t => "< " + t + " > x + 42",
      t => "< " + t + " > x ? 1 : 0",
      // With other unary expressions.
      t => "< " + t + " > + x",
      t => "< " + t + " > ! x",
      t => "< " + t + " > typeof x",
      t => "< " + t + " > void x",
      // With expressions of higher precedence.
      t => "< " + t + " > x++",
      t => "< " + t + " > (fo().x)++",
      t => "< " + t + " > ++x",
      t => "< " + t + " > ++(fo().x)",
      t => "< " + t + " > fo(42)",
      t => "< " + t + " > ff(42)(17)",
      t => "< " + t + " > fa(42)[0]",
      t => "< " + t + " > fo(42).x",
      t => "< " + t + " > a[0]",
      t => "< " + t + " > o.a",
      t => "< " + t + " > new fo(42)",
      t => "< " + t + " > new fa(42)[0]",
      t => "< " + t + " > new fo(42).x",
      t => "< " + t + " > new new ff(42)",
      t => "< " + t + " > new new ffa(42)[0]",
      t => "< " + t + " > new new ffo(42).x"
    ])
  ]);
}

function InvalidTypeAssertions(size) {
  return Generate(size, [
    "<number, string> 42",
    "<A, B> 42",
    "<A, B> (42)",
    "<A, B> (x)",
    "<A extends number> 42",
    "<A extends number> (42)",
    "<A extends number> (x)",
    "<> 42",
    "<> (42)",
    "<> (x)",
    new TestGen(1, ValidTypes, [
      // With assignment expressions (lower precedence).
      t => "< " + t + " > yield",
      t => "< " + t + " > yield 42"
    ]),
    new TestGen(1, InvalidTypes, [
      // With primary expressions.
      t => "< " + t + " > 42",
      t => "< " + t + " > x",
      t => "< " + t + " > (x+42)",
      t => "< " + t + " > [1,2,3]",
      t => "< " + t + " > {a: 42}",
      t => "< " + t + " > function() {}",
      t => "< " + t + " > function*() {}",
      t => "< " + t + " > class C() {}",
      // With expressions of lower precedence.
      t => "< " + t + " > x + 42",
      t => "< " + t + " > x ? 1 : 0",
      // With other unary expressions.
      t => "< " + t + " > + x",
      t => "< " + t + " > ! x",
      t => "< " + t + " > typeof x",
      t => "< " + t + " > void x",
      // With expressions of higher precedence.
      t => "< " + t + " > x++",
      t => "< " + t + " > (fo().x)++",
      t => "< " + t + " > ++x",
      t => "< " + t + " > ++(fo().x)",
      t => "< " + t + " > fo(42)",
      t => "< " + t + " > ff(42)(17)",
      t => "< " + t + " > fa(42)[0]",
      t => "< " + t + " > fo(42).x",
      t => "< " + t + " > a[0]",
      t => "< " + t + " > o.a",
      t => "< " + t + " > new fo(42)",
      t => "< " + t + " > new fa(42)[0]",
      t => "< " + t + " > new fo(42).x",
      t => "< " + t + " > new new ff(42)",
      t => "< " + t + " > new new ffa(42)[0]",
      t => "< " + t + " > new new ffo(42).x"
    ]),
    // This should be a syntax error (tokens << and >> are operators).
    "<<A>(x: A) : A> 42",
    "<I<number>> 42",
    "<I<J<number>>> 42",
    "<I<J<K<number>>>> 42",
    "<<A>(x: A) : I<A>> 42"
  ]);
}

function InvalidReferenceTypeAssertions(size) {
  return Generate(size, [
    new TestGen(1, ValidTypes, [
      // With assignment expressions (lower precedence).
      t => "< " + t + " > x = 42",
      t => "< " + t + " > x += 42",
    ])
  ]);
}

(function TestTypeAssertions(size) {
  Test(size, [
    new TestGen(4, ValidTypeAssertions, [CheckValidInContext]),
    new TestGen(4, InvalidTypeAssertions, [CheckInvalidInContext]),
    new TestGen(1, InvalidReferenceTypeAssertions, [CheckInvalidReferenceInContext])
  ]);
})(test_size);
