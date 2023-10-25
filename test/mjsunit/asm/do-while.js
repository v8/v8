// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(crbug.com/v8/14410): The mid tier allocator caused a crash for this
// test. As the allocator gets removed soon, we just disable the flag for this
// test now instead of investigating the issue.
// Flags: --noturbo-force-mid-tier-regalloc
function Module(stdlib, foreign, buffer) {
  "use asm";
  function f(i) {
    i = i|0;
    var j = 0;
    do {
      if ((i | 0) > 0) {
        j = (i | 0) != 0;
        i = (i - 1) | 0;
      } else {
        j = 0;
      }
    } while (j);
    return i | 0;
  }
  return {f:f};
}

var m = Module(this, {}, new ArrayBuffer(64*1024));

assertEquals(-1, m.f("-1"));
assertEquals(0, m.f(-Math.infinity));
assertEquals(0, m.f(undefined));
assertEquals(0, m.f(0));
assertEquals(0, m.f(1));
assertEquals(0, m.f(100));
