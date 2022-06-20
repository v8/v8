// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-rab-gsab --allow-natives-syntax

'use strict';

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function ArrayConcatDefault() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);

    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength

    function helper(receiver, ...params) {
      return ToNumbers(Array.prototype.concat.call(receiver, ...params));
    }

    // TypedArrays aren't concat spreadable by default.
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));

    // OOBness doesn't matter since the TA is added as a single item.
    rab.resize(0);
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));

    // The same for detached buffers.
    %ArrayBufferDetach(rab);
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));
  }
})();

(function ArrayConcatConcatSpreadable() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                           8 * ctor.BYTES_PER_ELEMENT);
    const fixedLength = new ctor(rab, 0, 4);
    const fixedLengthWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT, 2);
    const lengthTracking = new ctor(rab, 0);
    const lengthTrackingWithOffset = new ctor(rab, 2 * ctor.BYTES_PER_ELEMENT);
    fixedLength[Symbol.isConcatSpreadable] = true;
    fixedLengthWithOffset[Symbol.isConcatSpreadable] = true;
    lengthTracking[Symbol.isConcatSpreadable] = true;
    lengthTrackingWithOffset[Symbol.isConcatSpreadable] = true;

    const taWrite = new ctor(rab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }

    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4] << fixedLength
    //                    [3, 4] << fixedLengthWithOffset
    //              [1, 2, 3, 4, ...] << lengthTracking
    //                    [3, 4, ...] << lengthTrackingWithOffset

    function helper(receiver, ...params) {
      return ToNumbers(Array.prototype.concat.call(receiver, ...params));
    }

    assertEquals([0, 1, 2, 3, 4, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 3, 4, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 1, 2, 3, 4, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 3, 4, 5, 6],
                 helper([0], lengthTrackingWithOffset, [5, 6]));

    // Shrink so that fixed length TAs go out of bounds.
    rab.resize(3 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [1, 2, 3]
    //              [1, 2, 3, ...] << lengthTracking
    //                    [3, ...] << lengthTrackingWithOffset

    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 1, 2, 3, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 3, 5, 6],
                 helper([0], lengthTrackingWithOffset, [5, 6]));

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [1]
    //              [1, ...] << lengthTracking

    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 1, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTrackingWithOffset, [5, 6]));

    // Shrink to zero.
    rab.resize(0);

    // Orig. array: []
    //              [...] << lengthTracking

    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTrackingWithOffset, [5, 6]));

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

    assertEquals([0, 1, 2, 3, 4, 7, 8], helper([0], fixedLength, [7, 8]));
    assertEquals([0, 3, 4, 7, 8], helper([0], fixedLengthWithOffset, [7, 8]));
    assertEquals([0, 1, 2, 3, 4, 5, 6, 7, 8],
                 helper([0], lengthTracking, [7, 8]));
    assertEquals([0, 3, 4, 5, 6, 7, 8],
                 helper([0], lengthTrackingWithOffset, [7, 8]));

    // After detaching, all TAs behave like zero length.
    %ArrayBufferDetach(rab);
    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTrackingWithOffset, [5, 6]));
  }
})();

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
      const newArray = [];
      newArray[largeIndex] = 11111;  // Force dictionary mode.
      assertTrue(%HasDictionaryElements(newArray));
      newArray.__proto__ = ta;
      return Array.prototype.concat.call([], newArray);
    }

    function assertArrayContents(expectedStart, array) {
      for (let i = 0; i < expectedStart.length; ++i) {
        assertEquals(expectedStart[i], Number(array[i]));
      }
      assertEquals(largeIndex + 1, array.length);
      // Don't check every index to keep the test run time reasonable.
      for (let i = expectedStart.length; i < largeIndex - 1; i += 153) {
        assertEquals(undefined, array[i]);
      }
      assertEquals(11111, Number(array[largeIndex]));
    }

    assertArrayContents([1, 2, 3, 4], helper(fixedLength));
    assertArrayContents([3, 4], helper(fixedLengthWithOffset));
    assertArrayContents([1, 2, 3, 4], helper(lengthTracking));
    assertArrayContents([3, 4], helper(lengthTrackingWithOffset));

    // Shrink so that fixed length TAs go out of bounds.
    rab.resize(3 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [1, 2, 3]
    //              [1, 2, 3, ...] << lengthTracking
    //                    [3, ...] << lengthTrackingWithOffset

    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([1, 2, 3], helper(lengthTracking));
    assertArrayContents([3], helper(lengthTrackingWithOffset));

    // Shrink so that the TAs with offset go out of bounds.
    rab.resize(1 * ctor.BYTES_PER_ELEMENT);

    // Orig. array: [1]
    //              [1, ...] << lengthTracking

    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([1], helper(lengthTracking));
    assertArrayContents([], helper(lengthTrackingWithOffset));

    // Shrink to zero.
    rab.resize(0);

    // Orig. array: []
    //              [...] << lengthTracking

    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([], helper(lengthTracking));
    assertArrayContents([], helper(lengthTrackingWithOffset));

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

    assertArrayContents([1, 2, 3, 4], helper(fixedLength));
    assertArrayContents([3, 4], helper(fixedLengthWithOffset));
    assertArrayContents([1, 2, 3, 4, 5, 6], helper(lengthTracking));
    assertArrayContents([3, 4, 5, 6], helper(lengthTrackingWithOffset));

    // After detaching, all TAs behave like zero length.
    %ArrayBufferDetach(rab);
    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([], helper(lengthTracking));
    assertArrayContents([], helper(lengthTrackingWithOffset));
  }
})();
