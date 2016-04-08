// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var debug = false;

function CheckValid(script) {
  if (debug) { print("V:", script); }
  assertDoesNotThrow("'use types'; " + script);
}

function CheckInvalid(script, exception=SyntaxError) {
  if (debug) { print("I:", script); }
  assertThrows("'use types'; " + script, exception);
}

function CheckValidType(type) {
  CheckValid("var x: " + type + ";");
}

function CheckInvalidType(type, exception=SyntaxError) {
  CheckInvalid("var x: " + type + ";", exception);
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
class TestGen {
  constructor(multiplicity, generator, transformations, ...params) {
    // Determines how often this generator will run, compared to other ones.
    this.multiplicity = multiplicity;
    // The generator function.  It will be called with (size, ...params),
    // for an appropriate size provided externally to method initialize.
    this.generator = generator;
    // The list of transformation functions, excluding false ones.
    this.transformations = transformations.filter(f => f !== false);
    // The optional parameters to be passed to the generator function.
    this.params = params;
  }
  // Returns how many tests this test generator is expected to yield
  // in a row.
  weight() {
    return this.multiplicity * this.transformations.length;
  }
  // Initialize the generator function.
  initialize(size) {
    this.factory = this.generator(size, ...this.params);
  }
  // Is the test generator exhausted?
  exhausted() {
    return this.factory === null;
  }
  // Return a generator that will yield up to weight tests.
  // It returns an Iterator<OtherType>.
  * [Symbol.iterator]() {
    for (let i = 0; i < this.multiplicity; i++) {
      let element = this.factory.next();
      if (element.done) {
        this.factory = null;
        return;
      }
      for (let f of this.transformations) yield f(element.value);
    }
  }
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
      for (let test of gen) {
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
// whose transformation functions test for validity (e.g. CheckValidType
// or CheckInvalidType) and do not return anything interesting.
function Test(size, generators) {
  for (let attempt of Generate(size, generators)) continue;
}
