// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-unicode-regexps

var r1 = /abc/gi;
assertEquals("abc", r1.source);
assertTrue(r1.global);
assertTrue(r1.ignoreCase);
assertFalse(r1.multiline);
assertFalse(r1.sticky);
assertFalse(r1.unicode);

// Internal slot of prototype is not read.
var r2 = { __proto__: r1 };
assertThrows(function() { r2.source; }, TypeError);
assertThrows(function() { r2.global; }, TypeError);
assertThrows(function() { r2.ignoreCase; }, TypeError);
assertThrows(function() { r2.multiline; }, TypeError);
assertThrows(function() { r2.sticky; }, TypeError);
assertThrows(function() { r2.unicode; }, TypeError);

var r3 = /I/;
var string = "iIiIi";
var expected = "iXiIi";
assertFalse(r3.global);
assertFalse(r3.ignoreCase);
assertEquals("", r3.flags);
assertEquals(expected, string.replace(r3, "X"));

var get_count = 0;
Object.defineProperty(r3, "global", {
  get: function() { get_count++; return true; }
});
Object.defineProperty(r3, "ignoreCase", {
  get: function() { get_count++; return true; }
});

assertTrue(r3.global);
assertEquals(1, get_count);
assertTrue(r3.ignoreCase);
assertEquals(2, get_count);
// Overridden flag getters affects the flags getter.
assertEquals("gi", r3.flags);
assertEquals(4, get_count);
// Overridden flag getters do not affect the internal flags.
assertEquals(expected, string.replace(r3, "X"));
assertEquals(4, get_count);


function testName(name) {
  // TODO(littledan): For web compatibility, we don't throw an exception,
  // but ES2015 expects an exception to be thrown from this getter.
  assertEquals(undefined, RegExp.prototype[name]);
  assertEquals(
      "get " + name,
      Object.getOwnPropertyDescriptor(RegExp.prototype, name).get.name);
}

testName("global");
testName("ignoreCase");
testName("multiline");
testName("source");
testName("sticky");
testName("unicode");


RegExp.prototype.flags = 'setter should be undefined';

assertEquals('', RegExp('').flags);
assertEquals('', /./.flags);
assertEquals('gimuy', RegExp('', 'yugmi').flags);
assertEquals('gimuy', /foo/yumig.flags);

var descriptor = Object.getOwnPropertyDescriptor(RegExp.prototype, 'flags');
assertTrue(descriptor.configurable);
assertFalse(descriptor.enumerable);
assertInstanceof(descriptor.get, Function);
assertEquals(undefined, descriptor.set);

function testGenericFlags(object) {
  return descriptor.get.call(object);
}

assertEquals('', testGenericFlags({}));
assertEquals('i', testGenericFlags({ ignoreCase: true }));
assertEquals('uy', testGenericFlags({ global: 0, sticky: 1, unicode: 1 }));
assertEquals('m', testGenericFlags({ __proto__: { multiline: true } }));
assertThrows(function() { testGenericFlags(); }, TypeError);
assertThrows(function() { testGenericFlags(undefined); }, TypeError);
assertThrows(function() { testGenericFlags(null); }, TypeError);
assertThrows(function() { testGenericFlags(true); }, TypeError);
assertThrows(function() { testGenericFlags(false); }, TypeError);
assertThrows(function() { testGenericFlags(''); }, TypeError);
assertThrows(function() { testGenericFlags(42); }, TypeError);

var counter = 0;
var map = {};
var object = {
  get global() {
    map.g = counter++;
  },
  get ignoreCase() {
    map.i = counter++;
  },
  get multiline() {
    map.m = counter++;
  },
  get unicode() {
    map.u = counter++;
  },
  get sticky() {
    map.y = counter++;
  }
};
testGenericFlags(object);
assertEquals({ g: 0, i: 1, m: 2, u: 3, y: 4 }, map);
