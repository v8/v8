// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function TestError() {}

// Force slow, generic assess path that will always allocate a temporary fixed
// array.
String.prototype.__defineGetter__(0, function() { });

const a = new Array(2**32 - 1);

// Force early exit to avoid an unreasonably long test.
a[0] = {
  toString() { throw new TestError(); }
};

// Verify join throws test error and does not fail due to asserts (Negative
// length fixed array allocation).
assertThrows(() => a.join(), TestError);
