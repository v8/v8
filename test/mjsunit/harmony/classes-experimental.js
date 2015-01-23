// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --experimental-classes --harmony-classes

'use strict';

class Base {
  constructor() {
    let o = new Object();
    o.prp = 1;
    return o;
  }
}

class Subclass extends Base {
  constructor() {
    try {
      this.prp1 = 3;
    } catch (e) {
      // TODO(dslomov): actually test the exception once TDZ is implemented.
    }
    super();
    assertSame(1, this.prp);
    assertSame(undefined, this.prp1);
    assertFalse(this.hasOwnProperty("prp1"));
    return this;
  }
}

let s = new Subclass();
assertSame(1, s.prp);
assertSame(undefined, s.prp1);
assertFalse(s.hasOwnProperty("prp1"));
