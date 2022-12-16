// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --turboshaft --allow-natives-syntax

// NOTE: The following tests are very platform specific and prone to break on
// any compiler changes. Test are used for type system development only.
// TODO(nicohartmann@): Replace by more stable tests or remove completely once
// type system development is close to completion.

/*
function use() {}
%NeverOptimizeFunction(use);

function add1(x) {
  let a = x ? 3 : 7;
  let r = -1;
  %CheckTurboshaftTypeOf(a, "Word32[3, 7]");
  if (a < 5) r = a + 2;
  else r = a - 2;
  let result = r + 1;
  return %CheckTurboshaftTypeOf(result, "Word32{6}");
}

function add2(x) {
  let a = x ? 3.5 : 7.5;
  let r = -1.0;
  %CheckTurboshaftTypeOf(a, "Float64[3.5, 7.5]");
  if (a < 5.5) r = a + 2.0;
  else r = a - 2.0;
  let result = r - 0.5;
  return %CheckTurboshaftTypeOf(result, "Float64{5.0}");
}

let targets = [ add1, add2 ];
for(let f of targets) {
  %PrepareFunctionForOptimization(f);
  f(true);
  f(false);
  %OptimizeFunctionOnNextCall(f);
  f(true);
}
*/
