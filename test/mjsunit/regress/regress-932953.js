// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function NonExtensibleBetweenSetterAndGetter() {
  o = {};
  o.x = 42;
  o.__defineGetter__("y", function() { });
  Object.preventExtensions(o);
  o.__defineSetter__("y", function() { });
  o.x = 0.1;
})();

(function InterleavedIntegrityLevel() {
  o = {};
  o.x = 42;
  o.__defineSetter__("y", function() { });
  Object.preventExtensions(o);
  o.__defineGetter__("y", function() { return 44; });
  Object.seal(o);
  o.x = 0.1;
  assertEquals(44, o.y);
})();
