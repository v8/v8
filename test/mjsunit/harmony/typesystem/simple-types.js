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

function Test(f, gen, size, ...rest) {
  for (let t of gen(size, ...rest)) f(t);
}

function div(x, y) {
  return Math.floor(x / y);
}

function factor(size, divisor, proper) {
  if (!proper) divisor++;
  let result = div(divisor-1, divisor);
  return (result == 0) ? div(size+1, 2) : result;
}

function* ValidPrimaryTypes(size, proper=false) {
  if (size-- <= 0) return; else yield "any";
  if (size-- <= 0) return; else yield "void";
  if (size-- <= 0) return; else yield "this"
  let g1 = ValidTypes(factor(size, 5, true));
  let g2 = ValidPrimaryTypes(factor(size, 5, true));
  for (let done_simple = false; true; done_simple = true) {
    let t1 = g1.next().value;
    let t2 = g2.next().value;
    if (size-- <= 0) return; else yield "(" + t1 + ")";
    if (size-- <= 0) return; else yield t2 + "[]";
    if (size-- <= 0) return; else yield t2 + "[][]";
    if (size-- <= 0) return; else yield "(" + t2 + "[])";
    if (size-- <= 0) return; else yield "(" + t2 + "[])[]";
    if (done_simple) continue;
    if (size-- <= 0) return; else yield "number";
    if (size-- <= 0) return; else yield "boolean";
    if (size-- <= 0) return; else yield "string";
    if (size-- <= 0) return; else yield "symbol";
  }
}

function* InvalidPrimaryTypes(size, proper=false) {
  // Undefined variable.
  if (size-- <= 0) return; else yield "whatever";
  // Legal parenthesized parameter lists that are not types.
  if (size-- <= 0) return; else yield "()";
  if (size-- <= 0) return; else yield "(number, string)";
  if (size-- <= 0) return; else yield "(number, string, void)";
  // Illegal types in legal places.
  let g1 = InvalidTypes(factor(size, 5, true));
  let g2 = InvalidPrimaryTypes(factor(size, 5, true));
  while (true) {
    let t1 = g1.next().value;
    let t2 = g2.next().value;
    if (size-- <= 0) return; else yield "(" + t1 + ")";
    if (size-- <= 0) return; else yield t2 + "[]";
    if (size-- <= 0) return; else yield t2 + "[][]";
    if (size-- <= 0) return; else yield "(" + t2 + "[])";
    if (size-- <= 0) return; else yield "(" + t2 + "[])[]";
  }
  if (proper) {
    // Legal intersection types
    // Legal union types
    // Legal function types
    // Legal constructor types
  }
}

(function TestPrimaryTypes(size) {
  Test(CheckValid, ValidPrimaryTypes, size - div(size, 5), true);
  Test(CheckInvalid, InvalidPrimaryTypes, div(size, 5), true);
})(test_size);

function* ValidIntersectionTypes(size, proper=false) {
  let g = ValidPrimaryTypes(factor(size, 4, proper));
  while (true) {
    let t = g.next().value;
    if (!proper) {
      if (size-- <= 0) return; else yield t;
    }
    if (size-- <= 0) return; else yield t + " & " + t;
    if (size-- <= 0) return; else yield "(" + t + " & " + t + ") & " + t;
    if (size-- <= 0) return; else yield t + " & (" + t + " & " + t + ")";
    if (size-- <= 0) return; else yield t + " & " + t + " & " + t;
  }
}

function* InvalidIntersectionTypes(size, proper=false) {
  // Illegal types in legal places.
  let g = InvalidPrimaryTypes(factor(size, 4, proper));
  while (true) {
    let t = g.next().value;
    if (!proper) {
      if (size-- <= 0) return; else yield t;
    }
    if (size-- <= 0) return; else yield t + " & " + t;
    if (size-- <= 0) return; else yield "(" + t + " & " + t + ") & " + t;
    if (size-- <= 0) return; else yield t + " & (" + t + " & " + t + ")";
    if (size-- <= 0) return; else yield t + " & " + t + " & " + t;
  }
  if (proper) {
    // Legal union types
    // Legal function types
    // Legal constructor types
  }
}

