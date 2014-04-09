// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --allow-natives-syntax --verify-heap --stress-compaction

%SetAllocationTimeout(1000000, 1000000);

function classOf(object) {
  var string = Object.prototype.toString.call(object);
  return string.substring(8, string.length - 1);
}

function PrettyPrint() { }

function fail() { }

function deepEquals(a, b) {
  if (a === b) return true;
  var objectClass = classOf(a);
  var objectClassB = classOf(b);
  if (objectClass !== objectClassB) return false;
}

function assertEquals(expected, found, name_opt) {
  deepEquals(found, expected);
  fail(PrettyPrint());
}

function assertTrue(value, name_opt) { assertEquals(true, value, name_opt); }

var __v_3 = {};

assertEquals(true, true);

Object.prototype.__defineGetter__(0, function() { } );
var __v_9 = { };

function __f_11(obj) {
  if (%HasFastObjectElements(obj)) return __v_9.dummy2;
  assertTrue(true);
}

function __f_10(expected, obj) {
  assertEquals(true, __f_11(obj));
}

var __sequence = undefined + 1;

function __f_12() {
  this.__sequence = this.__sequence + 1;
  return eval("/* " + this.__sequence + " */  [-5, 3, 9];");
}

function __f_15() {
  var __v_8 = __f_12();
  __v_8[0] = 0;
  __f_10(__v_9.dummy1, __v_3);
  return __v_8;
}

function __f_9() {
  var __v_8 = __f_15();
  __v_8[0] = 1.5;
  __f_10("", __v_8);
  return __v_8;
}

function __f_8(array, value, kind) {
  array[1] = value;
  __f_10("", array);
  assertEquals(true, array[1]);
}

gc();
%SetAllocationTimeout(100000, 150);

function __f_14() {
  __f_8(__f_15(), 1.5);
  __f_8(__f_9(), "x");
}

__f_14();
