// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/testgen.js");


(function TestMethodDefinitions() {
  // Normal methods.
  CheckValid("class C { m(a: number, b: number) { return a+b; } }");
  CheckValid("class C { m(a: number, b?: number) { return a; } }");
  CheckValid("class C { m(a?: number, b?: number) { return 42; } }");
  CheckValid("class C { m(a: number, ...rest: number[]) {} }");
  CheckValid("class C { m(a?: number, ...rest: number[]) {} }");
  CheckValid("class C { m() : boolean { return true; } }");
  CheckValid("class C { m(a: number, b: number) : boolean { return true; } }");
  CheckValid("class C { m(a: number, b?: number) : boolean { return true; } }");
  CheckValid("class C { m(a?: number, b?: number) : boolean { return true; } }");
  CheckValid("class C { m(a: number, ...rest: number[]) : void {} }");
  CheckValid("class C { m(a: number, ...rest: number[]) : void {} }");
  CheckValid("class C { m([first, ...rest]: number[]) : void {} }");
  CheckValid("class C { m(a: number = 7, b: number) { return a+b; } }");
  CheckValid("class C { m(a: number = 1, b?: number) { return a; } }");
  CheckValid("class C { m(a: number = 2, ...rest: number[]) {} }");
  CheckValid("class C { m(a: number = 1, b: number) : boolean { return true; } }");
  CheckValid("class C { m(a: number = 1, b?: number) : boolean { return true; } }");
  CheckValid("class C { m(a: number = 1, ...rest: number[]) : void {} }");
  CheckValid("class C { m([first, ...rest]: number[] = [1]) : void {} }");
  // Type parametric.
  CheckValid("class C { m <A>(a: A, b: A) { return a+b; } }");
  CheckValid("class C { m <A extends number>(a: A, b: A) { return a+b; } }");
  CheckValid("class C { m <A, B>(a: A, b: B) { return a+b; } }");
  CheckValid("class C { m <A extends number, B extends number>(a: A, b: B) { return a+b; } }");
  // Constructors.
  CheckValid("class C { constructor () {} }");
  CheckValid("class C { constructor (a : number) {} }");
  CheckValid("class C { constructor (a? : number) {} }");
  CheckValid("class C { constructor (a : number, b : string) {} }");
  CheckValid("class C { constructor (a, b? : string) {} }");
  CheckValid("class C { constructor (a, b? : string, ...rest: any[]) {} }");
  // Getters.
  CheckValid("class C { get x () { return 42; } }");
  CheckValid("class C { get x () : number { return 42; } }");
  // Setters.
  CheckValid("class C { set x (a) {} }");
  CheckValid("class C { set x (a) : void {} }");
  CheckValid("class C { set x (a : number) {} }");
  CheckValid("class C { set x (a : number) : void {} }");
  // Invalid constructors.
  CheckInvalid("class C { constructor (a : number) : boolean {} }");
  CheckInvalid("class C { constructor <A>(a : A) {} }");
  // Invalid getters.
  CheckInvalid("class C { get x (a) { return 42; } }");
  CheckInvalid("class C { get x (a) : number { return 42; } }");
  CheckInvalid("class C { get x (a, b) { return 42; } }");
  CheckInvalid("class C { get x (a, b) : number { return 42; } }");
  CheckInvalid("class C { get x (a : number) { return 42; } }");
  CheckInvalid("class C { get x (a : number) : number { return 42; } }");
  CheckInvalid("class C { get x <A>() { return 42; } }");
  // Invalid setters.
  CheckInvalid("class C { set x () {} }");
  CheckInvalid("class C { set x () : void {} }");
  CheckInvalid("class C { set x (a : number, b : number) {} }");
  CheckInvalid("class C { set x (a : number, b : number) : void {} }");
  CheckInvalid("class C { set x (...rest) {} }");
  CheckInvalid("class C { set x (...rest : string[]) : void {} }");
  CheckInvalid("class C { set x <A>(a : A) {} }");
})();
