// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const childIdx = Realm.create();
const childGlobal = Realm.global(childIdx);
const secret = "SECRET";

Realm.eval(childIdx, `globalThis[Symbol.toStringTag] = "${secret}"`);

assertThrows(() => { childGlobal.foo; }, TypeError, /no access/);

(function TestJSONStringify() {
  const o = {};
  Object.setPrototypeOf(o, childGlobal);
  Object.defineProperty(o, "toJSON", {value: null});
  Object.defineProperty(o, "self", {
    value: o,
    writable: true,
    enumerable: true,
    configurable: true
  });

  assertThrows(() => JSON.stringify(o), TypeError,
               /starting at object with constructor 'Object'/);
})();

(function TestCallSiteGetTypeName() {
  const o = {};
  Object.setPrototypeOf(o, childGlobal);
  Object.create(o);
  const saved = Error.prepareStackTrace;
  Error.prepareStackTrace = (_, frames) => frames[0].getTypeName();
  const name = (function f() { return new Error().stack; }).call(o);
  Error.prepareStackTrace = saved;
  assertEquals("Object", name);
})();
