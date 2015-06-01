// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-default-parameters --harmony-arrow-functions

function return_specified() { return "specified"; }

var method_returns_specified = {
  method() { return "specified"; }
};


(function testDefaultFunctions() {
  function optional_function(handler = function() { }) {
    assertEquals("function", typeof handler);

    // TODO(caitp): infer function name correctly
    // (https://code.google.com/p/v8/issues/detail?id=3699)
    // assertEquals("handler", handler.name);

    return handler();
  }
  assertEquals(undefined, optional_function());
  assertEquals(undefined, optional_function(undefined));
  assertEquals("specified", optional_function(return_specified));
})();


(function testDefaultFunctionReferencesParameters() {
  function fn1(handler = function() { return value; }, value) {
    return handler();
  }
  assertEquals(undefined, fn1());
  assertEquals(undefined, fn1(undefined, undefined));
  assertEquals(1, fn1(undefined, 1));

  function fn2(value, handler = function() { return value; }) {
    return handler();
  }
  assertEquals(undefined, fn2());
  assertEquals(undefined, fn2(undefined));
  assertEquals(1, fn2(1));
})();


(function testDefaultObjects() {
  function optional_object(object = { method() { return "method"; } }) {
    assertEquals("object", typeof object);

    assertEquals("function", typeof object.method);
    return object.method();
  }

  assertEquals("method", optional_object());
  assertEquals("method", optional_object(undefined));
  assertEquals("specified", optional_object(method_returns_specified));


  assertEquals(4, (function(x = { a: 4 }) { return x.a; })());
  assertEquals(5, (function(x, y = { a: x }) { return y.a; })(5));
  assertEquals(6, (function(x, y = { a: eval("x") }) { return y.a; })(6));
})();


// TDZ

(function testReferencesUninitializedParameter() {
  assertThrows(function(a = b, b) {}, ReferenceError);
})();


(function testEvalReferencesUninitializedParameter() {
  assertThrows( function(x = { a: y }, y) { return x.a; }, ReferenceError);
  assertThrows(function(a = eval("b"), b = 0) { return a; }, ReferenceError);
  assertThrows(
      function(x = { a: eval("y") }, y) { return x.a; }, ReferenceError);
})();


(function testReferencesInitializedParameter() {
  assertEquals(1, (function(a = 1, b = a) { return b; })());
})();


// Scoping
//
// TODO(caitp): fix scoping --- var declarations in function body can't be
// resolved in formal parameters
// assertThrows(function referencesVariableBodyDeclaration(a = body_var) {
//   var body_var = true;
//   return a;
// }, ReferenceError);


// TODO(caitp): default function length does not include any parameters
// following the first optional parameter
// assertEquals(0, (function(a = 1) {}).length);
// assertEquals(1, (function(a, b = 1) {}).length);
// assertEquals(2, (function(a, b, c = 1) {}).length);
// assertEquals(3, (function(a, b, c, d = 1) {}).length);
// assertEquals(1, (function(a, b = 1, c, d = 1) {}).length);


(function testInitializerReferencesThis() {
  var O = {};
  function fn(x = this) { return x; }
  assertEquals(O, fn.call(O));

  function fn2(x = () => this) { return x(); }
  assertEquals(O, fn2.call(O));
})();


(function testInitializerReferencesSelf() {
  function fn(x, y = fn) { return x ? y(false) + 1 : 0 }
  assertEquals(1, fn(true));
})();


(function testInitializerEvalParameter() {
  assertEquals(7, (function(x, y = eval("x")) { return y; })(7));
  assertEquals(9, (function(x = () => eval("y"), y = 9) { return x(); })());
})();


(function testContextAllocatedUsedInBody() {
  assertEquals("Monkey!Monkey!Monkey!", (function(x, y = eval("x")) {
    return "Mon" + x + "Mon" + eval("y") + "Mon" + y;
  })("key!"));
  assertEquals("Monkey!", (function(x = "Mon", y = "key!") {
    return eval("x") + eval("y");
  })());
})();


(function testContextAllocatedEscapesFunction() {
  assertEquals("Monkey!", (function(x = "Monkey!") {
    return function() {
      return x;
    };
  })()());
})();
