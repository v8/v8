// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --allow-natives-syntax --noverify-heap --noenable-slow-asserts

// --noverify-heap and --noenable-slow-asserts are set because the test is too
// slow with it on.

// Ensure that keyed stores work, and optimized functions learn if the
// store required change to dictionary mode. Verify that stores that grow
// the array into large object space don't cause a deopt.
(function() {
  var a = [];

  function foo(a, i) {
    a[i] = 5.3;
  }

  foo(a, 1);
  foo(a, 2);
  foo(a, 3);
  %OptimizeFunctionOnNextCall(foo);
  a[3] = 0;
  foo(a, 3);
  assertEquals(a[3], 5.3);
  foo(a, 50000);
  assertUnoptimized(foo);
  assertTrue(%HasDictionaryElements(a));

  var b = [];
  foo(b, 1);
  foo(b, 2);
  // Put b in dictionary mode.
  b[10000] = 5;
  assertTrue(%HasDictionaryElements(b));
  foo(b, 3);
  %OptimizeFunctionOnNextCall(foo);
  foo(b, 50000);
  assertOptimized(foo);
  assertTrue(%HasDictionaryElements(b));

  // Clearing feedback for the StoreIC in foo is important for runs with
  // flag --stress-opt.
  %ClearFunctionTypeFeedback(foo);
})();


(function() {
  var a = new Array(10);

  function foo2(a, i) {
    a[i] = 50;
  }

  // The KeyedStoreIC will learn GROW_MODE.
  foo2(a, 10);
  foo2(a, 12);
  foo2(a, 31);
  %OptimizeFunctionOnNextCall(foo2);
  foo2(a, 40);

  // This test is way too slow without crankshaft.
  if (4 != %GetOptimizationStatus(foo2)) {
    assertOptimized(foo2);
    assertTrue(%HasFastSmiElements(a));

    // Grow a large array into large object space through the keyed store
    // without deoptimizing. Grow by 10s. If we set elements too sparsely, the
    // array will convert to dictionary mode.
    a = new Array(99999);
    assertTrue(%HasFastSmiElements(a));
    for (var i = 0; i < 263000; i += 10) {
      foo2(a, i);
    }

    // Verify that we are over 1 page in size, and foo2 remains optimized.
    // This means we've smoothly transitioned to allocating in large object
    // space.
    assertTrue(%HasFastSmiElements(a));
    assertTrue(a.length * 4 > (1024 * 1024));
    assertOptimized(foo2);
  }

  %ClearFunctionTypeFeedback(foo2);
})();
