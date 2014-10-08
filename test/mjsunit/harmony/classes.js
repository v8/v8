// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony

(function TestBasics() {
  var C = class C {}
  assertEquals(typeof C, 'function');
  assertEquals(C.__proto__, Function.prototype);
  assertEquals(Object.prototype, Object.getPrototypeOf(C.prototype));
  assertEquals(Function.prototype, Object.getPrototypeOf(C));
  assertEquals('C', C.name);

  class D {}
  assertEquals(typeof D, 'function');
  assertEquals(D.__proto__, Function.prototype);
  assertEquals(Object.prototype, Object.getPrototypeOf(D.prototype));
  assertEquals(Function.prototype, Object.getPrototypeOf(D));
  assertEquals('D', D.name);

  var E = class {}
  assertEquals('', E.name);
})();


(function TestBasicsExtends() {
  class C extends null {}
  assertEquals(typeof C, 'function');
  assertEquals(C.__proto__, Function.prototype);
  assertEquals(null, Object.getPrototypeOf(C.prototype));

  class D extends C {}
  assertEquals(typeof D, 'function');
  assertEquals(D.__proto__, C);
  assertEquals(C.prototype, Object.getPrototypeOf(D.prototype));
})();


(function TestSideEffectInExtends() {
  var calls = 0;
  class C {}
  class D extends (calls++, C) {}
  assertEquals(1, calls);
  assertEquals(typeof D, 'function');
  assertEquals(D.__proto__, C);
  assertEquals(C.prototype, Object.getPrototypeOf(D.prototype));
})();


(function TestInvalidExtends() {
  assertThrows(function() {
    class C extends 42 {}
  }, TypeError);

  assertThrows(function() {
    // Function but its .prototype is not null or a function.
    class C extends Math.abs {}
  }, TypeError);

  assertThrows(function() {
    Math.abs.prototype = 42;
    class C extends Math.abs {}
  }, TypeError);
  delete Math.abs.prototype;
})();


(function TestConstructorProperty() {
  class C {}
  assertEquals(C, C.prototype.constructor);
  var descr = Object.getOwnPropertyDescriptor(C.prototype, 'constructor');
  assertTrue(descr.configurable);
  assertFalse(descr.enumerable);
  assertTrue(descr.writable);
})();


(function TestPrototypeProperty() {
  class C {}
  var descr = Object.getOwnPropertyDescriptor(C, 'prototype');
  assertFalse(descr.configurable);
  assertFalse(descr.enumerable);
  assertFalse(descr.writable);
})();


(function TestConstructor() {
  var count = 0;
  class C {
    constructor() {
      assertEquals(Object.getPrototypeOf(this), C.prototype);
      count++;
    }
  }
  assertEquals(C, C.prototype.constructor);
  var descr = Object.getOwnPropertyDescriptor(C.prototype, 'constructor');
  assertTrue(descr.configurable);
  assertFalse(descr.enumerable);
  assertTrue(descr.writable);

  var c = new C();
  assertEquals(1, count);
  assertEquals(Object.getPrototypeOf(c), C.prototype);
})();


(function TestImplicitConstructor() {
  class C {}
  var c = new C();
  assertEquals(Object.getPrototypeOf(c), C.prototype);
})();


(function TestConstructorStrict() {
  class C {
    constructor() {
      assertThrows(function() {
        nonExistingBinding = 42;
      }, ReferenceError);
    }
  }
  new C();
})();


(function TestSuperInConstructor() {
  var calls = 0;
  class B {}
  B.prototype.x = 42;

  class C extends B {
    constructor() {
      calls++;
      assertEquals(42, super.x);
    }
  }

  new C;
  assertEquals(1, calls);
})();


(function TestStrictMode() {
  class C {}

  with ({a: 1}) {
    assertEquals(1, a);
  }

  assertThrows('class C extends function B() { with ({}); return B; }() {}',
               SyntaxError);

})();

/* TODO(arv): Implement
(function TestNameBindingInConstructor() {
  class C {
    constructor() {
      assertThrows(function() {
        C = 42;
      }, ReferenceError);
    }
  }
  new C();
})();
*/


(function TestToString() {
  class C {}
  assertEquals('class C {}', C.toString());

  class D { constructor() { 42; } }
  assertEquals('class D { constructor() { 42; } }', D.toString());

  class E { x() { 42; } }
  assertEquals('class E { x() { 42; } }', E.toString());
})();
