// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --no-always-turbofan

load('test/mjsunit/elements-kinds-helpers.js');

function plusOne(x) {
  return x + 1;
}

function plusOneAndMore(x) {
  return x + 1.1;
}

function plusOneInObject(x) {
  return {a: x.a + 1};
}

function wrapInObject(x) {
  return {a: x};
}

function unwrapObject(x) {
  return x.a;
}

let ix = 0;
function transitionFromSmiToDouble(x) {
  if (ix++ == 0) {
    return 0;
  }
  return 1.1;
}

function transitionFromSmiToObject(x) {
  if (ix++ == 0) {
    return 0;
  }
  return {a: 1};
}

function transitionFromDoubleToObject(x) {
  if (ix++ == 0) {
    return 1.1;
  }
  return {a: 1};
}

function transitionFromObjectToDouble(x) {
  if (ix++ == 0) {
    return {a: 1};
  }
  return 1.1;
}

function resetTransitionFunctions() {
  ix = 0;
}

(function testPackedSmiElements() {
  function foo(a) {
    return a.map(plusOne);
  }

  const array = [1, 2, 3];
  const result = foo(array);
  assertTrue(HasPackedSmiElements(result));
})();

// The only way to end up with a holey result is to start with a holey elements
// kind; the mapper function cannot insert holes into the array.

(function testHoleySmiElements() {
  function foo(a) {
    return a.map(plusOne);
  }

  const array = [1, 2, , 3];
  const result = foo(array);
  assertTrue(HasHoleySmiElements(result));
})();

(function testPackedDoubleElements1() {
  function foo(a) {
    return a.map(plusOne);
  }

  const array = [1, 2, 3.3];
  const result = foo(array);
  assertTrue(HasPackedDoubleElements(result));
})();

(function testPackedDoubleElements2() {
  function foo(a) {
    return a.map(plusOneAndMore);
  }

  const array = [1, 2, 3];
  const result = foo(array);
  assertTrue(HasPackedDoubleElements(result));
})();

(function testHoleyDoubleElements1() {
  function foo(a) {
    return a.map(plusOne);
  }

  const array = [1, 2, , 3.3];
  const result = foo(array);
  assertTrue(HasHoleyDoubleElements(result));
})();

(function testHoleyDoubleElements2() {
  function foo(a) {
    return a.map(plusOneAndMore);
  }

  const array = [1, 2, , 3];
  const result = foo(array);
  assertTrue(HasHoleyDoubleElements(result));
})();

(function testPackedElements1() {
  function foo(a) {
    return a.map(plusOneInObject);
  }

  const array = [{a: 1}, {a: 2}, {a: 3}];
  const result = foo(array);
  assertTrue(HasPackedObjectElements(result));
})();

(function testPackedElements2() {
  function foo(a) {
    return a.map(wrapInObject);
  }

  const array = [1, 2, 3];
  const result = foo(array);
  assertTrue(HasPackedObjectElements(result));
})();

(function testHoleyObjectElements1() {
  function foo(a) {
    return a.map(plusOneInObject);
  }

  const array = [{a: 1}, {a: 2}, , {a: 3}];
  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));
})();

(function testHoleyObjectElements2() {
  function foo(a) {
    return a.map(wrapInObject);
  }

  const array = [1, 2, , 3];
  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));
})();

(function testDictionaryElements1() {
  function foo(a) {
    return a.map(plusOne);
  }

  const array = [1, 2, 3];
  for (let i = 0; i < 100000; i += 100) {
    array[i] = 0;
    if (%HasDictionaryElements(array)) {
      break;
    }
  }
  assertTrue(%HasDictionaryElements(array));

  const result = foo(array);
  assertTrue(HasHoleySmiElements(result));
})();

(function testDictionaryElements2() {
  function foo(a) {
    return a.map(plusOne);
  }

  const array = [1, 2, 3];
  for (let i = 0; i < 100000; i += 100) {
    array[i] = 0.1;
    if (%HasDictionaryElements(array)) {
      break;
    }
  }
  assertTrue(%HasDictionaryElements(array));

  const result = foo(array);
  assertTrue(HasHoleyDoubleElements(result));
})();

(function testDictionaryElements3() {
  function foo(a) {
    return a.map(plusOneInObject);
  }

  const array = [{a: 1}, {a: 2}, {a: 3}];
  for (let i = 0; i < 100000; i += 100) {
    array[i] = {a: 0};
    if (%HasDictionaryElements(array)) {
      break;
    }
  }
  assertTrue(%HasDictionaryElements(array));

  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));
})();

(function testFromObjectToPackedSmi() {
  function foo(a) {
    return a.map(unwrapObject);
  }

  const array = [{a: 1}, {a: 2}, {a: 3}];
  const result = foo(array);
  assertTrue(HasPackedSmiElements(result));
})();

(function testFromObjectToHoleySmi() {
  function foo(a) {
    return a.map(unwrapObject);
  }

  const array = [{a: 1}, {a: 2}, , {a: 3}];
  const result = foo(array);
  assertTrue(HasHoleySmiElements(result));
})();

(function testTransitionFromPackedSmiToDoubleWhileMapping() {
  resetTransitionFunctions();

  function foo(a) {
    return a.map(transitionFromSmiToDouble);
  }

  const array = [0, 0, 0];
  const result = foo(array);
  assertTrue(HasPackedDoubleElements(result));
})();

(function testTransitionFromHoleySmiToDoubleWhileMapping() {
  resetTransitionFunctions();

  function foo(a) {
    return a.map(transitionFromSmiToDouble);
  }

  const array = [0, 0, , 0];
  const result = foo(array);
  assertTrue(HasHoleyDoubleElements(result));
})();

(function testTransitionFromPackedSmiToObjectWhileMapping() {
  resetTransitionFunctions();

  function foo(a) {
    return a.map(transitionFromSmiToObject);
  }

  const array = [0, 0, 0];
  const result = foo(array);
  assertTrue(HasPackedObjectElements(result));
})();

(function testTransitionFromHoleySmiToObjectWhileMapping() {
  resetTransitionFunctions();

  function foo(a) {
    return a.map(transitionFromSmiToObject);
  }

  const array = [0, 0, , 0];
  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));
})();

(function testTransitionFromPackedSmiToDoubleToObjectWhileMapping() {
  resetTransitionFunctions();

  function foo(a) {
    return a.map(transitionFromDoubleToObject);
  }

  const array = [0, 0, 0];
  const result = foo(array);
  assertTrue(HasPackedObjectElements(result));
})();

(function testTransitionFromHoleySmiToDoubleToObjectWhileMapping() {
  resetTransitionFunctions();

  function foo(a) {
    return a.map(transitionFromDoubleToObject);
  }

  const array = [0, 0, , 0];
  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));
})();

(function testTransitionFromPackedSmiToObjectToDoubleWhileMapping() {
  resetTransitionFunctions();

  function foo(a) {
    return a.map(transitionFromObjectToDouble);
  }

  const array = [0, 0, 0];
  const result = foo(array);
  assertTrue(HasPackedObjectElements(result));
})();

(function testTransitionFromHoleySmiToObjectToDoubleWhileMapping() {
  resetTransitionFunctions();

  function foo(a) {
    return a.map(transitionFromObjectToDouble);
  }

  const array = [0, 0, , 0];
  const result = foo(array);
  assertTrue(HasHoleyObjectElements(result));
})();
