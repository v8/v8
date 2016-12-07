// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("Tests that Runtime.evaluate will generate correct previews.");

InspectorTest.addScript(
`
Object.prototype[0] = 'default-first';
var obj = {p1: {a:1}, p2: {b:'foo'}};
Object.defineProperties(obj, {
  p3: {
    get() { return 2 }
  },
  p4: {
    set(x) { return x }
  },
  p5: {
    get() { return 2 },
    set(x) { return x }
  }
});

Array.prototype[0] = 'default-first';
var arr = [,, 1, [2]];
Object.defineProperties(arr, {
  4: {
    get() { return 2 }
  },
  5: {
    set(x) { return x }
  },
  6: {
    get() { return 2 },
    set(x) { return x }
  }
});
`);

InspectorTest.runTestSuite([
  function testObjectPropertiesPreview(next)
  {
    Protocol.Runtime.evaluate({ "expression": "obj", "generatePreview": true })
        .then(result => InspectorTest.logMessage(result.result.result.preview))
        .then(next);
  },

  function testArrayPropertiesPreview(next)
  {
    Protocol.Runtime.evaluate({ "expression": "arr", "generatePreview": true })
        .then(result => InspectorTest.logMessage(result.result.result.preview))
        .then(next);
  }
]);
