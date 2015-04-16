// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode

// Note that it's essential for these tests that the reference is inside dead
// code (because we already produce ReferenceErrors for run-time unresolved
// variables and don't want to confuse those with strong mode errors). But the
// errors should *not* be inside lazy, unexecuted functions, since lazy parsing
// doesn't produce strong mode scoping errors).

// In addition, assertThrows will call eval and that changes variable binding
// types (see e.g., UNBOUND_EVAL_SHADOWED). We can avoid unwanted side effects
// by wrapping the code to be tested inside an outer function.
function assertThrowsHelper(code) {
  "use strict";
  let prologue_dead = "(function outer() { if (false) { ";
  let epilogue_dead = " } })();";

  assertThrows("'use strong'; " + prologue_dead + code + epilogue_dead, ReferenceError);

  // Make sure the error happens only in strong mode (note that we need strict
  // mode here because of let).
  assertDoesNotThrow("'use strict'; " + prologue_dead + code + epilogue_dead);

  // But if we don't put the references inside a dead code, it throws a run-time
  // error (also in strict mode).
  let prologue_live = "(function outer() { ";
  let epilogue_live = "})();";

  assertThrows("'use strong'; " + prologue_live + code + epilogue_live, ReferenceError);
  assertThrows("'use strict'; " + prologue_live + code + epilogue_live, ReferenceError);
}

(function InitTimeReferenceForward() {
  // It's never OK to have an init time reference to a class which hasn't been
  // declared.
  assertThrowsHelper(
      `class A extends B { };
      class B {}`);

  assertThrowsHelper(
      `class A {
        [B.sm()]() { }
      };
      class B {
        static sm() { return 0; }
      }`);
})();

(function InitTimeReferenceBackward() {
  // Backwards is of course fine.
  "use strong";
  class A {
    static sm() { return 0; }
  };
  let i = "making these classes non-consecutive";
  class B extends A {};
  "by inserting statements and declarations in between";
  class C {
    [A.sm()]() { }
  };
})();

(function BasicMutualRecursion() {
  "use strong";
  class A {
    m() { B; }
    static sm() { B; }
  };
  // No statements or declarations between the classes.
  class B {
    m() { A; }
    static sm() { A; }
  };
})();
