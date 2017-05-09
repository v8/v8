// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-gc

function Data() {
}
Data.prototype = { x: 1 };

function CreateClosure() {
  return function() { return new Data() }
}

// Create some function closures which don't have
// optimized code.
var compile_lazy_closure = CreateClosure();
var baseline_closure = CreateClosure();
baseline_closure();

// Run and optimize the code (do this in a seperate function
// so that the closure doesn't leak in a dead register).
(() => {
  var optimized_closure = CreateClosure();
  // Use .call to avoid the CallIC retaining the JSFunction in the
  // feedback vector via a weak map, which would mean it wouldn't be
  // collected in the minor gc below.
  optimized_closure.call(undefined);
  %OptimizeFunctionOnNextCall(optimized_closure);
  optimized_closure.call(undefined);
})();

// Optimize a dummy function, just so it gets linked into the
// Contexts optimized_functions list head, which is in the old
// space, and the link from to the optimized_closure's JSFunction
// moves to the inline link in dummy's JSFunction in the new space,
// otherwise optimized_closure's JSFunction will be retained by the
// old->new remember set.
(() => {
  var dummy = function() { return 1; };
  %OptimizeFunctionOnNextCall(dummy);
  dummy();
})();

// GC the optimized closure with a minor GC - the optimized
// code will remain in the feedback vector.
gc(true);

// Trigger deoptimization by changing the prototype of Data. This
// will mark the code for deopt, but since no live JSFunction has
// optimized code, we won't clear the feedback vector.
Data.prototype = { x: 2 };

// Call pre-existing functions, these will try to self-heal with the
// optimized code in the feedback vector op, but should bail-out
// since the code is marked for deoptimization.
compile_lazy_closure();
baseline_closure();
