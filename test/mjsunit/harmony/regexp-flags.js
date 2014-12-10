// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-regexps

delete RegExp.prototype.flags;
RegExp.prototype.flags = 'setter should be undefined';

assertEquals('', RegExp('').flags);
assertEquals('', /./.flags);
assertEquals('gimy', RegExp('', 'ygmi').flags);
assertEquals('gimy', /foo/ymig.flags);

// TODO(dslomov): When support for the `u` flag is added, uncomment the first
// line below and remove the second line.
//assertEquals(RegExp('', 'yumig').flags, 'gimuy');
assertThrows(function() { RegExp('', 'yumig').flags; }, SyntaxError);

var descriptor = Object.getOwnPropertyDescriptor(RegExp.prototype, 'flags');
assertFalse(descriptor.configurable);
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
