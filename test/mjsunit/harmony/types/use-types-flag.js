// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --use-types

function CheckInvalid(code, exception) {
  assertThrows(code, exception);
  assertThrows("\
    function outer() {\
      function inner() {\n"
        + code +
      "\n}\
    }", exception);
}

(function TypedImpliesStrict() {
  CheckInvalid("function strict() { var x; delete x; }",
               SyntaxError);
})();
