// Copyright 2016 the V8 project authors. All rights reserved.
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

%PrintTypeProfile(testFunction);

class MyClass {
  constructor() {}
}


function testConstructorNames(param) {
  var my_var = param;
}

testConstructorNames(new MyClass());
testConstructorNames({});
testConstructorNames(2);

function testReturnOfNonVariable() {
  return 32;
}

testReturnOfNonVariable();

throw "throw otherwise test fails with --stress-opt";
