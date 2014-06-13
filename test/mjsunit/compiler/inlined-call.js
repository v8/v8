// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --noalways-opt

var array = [];
for (var i = 0; i < 100; ++i) {
  array[i] = i;
}

var copy = array.slice();

function unshiftsArray(num) {
  [].unshift.call(array, num);
}

unshiftsArray(50);
unshiftsArray(60);
%OptimizeFunctionOnNextCall(unshiftsArray);
unshiftsArray(80);
unshiftsArray(50);
unshiftsArray(60);

copy.unshift(50);
copy.unshift(60);
copy.unshift(80);
copy.unshift(50);
copy.unshift(60);

assertOptimized(unshiftsArray);
assertArrayEquals(array, copy);


var called = 0;
var funRecv;
// For the HConstant
Array.prototype.fun = function() {
  funRecv = this;
  called++;
  assertEquals(0, arguments.length);
};

function callNoArgs() {
  [].fun.call();
}

callNoArgs();
callNoArgs();
assertEquals(this, funRecv);
%OptimizeFunctionOnNextCall(callNoArgs);
callNoArgs();
assertEquals(this, funRecv);
assertEquals(3, called);
assertOptimized(callNoArgs);

var funStrictRecv;
called = 0;
Array.prototype.funStrict = function() {
  "use strict";
  funStrictRecv = this;
  called++;
  assertEquals(0, arguments.length);
};

function callStrictNoArgs() {
  [].funStrict.call();
}

callStrictNoArgs();
callStrictNoArgs();
assertEquals(undefined, funStrictRecv);
%OptimizeFunctionOnNextCall(callStrictNoArgs);
callStrictNoArgs();
assertEquals(undefined, funStrictRecv);
assertEquals(3, called);
assertOptimized(callStrictNoArgs);

called = 0;
Array.prototype.manyArgs = function() {
  "use strict";
  assertEquals(5, arguments.length);
  assertEquals(0, this);
  assertEquals(5, arguments[4]);
  called++;
}

function callManyArgs() {
  [].manyArgs.call(0, 1, 2, 3, 4, 5);
}

callManyArgs();
callManyArgs();
%OptimizeFunctionOnNextCall(callManyArgs);
callManyArgs();
assertOptimized(callManyArgs);
assertEquals(called, 3);

called = 0;
Array.prototype.manyArgsSloppy = function() {
  assertTrue(this instanceof Number);
  assertEquals(5, arguments.length);
  assertEquals(0, this.valueOf());
  assertEquals(5, arguments[4]);
  called++;
}

function callManyArgsSloppy() {
  [].manyArgsSloppy.call(0, 1, 2, 3, 4, 5);
}

callManyArgsSloppy();
callManyArgsSloppy();
%OptimizeFunctionOnNextCall(callManyArgsSloppy);
callManyArgsSloppy();
assertOptimized(callManyArgsSloppy);
assertEquals(called, 3);

var str = "hello";
var code = str.charCodeAt(3);
called = 0;
function callBuiltinIndirectly() {
  called++;
  return "".charCodeAt.call(str, 3);
}

callBuiltinIndirectly();
callBuiltinIndirectly();
%OptimizeFunctionOnNextCall(callBuiltinIndirectly);
assertEquals(code, callBuiltinIndirectly());
assertOptimized(callBuiltinIndirectly);
assertEquals(3, called);

this.array = [1,2,3,4,5,6,7,8,9];
var copy = this.array.slice();
called = 0;

function callInlineableBuiltinIndirectlyWhileInlined() {
    called++;
    return [].push.apply(array, arguments);
}

function callInlined(num) {
    return callInlineableBuiltinIndirectlyWhileInlined(num);
}

callInlined(1);
callInlined(2);
%OptimizeFunctionOnNextCall(callInlineableBuiltinIndirectlyWhileInlined);
%OptimizeFunctionOnNextCall(callInlined);
callInlined(3);
copy.push(1, 2, 3);
assertOptimized(callInlined);
assertOptimized(callInlineableBuiltinIndirectlyWhileInlined);
assertArrayEquals(copy, this.array);
assertEquals(3, called);
