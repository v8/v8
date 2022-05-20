// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-d8-web-snapshot-api --allow-natives-syntax

'use strict';

d8.file.execute('test/mjsunit/web-snapshot/web-snapshot-helpers.js');

(function TestBuiltin() {
  function createObjects() {
    globalThis.obj1 = {'a': Error};
    globalThis.obj2 = {'b': Error.prototype};
  }
  const realm = Realm.create();
  const {obj1, obj2} = takeAndUseWebSnapshot(
      createObjects, ['obj1', 'obj2'], realm);
  assertTrue(obj1.a === Realm.eval(realm, "Error"));
  assertTrue(obj2.b === Realm.eval(realm, "Error.prototype"));
})();

(function TestObjectPrototype() {
  function createObjects() {
    globalThis.obj = {a: 1, __proto__: {x: 1}};
  }
  const realm = Realm.create();
  const {obj} = takeAndUseWebSnapshot(createObjects, ['obj'], realm);
  assertEquals(1, obj.x);
  assertEquals(1, obj.__proto__.x);
  assertSame(Realm.eval(realm, 'Object.prototype'), obj.__proto__.__proto__);
})();

(function TestEmptyObjectPrototype() {
  function createObjects() {
    globalThis.obj = {__proto__: {x: 1}};
  }
  const realm = Realm.create();
  const {obj} = takeAndUseWebSnapshot(createObjects, ['obj'], realm);
  assertEquals(1, obj.x);
  assertEquals(1, obj.__proto__.x);
  assertSame(Realm.eval(realm, 'Object.prototype'), obj.__proto__.__proto__);
})();

(function TestDictionaryObjectPrototype() {
  function createObjects() {
    const obj = {};
    // Create an object with dictionary map.
    for (let i = 0; i < 2000; i++){
      obj[`key${i}`] = `value${i}`;
    }
    obj.__proto__ = {x: 1};
    globalThis.foo = obj;
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals(2000, Object.keys(foo).length);
  assertEquals(2000, Object.values(foo).length);
  for (let i = 0; i < 2000; i++){
    assertEquals(`value${i}`, foo[`key${i}`]);
  }
  assertEquals(1, foo.x);
  assertEquals(1, foo.__proto__.x);
})();

(function TestNullPrototype() {
  function createObjects() {
    globalThis.foo = Object.create(null);
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals(null, Object.getPrototypeOf(foo));
})();

(function TestInheritFromBuiltin() {
  function createObjects() {
    function inherit(subclass, superclass) {
      function middle() {}
      middle.prototype = superclass.prototype;
      subclass.prototype = new middle();
      subclass.prototype.constructor = subclass;
    };
    function MyError() {}
    inherit(MyError, Error);
    globalThis.MyError = MyError;
  }
  const realm = Realm.create();
  const {MyError} = takeAndUseWebSnapshot(createObjects, ['MyError'], realm);
  const obj = new MyError();
  assertTrue(obj.__proto__.__proto__ === Realm.eval(realm, "Error.prototype"));
})();

(function TestFunctionKinds() {
  function createObjects() {
    globalThis.normalFunction = function() {}
    globalThis.asyncFunction = async function() {}
    globalThis.generatorFunction = function*() {}
    globalThis.asyncGeneratorFunction = async function*() {}
  }
  const realm = Realm.create();
  const {normalFunction, asyncFunction, generatorFunction,
         asyncGeneratorFunction} =
      takeAndUseWebSnapshot(createObjects, ['normalFunction', 'asyncFunction',
          'generatorFunction', 'asyncGeneratorFunction'], realm);
  const newNormalFunction = Realm.eval(realm, 'f1 = function() {}');
  const newAsyncFunction = Realm.eval(realm, 'f2 = async function() {}');
  const newGeneratorFunction = Realm.eval(realm, 'f3 = function*() {}');
  const newAsyncGeneratorFunction =
      Realm.eval(realm, 'f4 = async function*() {}');

  assertSame(newNormalFunction.__proto__, normalFunction.__proto__);
  assertSame(newNormalFunction.prototype.__proto__,
             normalFunction.prototype.__proto__);

  assertSame(newAsyncFunction.__proto__, asyncFunction.__proto__);
  assertEquals(undefined, asyncFunction.prototype);
  assertEquals(undefined, newAsyncFunction.prototype);

  assertSame(newGeneratorFunction.__proto__, generatorFunction.__proto__);
  assertSame(newGeneratorFunction.prototype.__proto__,
             generatorFunction.prototype.__proto__);

  assertSame(newAsyncGeneratorFunction.__proto__,
             asyncGeneratorFunction.__proto__);
  assertSame(newAsyncGeneratorFunction.prototype.__proto__,
             asyncGeneratorFunction.prototype.__proto__);
})();

(function TestContextTree() {
  function createObjects() {
    (function outer() {
      let a = 10;
      let b = 20;
      (function inner1() {
        let c = 5;
        globalThis.f1 = function() { return a + b + c; };
      })();
      (function inner2() {
        let d = 10;
        globalThis.f2 = function() { return a - b - d; };
      })();
    })();
  }
  const {f1, f2} = takeAndUseWebSnapshot(createObjects, ['f1', 'f2']);
  assertEquals(35, f1());
  assertEquals(-20, f2());
})();

(function TestContextReferringToFunction() {
  function createObjects() {
    (function outer() {
      let a = function() { return 10; }
      globalThis.f = function() { return a(); };
    })();
  }
  const {f} = takeAndUseWebSnapshot(createObjects, ['f']);
  assertEquals(10, f());
})();
