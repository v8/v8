// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --stack-size=100 --harmony --harmony-reflect

function test(f, expected, type) {
  try {
    f();
  } catch (e) {
    assertInstanceof(e, type);
    assertEquals(expected, e.message);
    return;
  }
  assertUnreachable();
}

// === Error ===

// kCyclicProto
test(function() {
  var o = {};
  o.__proto__ = o;
}, "Cyclic __proto__ value", Error);


// === TypeError ===

// kApplyNonFunction
test(function() {
  Function.prototype.apply.call(1, []);
}, "Function.prototype.apply was called on 1, which is a number " +
   "and not a function", TypeError);

// kCannotConvertToPrimitive
test(function() {
  [].join(Object(Symbol(1)));
}, "Cannot convert object to primitive value", TypeError);

// kGeneratorRunning
test(function() {
  var iter;
  function* generator() { yield iter.next(); }
  var iter = generator();
  iter.next();
}, "Generator is already running", TypeError);

// kCalledNonCallable
test(function() {
  [].forEach(1);
}, "1 is not a function", TypeError);

// kIncompatibleMethodReceiver
test(function() {
  RegExp.prototype.compile.call(RegExp.prototype);
}, "Method RegExp.prototype.compile called on incompatible receiver " +
   "[object RegExp]", TypeError);

// kInstanceofFunctionExpected
test(function() {
  1 instanceof 1;
}, "Expecting a function in instanceof check, but got 1", TypeError);

// kInstanceofNonobjectProto
test(function() {
  function f() {}
  var o = new f();
  f.prototype = 1;
  o instanceof f;
}, "Function has non-object prototype '1' in instanceof check", TypeError);

// kInvalidInOperatorUse
test(function() {
  1 in 1;
}, "Cannot use 'in' operator to search for '1' in 1", TypeError);

// kNotConstructor
test(function() {
  new Symbol();
}, "Symbol is not a constructor", TypeError);

// kPropertyNotFunction
test(function() {
  Set.prototype.add = 0;
  new Set(1);
}, "Property 'add' of object #<Set> is not a function", TypeError);

// kSymbolToPrimitive
test(function() {
  1 + Object(Symbol());
}, "Cannot convert a Symbol wrapper object to a primitive value", TypeError);

// kSymbolToString
test(function() {
  "" + Symbol();
}, "Cannot convert a Symbol value to a string", TypeError);

// kSymbolToNumber
test(function() {
  1 + Symbol();
}, "Cannot convert a Symbol value to a number", TypeError);

// kUndefinedOrNullToObject
test(function() {
  Array.prototype.toString.call(null);
}, "Cannot convert undefined or null to object", TypeError);

// kWithExpression
test(function() {
  with (null) {}
}, "null has no properties", TypeError);

// kWrongArgs
test(function() {
  (function() {}).apply({}, 1);
}, "Function.prototype.apply: Arguments list has wrong type", TypeError);

test(function() {
  Reflect.apply(function() {}, {}, 1);
}, "Reflect.apply: Arguments list has wrong type", TypeError);

test(function() {
  Reflect.construct(function() {}, 1);
}, "Reflect.construct: Arguments list has wrong type", TypeError);


// === RangeError ===

// kStackOverflow
test(function() {
  function f() { f(Array(1000)); }
  f();
}, "Maximum call stack size exceeded", RangeError);
