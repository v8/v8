// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-object-literals --allow-natives-syntax


(function TestDescriptor() {
  var object = {
    method() {
      return 42;
    }
  };
  assertEquals(42, object.method());
})();


(function TestDescriptor() {
  var object = {
    method() {
      return 42;
    }
  };

  var desc = Object.getOwnPropertyDescriptor(object, 'method');
  assertTrue(desc.enumerable);
  assertTrue(desc.configurable);
  assertTrue(desc.writable);
  assertEquals('function', typeof desc.value);

  assertEquals(42, desc.value());
})();


(function TestProto() {
  var object = {
    method() {
      return 42;
    }
  };

  assertEquals(Function.prototype, Object.getPrototypeOf(object.method));
})();


(function TestNotConstructable() {
  var object = {
    method() {
      return 42;
    }
  };

  assertThrows(function() {
    new object.method;
  });
})();


(function TestFunctionName() {
  var object = {
    method() {
      return 42;
    },
    1() {},
    2.0() {}
  };
  var f = object.method;
  assertEquals('method', f.name);
  var g = object[1];
  assertEquals('1', g.name);

  var h = object[2];
  assertEquals('2', h.name);
})();


(function TestNoBinding() {
  var method = 'local';
  var calls = 0;
  var object = {
    method() {
      calls++;
      assertEquals('local', method);
    }
  };
  object.method();
  assertEquals(1, calls);
})();


(function TestNoPrototype() {
  var object = {
    method() {
      return 42;
    }
  };
  var f = object.method;
  assertFalse(f.hasOwnProperty('prototype'));
  assertEquals(undefined, f.prototype);

  f.prototype = 42;
  assertEquals(42, f.prototype);
})();


(function TestToString() {
  var object = {
    method() { 42; }
  };
  assertEquals('method() { 42; }', object.method.toString());
})();


(function TestOptimized() {
  var object = {
    method() { return 42; }
  };
  assertEquals(42, object.method());
  assertEquals(42, object.method());
  %OptimizeFunctionOnNextCall(object.method);
  assertEquals(42, object.method());
  assertFalse(object.method.hasOwnProperty('prototype'));
})();
