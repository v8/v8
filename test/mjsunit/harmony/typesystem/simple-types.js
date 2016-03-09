// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types

let test_size = 1000;

function CheckValid(type) {
  // print("V:", type);
  assertDoesNotThrow("'use types'; var x: " + type + ";");
}

function CheckInvalid(type) {
  // print("I:", type);
  assertThrows("'use types'; var x: " + type + ";", SyntaxError);
}

function Test(size, listgen) {
  for (let attempt of Serve(size, listgen)) continue;
}

function* Serve(size, listgen) {
  let fixed = 0, flexible = 0;
  for (let gen of listgen) {
    if (typeof gen === "string") fixed++;
    else flexible += gen[0] * gen[2].length;
  }
  if (fixed + flexible == 0) throw "Empty generator";
  let remaining = 0;
  for (let gen of listgen) {
    if (typeof gen !== "string") {
      let [freq, G, F, ...params] = gen;
      let weight = 1 + freq * F.length;
      gen.factory = G(Math.ceil(size * weight / flexible), ...params);
      remaining++;
    }
  }
  for (let once = true; true; once = false) {
    if (remaining == 0) return;
    for (let gen of listgen) {
      if (typeof gen === "string") {
        if (once) {
          if (size-- <= 0) return; else yield gen;
        }
        continue;
      }
      let G = gen.factory;
      if (!G) continue;
      for (let i = 0; i < gen[0]; i++) {
        let element = G.next();
        if (!element.done) {
          for (let f of gen[2]) {
            if (size-- <= 0) return; else yield f(element.value);
          }
        }
        else {
          gen.factory = null;
          remaining--;
        }
      }
    }
  }
}

function* ValidPrimaryTypes(size, proper=false) {
  let L = ["any", "void", "this" ];
  L.push([1, ValidTypes, [
    (t) => "(" + t + ")"
  ]]);
  L.push([1, ValidPrimaryTypes, [
    (t) => t + "[]",
    (t) => t + "[][]",
    (t) => "(" + t + "[])",
    (t) => "(" + t + "[])[]",
  ]]);
  if (proper) L.push("number", "boolean", "string", "symbol");
  yield* Serve(size, L);
}

function* InvalidPrimaryTypes(size, proper=false) {
  let L = [
    // Undefined variable.  Removed, this is a semantic error now.
    // "whatever",
    // Legal parenthesized parameter lists that are not types.
    "()", "(a: number, b: string)", "(x, y, z)"
  ];
  // Illegal types in legal places.
  L.push([1, InvalidTypes, [
    (t) => "(" + t + ")"
  ]]);
  L.push([1, InvalidPrimaryTypes, [
    (t) => t + "[]",
    (t) => t + "[][]",
    (t) => "(" + t + "[])",
    (t) => "(" + t + "[])[]",
  ]]);
  // Line terminator in arrays.
  L.push([1, ValidTypes, [
    (t) => "(" + t + "\n[])"
  ]]);
  yield* Serve(size, L);
}

(function TestPrimaryTypes(size) {
  Test(size, [
    [4, ValidPrimaryTypes, [CheckValid], true],
    [1, InvalidPrimaryTypes, [CheckInvalid], true]
  ]);
})(test_size);

function* ValidIntersectionTypes(size, proper=false) {
  let F = [];
  if (!proper) F.push(...[
    (t) => t
  ]);
  F.push(...[
    (t) => t + " & " + t,
    (t) => "(" + t + " & " + t + ") & " + t,
    (t) => t + " & (" + t + " & " + t + ")",
    (t) => t + " & " + t + " & " + t
  ]);
  let L = [[1, ValidPrimaryTypes, F]];
  yield* Serve(size, L);
}

function* InvalidIntersectionTypes(size, proper=false) {
  // Illegal types in legal places.
  let F = [];
  if (!proper) F.push(...[
    (t) => t
  ]);
  F.push(...[
    (t) => t + " & " + t,
    (t) => "(" + t + " & " + t + ") & " + t,
    (t) => t + " & (" + t + " & " + t + ")",
    (t) => t + " & " + t + " & " + t
  ]);
  let L = [[4, InvalidPrimaryTypes, F]];
  // Right hand side is a function or constructor type.
  L.push([1, ValidFunctionTypes, [(t) => "any & " + t], false]);
  L.push([1, ValidFunctionTypes, [(t) => "any & " + t], true]);
  yield* Serve(size, L);
}

