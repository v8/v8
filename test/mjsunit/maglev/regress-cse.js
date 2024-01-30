// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --maglev --allow-natives-syntax --maglev-cse

// CheckValueEqualsFloat64 with NaN
const o22 = {
};
let v34 = o22.__proto__;
var b;
function foo() {
  v34 = o22.__proto__;
  b = v34++;
  class C37 {};
}
%PrepareFunctionForOptimization(foo);
foo();
%OptimizeMaglevOnNextCall(foo);
foo();

// Skipped exceptions
var __caught = 0;
class __c_0 {
}
class __c_1 extends __c_0 {
  constructor() {
    try {
      this;
    } catch (e) {}
    try {
      this;
    } catch (e) {
      __caught++;
    }
      super();
  }
}
%PrepareFunctionForOptimization(__c_1);
new __c_1();
%OptimizeMaglevOnNextCall(__c_1);
new __c_1();
assertEquals(__caught, 2);

// Expressions across exception handlers.
function main() {
    function func() {
      return '' + '<div><div><di';
    }
    try {
      func();
    } catch (e) {}
    /./.test(func());
}
%PrepareFunctionForOptimization(main);
main();
%OptimizeMaglevOnNextCall(main);
main();
