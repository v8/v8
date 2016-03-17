// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/testgen.js");


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = 1000;


// In the rest, for each NonTerminal symbol in the parser grammar that we
// care to test, there are two generator functions (ValidNonTerminal and
// InvalidNonTerminal) yielding valid and non valid terms for this symbol.
// These functions are of the form to be passed to Generate.
// There is also a test (using the TestNonTerminal function).


// Primary types.

function ValidPrimaryTypes(size, proper=false) {
  return Generate(size, [
    "any",
    "void",
    "this",
    new TestGen(1, ValidTypes, [
      t => "(" + t + ")"
    ]),
    new TestGen(1, ValidPrimaryTypes, [
      t => t + "[]",
      t => t + "[][]",
      t => "(" + t + "[])",
      t => "(" + t + "[])[]",
    ]),
    proper && "number",
    proper && "boolean",
    proper && "string",
    proper && "symbol"
  ]);
}

function InvalidPrimaryTypes(size, proper=false) {
  return Generate(size, [
    // Undefined variable.  Removed, this is a semantic error now.
    // "whatever",
    // Legal parenthesized parameter lists that are not types.
    "()", "(a: number, b: string)", "(x, y, z)",
    // Illegal types in legal places.
    new TestGen(1, InvalidTypes, [
      t => "(" + t + ")"
    ]),
    new TestGen(1, InvalidPrimaryTypes, [
      t => t + "[]",
      t => t + "[][]",
      t => "(" + t + "[])",
      t => "(" + t + "[])[]",
    ]),
    // Line terminator in arrays.
    new TestGen(1, ValidTypes, [
      t => "(" + t + "\n[])"
    ])
  ]);
}

(function TestPrimaryTypes(size) {
  Test(size, [
    new TestGen(4, ValidPrimaryTypes, [CheckValid], true),
    new TestGen(1, InvalidPrimaryTypes, [CheckInvalid], true)
  ]);
})(test_size);


// Intersection types.

function ValidIntersectionTypes(size, proper=false) {
  return Generate(size, [
    new TestGen(1, ValidPrimaryTypes, [
      !proper && (t => t),
      t => t + " & " + t,
      t => "(" + t + " & " + t + ") & " + t,
      t => t + " & (" + t + " & " + t + ")",
      t => t + " & " + t + " & " + t
    ])
  ]);
}

function InvalidIntersectionTypes(size, proper=false) {
  return Generate(size, [
    // Illegal types in legal places.
    new TestGen(4, InvalidPrimaryTypes, [
      !proper && (t => t),
      t => t + " & " + t,
      t => "(" + t + " & " + t + ") & " + t,
      t => t + " & (" + t + " & " + t + ")",
      t => t + " & " + t + " & " + t
    ]),
    // Right hand side is a function or constructor type.
    new TestGen(1, ValidFunctionTypes, [t => "any & " + t], false),
    new TestGen(1, ValidFunctionTypes, [t => "any & " + t], true)
  ]);
}

(function TestIntersectionTypes(size) {
  Test(size, [
    new TestGen(4, ValidIntersectionTypes, [CheckValid], true),
    new TestGen(1, InvalidIntersectionTypes, [CheckInvalid], true)
  ]);
})(test_size);


// Union types.

function ValidUnionTypes(size, proper=false) {
  return Generate(size, [
    new TestGen(1, ValidIntersectionTypes, [
      !proper && (t => t),
      t => t + " | " + t,
      t => "(" + t + " | " + t + ") | " + t,
      t => t + " | (" + t + " | " + t + ")",
      t => t + " | " + t + " | " + t
    ])
  ]);
}

function InvalidUnionTypes(size, proper=false) {
  return Generate(size, [
    // Illegal types in legal places.
    new TestGen(1, InvalidIntersectionTypes, [
      !proper && (t => t),
      t => t + " | " + t,
      t => "(" + t + " | " + t + ") | " + t,
      t => t + " | (" + t + " | " + t + ")",
      t => t + " | " + t + " | " + t
    ]),
    // Right hand side is a function or constructor type.
    new TestGen(1, ValidFunctionTypes, [t => "any | " + t], false),
    new TestGen(1, ValidFunctionTypes, [t => "any | " + t], true)
  ]);
}

(function TestUnionTypes(size) {
  Test(size, [
    new TestGen(4, ValidUnionTypes, [CheckValid], true),
    new TestGen(1, InvalidUnionTypes, [CheckInvalid], true)
  ]);
})(test_size);


// Function and constructor types.

function ValidFunctionTypes(size, constr) {
  let c = constr ? "new " : "";
  return Generate(size, [
    new TestGen(1, ValidTypes, [
      t => c + "() => " + t,
      t => c + "(a: " + t + ") => " + t,
      t => c + "(a:" + t + ", b?:" + t + ") => " + t,
      t => c + "(a:" + t + ", b?:" + t + ", c) => " + t,
      t => c + "(a:" + t + ", b?:" + t + ", c?) => " + t,
      t => c + "(a:" + t + ", b:" + t + ", c, ...d) => " + t,
      t => c + "(a:" + t + ", b:" + t + ", c, ...d) => " + t,
      t => c + "(a:" + t + ", b:" + t + ", c, ...d: string[]) => " + t
    ])
  ]);
}

function InvalidFunctionTypes(size, constr) {
  let c = constr ? "new " : "";
  return Generate(size, [
    // Illegal types in legal places.
    new TestGen(1, InvalidTypes, [
      t => c + "() => " + t,
      t => c + "(a: " + t + ") => " + t,
      t => c + "(a:" + t + ", b?:" + t + ") => " + t,
      t => c + "(a:" + t + ", b?:" + t + ", c) => " + t,
      t => c + "(a:" + t + ", b?:" + t + ", c?) => " + t,
      t => c + "(a:" + t + ", b:" + t + ", c, ...d) => " + t,
      t => c + "(a:" + t + ", b:" + t + ", c, ...d) => " + t,
      t => c + "(a:" + t + ", b:" + t + ", c, ...d: string[]) => " + t
    ])
  ]);
}

(function TestFunctionAndConstructorTypes(size) {
  Test(size, [
    new TestGen(4, ValidFunctionTypes, [CheckValid], false),
    new TestGen(1, InvalidFunctionTypes, [CheckInvalid], false),
    new TestGen(4, ValidFunctionTypes, [CheckValid], true),
    new TestGen(1, InvalidFunctionTypes, [CheckInvalid], true)
  ]);
})(test_size);


// All simple types.

function ValidTypes(size) {
  return Generate(size, [
    new TestGen(3, ValidUnionTypes, [t => t]),
    new TestGen(1, ValidFunctionTypes, [t => t], false),
  ]);
}

function InvalidTypes(size) {
  return Generate(size, [
    new TestGen(3, InvalidUnionTypes, [t => t]),
    new TestGen(1, InvalidFunctionTypes, [t => t], false),
  ]);
}
