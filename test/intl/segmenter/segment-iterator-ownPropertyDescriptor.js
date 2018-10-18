// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-intl-segmenter

let seg = new Intl.Segmenter();
let descriptor = Object.getOwnPropertyDescriptor(
    Intl.Segmenter, "supportedLocalesOf");
assertTrue(descriptor.writable);
assertFalse(descriptor.enumerable);
assertTrue(descriptor.configurable);

// ecma402 #sec-Intl.Segmenter.prototype
// Intl.Segmenter.prototype
// The value of Intl.Segmenter.prototype is %SegmenterPrototype%.
// This property has the attributes
// { [[Writable]]: false, [[Enumerable]]: false, [[Configurable]]: false }.
descriptor = Object.getOwnPropertyDescriptor(Intl.Segmenter, "prototype");
assertFalse(descriptor.writable);
assertFalse(descriptor.enumerable);
assertFalse(descriptor.configurable);

for (let func of ["segment", "resolvedOptions"]) {
  let descriptor = Object.getOwnPropertyDescriptor(
      Intl.Segmenter.prototype, func);
  assertTrue(descriptor.writable);
  assertFalse(descriptor.enumerable);
  assertTrue(descriptor.configurable);
}

let prototype = Object.getPrototypeOf(seg.segment('text'));
for (let func of ["next", "following", "preceding"]) {
  let descriptor = Object.getOwnPropertyDescriptor(prototype, func);
  assertTrue(descriptor.writable);
  assertFalse(descriptor.enumerable);
  assertTrue(descriptor.configurable);
}
