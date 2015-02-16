// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode

function strongFunction() {
  "use strong";
  // This should not be a "use before declaration" error, even though the
  // variable is defined later (by non-strong code). Rationale: Non-strong code
  // is allowed to use var declarations, and the semantics of var declarations
  // say that the variable should be usable before the declaration.
  return notStrong + 123;
}

var notStrong = 456;

(function TestStrongFunctionUsingLaterDefinedNonStrongVar() {
  assertEquals(strongFunction(), 579);
})();
