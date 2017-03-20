// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --type-profile --turbo --allow-natives-syntax

function test(param) {
  var my_var1 = param;
  var my_var2 = 17;
}

%PrintTypeProfile(test);

test({});
test(123);
test('hello');
test(123);
%PrintTypeProfile(test);

test(undefined);
test('hello');
test({x: 12});
test({x: 12});

%PrintTypeProfile(test);

class MyClass {
  constructor() {}
}


function testConstructorNames(param) {
  var my_var = param;
}

testConstructorNames(new MyClass());
testConstructorNames({});
testConstructorNames(2);

throw "throw otherwise test fails with --stress-opt";
