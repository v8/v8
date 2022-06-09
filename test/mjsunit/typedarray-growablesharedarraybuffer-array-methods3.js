// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-rab-gsab --allow-natives-syntax

'use strict';

d8.file.execute('test/mjsunit/typedarray-helpers.js');

// Hand-crafted test to hit a somewhat esoteric code path in Array.p.concat.
(function ArrayConcatConcatDictionaryElementsProto() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(gsab, 0, 4);
    const fixedLengthWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(gsab, 0);
    const lengthTrackingWithOffset = new ctor(gsab, 2 * ctor.BYTES_PER_ELEMENT);
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }
    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, ...] << lengthTracking
    //                    [3, 4, ...] << lengthTrackingWithOffset
    const largeIndex = 5000;
    function helper(ta) {
      let newArray = [];
      newArray[largeIndex] = 11111;  // Force dictionary mode.
      assertTrue(%HasDictionaryElements(newArray));
      newArray.__proto__ = ta;
      return ToNumbers(Array.prototype.concat.call([], newArray));
    }
    function expected(arr) {
      arr[largeIndex] = 11111;
      return arr;
    }
    assertEquals(expected([1, 2, 3, 4]), helper(fixedLength));
    assertEquals(expected([3, 4]), helper(fixedLengthWithOffset));
    assertEquals(expected([1, 2, 3, 4]), helper(lengthTracking));
    assertEquals(expected([3, 4]), helper(lengthTrackingWithOffset));
    // Grow.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    for (let i = 0; i < 6; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }
    // Orig. array: [1, 2, 3, 4, 5, 6]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, 5, 6, ...] << lengthTracking
    //                    [3, 4, 5, 6, ...] << lengthTrackingWithOffset
    assertEquals(expected([1, 2, 3, 4]), helper(fixedLength));
    assertEquals(expected([3, 4]), helper(fixedLengthWithOffset));
    assertEquals(expected([1, 2, 3, 4, 5, 6]), helper(lengthTracking));
    assertEquals(expected([3, 4, 5, 6]), helper(lengthTrackingWithOffset));
  }
})();
