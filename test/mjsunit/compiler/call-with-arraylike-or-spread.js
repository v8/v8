// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbo-optimize-apply --opt

// These tests do not work well if this script is run more than once (e.g.
// --stress-opt); after a few runs the whole function is immediately compiled
// and assertions would fail. We prevent re-runs.
// Flags: --nostress-opt --no-always-opt

// Some of the tests rely on optimizing/deoptimizing at predictable moments, so
// this is not suitable for deoptimization fuzzing.
// Flags: --deopt-every-n-times=0

// Tests for optimization of CallWithSpread and CallWithArrayLike.

// Test JSCallReducer::ReduceJSCallWithArrayLike.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y, z) {
    return sum_js3.apply(null, [x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test with holey array.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y) {
    return sum_js3.apply(null, [x,,y]);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals('AundefinedB', foo('A', 'B'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);
  assertEquals('AundefinedB', foo('A', 'B'));
  assertFalse(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test with holey-double array.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + (b ? b : .0) + c;
  }
  function foo(x, y) {
    return sum_js3.apply(null, [x,,y]);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals(42.17, foo(16.11, 26.06));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);

  // This is expected to deoptimize
  assertEquals(42.17, foo(16.11, 26.06));
  assertTrue(sum_js3_got_interpreted);
  assertUnoptimized(foo);

  // Optimize again
  %PrepareFunctionForOptimization(foo);
  assertEquals(42.17, foo(16.11, 26.06));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);

  // This this it should stay optimized, but with the call not inlined.
  assertEquals(42.17, foo(16.11, 26.06));
  assertTrue(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test deopt when array size changes.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y, z) {
    let a = [x, y, z];
    a.push('*');
    return sum_js3.apply(null, a);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  // Here array size changes.
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);
  // Here it should deoptimize.
  assertEquals('abc', foo('a', 'b', 'c'));
  assertUnoptimized(foo);
  assertTrue(sum_js3_got_interpreted);
  // Now speculation mode prevents the optimization.
  %PrepareFunctionForOptimization(foo);
  %OptimizeFunctionOnNextCall(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertTrue(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test with FixedDoubleArray.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y, z) {
    return sum_js3.apply(null, [x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals(53.2, foo(11.03, 16.11, 26.06));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);
  assertEquals(53.2, foo(11.03, 16.11, 26.06));
  assertFalse(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test with empty array.
(function () {
  "use strict";
  var got_interpreted = true;
  function fortytwo() {
    got_interpreted = %IsBeingInterpreted();
    return 42;
  }
  function foo() {
    return fortytwo.apply(null, []);
  }

  %PrepareFunctionForOptimization(fortytwo);
  %PrepareFunctionForOptimization(foo);
  assertEquals(42, foo());
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(got_interpreted);
  assertEquals(42, foo());
  assertFalse(got_interpreted);
  assertOptimized(foo);
})();

// Test with empty array that changes size.
(function () {
  "use strict";
  var got_interpreted = true;
  function fortytwo() {
    got_interpreted = %IsBeingInterpreted();
    return 42 + arguments.length;
  }
  function foo() {
    let v = [];
    v.push(42);
    let result = fortytwo.apply(null, v);
    return result;
  }

  %PrepareFunctionForOptimization(fortytwo);
  %PrepareFunctionForOptimization(foo);
  assertEquals(43, foo(1));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(got_interpreted);
  assertEquals(43, foo(1));
  assertTrue(got_interpreted);
  assertUnoptimized(foo);

  // Call again, verifies that it stays optimized, but the call is not inlined.
  %PrepareFunctionForOptimization(foo);
  assertEquals(43, foo(1));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(got_interpreted);
  assertEquals(43, foo(1));
  assertTrue(got_interpreted);
  assertOptimized(foo);
})();

// Test Reflect.apply().
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y ,z) {
    return Reflect.apply(sum_js3, null, [x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test JSCallReducer::ReduceJSCallWithSpread.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y, z) {
    const numbers = [x, y, z];
    return sum_js3(...numbers);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test spread call with empty array.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y, z) {
    const args = [];
    return sum_js3(x, y, z, ...args);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test spread call with more args.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c, d, e, f) {
    assertEquals(6, arguments.length);
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + d + e + f;
  }
  function foo(x, y, z) {
    const numbers = [z, y, x];
    return sum_js(x, y, z, ...numbers);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abccba', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abccba', foo('a', 'b', 'c'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test apply on JSCreateClosure.
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function foo_closure() {
    return function(a, b, c) {
      sum_got_interpreted = %IsBeingInterpreted();
      return a + b + c;
    }
  }
  const _foo_closure = foo_closure();
  %PrepareFunctionForOptimization(_foo_closure);

  function foo(x, y, z) {
    return foo_closure().apply(null, [x, y, z]);
  }

  %PrepareFunctionForOptimization(foo_closure);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  %OptimizeFunctionOnNextCall(foo_closure);
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_got_interpreted);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test apply with JSBoundFunction
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function sum_js(a, b, c, d, e) {
    sum_got_interpreted = %IsBeingInterpreted();
    return this.x + a + b + c + d + e;
  }
  const f = sum_js.bind({ x: 26 }, 11, 3);
  function foo(a, b, c) {
    return f.apply(null, [a, b, c]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals(166, foo(40, 42, 44));
  assertTrue(sum_got_interpreted);

  %OptimizeFunctionOnNextCall(foo);
  assertEquals(166, foo(40, 42, 44));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test apply with nested bindings
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function sum_js(a, b, c, d, e) {
    sum_got_interpreted = %IsBeingInterpreted();
    return this.x + a + b + c + d + e;
  }
  const f = sum_js.bind({ x: 26 }, 11).bind({ y: 4 }, 3);
  function foo(x, y, z) {
    return f.apply(null, [x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals(166, foo(40, 42, 44));
  assertTrue(sum_got_interpreted);

  %OptimizeFunctionOnNextCall(foo);
  assertEquals(166, foo(40, 42, 44));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test apply on bound function (JSCreateBoundFunction).
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_got_interpreted = %IsBeingInterpreted();
    return this.x + a + b + c;
  }
  function foo(x, y, z) {
    return sum_js3.bind({ x: 42 }).apply(null, [x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals('42abc', foo('a', 'b', 'c'));
  assertTrue(sum_got_interpreted);

  %OptimizeFunctionOnNextCall(foo);
  assertEquals('42abc', foo('a', 'b', 'c'));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test apply on bound function (JSCreateBoundFunction) with args.
(function () {
  "use strict";
  var sum_got_interpreted = true;
  function sum_js(a, b, c, d, e) {
    sum_got_interpreted = %IsBeingInterpreted();
    return this.x + a + b + c + d + e;
  }
  function foo(x, y, z) {
    return sum_js.bind({ x: 3 }, 11, 31).apply(null, [x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('45abc', foo('a', 'b', 'c'));
  assertTrue(sum_got_interpreted);

  %OptimizeFunctionOnNextCall(foo);
  assertEquals('45abc', foo('a', 'b', 'c'));
  assertFalse(sum_got_interpreted);
  assertOptimized(foo);
})();

// Test call with array-like under-application.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + arguments.length;
  }
  function foo(x, y) {
    return sum_js.apply(null, [x, y]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('ABundefined2', foo('A', 'B'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('ABundefined2', foo('A', 'B'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test call with array-like over-application.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + arguments.length;
  }
  function foo(v, w, x, y, z) {
    return sum_js.apply(null, [v, w, x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc5', foo('a', 'b', 'c', 'd', 'e'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abc5', foo('a', 'b', 'c', 'd', 'e'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test call with spread under-application.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, c) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + c + arguments.length;
  }
  function foo(x, y) {
    const numbers = [x, y];
    return sum_js(...numbers);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('ABundefined2', foo('A', 'B'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('ABundefined2', foo('A', 'B'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test call with spread over-application.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c + arguments.length;
  }
  function foo(v, w, x, y, z) {
    const numbers = [v, w, x, y, z];
    return sum_js3(...numbers);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc5', foo('a', 'b', 'c', 'd', 'e'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js3_got_interpreted);
  assertEquals('abc5', foo('a', 'b', 'c', 'd', 'e'));
  assertFalse(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test calling function that has rest parameters.
(function () {
  "use strict";
  var sum_js_got_interpreted = true;
  function sum_js(a, b, ...moreArgs) {
    sum_js_got_interpreted = %IsBeingInterpreted();
    return a + b + moreArgs[0] + moreArgs[1] + moreArgs[2];
  }
  function foo(v, w, x, y, z) {
    return sum_js.apply(null, [v, w, x, y, z]);
  }

  %PrepareFunctionForOptimization(sum_js);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abcde', foo('a', 'b', 'c', 'd', 'e'));
  %OptimizeFunctionOnNextCall(foo);
  assertTrue(sum_js_got_interpreted);
  assertEquals('abcde', foo('a', 'b', 'c', 'd', 'e'));
  assertFalse(sum_js_got_interpreted);
  assertOptimized(foo);
})();

// Test with 'arguments'.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo() {
    return sum_js3.apply(null, arguments);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertTrue(sum_js3_got_interpreted);

  // The call is not inlined with CreateArguments.
  %OptimizeFunctionOnNextCall(foo);
  assertEquals('abc', foo('a', 'b', 'c'));
  assertTrue(sum_js3_got_interpreted);
  assertOptimized(foo);
})();

// Test with inlined calls.
(function () {
  "use strict";
  var sum_js3_got_interpreted = true;
  function sum_js3(a, b, c) {
    sum_js3_got_interpreted = %IsBeingInterpreted();
    return a + b + c;
  }
  function foo(x, y, z) {
    return sum_js3.apply(null, [x, y, z]);
  }
  function bar(a, b, c) {
    return foo(c, b, a);
  }

  %PrepareFunctionForOptimization(sum_js3);
  %PrepareFunctionForOptimization(foo);
  %PrepareFunctionForOptimization(bar);
  assertEquals('cba', bar('a', 'b', 'c'));
  assertTrue(sum_js3_got_interpreted);

  // Optimization also works if the call is in an inlined function.
  %OptimizeFunctionOnNextCall(bar);
  assertEquals('cba', bar('a', 'b', 'c'));
  assertFalse(sum_js3_got_interpreted);
  assertOptimized(bar);
})();
