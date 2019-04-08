// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --verify-heap

function foo(x) { return x + "0123456789012"; }
foo('a');
foo('\u10D0');
%OptimizeFunctionOnNextCall(foo);
foo(null);
