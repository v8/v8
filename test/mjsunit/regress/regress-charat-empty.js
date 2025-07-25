// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --allow-natives-syntax
(() => {
  function f(s) {
    return s.charAt();
  };
  %PrepareFunctionForOptimization(f);
  f('');
  f("");
  %OptimizeFunctionOnNextCall(f);
  f("");
})();
