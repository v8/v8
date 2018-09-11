// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Exercises the check that determines whether to use the
// "FastCallFunction_" stub when calling the comparison function.

(function TestClassConstructorAsCmpFn() {
  class FooBar {};
  assertThrows(() => [1, 2].sort(FooBar));
})();


const globalThis = this;
(function TestGlobalProxyIsSetAsReceiverWhenSloppy() {
  [1, 2].sort((a, b) => {
    assertSame(globalThis, this);
    return a - b;
  });
})();


(function TestReceiverIsUndefinedWhenStrict() {
  "use strict";

  [1, 2].sort((a, b) => {
    assertSame(undefined, this);
    return a - b;
  });
})();


(function TestBoundFunctionAsCmpFn() {
  const object = { foo: "bar" };

  function cmpfn(a, b) {
    assertSame(this, object);
    assertSame(this.foo, "bar");
    return a - b;
  };

  const bound_cmpfn = cmpfn.bind(object);
  [1, 2].sort(bound_cmpfn);
})();
