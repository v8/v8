// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/typegen.js");


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = default_test_size;


function ValidVariableDeclarations(size, keyword="var") {
  return Generate(size, [
    new TestGen(1, ValidTypes, [
      keyword != "const" && (t => keyword + " x : " + t + ";"),
      t => keyword + " x : " + t + " = undefined;"
    ]),
    new TestGen(1, ValidTypes, [
      t => keyword + " [x, y, ...rest] : (" + t + ")[] = [];"
    ]),
    new TestGen(1, ValidTupleTypes, [
      t => keyword + " [x,, z] : " + t + " = [undefined,, undefined];"
    ]),
    new TestGen(1, ValidObjectTypes, [
      t => keyword + " {a: x, b: y} : " + t + " = {a: 17, b: 42};"
    ])
  ]);
}

function InvalidVariableDeclarations(size, keyword="var") {
  return Generate(size, [
    new TestGen(1, InvalidTypes, [
      keyword != "const" && (t => keyword + " x : " + t + ";"),
      t => keyword + " x : " + t + " = undefined;"
    ]),
    new TestGen(1, InvalidTypes, [
      keyword != "const" && (t => keyword + " [x, y, ...rest] : (" + t + ")[];"),
      t => keyword + " [x, y, ...rest] : (" + t + ")[] = [];"
    ]),
    new TestGen(1, InvalidTupleTypes, [
      keyword != "const" && (t => keyword + " [x,, z] : " + t + ";"),
      t => keyword + " [x,, z] : " + t + " = [undefined,, undefined];"
    ]),
    new TestGen(1, InvalidObjectTypes, [
      keyword != "const" && (t => keyword + " {a: x, b: y} : " + t + ";"),
      t => keyword + " {a: x, b: y} : " + t + " = {a: 17, b: 42};"
    ]),
    keyword + " [x, y]: number[];",
    keyword + " {a: x, b: y}: {a: number, b: string};",
    keyword == "const" && keyword + " x: number;"
  ]);
}

(function TestVariableDeclarations(size) {
  Test(size, [
    new TestGen(1, ValidVariableDeclarations, [CheckValid], "var"),
    new TestGen(1, ValidVariableDeclarations, [CheckValid], "let"),
    new TestGen(1, ValidVariableDeclarations, [CheckValid], "const"),
    new TestGen(1, InvalidVariableDeclarations, [CheckInvalid], "var"),
    new TestGen(1, InvalidVariableDeclarations, [CheckInvalid], "let"),
    new TestGen(1, InvalidVariableDeclarations, [CheckInvalid], "const")
  ]);
})(test_size);
