// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/typegen.js");


// In all the following functions, the size parameter (positive integer)
// denotes how many test cases will be tried.  The variable test_size
// controls execution time of this test.  It should not be too high.
let test_size = default_test_size;


function CheckValidArrow(arrow, args = "") {
  CheckValid("(" + arrow + ")(" + args + ")");
  CheckValid("var f = " + arrow + "; f(" + args + ")");
}

function CheckInvalidArrow(arrow, args = "") {
  CheckInvalid("(" + arrow + ")(" + args + ")");
  CheckInvalid("var f = " + arrow + "; f(" + args + ")");
}


function ValidFormalParameters(size) {
  return Generate(size, [
    "()",
    "(x)",
    "(x, y)",
    "(x?)",
    "(x?, y)",
    "(x, y?)",
    new TestGen(1, ValidTypes, [
      t => "(x : " + t + ")",
      t => "(x : " + t + " = undefined)",
      t => "(x? : " + t + ")",
      t => "(x : " + t + ", y : " + t + ")",
      t => "(x : " + t + " = undefined, y : " + t + ")",
      t => "(x? : " + t + ", y)",
      t => "(x? : " + t + ", y : " + t + ")"
    ]),
    "(x, ...rest)",
    "(x, y, ...rest)",
    "(x?, ...rest)",
    "(x?, y, ...rest)",
    "(x, y?, ...rest)",
    new TestGen(1, ValidTypes, [
      t => "(x : " + t + ", ...rest : (" + t + ")[])",
      t => "(x? : " + t + ", y : " + t + ", ...rest : (" + t + ")[])"
    ]),
  ]);
}

function InvalidFormalParameters(size) {
  return Generate(size, [
    "x : number",
    new TestGen(1, InvalidTypes, [
      t => "(x : " + t + ")",
      t => "(x : " + t + "= undefined)",
      t => "(x? : " + t + ")",
      t => "(x : " + t + ", y : " + t + ")",
      t => "(x = undefined, y : " + t + ")",
      t => "(x? : " + t + ", y)",
      t => "(x?, y : " + t + ")"
    ]),
    "(x? = 42)",
    "(x? : number = 42)",
    "(...rest?)",
    "(...rest? : number)",
    "(first, ...rest?)",
    "(first, ...rest? : number)"
  ]);
}

function ValidParameters(size) {
  return Generate(size, [
    new TestGen(1, ValidFormalParameters, [
      p => p,
      p => "<A>" + p,
      p => "<A, B>" + p,
      p => "<A extends {x:number}>" + p,
      p => "<A extends {x:number}, B>" + p,
      p => "<A, B extends {x:number}>" + p
    ])
  ]);
}

function InvalidParameters(size) {
  return Generate(size, [
    new TestGen(1, InvalidFormalParameters, [
      p => p,
      p => "<A>" + p,
      p => "<A, B>" + p,
      p => "<A extends {x:number}>" + p,
      p => "<A extends {x:number}, B>" + p,
      p => "<A, B extends {x:number}>" + p
    ]),
    "<>(x : number)",
    "<A,>(s : string)",
    "<A extends ()>()",
    "<number[]>()",
    "<number[]>(x)",
    "<number[]>(x, y)",
    "<number[]>(x: number, y)",
  ]);
}

function ValidArrowFunctions(size) {
  return Generate(size, [
    "x => {}",
    new TestGen(1, ValidParameters, [
      p => p + " => {}",
      p => p + " : number => 42",
      p => p + " : number => {}",
      p => p + " : string[] => 42",
      p => p + " : string[] => {}",
      p => p + " : [any, number] => 42",
      p => p + " : [any, number] => {}",
      p => p + " : {a:number, b:string|boolean} => 42",
      p => p + " : {a:number, b:string|boolean} => {}"
    ]),
  ]);
}

function InvalidArrowFunctions(size) {
  return Generate(size, [
    new TestGen(1, InvalidParameters, [
      p => p + " => {}",
      p => p + " : number => 42",
      p => p + " : number => {}",
      p => p + " : string[] => 42",
      p => p + " : string[] => {}",
      p => p + " : [any, number] => 42",
      p => p + " : [any, number] => {}",
      p => p + " : {a:number, b:string|boolean} => 42",
      p => p + " : {a:number, b:string|boolean} => {}"
    ]),
    new TestGen(1, InvalidTypes, [
      t => "() : " + t + " {}",
    ])
  ]);
}

(function TestArrowFunctions(size) {
  Test(size, [
    new TestGen(1, ValidArrowFunctions, [CheckValidArrow]),
    new TestGen(1, s => Generate(s, [
      "([x, y] : [any, any]) => {}",
      "([x, y] : [any, any]) : number => {}",
    ]), [arrow => CheckValidArrow(arrow, "[1, 2]")]),
    new TestGen(1, s => Generate(s, [
      "([first, ...rest] : string[]) => {}",
      "([first, ...rest] : string[]) : number => {}",
    ]), [arrow => CheckValidArrow(arrow, "['hello', 'world']")]),
    new TestGen(1, s => Generate(s, [
      "([one,,three] : number[], ...rest : string[]) => {}",
      "([one,,three] : number[], ...rest : string[]) : number => {}",
    ]), [arrow => CheckValidArrow(arrow, "[1,2,3]")]),
    new TestGen(1, s => Generate(s, [
      "({a:x, b:y}? : {a:number, b:string}) => {}",
      "({a:x, b:y}? : {a:number, b:string}) : number => {}"
    ]), [arrow => CheckValidArrow(arrow, "{}")]),
    new TestGen(1, InvalidArrowFunctions, [CheckInvalidArrow]),
  ]);
})(test_size);
