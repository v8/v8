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

function mul2(x) {
  let a = x ? 3.5 : 7.0;
  let r = -1.0;
  if (a < 5.0) r = a * 5.0;
  else r = a * 2.5;
  let result = r - 0.5;
  return result;
}

function div2(x) {
  let a = x ? 3.3 : 6.6;
  let r = -1.0;
  if (a < 5.0) r = a / 1.1;
  else r = a / 2.2;
  let result = r - 0.5;
  return %CheckTypeOf(result, "Float64[2.49999,2.50001]");
}

//function min2(x) {
//  let a = x ? 3.3 : 6.6;
//  let r = -1.0;
//  if (a < 5.0) r = Math.min(a, 6.6);
//  else r = Math.min(3.3, a);
//  let result = r - 0.3;
//  return %CheckTypeOf(result, "Float64{3}");
//}
//
//function max2(x) {
//  let a = x ? 3.3 : 6.6;
//  let r = -1.0;
//  if (a < 5.0) r = Math.max(a, 6.6);
//  else r = Math.max(3.3, a);
//  let result = r - 0.6;
//  return %CheckTypeOf(result, "Float64{6}");
//}

let targets = [ constants, add1, add2, mul2, div2, min2, max2 ];
for(let f of targets) {
  %PrepareFunctionForOptimization(f);
  f(true);
  f(false);
  %OptimizeFunctionOnNextCall(f);
  f(true);
}
*/
