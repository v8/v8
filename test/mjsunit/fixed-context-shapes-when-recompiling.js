// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --min-preparse-length 1 --allow-natives-syntax

// Test that the information on which variables to allocate in context doesn't
// change when recompiling.

(function TestVarInInnerFunction() {
  // Introduce variables which would potentially be context allocated, depending
  // on whether an inner function refers to them or not.
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    var a; // This will make "a" actually not be context allocated.
    a; b; c;
  }
  // Force recompilation.
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();


// Other tests are the same, except that the shadowing variable "a" in inner
// functions is declared differently.

(function TestLetInInnerFunction() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    let a;
    a; b; c;
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionParameter() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner(a) {
    a; b; c;
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerInnerFunctionParameter() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    function innerinner(a) { a; b; c; }
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerArrowFunctionParameter() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    var f = a => a + b + c;
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionInnerFunction() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    function a() { }
    a; b; c;
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionSloppyBlockFunction() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    if (true) { function a() { } }
    a; b; c;
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionCatchVariable() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    try {
    }
    catch(a) {
      a; b; c;
    }
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionLoopVariable1() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    for (var a in {}) {
      a; b; c;
    }
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionLoopVariable2() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    for (let a in {}) {
      a; b; c;
    }
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionLoopVariable3() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    for (var a of []) {
      a; b; c;
    }
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionLoopVariable4() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    for (let a of []) {
      a; b; c;
    }
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();

(function TestInnerFunctionClass() {
  var a = 1;
  var b = 2;
  var c = 3;
  function inner() {
    class a {}
    a; b; c;
  }
  for (var i = 0; i < 3; ++i) {
    if (i == 1) {
      %OptimizeOsr();
    }
    assertEquals(1, a);
    assertEquals(2, b);
    assertEquals(3, c);
  }
})();
