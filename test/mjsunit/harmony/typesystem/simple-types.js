// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/typegen.js");


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = 1000;


(function TestPrimaryTypes(size) {
  Test(size, [
    new TestGen(4, ValidPrimaryTypes, [CheckValid], true),
    new TestGen(1, InvalidPrimaryTypes, [CheckInvalid], true)
  ]);
})(test_size);


(function TestIntersectionTypes(size) {
  Test(size, [
    new TestGen(4, ValidIntersectionTypes, [CheckValid], true),
    new TestGen(1, InvalidIntersectionTypes, [CheckInvalid], true)
  ]);
})(test_size);


(function TestUnionTypes(size) {
  Test(size, [
    new TestGen(4, ValidUnionTypes, [CheckValid], true),
    new TestGen(1, InvalidUnionTypes, [CheckInvalid], true)
  ]);
})(test_size);


(function TestFunctionAndConstructorTypes(size) {
  Test(size, [
    new TestGen(4, ValidFunctionTypes, [CheckValid], false),
    new TestGen(1, InvalidFunctionTypes, [CheckInvalid], false),
    new TestGen(4, ValidFunctionTypes, [CheckValid], true),
    new TestGen(1, InvalidFunctionTypes, [CheckInvalid], true)
  ]);
})(test_size);
