// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --js-staging
// Flags: --experimental-wasm-rab-integration

'use strict';

d8.file.execute('test/mjsunit/typedarray-helpers.js');

const kPageSize = 0x10000;

function Pad(a, v, start, ctor, pages) {
  for (let i = start; i < (pages * kPageSize) / ctor.BYTES_PER_ELEMENT; ++i) {
    a.push(v);
  }
}

function ZeroPad(a, start, ctor, pages) {
  Pad(a, 0, start, ctor, pages);
}

(function ArrayConcatDefault() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBufferViaWasm(1, 2);
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

    // Wasm memories can't shrink.
    assertThrows(() => rab.resize(0), RangeError);

    // OOBness doesn't matter since the TA is added as a single item.
    %ArrayBufferDetachForceWasm(rab);
    assertEquals([fixedLength, 5, 6, 7], helper(fixedLength, [5, 6], [7]));
  }
})();

(function ArrayConcatConcatSpreadable() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBufferViaWasm(1, 2);
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
    let expectedLengthTracking = [0];
    for (let i = 0; i < 4; ++i) {
      expectedLengthTracking.push(i + 1);
    }
    ZeroPad(expectedLengthTracking, 4, ctor, 1);
    expectedLengthTracking.push(5);
    expectedLengthTracking.push(6);
    assertEquals(expectedLengthTracking, helper([0], lengthTracking, [5, 6]));
    let expectedLengthTrackingWithOffset = [0];
    for (let i = 2; i < 4; ++i) {
      expectedLengthTrackingWithOffset.push(i + 1);
    }
    // Note the start offset is still 5, because the TA itself is offset by 2.
    ZeroPad(expectedLengthTrackingWithOffset, 4, ctor, 1);
    expectedLengthTrackingWithOffset.push(5);
    expectedLengthTrackingWithOffset.push(6);
    assertEquals(expectedLengthTrackingWithOffset,
                 helper([0], lengthTrackingWithOffset, [5, 6]));

    // Wasm memories can't shrink.
    assertThrows(() => rab.resize(0), RangeError);

    // After detaching, all TAs behave like zero length.
    %ArrayBufferDetachForceWasm(rab);
    assertEquals([0, 5, 6], helper([0], fixedLength, [5, 6]));
    assertEquals([0, 5, 6], helper([0], fixedLengthWithOffset, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTracking, [5, 6]));
    assertEquals([0, 5, 6], helper([0], lengthTrackingWithOffset, [5, 6]));
  }
})();

// Hand-crafted test to hit a somewhat esoteric code path in Array.p.concat.
(function ArrayConcatConcatDictionaryElementsProto() {
  for (let ctor of ctors) {
    const rab = CreateResizableArrayBufferViaWasm(1, 2);
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

    const largeIndex = kPageSize * 2;
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
    let aaa = helper(lengthTracking);
    let expectedLengthTracking = [1, 2, 3, 4];
    ZeroPad(expectedLengthTracking, 4, ctor, 1);
    assertArrayContents(expectedLengthTracking, helper(lengthTracking));
    let expectedLengthTrackingWithOffset = [3, 4];
    ZeroPad(expectedLengthTrackingWithOffset, 4, ctor, 1);
    assertArrayContents(expectedLengthTrackingWithOffset, helper(lengthTrackingWithOffset));

    // Wasm memories can't shrink.
    assertThrows(() => rab.resize(0), RangeError);

    // After detaching, all TAs behave like zero length.
    %ArrayBufferDetachForceWasm(rab);
    assertArrayContents([], helper(fixedLength));
    assertArrayContents([], helper(fixedLengthWithOffset));
    assertArrayContents([], helper(lengthTracking));
    assertArrayContents([], helper(lengthTrackingWithOffset));
  }
})();