(function TestIntersectionTypes(size) {
  Test(size, [
    [4, ValidIntersectionTypes, [CheckValid], true],
    [1, InvalidIntersectionTypes, [CheckInvalid], true]
  ]);
})(test_size);

function* ValidUnionTypes(size, proper=false) {
  let F = [];
  if (!proper) F.push(...[
    (t) => t
  ]);
  F.push(...[
    (t) => t + " | " + t,
    (t) => "(" + t + " | " + t + ") | " + t,
    (t) => t + " | (" + t + " | " + t + ")",
    (t) => t + " | " + t + " | " + t
  ]);
  let L = [[1, ValidIntersectionTypes, F]];
  yield* Serve(size, L);
}

function* InvalidUnionTypes(size, proper=false) {
  // Illegal types in legal places.
  let F = [];
  if (!proper) F.push(...[
    (t) => t
  ]);
  F.push(...[
    (t) => t + " | " + t,
    (t) => "(" + t + " | " + t + ") | " + t,
    (t) => t + " | (" + t + " | " + t + ")",
    (t) => t + " | " + t + " | " + t
  ]);
  let L = [[1, InvalidIntersectionTypes, F]];
  // Right hand side is a function or constructor type.
  L.push([1, ValidFunctionTypes, [(t) => "any | " + t], false]);
  L.push([1, ValidFunctionTypes, [(t) => "any | " + t], true]);
  yield* Serve(size, L);
}

(function TestUnionTypes(size) {
  Test(size, [
    [4, ValidUnionTypes, [CheckValid], true],
    [1, InvalidUnionTypes, [CheckInvalid], true]
  ]);
})(test_size);

function* ValidFunctionTypes(size, constr) {
  let c = constr ? "new " : "";
  let L = [[1, ValidTypes, [
    (t) => c + "() => " + t,
    (t) => c + "(a: " + t + ") => " + t,
    (t) => c + "(a:" + t + ", b?:" + t + ") => " + t,
    (t) => c + "(a:" + t + ", b?:" + t + ", c) => " + t,
    (t) => c + "(a:" + t + ", b?:" + t + ", c?) => " + t,
    (t) => c + "(a:" + t + ", b:" + t + ", c, ...d) => " + t,
    (t) => c + "(a:" + t + ", b:" + t + ", c, ...d) => " + t,
    (t) => c + "(a:" + t + ", b:" + t + ", c, ...d: string[]) => " + t,
    //(t) => c + "(a: 'string-lit', b: " + t + ") => " + t,
    //(t) => c + "(a?: 'string-lit', b?: " + t + ") => " + t
  ]]];
  yield* Serve(size, L);
}

function* InvalidFunctionTypes(size, constr) {
  // Illegal types in legal places.
  let c = constr ? "new " : "";
  let L = [[1, InvalidTypes, [
    (t) => c + "() => " + t,
    (t) => c + "(a: " + t + ") => " + t,
    (t) => c + "(a:" + t + ", b?:" + t + ") => " + t,
    (t) => c + "(a:" + t + ", b?:" + t + ", c) => " + t,
    (t) => c + "(a:" + t + ", b?:" + t + ", c?) => " + t,
    (t) => c + "(a:" + t + ", b:" + t + ", c, ...d) => " + t,
    (t) => c + "(a:" + t + ", b:" + t + ", c, ...d) => " + t,
    (t) => c + "(a:" + t + ", b:" + t + ", c, ...d: string[]) => " + t,
  ]]];
  yield* Serve(size, L);
}

(function TestFunctionAndConstructorTypes(size) {
  Test(size, [
    [4, ValidFunctionTypes, [CheckValid], false],
    [1, InvalidFunctionTypes, [CheckInvalid], false],
    [4, ValidFunctionTypes, [CheckValid], true],
    [1, InvalidFunctionTypes, [CheckInvalid], true]
  ]);
})(test_size);

function* ValidTypes(size) {
  let L = [
    [3, ValidUnionTypes, [(t) => t]],
    [1, ValidFunctionTypes, [(t) => t], false],
  ];
  yield* Serve(size, L);
}

function* InvalidTypes(size) {
  let L = [
    [3, InvalidUnionTypes, [(t) => t]],
    [1, InvalidFunctionTypes, [(t) => t], false],
  ];
  yield* Serve(size, L);
}
