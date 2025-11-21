// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --turbolev --no-inline-new --clear-free-memory

// This test checks that repeated loads from the same DataView work correctly:
// the DataView should not be freed by GCs in between the loads.

function foo(x) {
  let dv = new DataView(new ArrayBuffer(8));

  // We need to be reading a value that's not 0 because --clear-free-memory sets
  // freed memory to 0 and thus we won't be able to tell if we're reading stale
  // memory or not.
  dv.setFloat64(0, 5.695);

  // Loading a first value from the DataView. This will compute the data pointer
  // that can be reused for subsequent loads.
  let v1 = dv.getFloat64(0);

  // Allocating to potentially trigger a GC.
  let arr = [1, 2, x];

  // If we don''t have anything retaining {dv} and we GVNed the computation of
  // the data pointer, then it will have been freed by the allocation of {arr}
  // above if it triggered a GC. In that case, we're about to access a stale
  // pointer.
  let v2 = dv.getFloat64(0);

  // Storring v1 and v2 so that they don't get DCEed.
  arr[0] = v1;
  arr[1] = v2;

  // Returning arr so that it doesn't get escaped-analyzed away.
  return arr;
}

%PrepareFunctionForOptimization(foo);
assertEquals(5.695, foo()[1]);
assertEquals(5.695, foo()[1]);

%OptimizeFunctionOnNextCall(foo);
foo();

// Now calling {foo} while making sure that the allocation of {arr} will trigger
// a GC.
%SetAllocationTimeout(1, 4);
let val = foo()[1];
%SetAllocationTimeout(-1, -1);

assertEquals(5.695, val);
