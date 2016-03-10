// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = 1000;

function CheckValid(type) {
  // print("V:", type);
  assertDoesNotThrow("'use types'; var x: " + type + ";");
}

function CheckInvalid(type) {
  // print("I:", type);
  assertThrows("'use types'; var x: " + type + ";", SyntaxError);
}

// Parameters:
//   multiplicity: positive integer
//   generator: (size: positive integer, ...params: any[]) => Iterator<SomeType>
//   transformations: [false | ((t: SomeType) => OtherType)]
//   params: any[]
//
// A "test generator" object will have its "generator" generate tests,
// will transform each generated test with all of its "transformations"
// and will yield the results.  false transformations will be ignored.
function TestGen(multiplicity, generator, transformations, ...params) {
  // Determines how often this generator will run, compared to other ones.
  this.multiplicity = multiplicity;
  // The generator function.  It will be called with (size, ...params),
  // for an appropriate size provided externally to method initialize.
  this.generator = generator;
  // The list of transformation functions, excluding false ones.
  this.transformations = transformations.filter(f => f !== false);
  // The optional parameters to be passed to the generator function.
  this.params = params;
  // Returns how many tests this test generator is expected to yield
  // in a row.
  this.weight = function() {
    return this.multiplicity * transformations.length;
  };
  // Initialize the generator function.
  this.initialize = function(size) {
    this.factory = this.generator(size, ...this.params);
  };
  // Is the test generator exhausted?
  this.exhausted = function() {
    return this.factory === null;
  };
  // Return a generator that will yield up to weight tests.
  // It returns an Iterator<OtherType>.
  this.tests = function*() {
    for (let i = 0; i < this.multiplicity; i++) {
      let element = this.factory.next();
      if (element.done) {
        this.factory = null;
        return;
      }
      for (let f of this.transformations) yield f(element.value);
    }
  };
}

// Parameters:
//   size:       positive integer
//   generators: [false | string | TestGen]
//
// This generator function will yield up to size tests.  It will iterate
// cyclically through the list of test generators.  A string in this list
// behaves as a simple generator yielding just one test; a false value
// behaves as a generator yielding nothing.  For every test generator in
// the list, this function will generate as many tests as its weight
// before proceeding to the next test generator.  Once a generator is
// exhausted, it is ignored in subsequent iterations.
function* Generate(size, generators) {
  let fixed = 0, flexible = 0;
  for (let gen of generators) {
    if (gen === false) continue;
    if (typeof gen === "string") fixed++;
    else flexible += gen.weight();
  }
  if (fixed + flexible == 0) throw "Empty list of test generators";
  let remaining = 0;
  for (let gen of generators) {
    if (gen === false) continue;
    if (typeof gen !== "string") {
      let weight = 1 + gen.weight();
      gen.initialize(Math.ceil(size * weight / flexible));
      remaining++;
    }
  }
  for (let once = true; true; once = false) {
    if (remaining == 0) return;
    for (let gen of generators) {
      if (gen === false) continue;
      if (typeof gen === "string") {
        if (once) {
          if (size-- <= 0) return; else yield gen;
        }
        continue;
      }
      if (gen.exhausted()) continue;
      for (test of gen.tests()) {
        if (size-- <= 0) return; else yield test;
      }
      if (gen.exhausted()) remaining--;
    }
  }
}

// Parameters:
//   size:       positive integer
//   generators: [string | TestGen]
//
// This function will generate all tests yielded by Generate and will
// discard the results.  It will normally be called with test generators
// whose transformation functions test for validity (e.g. CheckValid or
// CheckInvalid) and do not return anything interesting.
function Test(size, generators) {
  for (let attempt of Generate(size, generators)) continue;
}


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
