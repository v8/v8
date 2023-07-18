// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --allow-natives-syntax

// Tests megamorphic case
function clone1(o) {
  return {...o};
}
// Tests monomorphic case
function clone2(o) {
  return {...o};
}
%PrepareFunctionForOptimization(clone1);
%PrepareFunctionForOptimization(clone2);

function test(a, b) {
  %ClearFunctionFeedback(clone2);
  assertEquals({...b}, a);
  assertEquals(clone1(b), a);
  assertEquals(clone1(b), a);
  assertEquals(clone2(b), a);
  assertEquals(clone2(b), a);
  assertEquals(clone1(b).constructor, a.constructor);
  assertEquals(clone2(b).constructor, a.constructor);
  %ClearFunctionFeedback(clone2);
  assertEquals(clone2(b).constructor, a.constructor);
}

test({});
test({}, false);
test({}, 1.1);
test({}, NaN);
test({}, 2);
test({}, new function(){});
test({}, test);
test({}, {}.__proto__);
test({}, new Proxy({}, function(){}));
test({a: "a"}, new Proxy({a: "a"}, function(){}));
test({}, BigInt(2));
test({}, Symbol("ab"));
test({0: "a", 1: "b"}, "ab");

// non-enumerable
var obj = {a: 1}
Object.defineProperty(obj, 'b', {
  value: 42,
});
test({a: 1}, obj);
assertFalse(%HaveSameMap({...obj},obj));

// some not writable
var obj = {}
Object.defineProperty(obj, 'a', {
  value: 42,
  writable: false,
  enumerable: true
});
obj.b = 1;
test({a: 42, b: 1}, obj);
assertFalse(%HaveSameMap({...obj},obj));

// non-enumerable after non-writable
var obj = {}
Object.defineProperty(obj, 'a', {
  value: 1,
  writable: false,
  enumerable: true,
});
Object.defineProperty(obj, 'b', {
  value: 2,
});
test({a: 1}, obj);
var c = {...obj, a: 4};

test({0:1,1:2}, [1,2]);

var buffer = new ArrayBuffer(24);
var idView = new Uint32Array(buffer, 0, 2);
test({}, buffer);
test({0:0,1:0}, idView);
