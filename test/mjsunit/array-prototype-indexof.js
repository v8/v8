// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* Test behaviors when the prototype has elements */

// indexOf

(function () {
  var array = [,];

  function indexOf(val) {
    return array.indexOf(val);
  }

  assertEquals(indexOf(6), -1);

  array.__proto__.push(6);
  assertEquals(indexOf(6), 0);
})();
