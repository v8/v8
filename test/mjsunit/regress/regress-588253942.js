// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --expose-fast-api

const fast_c_api = new d8.test.FastCAPI();

function Foo() {}
Foo.prototype.foo = function(a, b) {
  return a * b;
};

function outer(val, a, b) {
  fast_c_api.call_to_number(val);
  const obj = new Foo();
  return obj.foo(a, b);
}

const small = 1n << 100n;
const big = (1n << 2000000n) - 1n;

%PrepareFunctionForOptimization(Foo);
%PrepareFunctionForOptimization(Foo.prototype.foo);
%PrepareFunctionForOptimization(outer);

for (let i = 0; i < 20; i++) {
  outer(1, small, small);
}

%OptimizeFunctionOnNextCall(outer);
outer(1, small, small);

outer(
    {
      valueOf() {
        d8.terminate();
        return 1;
      }
    },
    big, big);
assertUnreachable();
