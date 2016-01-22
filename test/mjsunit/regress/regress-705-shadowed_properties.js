// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function allKeys(object) {
  var keys = [];
  for (var key in object) {
    keys.push(key);
  }
  return keys;
}

var a = { a1: true, a2: true};
var b = { b1: true, b2: true};
a.__proto__ = b;

assertEquals(['a1', 'a2', 'b1', 'b2'], allKeys(a));
assertEquals(['b1', 'b2'], allKeys(b));

// Adding a non-enumerable property to either a or b shouldn't change
// the result.
var propertyDescriptor = {
  enumerable: false,
  configurable: true,
  writable: true,
  value: true
};

Object.defineProperty(a, 'a3', propertyDescriptor);
assertEquals(['a1', 'a2', 'b1', 'b2'], allKeys(a));
assertEquals(['b1', 'b2'], allKeys(b));

Object.defineProperty(b, 'b3', propertyDescriptor);
assertEquals(['a1', 'a2', 'b1', 'b2'], allKeys(a));
assertEquals(['b1', 'b2'], allKeys(b));

// A non-enumerable property shadows an enumerable version on the prototype
// chain.
b['a3'] = true;
assertEquals(['a1', 'a2', 'b1', 'b2'], allKeys(a));
assertEquals(['b1', 'b2', 'a3'], allKeys(b));

// Try the same with indexed-properties.
var aIndex  = {0:true, 1:true};
var bIndex = {10:true, 11:true};
aIndex.__proto__= bIndex;
assertEquals(['0', '1', '10', '11'], allKeys(aIndex));
assertEquals(['10', '11'], allKeys(bIndex));

Object.defineProperty(aIndex, 2, propertyDescriptor);
Object.defineProperty(bIndex, 12, propertyDescriptor);
assertEquals(['0', '1', '10', '11'], allKeys(aIndex));
assertEquals(['10', '11'], allKeys(bIndex));

bIndex[2] = 2;
assertEquals(['0', '1', '10', '11'], allKeys(aIndex));
assertEquals(['2', '10', '11'], allKeys(bIndex));
