// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


load("test/mjsunit/harmony/typesystem/testgen.js");


// In the rest, for each NonTerminal symbol in the parser grammar that we
// care to test, there are two generator functions (ValidNonTerminal and
// InvalidNonTerminal) yielding valid and non valid terms for this symbol.
// These functions are of the form to be passed to Generate.
//
// Actual tests are in separate test files.


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
    new TestGen(1, ValidTupleTypes, [t => t]),
    new TestGen(1, ValidObjectTypes, [t => t]),
    proper && "number",
    proper && "boolean",
    proper && "string",
    proper && "symbol",
    // Type references
    "A",
    "Tree<number>",
    "Map<string, number>",
    // Type queries
    "typeof x",
    "typeof x.a",
    "typeof x.a.b.c"
  ]);
}

function InvalidPrimaryTypes(size, proper=false) {
  return Generate(size, [
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
    new TestGen(1, InvalidTupleTypes, [t => t]),
    new TestGen(1, InvalidObjectTypes, [t => t]),
    // Line terminator in arrays.
    new TestGen(1, ValidTypes, [
      t => "(" + t + "\n[])"
    ]),
    // Type references
    "Map<string, (number, void)>",
    "number<string>",
    "Foo<>",
    // Type queries
    "typeof",
    "typeof x.",
    "typeof x => any"
  ]);
}


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
    ]),
    // Parametric function types.
    "<A> (x: A) => A",
    "<A extends string> (x: A) => A",
    "<A, B> (x: A, y: B) => A",
    "<A, B extends number[], C> (x: A, y: B) => C",
    "<A, B, C, D> (x: A, y: B, z: C[], d: (e: D) => D) => A",
    // String literal types.
    "(cmd: 'add', x: number, y: number) => number",
    '(cmd: "sum", a: number[]) => number',
    "(x: number, cmd: 'one', ...rest) => any",
    "(x: string, y: number, cmd: 'two', ...rest) => any",
    "(x: number, cmd?: 'two', ...rest) => string",
    // With binding patterns.
    "([]: number[]) => boolean",
    "([x, y]: number[]) => boolean",
    "([x, ...rest]: number[]) => boolean",
    "([,x, y]: number[]) => boolean",
    "([x, y,]: number[]) => boolean",
    "([x, y,,]: number[]) => boolean",
    "([x, [a, b, c], z]: [number, any[], string]) => boolean",
    "({x: a, y: b}: {x: number, y: string}) => boolean",
    "({x: a, y: [b, c, ...rest]}: {x: number, y: string[]}) => boolean",
    "({x: {a, b}, y: c}: {x: {a, b}, y: string}) => boolean"
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
      t => c + "(a:" + t + ", b:" + t + ", c, ...d: string[]) => " + t,
    ]),
    // Parametric function types.
    "<A> A[]",
    "<> (x: number) => number",
    "<A extends ()> (x: A) => A",
    // With illegal binding patterns.
    "({x: {a; b}, y: c}: {x: {a; b}, y: string}) => boolean"
  ]);
}


// Tuple types.

function ValidTupleTypes(size) {
  return Generate(size, [
    new TestGen(1, ValidTypes, [
      t => "[" + t + "]",
      t => "[" + t + ", " + t + "]",
      t => "[" + t + ", " + t + ", " + t + "]"
    ])
  ]);
}

function InvalidTupleTypes(size) {
  return Generate(size, [
    // Illegal types in legal places.
    new TestGen(1, InvalidTypes, [
      t => "[" + t + "]",
      t => "[" + t + ", " + t + "]",
      t => "[" + t + ", " + t + ", " + t + "]"
    ]),
    // Valid binding patterns that are not tuple types.
    "[]",
    new TestGen(1, ValidTypes, [t => "[" + t + ",]"]),
    new TestGen(1, ValidTypes, [t => "[" + t + ",,]"]),
    new TestGen(1, ValidTypes, [t => "[," + t + "]"]),
    new TestGen(1, ValidTypes, [t => "[,," + t + "]"]),
    new TestGen(1, ValidTypes, [t => "[" + t + ",," + t + "]"]),
    new TestGen(1, ValidTypes, [t => "[..." + t + "]"]),
    new TestGen(1, ValidTypes, [t => "[" + t + ", ..." + t + "]"]),
  ]);
}


