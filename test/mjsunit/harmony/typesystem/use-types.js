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
  assertDoesNotThrow("\
    'use types';\
    function lazy() {" + code + "};\
    lazy()");
  assertDoesNotThrow("\
    'use types';\
    var arrow = () => {" + code + "};\
    arrow()");
  assertDoesNotThrow("\
    'use types';\
    var lazy = (function outer() {\
      return (function inner() {\
        return function () {" + code + "};\
      })()\
    })();\
    lazy()");
  assertDoesNotThrow("\
    'use types';\
    var arrow = (function outer() {\
      return (function inner() {\
        return () => {" + code + "};\
      })()\
    })();\
    arrow()");
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
  assertThrows("\
    'use types';\
    function lazy() {" + code + "};\
    lazy()", exception);
  assertThrows("\
    'use types';\
    var arrow = () => {" + code + "};\
    arrow()", exception);
  assertThrows("\
    'use types';\
    var lazy = (function outer() {\
      return (function inner() {\
        return function () {" + code + "};\
      })()\
    })();\
    lazy()", exception);
  assertThrows("\
    'use types';\
    var arrow = (function outer() {\
      return (function inner() {\
        return () => {" + code + "};\
      })()\
    })();\
    arrow()", exception);
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
