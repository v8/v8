// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

(function() {
  const page_size_bits = 18;
  const max_heap_object_size = (1 << (page_size_bits - 1));

  const filler = "Large amount of text per element, so that the joined array is"
      + "large enough to be allocated in the large object space"
  const size = Math.ceil(max_heap_object_size / filler.length + 1);
  const arr = Array(size).fill(filler);

  for (let i = 0; i < 10; i++) {
    assertTrue(%InLargeObjectSpace(arr.join("")));
  }

})();
