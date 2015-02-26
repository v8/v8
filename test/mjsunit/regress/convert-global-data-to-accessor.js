// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f(o) {
  return o.x;
}
this.x = 100;
f(this);
f(this);
f(this);

Object.defineProperty(this, 'x', { get: function() { return 10; }});
assertEquals(10, this.x);
assertEquals(10, f(this));
