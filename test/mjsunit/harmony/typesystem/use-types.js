// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types

function CheckValidInTypedMode(code, exception) {
  assertThrows(code, exception);
  assertDoesNotThrow("'use types';\n" + code);
  assertDoesNotThrow('"use types";\n' + code);
  assertDoesNotThrow("\
    'use types';\
    function outer() {\
      function inner() {\n"
        + code +
      "\n}\
    }");
  assertDoesNotThrow("\
    'use types';\
    (function outer() {\
      (function inner() {\
        eval(\'" + code + "\')\
      })()\
    })()");
}

function CheckInvalidInTypedMode(code, exception) {
  assertDoesNotThrow(code);
  assertThrows("'use types';\n" + code, exception);
  assertThrows('"use types";\n' + code, exception);
  assertThrows("\
    'use types';\
    function outer() {\
      function inner() {\n"
        + code +
      "\n}\
    }", exception);
  assertThrows("\
    'use types';\
    (function outer() {\
      (function inner() {\
        eval(\'" + code + "\')\
      })()\
    })()", exception);
}

(function UseTypesWorks() {
  CheckValidInTypedMode("var x: number = 42", SyntaxError);
})();

(function UseTypesIllegalInFunctionScope() {
  assertThrows("function f() { 'use types'; }", SyntaxError);
  assertThrows('function f() { "use types"; }', SyntaxError);
})();

(function TypedImpliesStrict() {
  CheckInvalidInTypedMode("function strict() { var x; delete x; }",
                          SyntaxError);
})();
