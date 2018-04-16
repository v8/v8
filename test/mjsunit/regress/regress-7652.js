// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let a = {length: 2**53 - 1};

const unshift = Array.prototype.unshift;
assertThrows(() => unshift.call(a, 42), TypeError);
assertEquals(unshift.call(a), 2**53 - 1);

const splice = Array.prototype.splice;
assertThrows(() => splice.call(a, 0, 0, 42), TypeError);
assertEquals(splice.call(a, 0, 1, 42).length, 1);
assertEquals(a[0], 42);
