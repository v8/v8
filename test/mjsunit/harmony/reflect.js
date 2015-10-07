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
  for (let tgt of objects) {
    prepare(tgt);
    assertEquals(true, Reflect.get(tgt, "bla"));
    assertEquals(42, Reflect.get(tgt, 4));
    assertEquals(42, Reflect.get(tgt, "4"));
    assertEquals("foo", Reflect.get(tgt, sym));
    assertEquals(undefined, Reflect.get(tgt, "doesnotexist"));
    assertEquals(undefined, Reflect.get(tgt, 666));
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
    assertFalse(Reflect.has(tgt, "doesnotexist"));
    assertFalse(Reflect.has(tgt, 666));
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
    assertTrue(Reflect.deleteProperty(tgt, "doesnotexist"));
    assertTrue(Reflect.deleteProperty(tgt, 666));
    assertFalse(Reflect.deleteProperty(tgt, "noconf"));
    assertEquals(43, tgt.noconf);
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
