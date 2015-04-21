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
  assertUnreachable("Exception expected");
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

// kCalledNonCallable
test(function() {
  [].forEach(1);
}, "1 is not a function", TypeError);

// kCannotConvertToPrimitive
test(function() {
  [].join(Object(Symbol(1)));
}, "Cannot convert object to primitive value", TypeError);

// kDefineDisallowed
test(function() {
  "use strict";
  var o = {};
  Object.preventExtensions(o);
  Object.defineProperty(o, "x", { value: 1 });
}, "Cannot define property:x, object is not extensible.", TypeError);

// kGeneratorRunning
test(function() {
  var iter;
  function* generator() { yield iter.next(); }
  var iter = generator();
  iter.next();
}, "Generator is already running", TypeError);

// kFunctionBind
test(function() {
  Function.prototype.bind.call(1);
}, "Bind must be called on a function", TypeError);

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

// kNotGeneric
test(function() {
  String.prototype.toString.call(1);
}, "String.prototype.toString is not generic", TypeError);

test(function() {
  String.prototype.valueOf.call(1);
}, "String.prototype.valueOf is not generic", TypeError);

test(function() {
  Boolean.prototype.toString.call(1);
}, "Boolean.prototype.toString is not generic", TypeError);

test(function() {
  Boolean.prototype.valueOf.call(1);
}, "Boolean.prototype.valueOf is not generic", TypeError);

test(function() {
  Number.prototype.toString.call({});
}, "Number.prototype.toString is not generic", TypeError);

test(function() {
  Number.prototype.valueOf.call({});
}, "Number.prototype.valueOf is not generic", TypeError);

test(function() {
  Function.prototype.toString.call(1);
}, "Function.prototype.toString is not generic", TypeError);


// kObjectGetterExpectingFunction
test(function() {
  ({}).__defineGetter__("x", 0);
}, "Object.prototype.__defineGetter__: Expecting function", TypeError);

// kObjectGetterCallable
test(function() {
  Object.defineProperty({}, "x", { get: 1 });
}, "Getter must be a function: 1", TypeError);

// kObjectSetterExpectingFunction
test(function() {
  ({}).__defineSetter__("x", 0);
}, "Object.prototype.__defineSetter__: Expecting function", TypeError);

// kObjectSetterCallable
test(function() {
  Object.defineProperty({}, "x", { set: 1 });
}, "Setter must be a function: 1", TypeError);

// kPropertyDescObject
test(function() {
  Object.defineProperty({}, "x", 1);
}, "Property description must be an object: 1", TypeError);

// kPropertyNotFunction
test(function() {
  Set.prototype.add = 0;
  new Set(1);
}, "Property 'add' of object #<Set> is not a function", TypeError);

// kProtoObjectOrNull
test(function() {
  Object.setPrototypeOf({}, 1);
}, "Object prototype may only be an Object or null: 1", TypeError);

// kRedefineDisallowed
test(function() {
  "use strict";
  var o = {};
  Object.defineProperty(o, "x", { value: 1, configurable: false });
  Object.defineProperty(o, "x", { value: 2 });
}, "Cannot redefine property: x", TypeError);

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

// kValueAndAccessor
test(function() {
  Object.defineProperty({}, "x", { get: function(){}, value: 1});
}, "Invalid property.  A property cannot both have accessors and be " +
   "writable or have a value, #<Object>", TypeError);

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


//=== SyntaxError ===

test(function() {
  new Function(")", "");
}, "Function arg string contains parenthesis", SyntaxError);


// === RangeError ===

// kArrayLengthOutOfRange
test(function() {
  "use strict";
  Object.defineProperty([], "length", { value: 1E100 });
}, "defineProperty() array length out of range", RangeError);

//kNumberFormatRange
test(function() {
Number(1).toFixed(100);
}, "toFixed() digits argument must be between 0 and 20", RangeError);

test(function() {
Number(1).toExponential(100);
}, "toExponential() argument must be between 0 and 20", RangeError);

// kStackOverflow
test(function() {
  function f() { f(Array(1000)); }
  f();
}, "Maximum call stack size exceeded", RangeError);

// kToPrecisionFormatRange
test(function() {
  Number(1).toPrecision(100);
}, "toPrecision() argument must be between 1 and 21", RangeError);
