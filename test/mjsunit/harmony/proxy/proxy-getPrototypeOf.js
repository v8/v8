// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-proxies

var target = { target: 1 };
target.__proto__ = {};
var handler = { handler: 1 };
var proxy = new Proxy(target, handler);

assertSame(Object.getPrototypeOf(proxy), target.__proto__ );

target.__proto__ = [];
assertSame(Object.getPrototypeOf(proxy), target.__proto__);

handler.getPrototypeOf = function() {
  return 1;
}
assertThrows(function() { Object.getPrototypeOf(proxy) }, TypeError);

var target_prototype = {a:1, b:2};
handler.getPrototypeOf = function() {
  return target_prototype ;
}
assertSame(Object.getPrototypeOf(proxy), target_prototype);

// Test with proxy target:
var proxy2 = new Proxy(proxy, {});
assertSame(Object.getPrototypeOf(proxy2), target_prototype);

// Test with Proxy handler:
// TODO(neis,cbruni): Uncomment once the get trap works again.
// var proxy3_prototype = {};
// var handler_proxy = new Proxy({
//   getPrototypeOf: function() { return proxy3_prototype }
// }, {});
// var proxy3 = new Proxy(target, handler_proxy);
// assertSame(Object.getPrototypeOf(proxy3), target_prototype);
