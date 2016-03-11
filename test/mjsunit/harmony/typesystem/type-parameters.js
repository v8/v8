// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types

function CheckValid(type) {
  // print("V:", type);
  assertDoesNotThrow("'use types'; var x: " + type + ";");
}

function CheckInvalid(type) {
  // print("I:", type);
  assertThrows("'use types'; var x: " + type + ";", SyntaxError);
}

(function TestParametricFunctionTypes() {
  CheckValid("<A> (x: A) => A");
  CheckValid("<A extends string> (x: A) => A");
  CheckValid("<A, B> (x: A, y: B) => A");
  CheckValid("<A, B extends number[], C> (x: A, y: B) => C");
  CheckValid("<A, B, C, D> (x: A, y: B, z: C[], d: (e: D) => D) => A");
  // Type parameter lists in non-function types.
  CheckInvalid("<A> A[]");
  // Empty type parameter list is disallowed.
  CheckInvalid("<> (x: number) => number");
  // Invalid type in extends.
  CheckInvalid("<A extends ()> (x: A) => A");
})();

(function TestParametricTypeReferences() {
  CheckValid("Tree<number>");
  CheckValid("Map<string, number>");
  // Invalid types as arguments.
  CheckInvalid("Map<string, (number, void)>");
  // Type arguments not in type references.
  CheckInvalid("number<string>");
  // Empty type argument list is disallowed.
  CheckInvalid("Foo<>");
})();