// Object types.

function ValidObjectTypes(size) {
  return Generate(size, [
    // Empty object type.
    "{}",
    // Property signatures.
    "{a}",
    "{a, b}",
    "{a, b?}",
    "{a?, b}",
    new TestGen(1, ValidTypes, [
      t => "{a: " + t + "}",
      t => "{a: " + t + ", b: " + t + "}",
      t => "{a?: " + t + "}",
      t => "{a: " + t + ", b?: " + t + "}",
    ]),
    // Method signatures.
    "{f()}",
    "{f?()}",
    "{f(a)}",
    "{f?(a)}",
    "{f(a, b)}",
    "{f?(a, b)}",
    new TestGen(1, ValidTypes, [
      t => "{f() : " + t + "}",
      t => "{f?() : " + t + "}",
      t => "{f(a: " + t + ")}",
      t => "{f?(a: " + t + ")}",
      t => "{f(a: " + t + ") : " + t + "}",
      t => "{f?(a: " + t + ") : " + t + "}",
      t => "{f<A>(a: " + t + ")}",
      t => "{f?<A>(a: " + t + ")}",
      t => "{f<A, B>(a: A, b: " + t + ") : B}",
      t => "{f?<A, B>(a: A, b: " + t + ") : B}",
      t => "{f<A extends " + t + ">(a: " + t + ")}",
      t => "{f?<A extends " + t + ">(a: " + t + ")}",
      t => "{f<A extends " + t + ", B>(a: A, b: " + t + ") : B}",
      t => "{f?<A extends " + t + ", B>(a: A, b: " + t + ") : B}",
      t => "{f<A extends " + t + ">(a: " + t + ") : " + t + "}",
      t => "{f?<A extends " + t + ">(a: " + t + ") : " + t + "}"
    ]),
    // Call signatures.
    "{()}",
    "{(a)}",
    "{(a, b)}",
    new TestGen(1, ValidTypes, [
      t => "{() : " + t + "}",
      t => "{(a: " + t + ")}",
      t => "{(a: " + t + ") : " + t + "}",
      t => "{<A extends " + t + ">(a: " + t + ")}",
      t => "{<A extends " + t + ">(a: " + t + ") : " + t + "}"
    ]),
    // Constructor signatures.
    "{new ()}",
    "{new (a)}",
    "{new (a, b)}",
    new TestGen(1, ValidTypes, [
      t => "{new () : " + t + "}",
      t => "{new (a: " + t + ")}",
      t => "{new (a: " + t + ") : " + t + "}",
      t => "{new <A extends " + t + ">(a: " + t + ")}",
      t => "{new <A extends " + t + ">(a: " + t + ") : " + t + "}"
    ]),
    // Index signatures.
    "{[a: number]}",
    "{[a: string]}",
    new TestGen(1, ValidTypes, [
      t => "{[a: number] : " + t + "}",
      t => "{[a: string] : " + t + "}"
    ])
  ]);
}

function InvalidObjectTypes(size) {
  return Generate(size, [
    // Illegal types in legal places.
    new TestGen(1, InvalidTypes, [
      t => "{a: " + t + "}",
      t => "{a: " + t + ", b?: " + t + "}",
      t => "{f() : " + t + "}",
      t => "{f(a: " + t + ")}",
      t => "{f<A extends " + t + ">()}",
      t => "{(a: " + t + ")}",
      t => "{new () : " + t + "}"
    ]),
    // Valid binding patterns that are not tuple types.
    "{a: []}",
    "{a: [...rest]}",
    // Invalid index signatures.
    "{[a: any]}",
    "{[a: any] : number}"
  ]);
}


// All types: simple, type references, type parametric, type queries
// and tuples.

function ValidTypes(size) {
  return Generate(size, [
    new TestGen(1, ValidUnionTypes, [t => t]),
    new TestGen(1, ValidFunctionTypes, [t => t], false),
  ]);
}

function InvalidTypes(size) {
  return Generate(size, [
    new TestGen(1, InvalidUnionTypes, [t => t]),
    new TestGen(1, InvalidFunctionTypes, [t => t], false),
  ]);
}
