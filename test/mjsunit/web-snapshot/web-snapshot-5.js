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
