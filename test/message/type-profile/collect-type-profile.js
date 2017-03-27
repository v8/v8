// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --type-profile --turbo --allow-natives-syntax

function testFunction(param, flag) {
  // We want to test 2 different return positions in one function.
  if (flag) {
    var first_var = param;
    return first_var;
  }
  var second_var = param;
  return second_var;
}

class MyClass {
  constructor() {}
}

%PrintTypeProfile(testFunction);

testFunction({});
testFunction(123, true);
testFunction('hello');
testFunction(123);
%PrintTypeProfile(testFunction);

testFunction(undefined);
testFunction('hello', true);
testFunction({x: 12}, true);
testFunction({x: 12});
testFunction(new MyClass());

%PrintTypeProfile(testFunction);

function testReturnOfNonVariable() {
  return 32;
}

// Return statement is reached but its expression is never really returned.
function try_finally() {
  try {
    return 23;
  } finally {
    return "nope, string is better"
  }
}
try_finally();
%PrintTypeProfile(try_finally);

function fall_off() {
  //nothing
}
fall_off();
%PrintTypeProfile(fall_off);

testReturnOfNonVariable();

function never_call() {}
%PrintTypeProfile(never_call);

throw "throw otherwise test fails with --stress-opt";
