// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test that Array builtins can be called on primitive values.
var values = [ 23, 4.2, true, false, 0/0 ];
for (var i = 0; i < values.length; ++i) {
  var v = values[i];
  Array.prototype.pop.call(v);
  Array.prototype.push.call(v);
  Array.prototype.shift.call(v);
  Array.prototype.unshift.call(v);
}

// Test that ToObject on primitive values is only called once.
var length_receiver, element_receiver;
function length() { length_receiver = this; return 1; }
function element() { element_receiver = this; return "x"; }
Object.defineProperty(Number.prototype, "length", { get:length, set:length });
Object.defineProperty(Number.prototype, "0", { get:element, set:element });
Object.defineProperty(Number.prototype, "1", { get:element, set:element });

assertDoesNotThrow("Array.prototype.pop.call(23)");
assertEquals(new Number(23), length_receiver);
assertSame(length_receiver, element_receiver);

assertDoesNotThrow("Array.prototype.push.call(42, 'y')");
assertEquals(new Number(42), length_receiver);
assertSame(length_receiver, element_receiver);

assertDoesNotThrow("Array.prototype.shift.call(65)");
assertEquals(new Number(65), length_receiver);
assertSame(length_receiver, element_receiver);

assertDoesNotThrow("Array.prototype.unshift.call(99, 'z')");
assertEquals(new Number(99), length_receiver);
assertSame(length_receiver, element_receiver);
