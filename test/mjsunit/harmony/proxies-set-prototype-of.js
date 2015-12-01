// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-proxies

var target = { target: 1 };
target.__proto__ = {};
var handler = { handler: 1 };
var proxy = new Proxy(target, handler);

assertSame(Object.getPrototypeOf(proxy), target.__proto__ );

assertThrows(function() { Object.setPrototypeOf(proxy, undefined) }, TypeError);
assertThrows(function() { Object.setPrototypeOf(proxy, 1) }, TypeError);

var prototype = [1];
assertSame(proxy, Object.setPrototypeOf(proxy, prototype));
assertSame(prototype, Object.getPrototypeOf(proxy));
assertSame(prototype, Object.getPrototypeOf(target));

handler.setPrototypeOf = function(target, proto) {
  return false;
};
assertThrows(function() { Object.setPrototypeOf(proxy, {a:1}) }, TypeError);

handler.setPrototypeOf = function(target, proto) {
  return undefined;
};
assertThrows(function() { Object.setPrototypeOf(proxy, {a:2}) }, TypeError);

handler.setPrototypeOf = function(proto) {};
assertThrows(function() { Object.setPrototypeOf(proxy, {a:3}) }, TypeError);

handler.setPrototypeOf = function(target, proto) {
  throw Error();
};
assertThrows(function() { Object.setPrototypeOf(proxy, {a:4}) }, Error);

var seen_prototype;
var seen_target;
handler.setPrototypeOf = function(target, proto) {
  seen_target = target;
  seen_prototype = proto;
  return true;
}
assertSame(Object.setPrototypeOf(proxy, {a:5}), proxy);
assertSame(target, seen_target);
assertEquals({a:5}, seen_prototype);

// Target is a Proxy:
var target2 = new Proxy(target, {});
var proxy2 = new Proxy(target2, {});
assertSame(Object.getPrototypeOf(proxy2), target.__proto__ );

prototype = [2,3];
assertSame(proxy2, Object.setPrototypeOf(proxy2, prototype));
assertSame(prototype, Object.getPrototypeOf(proxy2));
assertSame(prototype, Object.getPrototypeOf(target));
