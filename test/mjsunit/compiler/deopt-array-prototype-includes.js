// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt

/* Test deopt behaviors when the prototype has elements */

// includes

(function () {
  var array = [,];

  function includes(val) {
    return array.includes(val);
  }

  includes(6); includes(6);

  %OptimizeFunctionOnNextCall(includes);
  assertEquals(includes(6), false);

  array.__proto__.push(6);
  // deopt
  assertEquals(includes(6), true);
})();
