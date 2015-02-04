// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --experimental-classes --harmony-classes

'use strict';

class Base {
  constructor(a, b) {
    let o = new Object();
    o.prp = a + b;
    return o;
  }
}

class Subclass extends Base {
  constructor(a, b) {
    var exn;
    try {
      this.prp1 = 3;
    } catch (e) {
      exn = e;
    }
    assertTrue(exn instanceof ReferenceError);
    super(a, b);
    assertSame(a + b, this.prp);
    assertSame(undefined, this.prp1);
    assertFalse(this.hasOwnProperty("prp1"));
    return this;
  }
}

let b = new Base(1, 2);
assertSame(3, b.prp);

let s = new Subclass(2, -1);
assertSame(1, s.prp);
assertSame(undefined, s.prp1);
assertFalse(s.hasOwnProperty("prp1"));

class Subclass2 extends Base {
  constructor() {
    super(1,2);

    let called = false;
    function tmp() { called = true; return 3; }
    var exn = null;
    try {
      super(tmp(),4);
    } catch(e) { exn = e; }
    assertTrue(exn !== null);
    assertFalse(called);
  }
}

new Subclass2();
