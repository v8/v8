// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

var set_count = 0;
var get_count = 0;
var has_count = 0;
var property_descriptor_count = 0;
globalThis.__proto__ = new Proxy({},
                                 {get() {get_count++},
                                  has() {has_count++;},
                                  set() {set_count++;},
                                  getOwnPropertyDescriptor() {property_desciptor_count++}});
function checkCounts(count) {
  assertEquals(has_count, count);
  assertEquals(set_count, 0);
  assertEquals(get_count, 0);
  assertEquals(property_descriptor_count, 0);
}

function store_lookup_global_has_returns_false() {
  eval("var b = 10");
  return x = 10;
}
assertEquals(store_lookup_global_has_returns_false(), 10);
checkCounts(1);

%EnsureFeedbackVectorForFunction(store_lookup_global_has_returns_false);
delete x;
assertEquals(store_lookup_global_has_returns_false(), 10);
checkCounts(2);
delete x;
assertEquals(store_lookup_global_has_returns_false(), 10);
checkCounts(3);

function store_global_has_returns_false(n) {
  return x0 = 10;
}
assertEquals(store_global_has_returns_false(), 10);
checkCounts(4);
assertEquals("number", typeof(x));

%EnsureFeedbackVectorForFunction(store_global_has_returns_false);
delete x0;
assertEquals(store_global_has_returns_false(), 10);
checkCounts(5);
delete x0;
assertEquals(store_global_has_returns_false(), 10);
checkCounts(6);


// Check when the object is present on the proxy.
get_count = 0;
has_count = 0;
set_count = 0;
property_descriptor_count = 0;

var proxy = new Proxy({}, {get() {get_count++;},
                           has() {has_count++; return true;},
                           set() {set_count++; return true; },
                           getOwnPropertyDescriptor() {property_desciptor_count++}});
Object.setPrototypeOf(globalThis, proxy);
function checkCountsWithSet(count) {
  assertEquals(has_count, count);
  assertEquals(set_count, count);
  assertEquals(get_count, 0);
  assertEquals(property_descriptor_count, 0);
}

function store_lookup_global() {
  eval("var b = 10");
  return x1 = 10;
}
assertEquals(store_lookup_global(), 10);
checkCountsWithSet(1);

%EnsureFeedbackVectorForFunction(store_lookup_global);
assertEquals(store_lookup_global(), 10);
checkCountsWithSet(2);
assertEquals(store_lookup_global(), 10);
checkCountsWithSet(3);

function store_global() {
  return x1 = 10;
}

assertEquals(store_global(), 10);
checkCountsWithSet(4);

%EnsureFeedbackVectorForFunction(store_global);
assertEquals(store_global(), 10);
checkCountsWithSet(5);
assertEquals(store_global(), 10);
checkCountsWithSet(6);
assertEquals("undefined", typeof(x1));

// Check unbound variable access inside typeof
get_count = 0;
has_count = 0;
set_count = 0;

// Check that if has property returns true we don't have set trap.
proxy = new Proxy({}, {has() {has_count++; return true;},
                       getOwnPropertyDescriptor() {property_desciptor_count++}});
Object.setPrototypeOf(globalThis, proxy);

function store_global_no_set() {
  return x2 = 10;
}

has_count = 3;
assertEquals(store_global_no_set(), 10);
checkCounts(4);
assertEquals("number", typeof(x2));

%EnsureFeedbackVectorForFunction(store_global_no_set);
delete x2;
assertEquals(store_global_no_set(), 10);
checkCounts(5);
delete x2;
assertEquals(store_global_no_set(), 10);
checkCounts(6);
assertEquals("undefined", typeof(x1));
