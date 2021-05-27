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
  const exports = takeAndUseWebSnapshot(createObjects, ['foo']);
  assertEquals(exports.foo.str, 'hello');
  assertEquals(exports.foo.n, 42);
})();
