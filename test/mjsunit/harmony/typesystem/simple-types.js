// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types

function CheckValid(type) {
  assertDoesNotThrow("'use types'; var x: " + type + ";");
}

function CheckInvalid(type) {
  assertThrows("'use types'; var x: " + type + ";", SyntaxError);
}

function GenAll(list) {
  return (f) => (t) => list.forEach((g) => g(f)(t));
}

function GenPrimaryTypes(valid, invalid) {
  var G = GenAll([
    (f) => f,
    (f) => (t) => f('(' + t + ')'),
    (f) => (t) => f(t + '[]'),
    (f) => (t) => f(t + '[][]'),
    (f) => (t) => f('(' + t + '[])'),
    (f) => (t) => f('(' + t + '[])[]'),
  ]);
  // Test predefined types
  var atoms = ["any", "boolean", "number", "string", "symbol", "void", "this"];
  atoms.forEach(G(valid));
  // Unknown identifier
  G(invalid)("whatever");
  // Legal parenthesized parameter lists that are not types
  G(invalid)("()");
  G(invalid)("(number, string)");
  G(invalid)("(number, string, void)");
}

function GenIntersectionTypes(valid, invalid) {
  var G = GenAll([
    (f) => f,
    (f) => (t) => f(t + ' & ' + t),
    (f) => (t) => f('(' + t + ' & ' + t + ') & ' + t),
    (f) => (t) => f(t + ' & (' + t + ' & ' + t + ')'),
    (f) => (t) => f(t + ' & ' + t + ' & ' + t),
  ]);
  GenPrimaryTypes(G(valid), G(invalid));
}

function GenUnionTypes(valid, invalid) {
  var G = GenAll([
    (f) => f,
    (f) => (t) => f(t + ' | ' + t),
    (f) => (t) => f('(' + t + ' | ' + t + ') | ' + t),
    (f) => (t) => f(t + ' | (' + t + ' | ' + t + ')'),
    (f) => (t) => f(t + ' | ' + t + ' | ' + t),
  ]);
  GenIntersectionTypes(G(valid), G(invalid));
}

(function TestUnionTypes() {
  GenUnionTypes(CheckValid, CheckInvalid);
})();

function GenFunctionTypes(gen, constr) {
  return (valid, invalid) => {
    var G = GenAll([
      (f) => (t) => f('() => ' + t),
      (f) => (t) => f('(' + t + ') => ' + t),
      (f) => (t) => f('(' + t + ', ' + t + ') => ' + t),
      (f) => (t) => f('(' + t + ', ' + t + ', ' + t + ') => ' + t),
    ]);
    gen(G(valid), G(invalid));
  };
}

(function TestFunctionAndConstructorTypes() {
  var GF = GenFunctionTypes(GenUnionTypes, false);
  var GC = GenFunctionTypes(GenUnionTypes, true);
  GF(CheckValid, CheckInvalid);
  GC(CheckValid, CheckInvalid);
  GenFunctionTypes(GF, false)(CheckValid, CheckInvalid);
  GenFunctionTypes(GF, true)(CheckValid, CheckInvalid);
})();
