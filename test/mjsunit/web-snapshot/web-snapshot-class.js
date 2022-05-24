// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-d8-web-snapshot-api --allow-natives-syntax

'use strict';

d8.file.execute('test/mjsunit/web-snapshot/web-snapshot-helpers.js');

(function TestEmptyClass() {
  function createObjects() {
    globalThis.Foo = class Foo { };
  }
  const { Foo } = takeAndUseWebSnapshot(createObjects, ['Foo']);
  const x = new Foo();
})();

(function TestClassWithConstructor() {
  function createObjects() {
    globalThis.Foo = class {
      constructor() {
        this.n = 42;
      }
    };
  }
  const { Foo } = takeAndUseWebSnapshot(createObjects, ['Foo']);
  const x = new Foo(2);
  assertEquals(42, x.n);
})();

(function TestClassWithMethods() {
  function createObjects() {
    globalThis.Foo = class {
      f() { return 7; };
    };
  }
  const { Foo } = takeAndUseWebSnapshot(createObjects, ['Foo']);
  const x = new Foo();
  assertEquals(7, x.f());
})();

(function TestDerivedClass() {
  function createObjects() {
    globalThis.Base = class { f() { return 8; }};
    globalThis.Foo = class extends Base { };
  }
  const realm = Realm.create();
  const { Foo, Base } = takeAndUseWebSnapshot(createObjects, ['Foo', 'Base'], realm);
  assertEquals(Base.prototype, Foo.prototype.__proto__);
  assertEquals(Base, Foo.__proto__);
  const x = new Foo();
  assertEquals(8, x.f());
})();

(function TestDerivedClassWithConstructor() {
  function createObjects() {
    globalThis.Base = class { constructor() {this.m = 43;}};
    globalThis.Foo = class extends Base{
      constructor() {
        super();
        this.n = 42;
      }
    };
  }
  const { Foo } = takeAndUseWebSnapshot(createObjects, ['Foo']);
  const x = new Foo();
  assertEquals(42, x.n);
  assertEquals(43, x.m);
})();

(async function TestClassWithAsyncMethods() {
  function createObjects() {
    globalThis.Foo = class {
      async g() { return 6; };
    };
  }
  const { Foo } = takeAndUseWebSnapshot(createObjects, ['Foo']);
  const x = new Foo();
  assertEquals(6, await x.g());
})();
