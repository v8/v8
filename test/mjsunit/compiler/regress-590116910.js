// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --maglev

// 1. Storing a ThinString targeting a ReadOnlyRoots string twice to a kConstant
// global PropertyCell holding that ThinString must not enter a deopt loop.
var root_thin = ["len", "gth"].join("");
var root_holder = { s: root_thin };
// Internalize `root_thin` in-place into a ThinString targeting the ReadOnlyRoot
// "length" string while leaving `root_thin`'s PropertyCell as kConstant.
({})[root_thin];

function storeRootThinTwice(o) {
  let x = o.s;
  root_thin = x;
  root_thin = x;
  return x;
}

%PrepareFunctionForOptimization(storeRootThinTwice);
assertEquals("length", storeRootThinTwice(root_holder));
assertEquals("length", storeRootThinTwice(root_holder));
%OptimizeFunctionOnNextCall(storeRootThinTwice);
assertEquals("length", storeRootThinTwice(root_holder));
assertOptimized(storeRootThinTwice);

// 2. Same as above, but with a ThinString targeting a non-root internalized
// string.
var non_root_thin = ["non_root_", "thin_str"].join("");
var non_root_holder = { s: non_root_thin };
({})[non_root_thin];

function storeNonRootThinTwice(o) {
  let x = o.s;
  non_root_thin = x;
  non_root_thin = x;
  return x;
}

%PrepareFunctionForOptimization(storeNonRootThinTwice);
assertEquals("non_root_thin_str", storeNonRootThinTwice(non_root_holder));
assertEquals("non_root_thin_str", storeNonRootThinTwice(non_root_holder));
%OptimizeFunctionOnNextCall(storeNonRootThinTwice);
assertEquals("non_root_thin_str", storeNonRootThinTwice(non_root_holder));
assertOptimized(storeNonRootThinTwice);

// 3. Storing a ThinString to a kConstant PropertyCell after a monomorphic keyed
// load checked the same string via CheckValueEqualsString (which records the
// internalized string in checked_value).
var keyed_thin = ["keyed_", "thin_str"].join("");
var keyed_holder = { s: keyed_thin };
var keyed_obj = { keyed_thin_str: 42 };
({})[keyed_thin];

function storeAfterKeyedLoad(o) {
  let x = o.s;
  let v = keyed_obj[x];
  keyed_thin = x;
  return v;
}

%PrepareFunctionForOptimization(storeAfterKeyedLoad);
assertEquals(42, storeAfterKeyedLoad(keyed_holder));
assertEquals(42, storeAfterKeyedLoad(keyed_holder));
%OptimizeFunctionOnNextCall(storeAfterKeyedLoad);
assertEquals(42, storeAfterKeyedLoad(keyed_holder));
assertOptimized(storeAfterKeyedLoad);
