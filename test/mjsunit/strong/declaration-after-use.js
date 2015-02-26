// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode --harmony_rest_parameters --harmony_arrow_functions --harmony_classes --harmony-computed-property-names

// Note that it's essential for these tests that the reference is inside dead
// code (because we already produce ReferenceErrors for run-time unresolved
// variables and don't want to confuse those with strong mode errors). But the
// errors should *not* be inside lazy, unexecuted functions, since lazy parsing
// doesn't produce strong mode scoping errors).

// In addition, assertThrows will call eval and that changes variable binding
// types (see e.g., UNBOUND_EVAL_SHADOWED). We can avoid unwanted side effects
// by wrapping the code to be tested inside an outer function.
function assertThrowsHelper(code, error) {
  "use strict";
  let prologue = "(function outer() { ";
  let epilogue = " })();";
  assertThrows(prologue + code + epilogue, error);
}

(function DeclarationAfterUse() {
  // Note that these tests only test cases where the declaration is found but is
  // after the use. In particular, we cannot yet detect cases where the use can
  // possibly bind to a global variable.
  assertThrowsHelper("'use strong'; if (false) { x; let x = 0; }",
                     ReferenceError);
  assertThrowsHelper(
      "function f() { 'use strong'; if (false) { x; let x = 0; } } f();",
      ReferenceError);
  assertThrowsHelper(
      "'use strong'; function f() { if (false) { x; } } let x = 0; f();",
      ReferenceError);

  assertThrowsHelper(
      "function f() { 'use strong'; if (false) { x; } } var x = 0; f();",
      ReferenceError);
  assertThrowsHelper(
      "function f() { 'use strong'; if (false) { x; } } var x; f();",
      ReferenceError);
  // Errors are also detected when the declaration and the use are in the same
  // eval scope.
  assertThrowsHelper("'use strong'; eval('x; let x = 0;')", ReferenceError);

  // Use occurring in the initializer of the declaration:
  assertThrowsHelper("'use strong'; if (false) { let x = x + 1; }",
                     ReferenceError);
  assertThrowsHelper("'use strong'; if (false) { let x = x; }",
                     ReferenceError);
  assertThrowsHelper("'use strong'; if (false) { let x = y, y = 4; }",
                     ReferenceError);
  assertThrowsHelper("'use strong'; if (false) { let x = function() { x; } }",
                     ReferenceError);
  assertThrowsHelper("'use strong'; if (false) { let x = a => { x; } }",
                     ReferenceError);
  assertThrowsHelper(
      "'use strong'; if (false) { function f() {}; let x = f(x); }",
      ReferenceError);
  assertThrowsHelper("'use strong'; if (false) { const x = x; }",
                     ReferenceError);
  assertThrowsHelper("'use strong'; if (false) { const x = function() { x; } }",
                     ReferenceError);
  assertThrowsHelper("'use strong'; if (false) { const x = a => { x; } }",
                     ReferenceError);
  assertThrowsHelper(
      "'use strong'; if (false) { function f() {}; const x = f(x); }",
      ReferenceError);

  assertThrowsHelper(
      "'use strong'; if (false) { for (let x = x; ; ) { } }",
      ReferenceError);
  assertThrowsHelper(
      "'use strong'; if (false) { for (const x = x; ; ) { } }",
      ReferenceError);
  assertThrowsHelper(
      "'use strong'; if (false) { for (let x = y, y; ; ) { } }",
      ReferenceError);
  assertThrowsHelper(
      "'use strong'; if (false) { for (const x = y, y = 0; ; ) { } }",
      ReferenceError);

  // Computed property names
  assertThrowsHelper(
      "'use strong'; if (false) { let o = { 'a': 'b', [o.a]: 'c'}; }",
      ReferenceError);
})();


(function DeclarationAfterUseInClasses() {
  assertThrowsHelper("'use strong'; if (false) { class C extends C { } }",
                     ReferenceError);
  assertThrowsHelper(
      "'use strong'; if (false) { let C = class C2 extends C { } }",
      ReferenceError);
  assertThrowsHelper(
      "'use strong'; if (false) { let C = class C2 extends C2 { } }",
      ReferenceError);

  assertThrowsHelper(
      "'use strong'; if (false) { let C = class C2 { constructor() { C; } } }",
      ReferenceError);

  assertThrowsHelper(
      "'use strong'; if (false) { let C = class C2 { method() { C; } } }",
      ReferenceError);

  assertThrowsHelper(
      "'use strong'; if (false) { let C = class C2 { " +
          "static a() { return 'A'; } [C.a()]() { return 'B'; } }; }",
      ReferenceError);

  // TODO(marja, rossberg): More tests related to computed property names in
  // classes + recognize more errors. This one is not recognized as an error
  // yet:
  // let C = class C2 {
  //  static a() { return 'A'; }
  //  [C2.a()]() { return 'B'; } << C2 should not be allowed here
  // };
})();


(function UsesWhichAreFine() {
  "use strong";

  let var1 = 0;
  var1;

  let var2a = 0, var2b = var2a + 1, var2c = 2 + var2b;

  for (let var3 = 0; var3 < 1; var3++) {
    var3;
  }

  for (let var4a = 0, var4b = var4a; var4a + var4b < 4; var4a++, var4b++) {
    var4a;
    var4b;
  }

  let var5 = 5;
  for (; var5 < 10; ++var5) { }

  let arr = [1, 2];
  for (let i of arr) {
    i;
  }

  let var6 = [1, 2];
  // The second var6 resolves to outside (not to the first var6).
  for (let var6 of var6) { var6; }

  try {
    throw "error";
  } catch (e) {
    e;
  }

  function func1() { func1; this; }
  func1();
  func1;

  function * func2() { func2; this; }
  func2();
  func2;

  function func4(p, ...rest) { p; rest; this; func2; }
  func4();

  let func5 = (p1, p2) => { p1; p2; };
  func5();

  function func6() {
    var1, var2a, var2b, var2c;
  }

  (function eval1() {
    let var7 = 0; // Declaration position will be something large.
    // But use position will be something small, however, this is not an error,
    // since the use is inside an eval scope.
    eval("var7;");
  })();

  // https://code.google.com/p/v8/issues/detail?id=3927
  // class C1 { constructor() { C1; } }; new C1();
  // let C2 = class C3 { constructor() { C3; } }; new C2();

  // class C4 { method() { C4; method; } }; new C4();
  // let C5 = class C6 { method() { C6; method; } }; new C5();
})();
