// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

function* f([x]) { yield x }
// No warm-up of {f} to trigger soft deopt.
%OptimizeFunctionOnNextCall(f);
var gen = f([23]);

assertEquals("[object Generator]", gen.toString());
assertEquals({ done:false, value:23 }, gen.next());
assertEquals({ done:true, value:undefined }, gen.next());
