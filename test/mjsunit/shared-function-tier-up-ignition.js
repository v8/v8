// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --mark-shared-functions-for-tier-up --allow-natives-syntax
// Flags: --ignition-staging --no-turbo
// Flags: --crankshaft --no-always-opt

// If we are always or never optimizing it is useless.
assertFalse(isAlwaysOptimize());
assertFalse(isNeverOptimize());

(function() {
  var sum = 0;
  var i = 0;
  for (var i = 0; i < 5; ++i) {
    var f = function(x) {
      return 2 * x;
    }
    sum += f(i);

    if (i == 1) {
      // f must be interpreted code.
      assertTrue(isInterpreted(f));

      // Allow it to run twice (i = 0, 1), then tier-up to baseline.
      %BaselineFunctionOnNextCall(f);
    } else if (i == 2) {
      // Tier-up at i = 2 should only go up to baseline.
      assertTrue(isBaselined(f));

    } else if (i == 3) {
      // Now f must be baseline code.
      assertTrue(isBaselined(f));

      // Run two more times (i = 2, 3), then tier-up to optimized.
      %OptimizeFunctionOnNextCall(f);
    } else if (i == 4) {
      // Tier-up at i = 4 should now go up to crankshaft.
      assertTrue(isCrankshafted(f));
    }
  }
})()