(function TestIntersectionTypes(size) {
  Test(CheckValid, ValidIntersectionTypes, size - div(size, 5), true);
  Test(CheckInvalid, InvalidIntersectionTypes, div(size, 5), true);
})(test_size);

function* ValidUnionTypes(size, proper=false) {
  let g = ValidIntersectionTypes(factor(size, 4, proper));
  while (true) {
    let t = g.next().value;
    if (!proper) {
      if (size-- <= 0) return; else yield t;
    }
    if (size-- <= 0) return; else yield t + " | " + t;
    if (size-- <= 0) return; else yield "(" + t + " | " + t + ") | " + t;
    if (size-- <= 0) return; else yield t + " | (" + t + " | " + t + ")";
    if (size-- <= 0) return; else yield t + " | " + t + " | " + t;
  }
}

function* InvalidUnionTypes(size, proper=false) {
  // Illegal types in legal places.
  let g = InvalidIntersectionTypes(factor(size, 4, proper));
  while (true) {
    let t = g.next().value;
    if (!proper) {
      if (size-- <= 0) return; else yield t;
    }
    if (size-- <= 0) return; else yield t + " | " + t;
    if (size-- <= 0) return; else yield "(" + t + " | " + t + ") | " + t;
    if (size-- <= 0) return; else yield t + " | (" + t + " | " + t + ")";
    if (size-- <= 0) return; else yield t + " | " + t + " | " + t;
  }
  if (proper) {
    // Legal function types
    // Legal constructor types
  }
}

(function TestUnionTypes(size) {
  Test(CheckValid, ValidUnionTypes, size - div(size, 5), true);
  Test(CheckInvalid, InvalidUnionTypes, div(size, 5), true);
})(test_size);

function* ValidFunctionTypes(size, constr) {
  let g = ValidTypes(factor(size, 4, true));
  let c = constr ? "new " : "";
  while (true) {
    let t = g.next().value;
    if (size-- <= 0) return; else yield c + "() => " + t;
    if (size-- <= 0) return; else yield c + "(" + t + ") => " + t;
    if (size-- <= 0) return; else yield c + "(" + t + ", " + t + ") => " + t;
    if (size-- <= 0) return; else yield c + "(" + t + ", " + t + ", " + t + ") => " + t;
  }
}

function* InvalidFunctionTypes(size, constr) {
  // Illegal types in legal places.
  let g = InvalidTypes(factor(size, 4, true));
  let c = constr ? "new " : "";
  while (true) {
    let t = g.next().value;
    if (size-- <= 0) return; else yield c + "() => " + t;
    if (size-- <= 0) return; else yield c + "(" + t + ") => " + t;
    if (size-- <= 0) return; else yield c + "(" + t + ", " + t + ") => " + t;
    if (size-- <= 0) return; else yield c + "(" + t + ", " + t + ", " + t + ") => " + t;
  }
}

(function TestFunctionAndConstructorTypes(size) {
  Test(CheckValid, ValidFunctionTypes, size - div(size, 2) - div(size, 10), false);
  Test(CheckInvalid, InvalidFunctionTypes, div(size, 10), false);
  Test(CheckValid, ValidFunctionTypes, div(size, 2) - div(size, 10), true);
  Test(CheckInvalid, InvalidFunctionTypes, div(size, 10), true);
})(test_size);

function* ValidTypes(size) {
  let g1 = ValidUnionTypes(size - div(size, 4));
  let g2 = ValidFunctionTypes(div(size, 4));
  while (true) {
    for (let i=0; i<3; i++) {
      let t1 = g1.next().value;
      if (size-- <= 0) return; else yield t1;
    }
    for (let i=0; i<1; i++) {
      let t2 = g2.next().value;
      if (size-- <= 0) return; else yield t2;
    }
  }
}

function* InvalidTypes(size) {
  let g1 = InvalidUnionTypes(size - div(size, 4));
  let g2 = InvalidFunctionTypes(div(size, 4));
  while (true) {
    for (let i=0; i<3; i++) {
      let t1 = g1.next().value;
      if (size-- <= 0) return; else yield t1;
    }
    for (let i=0; i<1; i++) {
      let t2 = g2.next().value;
      if (size-- <= 0) return; else yield t2;
    }
  }
}
