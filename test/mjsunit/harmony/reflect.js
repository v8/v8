// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-reflect

// TODO(neis): Test with proxies.



////////////////////////////////////////////////////////////////////////////////
// (Auxiliaries)


"use strict";

var global = this;

var sym = Symbol("gaga");

var objects = [
  {},
  [],
  function() {},
  function() {
    return arguments;
  }(),
  function() {
    'use strict';
    return arguments;
  }(),
  Object(1),
  Object(true),
  Object('bla'),
  new Date,
  new RegExp,
  new Set,
  new Map,
  new WeakMap,
  new WeakSet,
  new ArrayBuffer(10),
  new Int32Array(5),
  Object,
  Function,
  Date,
  RegExp,
  global
];

function prepare(tgt) {
  tgt["bla"] = true;
  tgt[4] = 42;
  tgt[sym] = "foo";
  tgt["noconf"] = 43;
  Object.defineProperty(tgt, "noconf", {configurable: false});
  Object.defineProperty(tgt, "getter",
    { get: function () {return this.bla}, configurable: true });
  Object.defineProperty(tgt, "setter",
    { set: function () {}, configurable: true });
}



////////////////////////////////////////////////////////////////////////////////
// Reflect.get


(function testReflectGetArity() {
  assertEquals(3, Reflect.get.length);
})();


(function testReflectGetOnNonObject() {
  assertThrows(function() { Reflect.get(); }, TypeError);
  assertThrows(function() { Reflect.get(42, "bla"); }, TypeError);
  assertThrows(function() { Reflect.get(null, "bla"); }, TypeError);
})();


(function testReflectGetKeyConversion() {
  var tgt = {bla: 42};
  var a = { [Symbol.toPrimitive]: function() { return "bla" } };
  var b = { [Symbol.toPrimitive]: function() { throw "gaga" } };
  assertEquals(42, Reflect.get(tgt, a));
  assertThrows(function() { Reflect.get(tgt, b); }, "gaga");
})();


(function testReflectGetOnObject() {
  var receiver = {bla: false};
  for (let tgt of objects) {
    prepare(tgt);
    assertEquals(true, Reflect.get(tgt, "bla"));
    assertEquals(true, Reflect.get(tgt, "bla", tgt));
    assertEquals(true, Reflect.get(tgt, "bla", receiver));
    assertEquals(42, Reflect.get(tgt, 4));
    assertEquals(42, Reflect.get(tgt, 4, tgt));
    assertEquals(42, Reflect.get(tgt, 4, receiver));
    assertEquals(42, Reflect.get(tgt, "4"));
    assertEquals(42, Reflect.get(tgt, "4", tgt));
    assertEquals(42, Reflect.get(tgt, "4", receiver));
    assertEquals("foo", Reflect.get(tgt, sym));
    assertEquals("foo", Reflect.get(tgt, sym, tgt));
    assertEquals("foo", Reflect.get(tgt, sym, receiver));
    assertEquals(43, Reflect.get(tgt, "noconf"));
    assertEquals(43, Reflect.get(tgt, "noconf", tgt));
    assertEquals(43, Reflect.get(tgt, "noconf", receiver));
    assertEquals(true, Reflect.get(tgt, "getter"));
    assertEquals(true, Reflect.get(tgt, "getter", tgt));
    assertEquals(false, Reflect.get(tgt, "getter", receiver));
    assertEquals(undefined, Reflect.get(tgt, "setter"));
    assertEquals(undefined, Reflect.get(tgt, "setter", tgt));
    assertEquals(undefined, Reflect.get(tgt, "setter", receiver));
    assertEquals(undefined, Reflect.get(tgt, "foo"));
    assertEquals(undefined, Reflect.get(tgt, "foo", tgt));
    assertEquals(undefined, Reflect.get(tgt, "foo", receiver));
    assertEquals(undefined, Reflect.get(tgt, 666));
    assertEquals(undefined, Reflect.get(tgt, 666, tgt));
    assertEquals(undefined, Reflect.get(tgt, 666, receiver));

    let proto = tgt.__proto__;
    tgt.__proto__ = { get foo() {return this.bla} };
    assertEquals(true, Reflect.get(tgt, "foo"));
    assertEquals(true, Reflect.get(tgt, "foo", tgt));
    assertEquals(false, Reflect.get(tgt, "foo", receiver));
    tgt.__proto__ = proto;
  }
})();



////////////////////////////////////////////////////////////////////////////////
// Reflect.has


(function testReflectHasArity() {
  assertEquals(2, Reflect.has.length);
})();


