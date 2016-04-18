// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/typegen.js");


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = default_test_size;


function ValidInterfaceDeclarations(size) {
  return Generate(size, [
    new TestGen(4, ValidObjectTypes, [
      t => "interface I " + t
    ]),
    new TestGen(1, ValidObjectTypes, [
      t => "interface J extends I " + t,
      t => "interface K extends I, J " + t,
      t => "interface I<A> " + t,
      t => "interface I<A, B> extends A, J<B> " + t
    ])
  ]);
}

function InvalidInterfaceDeclarations(size) {
  return Generate(size, [
    new TestGen(4, InvalidObjectTypes, [
      t => "interface I " + t
    ]),
    new TestGen(1, InvalidObjectTypes, [
      t => "interface J extends I " + t,
      t => "interface K extends I, J " + t,
      t => "interface I<A> " + t,
      t => "interface I<A, B> extends A, J<B> " + t
    ]),
    "interface I<> {}"
  ]);
}

(function TestInterfaceDeclarations(size) {
  Test(size, [
    new TestGen(4, ValidInterfaceDeclarations, [CheckValid]),
    new TestGen(1, InvalidInterfaceDeclarations, [CheckInvalid]),
  ]);
})(test_size);
