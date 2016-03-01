// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --use-types

function CheckValid(code) {
  assertDoesNotThrow(code);
  assertDoesNotThrow("\
    function outer() {\
      function inner() {\n"
        + code +
      "\n}\
    }");
  assertDoesNotThrow("\
    (function outer() {\
      (function inner() {\
        eval(\'" + code + "\')\
      })()\
    })()");
}

function CheckInvalid(code, exception) {
  assertThrows(code, exception);
  assertThrows("\
    function outer() {\
      function inner() {\n"
        + code +
      "\n}\
    }", exception);
  assertThrows("\
    (function outer() {\
      (function inner() {\
        eval(\'" + code + "\')\
      })()\
    })()", exception);
}

(function UseTypesWorks() {
  CheckValid("var x: number = 42");
})();

(function TypedImpliesStrict() {
  CheckInvalid("function strict() { var x; delete x; }",
               SyntaxError);
})();
