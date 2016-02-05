// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-object-values-entries --harmony-proxies --harmony-reflect

function TestMeta() {
  assertEquals(1, Object.values.length);
  assertEquals(Function.prototype, Object.getPrototypeOf(Object.values));
}
TestMeta();


function TestBasic() {
  var x = 16;
  var O = {
    d: 1,
    c: 3,
    [Symbol.iterator]: void 0,
    0: 123,
    1000: 456,
    [x * x]: "ducks",
    [`0x${(x * x).toString(16)}`]: "quack"
  };
  O.a = 2;
  O.b = 4;
  Object.defineProperty(O, "HIDDEN", { enumerable: false, value: NaN });
  assertEquals([123, "ducks", 456, 1, 3, "quack", 2, 4], Object.values(O));
  assertEquals(Object.values(O), Object.keys(O).map(key => O[key]));
}
TestBasic();


function TestOrder() {
  var O = {
    a: 1,
    [Symbol.iterator]: null
  };
  O[456] = 123;
  Object.defineProperty(O, "HIDDEN", { enumerable: false, value: NaN });

  var log = [];
  var P = new Proxy(O, {
    ownKeys(target) {
      log.push("[[OwnPropertyKeys]]");
      return Reflect.ownKeys(target);
    },
    get(target, name) {
      log.push(`[[Get]](${JSON.stringify(name)})`);
      return Reflect.get(target, name);
    },
    set(target, name, value) {
      assertUnreachable();
    }
  });

  assertEquals([123, 1], Object.values(P));
  assertEquals([
    "[[OwnPropertyKeys]]",
    "[[Get]](\"456\")",
    "[[Get]](\"a\")"
  ], log);
}
TestOrder();