(function testReflectHasOnNonObject() {
  assertThrows(function() { Reflect.has(); }, TypeError);
  assertThrows(function() { Reflect.has(42, "bla"); }, TypeError);
  assertThrows(function() { Reflect.has(null, "bla"); }, TypeError);
})();


(function testReflectHasKeyConversion() {
  var tgt = {bla: 42};
  var a = { [Symbol.toPrimitive]: function() { return "bla" } };
  var b = { [Symbol.toPrimitive]: function() { throw "gaga" } };
  assertTrue(Reflect.has(tgt, a));
  assertThrows(function() { Reflect.has(tgt, b); }, "gaga");
})();


(function testReflectHasOnObject() {
  for (let tgt of objects) {
    prepare(tgt);
    assertTrue(Reflect.has(tgt, "bla"));
    assertTrue(Reflect.has(tgt, 4));
    assertTrue(Reflect.has(tgt, "4"));
    assertTrue(Reflect.has(tgt, sym));
    assertTrue(Reflect.has(tgt, "noconf"));
    assertTrue(Reflect.has(tgt, "getter"));
    assertTrue(Reflect.has(tgt, "setter"));
    assertFalse(Reflect.has(tgt, "foo"));
    assertFalse(Reflect.has(tgt, 666));

    let proto = tgt.__proto__;
    tgt.__proto__ = { get foo() {return this.bla} };
    assertEquals(true, Reflect.has(tgt, "foo"));
    tgt.__proto__ = proto;
  }
})();



////////////////////////////////////////////////////////////////////////////////
// Reflect.deleteProperty


(function testReflectDeletePropertyArity() {
  assertEquals(2, Reflect.deleteProperty.length);
})();


(function testReflectDeletePropertyOnNonObject() {
  assertThrows(function() { Reflect.deleteProperty(); }, TypeError);
  assertThrows(function() { Reflect.deleteProperty(42, "bla"); }, TypeError);
  assertThrows(function() { Reflect.deleteProperty(null, "bla"); }, TypeError);
})();


(function testReflectDeletePropertyKeyConversion() {
  var tgt = {bla: 42};
  var a = { [Symbol.toPrimitive]: function() { return "bla" } };
  var b = { [Symbol.toPrimitive]: function() { throw "gaga" } };
  assertTrue(Reflect.deleteProperty(tgt, a));
  assertThrows(function() { Reflect.deleteProperty(tgt, b); }, "gaga");
})();


(function testReflectDeletePropertyOnObject() {
  for (let tgt of objects) {
    prepare(tgt);
    assertTrue(Reflect.deleteProperty(tgt, "bla"));
    assertEquals(undefined, Object.getOwnPropertyDescriptor(tgt, "bla"));
    if (tgt instanceof Int32Array) {
      assertFalse(Reflect.deleteProperty(tgt, 4));
    } else {
      assertTrue(Reflect.deleteProperty(tgt, 4));
      assertEquals(undefined, Object.getOwnPropertyDescriptor(tgt, 4));
    }
    assertTrue(Reflect.deleteProperty(tgt, sym));
    assertEquals(undefined, Object.getOwnPropertyDescriptor(tgt, sym));
    assertFalse(Reflect.deleteProperty(tgt, "noconf"));
    assertEquals(43, tgt.noconf);
    assertTrue(Reflect.deleteProperty(tgt, "getter"));
    assertTrue(Reflect.deleteProperty(tgt, "setter"));
    assertTrue(Reflect.deleteProperty(tgt, "foo"));
    assertTrue(Reflect.deleteProperty(tgt, 666));

    let proto = tgt.__proto__;
    tgt.__proto__ = { get foo() {return this.bla} };
    assertEquals(true, Reflect.deleteProperty(tgt, "foo"));
    tgt.__proto__ = proto;
  }
})();



////////////////////////////////////////////////////////////////////////////////
// Reflect.isExtensible


(function testReflectIsExtensibleArity() {
  assertEquals(1, Reflect.isExtensible.length);
})();


(function testReflectIsExtensibleOnNonObject() {
  assertThrows(function() { Reflect.isExtensible(); }, TypeError);
  assertThrows(function() { Reflect.isExtensible(42); }, TypeError);
  assertThrows(function() { Reflect.isExtensible(null); }, TypeError);
})();


(function testReflectIsExtensibleOnObject() {
  // This should be the last test as it modifies the objects irreversibly.
  for (let tgt of objects) {
    prepare(tgt);
    if (tgt instanceof Int32Array) continue;  // issue v8:4460
    assertTrue(Reflect.isExtensible(tgt));
    Object.preventExtensions(tgt);
    assertFalse(Reflect.isExtensible(tgt));
  }
})();
