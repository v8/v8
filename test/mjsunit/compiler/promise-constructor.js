// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --experimental-inline-promise-constructor

// We have to patch mjsunit because normal assertion failures just throw
// exceptions which are swallowed in a then clause.
failWithMessage = (msg) => %AbortJS(msg);

// Don't crash.
(function() {
  function foo() {
    let resolve, reject, promise;
    promise = new Promise((a, b) => { resolve = a; reject = b; });

    return {resolve, reject, promise};
  }

  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();


// Check that when executor throws, the promise is rejected
(function() {
  function foo() {
    return new Promise((a, b) => { throw new Error(); });
  }

  function bar(i) {
  let error = null;
  foo().then(_ => error = 1, e => error = e);
  setTimeout(_ => assertInstanceof(error, Error));
  if (i == 1) %OptimizeFunctionOnNextCall(foo);
  if (i > 0) setTimeout(bar.bind(null, i - 1));
}
bar(3);
})();

(function() {
  function foo() {
    let p;
    try {
      p = new Promise((a, b) => { %DeoptimizeFunction(foo); });
    } catch (e) {
      // Nothing should throw
      assertUnreachable();
    }
    // TODO(petermarshall): This fails but should not.
    // assertInstanceof(p, Promise);
  }

  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

(function() {
  function foo() {
    let p;
    try {
      p = new Promise((a, b) => { %DeoptimizeFunction(foo); throw new Error(); });
    } catch (e) {
      // The promise constructor should catch the exception and reject the
      // promise instead.
      // TODO(petermarshall): This fails but should not. We need to fix deopts.
      // assertUnreachable();
    }
    // TODO(petermarshall): This fails but should not.
    // assertInstanceof(p, Promise);
  }

  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

// Test when the executor is not inlined.
(function() {
  let resolve, reject, promise;
  function bar(a, b) {
    resolve = a; reject = b;
    throw new Error();
  }
  function foo() {
    promise = new Promise(bar);
  }
  foo();
  foo();
  %NeverOptimizeFunction(bar);
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();

// Test that the stack trace contains 'new Promise'
(function() {
  let resolve, reject, promise;
  function bar(a, b) {
    resolve = a; reject = b;
    let stack = new Error().stack;
    // TODO(petermarshall): This fails but should not.
    // assertContains("new Promise", stack);
    throw new Error();
  }
  function foo() {
    promise = new Promise(bar);
  }
  foo();
  foo();
  %OptimizeFunctionOnNextCall(foo);
  foo();
})();
