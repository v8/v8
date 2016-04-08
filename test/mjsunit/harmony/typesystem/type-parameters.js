// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/testgen.js");


(function TestParametricFunctionTypes() {
  CheckValidType("<A> (x: A) => A");
  CheckValidType("<A extends string> (x: A) => A");
  CheckValidType("<A, B> (x: A, y: B) => A");
  CheckValidType("<A, B extends number[], C> (x: A, y: B) => C");
  CheckValidType("<A, B, C, D> (x: A, y: B, z: C[], d: (e: D) => D) => A");
  // Type parameter lists in non-function types.
  CheckInvalidType("<A> A[]");
  // Empty type parameter list is disallowed.
  CheckInvalidType("<> (x: number) => number");
  // Invalid type in extends.
  CheckInvalidType("<A extends ()> (x: A) => A");
})();

(function TestParametricTypeReferences() {
  CheckValidType("Tree<number>");
  CheckValidType("Map<string, number>");
  // Invalid types as arguments.
  CheckInvalidType("Map<string, (number, void)>");
  // Type arguments not in type references.
  CheckInvalidType("number<string>");
  // Empty type argument list is disallowed.
  CheckInvalidType("Foo<>");
})();
