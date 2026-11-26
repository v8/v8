// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// Array literals that contain only holes (for example `[,,]`) and have a
// length of at most 8 don't get copy-on-write elements. Longer hole-only
// literals, and literals that contain at least one constant element, still
// do.

// kMaxLengthOfHoleOnlyArrayWithoutCow in src/ast/ast.cc.
const kMaxLengthWithoutCow = 8;

let nextId = 0;
function holeOnlyLiteral(length) {
  // Give each generated function a distinct source so `new Function` doesn't
  // hit the eval compilation cache and share a FeedbackCell / AllocationSite.
  return new Function(`return [${','.repeat(length)}];  // ${nextId++}`);
}

function assertHoles(array, length) {
  assertEquals(length, array.length);
  for (let i = 0; i < length; i++) assertFalse(i in array);
}

for (const length of [1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 252, 253, 300]) {
  const f = holeOnlyLiteral(length);
  const cow = length > kMaxLengthWithoutCow;
  // Without a feedback vector, the array is created from the boilerplate
  // description.
  let a = f();
  assertHoles(a, length);
  assertEquals(cow, %HasCowElements(a));
  assertTrue(%HasSmiElements(a));
  assertTrue(%HasHoleyElements(a));
  // With a feedback vector, the first calls create the allocation site and the
  // boilerplate, and the next calls copy the boilerplate.
  %EnsureFeedbackVectorForFunction(f);
  for (let i = 0; i < 4; i++) {
    a = f();
    assertHoles(a, length);
    assertEquals(cow, %HasCowElements(a));
    assertTrue(%HasSmiElements(a));
    assertTrue(%HasHoleyElements(a));
  }
}

// Literals with at least one constant element keep copy-on-write elements.
for (const source of ['[0]', '[0,,]', '[,0]', '[,,0,,]', '[,,"a"]', '[1, 2]',
                      '[true]', '[null]']) {
  const f = new Function(`return ${source};`);
  assertTrue(%HasCowElements(f()));
  %EnsureFeedbackVectorForFunction(f);
  for (let i = 0; i < 4; i++) assertTrue(%HasCowElements(f()));
}

// Arrays from the same literal are independent, with and without
// copy-on-write elements.
for (const length of [4, 9]) {
  const f = holeOnlyLiteral(length);
  %EnsureFeedbackVectorForFunction(f);
  const arrays = [];
  for (let i = 0; i < 4; i++) {
    const a = f();
    a[i] = i;
    arrays.push(a);
  }
  for (let i = 0; i < 4; i++) {
    for (let j = 0; j < length; j++) {
      if (i === j) {
        assertEquals(i, arrays[i][j]);
      } else {
        assertFalse(j in arrays[i]);
      }
    }
  }
  assertHoles(f(), length);
}

// Elements kind transitions of an array don't change other arrays from the
// same literal.
(function() {
  function f() { return [,,,]; }
  %EnsureFeedbackVectorForFunction(f);
  // An array that is never written. The stores below can change the elements
  // kind of the boilerplate.
  const untouched = f();
  const arrays = [];
  const values = [];
  for (const value of [1, 1.5, {}, 'a']) {
    for (let i = 0; i < 3; i++) {
      const a = f();
      assertHoles(a, 3);
      assertFalse(%HasCowElements(a));
      a[1] = value;
      arrays.push(a);
      values.push(value);
      // The store doesn't change the arrays that were created before.
      for (let j = 0; j < arrays.length; j++) {
        assertEquals(3, arrays[j].length);
        assertSame(values[j], arrays[j][1]);
        assertFalse(0 in arrays[j]);
        assertFalse(2 in arrays[j]);
      }
      assertHoles(untouched, 3);
    }
  }
})();

// Nested literals.
(function() {
  function f() { return [[,,], {a: [,,]}, [0,,]]; }
  %EnsureFeedbackVectorForFunction(f);
  for (let i = 0; i < 4; i++) {
    const a = f();
    assertFalse(%HasCowElements(a[0]));
    assertFalse(%HasCowElements(a[1].a));
    assertTrue(%HasCowElements(a[2]));
    assertHoles(a[0], 2);
    assertHoles(a[1].a, 2);
    assertEquals(0, a[2][0]);
    assertFalse(1 in a[2]);
    a[0][0] = 1;
    a[1].a[1] = {};
    // A Smi store copies the copy-on-write elements but doesn't change the
    // elements kind, so the boilerplate keeps copy-on-write elements.
    a[2][1] = 1;
  }
})();

// Operations that read the holes.
(function() {
  const a = [,,,];
  assertEquals(',,', a.join());
  assertEquals([undefined, undefined, undefined], [...a]);
  assertEquals([], Object.keys(a));
  assertEquals(-1, a.indexOf(undefined));
  assertTrue(a.includes(undefined));
})();

// Accessors on Array.prototype see the holes. Keep this test last, because it
// changes Array.prototype.
(function() {
  let setterCalls = 0;
  Object.defineProperty(Array.prototype, 1, {
    get() { return 'from prototype'; },
    set(value) { setterCalls++; },
    configurable: true,
  });
  function f() { return [,,]; }
  %EnsureFeedbackVectorForFunction(f);
  for (let i = 0; i < 4; i++) {
    const a = f();
    assertEquals('from prototype', a[1]);
    a[1] = 42;
    assertEquals(i + 1, setterCalls);
    assertFalse(a.hasOwnProperty(1));
    a[0] = 7;
    assertEquals(7, a[0]);
    assertEquals('from prototype', a[1]);
  }
  delete Array.prototype[1];
})();
