// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbofan --no-always-turbofan

var global = this;
var unique_id = 0;
// Creates a function with unique SharedFunctionInfo to ensure the feedback
// vector is unique for each test case.
function MakeFunctionWithUniqueSFI(...args) {
  assertTrue(args.length > 0);
  var body = `/* Unique comment: ${unique_id++} */ ` + args.pop();
  return new Function(...args, body);
}


//
// Load constant field from constant object directly.
//
function TestLoadFromConstantFieldOfAConstantObject(the_value, other_value) {
  function A(v) { this.v = v; }
  function O() { this.a = new A(the_value); }
  var the_object = new O();

  // Ensure that {the_object.a}'s map is not stable to complicate compiler's
  // life.
  new A(the_value).blah = 0;

  // Ensure that constant tracking is enabled for {contant_object}.
  delete global.constant_object;
  global.constant_object = the_object;
  assertEquals(the_object, constant_object);

  assertTrue(%HasFastProperties(the_object));

  // {constant_object} is known to the compiler via global property cell
  // tracking.
  var load = MakeFunctionWithUniqueSFI("return constant_object.a.v;");
  %PrepareFunctionForOptimization(load);
  load();
  load();
  %OptimizeFunctionOnNextCall(load);
  assertEquals(the_value, load());
  assertOptimized(load);
  var a = new A(other_value);
  assertTrue(%HaveSameMap(a, the_object.a));
  // Make constant field mutable by assigning another value
  // to some other instance of A.
  new A(the_value).v = other_value;
  assertTrue(%HaveSameMap(a, new A(the_value)));
  assertTrue(%HaveSameMap(a, the_object.a));
  assertUnoptimized(load);
  assertEquals(the_value, load());
  assertUnoptimized(load);
  assertEquals(the_value, load());
}

// Test constant tracking with Smi value.
(function() {
  var the_value = 42;
  var other_value = 153;
  TestLoadFromConstantFieldOfAConstantObject(the_value, other_value);
})();

// Test constant tracking with double value.
(function() {
  var the_value = 0.9;
  var other_value = 0.42;
  TestLoadFromConstantFieldOfAConstantObject(the_value, other_value);
})();

// Test constant tracking with function value.
(function() {
  var the_value = function V() {};
  var other_value = function W() {};
  TestLoadFromConstantFieldOfAConstantObject(the_value, other_value);
})();

// Test constant tracking with heap object value.
(function() {
  function V() {}
  var the_value = new V();
  var other_value = new V();
  TestLoadFromConstantFieldOfAConstantObject(the_value, other_value);
})();


//
// Load constant field from a prototype.
//
function TestLoadFromConstantFieldOfAPrototype(the_value, other_value) {
  function Proto() { this.v = the_value; }
  var the_prototype = new Proto();

  function O() {}
  O.prototype = the_prototype;
  var the_object = new O();

  // Ensure O.prototype is in fast mode by loading from its field.
  function warmup() { return new O().v; }
  %EnsureFeedbackVectorForFunction(warmup);
  warmup(); warmup(); warmup();
  if (!%IsDictPropertyConstTrackingEnabled())
    assertTrue(%HasFastProperties(O.prototype));

  // The parameter object is not constant but all the values have the same
  // map and therefore the compiler knows the prototype object and can
  // optimize load of "v".
  var load = MakeFunctionWithUniqueSFI("o", "return o.v;");
  %PrepareFunctionForOptimization(load);
  load(new O());
  load(new O());
  %OptimizeFunctionOnNextCall(load);
  assertEquals(the_value, load(new O()));
  assertOptimized(load);
  // Invalidation of mutability should trigger deoptimization with a
  // "field-owner" reason.
  the_prototype.v = other_value;
  assertUnoptimized(load);
}

// Test constant tracking with Smi value.
(function() {
  var the_value = 42;
  var other_value = 153;
  TestLoadFromConstantFieldOfAPrototype(the_value, other_value);
})();

// Test constant tracking with double value.
(function() {
  var the_value = 0.9;
  var other_value = 0.42;
  TestLoadFromConstantFieldOfAPrototype(the_value, other_value);
})();

// Test constant tracking with function value.
(function() {
  var the_value = function V() {};
  var other_value = function W() {};
  TestLoadFromConstantFieldOfAPrototype(the_value, other_value);
})();

// Test constant tracking with heap object value.
(function() {
  function V() {}
  var the_value = new V();
  var other_value = new V();
  TestLoadFromConstantFieldOfAPrototype(the_value, other_value);
})();
