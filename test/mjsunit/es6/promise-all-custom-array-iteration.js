// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute('test/mjsunit/test-async.js');

// Arrays with custom iteration must not be iterated directly.
testAsync(assert => {
  assert.plan(2);

  const arr = [1, 2, 3];
  arr[Symbol.iterator] = function*() {
    yield 'a';
    yield 'b';
  };
  Promise.all(arr).then(res => assert.equals(['a', 'b'], res));
  Promise.allSettled(arr).then(
      res => assert.equals(
          [{status: 'fulfilled', value: 'a'}, {status: 'fulfilled', value: 'b'}],
          res));
});
