// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-d8-web-snapshot-api

function callString(f) {
    return '(' + f.toString() + ')()';
}

(function TestMinimal() {
    const r1 = Realm.create();
    function initialize() {
        globalThis.foo = {
            'str': 'hello',
            'n': 42,
        };
    }
    Realm.eval(r1, callString(initialize));
    const snapshot = Realm.takeWebSnapshot(r1, ['foo']);

    const r2 = Realm.create();
    function use() {
        return globalThis.foo;
    }
    const success = Realm.useWebSnapshot(r2, snapshot);
    assertTrue(success);

    const foo = Realm.eval(r2, callString(use));
    assertEquals(foo.str, 'hello');
    assertEquals(foo.n, 42);
})();
