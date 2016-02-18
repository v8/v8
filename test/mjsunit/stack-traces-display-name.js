// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var o = { f: function() { throw new Error(); } };
o.g1 = function() { o.f() }
o.g2 = o.g1;
o.h = function() { o.g1() }
o.f.displayName = "MySpecialFunction";

try {
  o.h();
} catch (e) {
  assertTrue(e.stack.indexOf("MySpecialFunction") != -1);
}
