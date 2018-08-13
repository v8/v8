// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --opt

/* Test deopt behaviors when the prototype has elements */

// indexOf

(function () {
  var array = [,];

  function indexOf(val) {
    return array.indexOf(val);
  }

  indexOf(6); indexOf(6);

  %OptimizeFunctionOnNextCall(indexOf);
  assertEquals(indexOf(6), -1);

  array.__proto__.push(6);
  // deopt
  assertEquals(indexOf(6), 0);
})();
