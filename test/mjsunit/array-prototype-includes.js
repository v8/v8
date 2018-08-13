// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* Test behaviors when the prototype has elements */

// includes

(function () {
  var array = [,];

  function includes(val) {
    return array.includes(val);
  }

  assertEquals(includes(6), false);

  array.__proto__.push(6);
  assertEquals(includes(6), true);
})();
