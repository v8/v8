// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-rab-gsab --allow-natives-syntax

'use strict';

d8.file.execute('test/mjsunit/typedarray-helpers.js');

// Hand-crafted test to hit a somewhat esoteric code path in Array.p.concat.
(function ArrayConcatConcatDictionaryElementsProto() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    const taWrite = new ctor(rab);
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
    // Shrink so that fixed length TAs go out of bounds.
    rab.resize(3 * ctor.BYTES_PER_ELEMENT);
    // Orig. array: [1, 2, 3]
    //              [1, 2, 3, ...] << lengthTracking
    //                    [3, ...] << lengthTrackingWithOffset
    assertEquals(expected([]), helper(fixedLength));
    assertEquals(expected([]), helper(fixedLengthWithOffset));
    assertEquals(expected([1, 2, 3]), helper(lengthTracking));
    assertEquals(expected([3]), helper(lengthTrackingWithOffset));
    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * ctor.BYTES_PER_ELEMENT);
    // Orig. array: [1]
    //              [1, ...] << lengthTracking
    assertEquals(expected([]), helper(fixedLength));
    assertEquals(expected([]), helper(fixedLengthWithOffset));
    assertEquals(expected([1]), helper(lengthTracking));
    assertEquals(expected([]), helper(lengthTrackingWithOffset));
    // Shrink to zero.
    rab.resize(0);
    // Orig. array: []
    //              [...] << lengthTracking
    assertEquals(expected([]), helper(fixedLength));
    assertEquals(expected([]), helper(fixedLengthWithOffset));
    assertEquals(expected([]), helper(lengthTracking));
    assertEquals(expected([]), helper(lengthTrackingWithOffset));
    // Grow so that all TAs are back in-bounds.
    rab.resize(6 * ctor.BYTES_PER_ELEMENT);
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
    // After detaching, all TAs behave like zero length.
    %ArrayBufferDetach(rab);
    assertEquals(expected([]), helper(fixedLength));
    assertEquals(expected([]), helper(fixedLengthWithOffset));
    assertEquals(expected([]), helper(lengthTracking));
    assertEquals(expected([]), helper(lengthTrackingWithOffset));
  }
})();
