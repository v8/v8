// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/testgen.js");


(function TestClassDeclarations() {
  // Implements clause.
  CheckValid("class C implements I {}");
  CheckValid("class C implements I, J {}");
  CheckValid("class C implements I<number>, J {}");
  CheckValid("class C implements I, J<I, number> {}");
  // Extends and implements clause.
  CheckValid("class B {}; class C extends B implements I {}");
  CheckValid("class B {}; class C extends B implements I, J {}");
  CheckValid("class B {}; class C extends B implements I<number>, J {}");
  CheckValid("class B {}; class C extends B implements I, J<I, number> {}");
  // Type parametric
  CheckValid("class C<A, B> {}");
  CheckValid("class D {}; class C<A, B> extends D {}");
  CheckValid("class D {}; class C<A, B> extends D implements A, I {}");
  CheckValid("class C<A, B> implements I<A, B> {}");
  // Invalid implements clause.
  CheckInvalid("class C implements () {}");
  CheckInvalid("class C implements number[] {}");
  CheckInvalid("class C implements I<> {}");
  // Invalid extends clause.
  CheckInvalid("class B1 {}; class B2 {}; class C extends B1, B2 {}");
  // Invalid type parametric
  CheckInvalid("class C<> {}");
})();

(function TestClassMembers() {
  // Test all possible valid members.
  CheckValid("class C {" + "\n" +
     // constructor
  "  constructor (x: number, y: boolean) { this.x = x; this.y = y; }" + "\n" +
     // variable members
  "  x" + "\n" +
  "  y: boolean" + "\n" +
  "  z = 42" + "\n" +
  "  w: number = 17" + "\n" +
  "  'one': number" + "\n" +
  "  '2': boolean" + "\n" +
  "  3: string" + "\n" +
  "  'four': number = 4" + "\n" +
  "  '5': boolean = false" + "\n" +
  "  6: string[] = [...['six', 'six', 'and', 'six']]" + "\n" +
     // methods
  "  f1 () : number { return 42; }" + "\n" +
  "  f2 (a: number[]) : number { return a[0]; }" + "\n" +
  "  f3 (a: number[], b: number) : number { return b || a[0]; }" + "\n" +
     // getters
  "  get p () : number { return 42; }" + "\n" +
  "  get 'q' () : number { return 42; }" + "\n" +
  "  get 42 () : number { return 42; }" + "\n" +
     // setters
  "  set p (x: number) {}" + "\n" +
  "  set 'q' (x: number) {}" + "\n" +
  "  set 42 (x : number) {}" + "\n" +
     // index members
  "  [x: string]" + "\n" +
  "  [x: string] : number" + "\n" +
  "  [x: number]" + "\n" +
  "  [x: number] : boolean" + "\n" +
  "}");
  // Test all possible valid static members.
  CheckValid("class C {" + "\n" +
     // variable members
  "  static x" + "\n" +
  "  static y: boolean" + "\n" +
  "  static z = 42" + "\n" +
  "  static w: number = 17" + "\n" +
  "  static 'one': number" + "\n" +
  "  static '2': boolean" + "\n" +
  "  static 3: string" + "\n" +
  "  static 'four': number = 4" + "\n" +
  "  static '5': boolean = false" + "\n" +
  "  static 6: string[] = [...['six', 'six', 'and', 'six']]" + "\n" +
     // methods
  "  static f1 () : number { return 42; }" + "\n" +
  "  static f2 (a: number[]) : number { return a[0]; }" + "\n" +
  "  static f3 (a: number[], b: number) : number { return b || a[0]; }" + "\n" +
     // getters
  "  static get p () : number { return 42; }" + "\n" +
  "  static get 'q' () : number { return 42; }" + "\n" +
  "  static get 42 () : number { return 42; }" + "\n" +
     // setters
  "  static set p (x: number) {}" + "\n" +
  "  static set 'q' (x: number) {}" + "\n" +
  "  static set 42 (x : number) {}" + "\n" +
  "}");
  // Test invalid member variable declarations.
  CheckInvalid("class C { x: () }");
  CheckInvalid("class C { [42] }");
  CheckInvalid("class C { [42]: number }");
  // Test invalid index members.
  CheckInvalid("class C { [x: any] }");
  CheckInvalid("class C { [x: any] : any }");
  CheckInvalid("class C { static [x: number] }");
})();

(function TestClassMemberSignatures() {
  CheckValid("class C {" + "\n" +
     // constructor signatures
  "  constructor (x: number, y: boolean)" + "\n" +
  "  constructor (x: number, y: boolean) { this.x = x; this.y = y; }" + "\n" +
     // method signatures
  "  f1 () : number" + "\n" +
  "  f1 () : number { return 42; }" + "\n" +
  "  f2 (a: number[]) : number" + "\n" +
  "  f2 (a: number[]) : number { return a[0]; }" + "\n" +
  "  f3 (a: number[], b: number) : number" + "\n" +
  "  f3 (a: number[], b: number) : number { return b || a[0]; }" + "\n" +
  "}");
  // Test all possible valid static members.
  CheckValid("class C {" + "\n" +
     // static method signatures
  "  static f1 () : number" + "\n" +
  "  static f1 () : number { return 42; }" + "\n" +
  "  static f2 (a: number[]) : number" + "\n" +
  "  static f2 (a: number[]) : number { return a[0]; }" + "\n" +
  "  static f3 (a: number[], b: number)" + "\n" +
  "  static f3 (a: number[], b: number) : number { return b || a[0]; }" + "\n" +
  "}");
})();
