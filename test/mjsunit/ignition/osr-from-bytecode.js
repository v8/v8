// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --ignition-osr

function f() {
  for (var i = 0; i < 10; i++) {
    if (i == 5 || i == 6) %OptimizeOsr(0, "concurrent");
  }
}
%PrepareFunctionForOptimization(f);
f();
