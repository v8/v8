// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function test(f, expected, type) {
  try {
    f();
    assertUnreachable();
  } catch (e) {
    assertInstanceof(e, type);
    assertEquals(expected, e.message);
  }
}

// === Error ===

// kCyclicProto
test(function() {
  var o = {};
  o.__proto__ = o;
}, "Cyclic __proto__ value", Error);


// === TypeError ===

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

// kPropertyNotFunction
test(function() {
  Set.prototype.add = 0;
  new Set(1);
}, "Property 'add' of object #<Set> is not a function", TypeError);

// kWithExpression
test(function() {
  with (null) {}
}, "null has no properties", TypeError);
