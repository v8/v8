// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --crankshaft

function f() {};
f();
f();
%OptimizeFunctionOnNextCall(f);
f();
assertOptimized(f);
assertThrows(function() { assertUnoptimized(f); });
%DeoptimizeFunction(f);
assertUnoptimized(f);
assertThrows(function() { assertOptimized(f); });
quit();  // Prevent stress runs.
