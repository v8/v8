// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-proxies

var target = {
  "target_one": 1
};
target.__proto__ = {
  "target_two": 2
};
var handler = {
  enumerate: function(target) {
    function* keys() {
      yield "foo";
      yield "bar";
    }
    return keys();
  },
  // For-in calls "has" on every iteration, so for TestForIn() below to
  // detect all results of the "enumerate" trap, "has" must return true.
  has: function(target, name) {
    return true;
  }
}

var proxy = new Proxy(target, handler);

function TestForIn(receiver, expected) {
  var result = [];
  for (var k in receiver) {
    result.push(k);
  }
  assertEquals(expected, result);
}

TestForIn(proxy, ["foo", "bar"]);

// Properly call traps on proxies on the prototype chain.
var receiver = {
  "receiver_one": 1
};
receiver.__proto__ = proxy;
TestForIn(receiver, ["receiver_one", "foo", "bar"]);

// Fall through to default behavior when trap is undefined.
handler.enumerate = undefined;
TestForIn(proxy, ["target_one", "target_two"]);
delete handler.enumerate;
TestForIn(proxy, ["target_one", "target_two"]);

// Non-string keys must be filtered.
function TestNonStringKey(key) {
  handler.enumerate = function(target) {
    function* keys() { yield key; }
    return keys();
  }
  assertThrows("for (var k in proxy) {}", TypeError);
}

TestNonStringKey(1);
TestNonStringKey(3.14);
TestNonStringKey(Symbol("foo"));
TestNonStringKey({bad: "value"});
TestNonStringKey(null);
TestNonStringKey(undefined);
TestNonStringKey(true);
