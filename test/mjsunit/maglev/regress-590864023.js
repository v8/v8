// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc-as=gc();f=function(){g()}//

// `f` is defined via `--expose-gc-as` so it lives in a non-user JS
// sloppy-mode extension script.
let count = 0;
function g() {
  count++;
}

%PrepareFunctionForOptimization(g);
%PrepareFunctionForOptimization(f);
f();
f();
%OptimizeMaglevOnNextCall(f);
f();
%OptimizeFunctionOnNextCall(f);
f();
assertEquals(4, count);
