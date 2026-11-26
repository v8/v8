// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const s = new String('abc');
Object.defineProperty(s, 3, {value: 0, configurable: false});

let next_index = 4;
assertThrows(() => {
  for (;; next_index++) {
    s[next_index] = next_index;
  }
}, RangeError, /Invalid array length/);

assertFalse(Object.hasOwn(s, next_index));
assertThrows(() => {
  s[next_index] = 1;
}, RangeError, /Invalid array length/);
assertThrows(
    () => Object.defineProperty(s, next_index, {value: 1, configurable: true}),
    RangeError, /Invalid array length/);
assertFalse(Object.hasOwn(s, next_index));

let caught_value = null;
try {
  throw 'sample_value';
} catch (e) {
  caught_value = e;
}
assertEquals('sample_value', caught_value);
assertEquals('a', s[0]);
assertEquals(0, s[3]);
