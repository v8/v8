// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev --no-stress-opt

(function() {
  function add(x, y) {
    return x + y;
  }

  %PrepareFunctionForOptimization(add);
  assertEquals(4.2, add(2.1, 2.1));

  %OptimizeMaglevOnNextCall(add);
  assertEquals(4.2, add(2.1, 2.1));
  assertTrue(isMaglevved(add));

  // We don't deopt if we use smis.
  assertEquals(42, add(22, 20));
  assertTrue(isMaglevved(add));

  // We deopt if not a number.
  assertEquals("42", add("4", "2"));
  assertFalse(isMaglevved(add));

  // TODO(victorgomes): Fix deopt when we have a float,
  // i.e., add(4, "2") will create a float with number 4
  // and correctly deopt, but the state is bogus.
})();
