// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-rab-gsab --allow-natives-syntax

'use strict';

d8.file.execute('test/mjsunit/typedarray-helpers.js');

(function ArrayConcatDefault() {
  for (let ctor of ctors) {
    const gsab = CreateGrowableSharedArrayBuffer(4 * ctor.BYTES_PER_ELEMENT,
                                                 8 * ctor.BYTES_PER_ELEMENT);
    const lengthTracking = new ctor(gsab);
    const taWrite = new ctor(gsab);
    for (let i = 0; i < 4; ++i) {
      WriteToTypedArray(taWrite, i, i + 1);
    }
    // Orig. array: [1, 2, 3, 4]
    //              [1, 2, 3, 4, ...] << lengthTracking
    function helper(receiver, ...params) {
      return ToNumbers(Array.prototype.concat.call(receiver, ...params));
    }
    // TypedArrays aren't concat spreadable.
    assertEquals([lengthTracking, 5, 6, 7],
                 helper(lengthTracking, [5, 6], [7]));
    // Resizing doesn't matter since the TA is added as a single item.
    gsab.grow(6 * ctor.BYTES_PER_ELEMENT);
    assertEquals([lengthTracking, 5, 6, 7],
                 helper(lengthTracking, [5, 6], [7]));
  }
})();
