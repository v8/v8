// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/typegen.js");


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = default_test_size;


(function TestObjectTypes(size) {
  Test(size, [
    new TestGen(4, ValidObjectTypes, [CheckValidType]),
    new TestGen(1, InvalidObjectTypes, [CheckInvalidType])
  ]);
})(test_size);
