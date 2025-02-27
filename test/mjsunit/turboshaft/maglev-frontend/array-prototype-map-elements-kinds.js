// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --turbolev --turbofan --no-always-turbofan

load('test/mjsunit/elements-kinds-helpers.js');

function plusOne(x) {
  return x + 1;
}
%PrepareFunctionForOptimization(plusOne);

function plusOneAndMore(x) {
  return x + 1.1;
}
%PrepareFunctionForOptimization(plusOneAndMore);

function plusOneInObject(x) {
  return {a: x.a + 1};
}
%PrepareFunctionForOptimization(plusOneInObject);

function wrapInObject(x) {
  return {a: x};
}
%PrepareFunctionForOptimization(wrapInObject);

(function testPackedSmiElements() {
  function foo(a) {
    return a.map(plusOne);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, 3];
  const result = foo(array);
  assertTrue(HasPackedSmiElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [1, 2, 3];
  const result2 = foo(array2);
  assertTrue(HasPackedSmiElements(result2));
  assertOptimized(foo);
})();

// The only way to end up with a holey result is to start with a holey elements
// kind; the mapper function cannot insert holes into the array.

(function testHoleySmiElements() {
  function foo(a) {
    return a.map(plusOne);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, , 3];
  const result = foo(array);
  assertTrue(HasHoleySmiElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [1, 2, , 3];
  const result2 = foo(array2);
  assertTrue(HasHoleySmiElements(result2));
  assertOptimized(foo);
})();

(function testPackedDoubleElements1() {
  function foo(a) {
    return a.map(plusOne);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, 3.3];
  const result = foo(array);
  assertTrue(HasPackedDoubleElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [1, 2, 3.3];
  const result2 = foo(array2);
  assertTrue(HasPackedDoubleElements(result2));
  assertOptimized(foo);
})();

(function testPackedDoubleElements2() {
  function foo(a) {
    return a.map(plusOneAndMore);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, 3];
  const result = foo(array);
  assertTrue(HasPackedDoubleElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [1, 2, 3];
  const result2 = foo(array2);
  assertTrue(HasPackedDoubleElements(result2));
  assertOptimized(foo);
})();

(function testHoleyDoubleElements1() {
  function foo(a) {
    return a.map(plusOne);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, , 3.3];
  const result = foo(array);
  assertTrue(HasHoleyDoubleElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [1, 2, , 3.3];
  const result2 = foo(array2);
  assertTrue(HasHoleyDoubleElements(result2));
  assertOptimized(foo);
})();

(function testHoleyDoubleElements2() {
  function foo(a) {
    return a.map(plusOneAndMore);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, , 3];
  const result = foo(array);
  assertTrue(HasHoleyDoubleElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [1, 2, , 3];
  const result2 = foo(array2);
  assertTrue(HasHoleyDoubleElements(result2));
  assertOptimized(foo);
})();

(function testPackedElements1() {
  function foo(a) {
    return a.map(plusOneInObject);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [{a: 1}, {a: 2}, {a: 3}];
  const result = foo(array);
  assertTrue(HasPackedObjectElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [{a: 1}, {a: 2}, {a: 3}];
  const result2 = foo(array2);
  assertTrue(HasPackedObjectElements(result2));
  assertOptimized(foo);
})();

(function testPackedElements2() {
  function foo(a) {
    return a.map(wrapInObject);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, 3];
  const result = foo(array);
  assertTrue(HasPackedObjectElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [1, 2, 3];
  const result2 = foo(array2);
  assertTrue(HasPackedObjectElements(result2));
  assertOptimized(foo);
})();

(function testHoleyObjectElements1() {
  function foo(a) {
    return a.map(plusOneInObject);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [{a: 1}, {a: 2}, , {a: 3}];
  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [{a: 1}, {a: 2}, , {a: 3}];
  const result2 = foo(array2);
  assertTrue(HasHoleyObjectElements(result2));
  assertOptimized(foo);
})();

(function testHoleyObjectElements2() {
  function foo(a) {
    return a.map(wrapInObject);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, , 3];
  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const array2 = [1, 2, , 3];
  const result2 = foo(array2);
  assertTrue(HasHoleyObjectElements(result2));
  assertOptimized(foo);
})();

(function testDictionaryElements1() {
  function foo(a) {
    return a.map(plusOne);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, 3];
  const array2 = [1, 2, 3];
  for (let i = 0; i < 100000; i += 100) {
    array[i] = 0;
    array2[i] = 0;
    if (%HasDictionaryElements(array)) {
      break;
    }
  }
  assertTrue(%HasDictionaryElements(array));
  assertTrue(%HasDictionaryElements(array2));

  const result = foo(array);
  assertTrue(HasHoleySmiElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const result2 = foo(array2);
  assertTrue(HasHoleySmiElements(result2));
  assertOptimized(foo);
})();

(function testDictionaryElements2() {
  function foo(a) {
    return a.map(plusOne);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [1, 2, 3];
  const array2 = [1, 2, 3];
  for (let i = 0; i < 100000; i += 100) {
    array[i] = 0.1;
    array2[i] = 0.1;
    if (%HasDictionaryElements(array)) {
      break;
    }
  }
  assertTrue(%HasDictionaryElements(array));
  assertTrue(%HasDictionaryElements(array2));

  const result = foo(array);
  assertTrue(HasHoleyDoubleElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const result2 = foo(array2);
  assertTrue(HasHoleyDoubleElements(result2));
  assertOptimized(foo);
})();

(function testDictionaryElements3() {
  function foo(a) {
    return a.map(plusOneInObject);
  }
  %PrepareFunctionForOptimization(foo);

  const array = [{a: 1}, {a: 2}, {a: 3}];
  const array2 = [{a: 1}, {a: 2}, {a: 3}];
  for (let i = 0; i < 100000; i += 100) {
    array[i] = {a: 0};
    array2[i] = {a: 0};
    if (%HasDictionaryElements(array)) {
      break;
    }
  }
  assertTrue(%HasDictionaryElements(array));
  assertTrue(%HasDictionaryElements(array2));

  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));

  %OptimizeFunctionOnNextCall(foo);

  const result2 = foo(array2);
  assertTrue(HasHoleyObjectElements(result2));
  assertOptimized(foo);
})();
