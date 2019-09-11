// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-private-methods

"use strict";

// Static private methods
{
  let store = 1;
  class C {
    static #a() { return store; }
  }
}

// Complementary static private accessors.
{
  let store = 1;
  class C {
    static get #a() { return store; }
    static set #a(val) { store = val; }
  }
}

// Duplicate static private accessors and methods.
{
  assertThrows('class C { static get #a() {} static get #a() {} }', SyntaxError);
  assertThrows('class C { static get #a() {} static #a() {} }', SyntaxError);
  assertThrows('class C { static get #a() {} get #a() {} }', SyntaxError);
  assertThrows('class C { static get #a() {} set #a(val) {} }', SyntaxError);
  assertThrows('class C { static get #a() {} #a() {} }', SyntaxError);

  assertThrows('class C { static set #a(val) {} static set #a(val) {} }', SyntaxError);
  assertThrows('class C { static set #a(val) {} static #a() {} }', SyntaxError);
  assertThrows('class C { static set #a(val) {} get #a() {} }', SyntaxError);
  assertThrows('class C { static set #a(val) {} set #a(val) {} }', SyntaxError);
  assertThrows('class C { static set #a(val) {} #a() {} }', SyntaxError);

  assertThrows('class C { static #a() {} static #a() {} }', SyntaxError);
  assertThrows('class C { static #a() {} #a(val) {} }', SyntaxError);
  assertThrows('class C { static #a() {} set #a(val) {} }', SyntaxError);
  assertThrows('class C { static #a() {} get #a() {} }', SyntaxError);
}
