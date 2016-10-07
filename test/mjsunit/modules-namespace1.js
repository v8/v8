// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// MODULE

let ja = 42;
export {ja as yo};
export const bla = "blaa";
export {foo as foo_again};
// See further below for the actual star import that declares "foo".

// The object itself.
assertEquals("object", typeof foo);
assertThrows(() => foo = 666, TypeError);
assertFalse(Reflect.isExtensible(foo));
assertTrue(Reflect.preventExtensions(foo));
assertThrows(() => Reflect.apply(foo, {}, []));
assertThrows(() => Reflect.construct(foo, {}, []));
assertSame(null, Reflect.getPrototypeOf(foo));
// TODO(neis): The next one should be False.
assertTrue(Reflect.setPrototypeOf(foo, null));
assertFalse(Reflect.setPrototypeOf(foo, {}));
assertSame(null, Reflect.getPrototypeOf(foo));
// TODO(neis): The next one should include @@iterator at the end.
assertEquals(
    ["bla", "foo_again", "yo", Symbol.toStringTag],
    Reflect.ownKeys(foo));

// Its "yo" property.
assertEquals(
    {value: 42, enumerable: true, configurable: false, writable: true},
    Reflect.getOwnPropertyDescriptor(foo, "yo"));
assertFalse(Reflect.deleteProperty(foo, "yo"));
assertTrue(Reflect.has(foo, "yo"));
// TODO(neis): The next three should be False.
assertTrue(Reflect.set(foo, "yo", true));
assertTrue(Reflect.defineProperty(foo, "yo",
    Reflect.getOwnPropertyDescriptor(foo, "yo")));
assertTrue(Reflect.defineProperty(foo, "yo", {}));
assertFalse(Reflect.defineProperty(foo, "yo", {get() {return 1}}));
assertEquals(42, Reflect.get(foo, "yo"));
assertEquals(43, (ja++, foo.yo));

// Its "foo_again" property.
assertSame(foo, foo.foo_again);

// Its @@toStringTag property.
assertTrue(Reflect.has(foo, Symbol.toStringTag));
assertEquals("string", typeof Reflect.get(foo, Symbol.toStringTag));
assertEquals(
    {value: "Module", configurable: true, writable: false, enumerable: false},
    Reflect.getOwnPropertyDescriptor(foo, Symbol.toStringTag));

// TODO(neis): Its @@iterator property.
// assertTrue(Reflect.has(foo, Symbol.iterator));
// assertEquals("function", typeof Reflect.get(foo, Symbol.iterator));
// assertEquals(["bla", "yo"], [...foo]);
// assertThrows(() => (42, foo[Symbol.iterator])(), TypeError);
// assertSame(foo[Symbol.iterator]().__proto__,
//     ([][Symbol.iterator]()).__proto__.__proto__);

// TODO(neis): Clarify spec w.r.t. other symbols.

// Nonexistant properties.
let nonexistant = ["gaga", 123, Symbol('')];
for (let key of nonexistant) {
  assertSame(undefined, Reflect.getOwnPropertyDescriptor(foo, key));
  assertTrue(Reflect.deleteProperty(foo, key));
  assertFalse(Reflect.set(foo, key, true));
  assertSame(undefined, Reflect.get(foo, key));
  assertFalse(Reflect.defineProperty(foo, key, {get() {return 1}}));
  assertFalse(Reflect.has(foo, key));
}

// The actual star import that we are testing. Namespace imports are
// initialized before evaluation
import * as foo from "modules-namespace1.js";

// There can be only one namespace object.
import * as bar from "modules-namespace1.js";
assertSame(foo, bar);
