// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/typegen.js");


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = 1000;


(function TestObjectTypes(size) {
  Test(size, [
    new TestGen(4, ValidObjectTypes, [CheckValid]),
    new TestGen(1, InvalidObjectTypes, [CheckInvalid])
  ]);
})(test_size);
