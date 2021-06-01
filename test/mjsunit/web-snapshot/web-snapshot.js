// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-d8-web-snapshot-api

function callString(f) {
  return '(' + f.toString() + ')()';
}

function use() {
  const result = Object.create(null);
  Realm.shared.exports.forEach(x => result[x] = globalThis[x]);
  return result;
}

function takeAndUseWebSnapshot(createObjects, exports) {
  // Make the exports list available across Realms.
  Realm.shared = { exports };
  // Take a snapshot in Realm r1.
  const r1 = Realm.create();
  Realm.eval(r1, callString(createObjects));
  const snapshot = Realm.takeWebSnapshot(r1, exports);
  // Use the snapshot in Realm r2.
  const r2 = Realm.create();
  const success = Realm.useWebSnapshot(r2, snapshot);
  assertTrue(success);
  return Realm.eval(r2, callString(use));
}

(function TestMinimal() {
  function createObjects() {
    globalThis.foo = {
      str: 'hello',
      n: 42,
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals('hello', foo.str);
  assertEquals(42, foo.n);
})();

(function TestEmptyObject() {
  function createObjects() {
    globalThis.foo = {};
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals([], Object.keys(foo));
})();

(function TestNumbers() {
  function createObjects() {
    globalThis.foo = {
      a: 6,
      b: -7,
      c: 7.3,
      d: NaN,
      e: Number.POSITIVE_INFINITY,
      f: Number.NEGATIVE_INFINITY,
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals(6, foo.a);
  assertEquals(-7, foo.b);
  assertEquals(7.3, foo.c);
  assertEquals(NaN, foo.d);
  assertEquals(Number.POSITIVE_INFINITY, foo.e);
  assertEquals(Number.NEGATIVE_INFINITY, foo.f);
})();

(function TestOddballs() {
  function createObjects() {
    globalThis.foo = {
      a: true,
      b: false,
      c: null,
      d: undefined,
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertTrue(foo.a);
  assertFalse(foo.b);
  assertEquals(null, foo.c);
  assertEquals(undefined, foo.d);
})();

(function TestFunction() {
  function createObjects() {
    globalThis.foo = {
      key: function () { return 'bar'; },
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals('bar', foo.key());
})();

(function TestFunctionWithContext() {
  function createObjects() {
    globalThis.foo = {
      key: (function () {
        let result = 'bar';
        function inner() { return result; }
        return inner;
      })(),
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals('bar', foo.key());
})();

(function TestInnerFunctionWithContextAndParentContext() {
  function createObjects() {
    globalThis.foo = {
      key: (function () {
        let part1 = 'snap';
        function inner() {
          let part2 = 'shot';
          function innerinner() {
            return part1 + part2;
          }
          return innerinner;
        }
        return inner();
      })()
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals('snapshot', foo.key());
})();

(function TestRegExp() {
  function createObjects() {
    globalThis.foo = {
      re: /ab+c/gi,
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals('/ab+c/gi', foo.re.toString());
  assertTrue(foo.re.test('aBc'));
  assertFalse(foo.re.test('ac'));
})();

(function TestRegExpNoFlags() {
  function createObjects() {
    globalThis.foo = {
      re: /ab+c/,
    };
  }
  const { foo } = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals('/ab+c/', foo.re.toString());
  assertTrue(foo.re.test('abc'));
  assertFalse(foo.re.test('ac'));
})();
