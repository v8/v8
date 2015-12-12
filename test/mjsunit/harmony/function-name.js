// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-function-name

(function testVariableDeclarationsFunction() {
  'use strict';
  var a = function(){};
  assertEquals('a', a.name);
  let b = () => {};
  assertEquals('b', b.name);
  const c = ((function(){}));
  assertEquals('c', c.name);

  var x = function(){}, y = () => {}, z = function withName() {};
  assertEquals('x', x.name);
  assertEquals('y', y.name);
  assertEquals('withName', z.name);
})();

(function testVariableDeclarationsClass() {
  'use strict';
  var a = class {};
  assertEquals('a', a.name);
  let b = ((class {}));
  assertEquals('b', b.name);
  // Should not overwrite name property.
  const c = class { static name() { } }
  assertEquals('function', typeof c.name);

  var x = class {}, y = class NamedClass {};
  assertEquals('x', x.name);
  assertEquals('NamedClass', y.name);
})();
