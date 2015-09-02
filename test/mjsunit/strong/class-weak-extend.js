// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode --allow-natives-syntax

function getStrongClass() {
  "use strong";
  return (class {});
}

function weakClass() {
  "use strict";
  class Weak extends getStrongClass() {}
}

function strongClass() {
  "use strong";
  class Strong extends getStrongClass() {}
}

assertThrows(weakClass, TypeError);
%OptimizeFunctionOnNextCall(weakClass);
assertThrows(weakClass, TypeError);

assertDoesNotThrow(strongClass);
%OptimizeFunctionOnNextCall(strongClass);
assertDoesNotThrow(strongClass);
