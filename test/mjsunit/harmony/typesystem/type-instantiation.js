// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/testgen.js");


(function TestTypeInstantiationForFunctions() {
  CheckValid("function f<A>(x: A) {}; f.<number>(42)");
  CheckValid("function f<A, B>(x: A, y: B) {}; f.<number, string>(42, 'hello')");
  CheckValid("var f = function <A>(x: A) {}; f.<number>(42)");
  CheckValid("var f = function <A, B>(x: A, y: B) {}; f.<number, string>(42, 'hello')");
  CheckValid("(function <A>(x: A) {}).<number>(42)");
  CheckValid("(function <A, B>(x: A, y: B) {}).<number, string>(42, 'hello')");
  CheckValid("(f => f)(function <A>(x: A) {}).<number>(42)");
  CheckValid("(f => f)(function <A, B>(x: A, y: B) {}).<number, string>(42, 'hello')");
  CheckInvalid("function f<A>(x: A) {}; f.(42)");
  CheckInvalid("function f<A>(x: A) {}; f.<>(42)");
  CheckInvalid("(function <A>(x: A) {}).(42)");
  CheckInvalid("(function <A>(x: A) {}).<>(42)");
})();

(function TestTypeInstantiationForGenerators() {
  CheckValid("function* f<A>(x: A) {}; f.<number>(42)");
  CheckValid("function* f<A, B>(x: A, y: B) {}; f.<number, string>(42, 'hello')");
  CheckValid("var f = function* <A>(x: A) {}; f.<number>(42)");
  CheckValid("var f = function* <A, B>(x: A, y: B) {}; f.<number, string>(42, 'hello')");
  CheckValid("(function* <A>(x: A) {}).<number>(42)");
  CheckValid("(function* <A, B>(x: A, y: B) {}).<number, string>(42, 'hello')");
  CheckValid("(f => f)(function* <A>(x: A) {}).<number>(42)");
  CheckValid("(f => f)(function* <A, B>(x: A, y: B) {}).<number, string>(42, 'hello')");
  CheckInvalid("function* f<A>(x: A) {}; f.(42)");
  CheckInvalid("function* f<A>(x: A) {}; f.<>(42)");
  CheckInvalid("(function* <A>(x: A) {}).(42)");
  CheckInvalid("(function* <A>(x: A) {}).<>(42)");
})();

(function TestTypeInstantiationForNewCalls() {
  CheckValid("function f<A>(x: A) {}; new f.<number>(42)");
  CheckValid("function f<A, B>(x: A, y: B) {}; new f.<number, string>(42, 'hello')");
  CheckValid("var f = function <A>(x: A) {}; new f.<number>(42)");
  CheckValid("var f = function <A, B>(x: A, y: B) {}; new f.<number, string>(42, 'hello')");
  CheckValid("new (function <A>(x: A) {}).<number>(42)");
  CheckValid("new (function <A, B>(x: A, y: B) {}).<number, string>(42, 'hello')");
  CheckValid("new ((f => f)(function <A>(x: A) {})).<number>(42)");
  CheckValid("new ((f => f)(function <A, B>(x: A, y: B) {})).<number, string>(42, 'hello')");
  CheckInvalid("function f<A>(x: A) {}; new f.(42)");
  CheckInvalid("function f<A>(x: A) {}; new f.<>(42)");
  CheckInvalid("new (function <A>(x: A) {}).(42)");
  CheckInvalid("new (function <A>(x: A) {}).<>(42)");
})();

(function TestTypeInstantiationForMethods() {
  CheckValid("var x = { f<A>(x: A) {} }; x.f.<number>(42)");
  CheckValid("var x = { f<A, B>(x: A, y: B) {} }; x.f.<number, string>(42, 'hello')");
  CheckValid("({ f<A>(x: A) {} }).f.<number>(42)");
  CheckValid("({ f<A, B>(x: A, y: B) {} }).f.<number, string>(42, 'hello')");
  CheckValid("(o => o)({ f<A>(x: A) {} }).f.<number>(42)");
  CheckValid("(o => o)({ f<A, B>(x: A, y: B) {} }).f.<number, string>(42, 'hello')");
  CheckInvalid("var x = { f<A>(x: A) {} }; x.f.(42)");
  CheckInvalid("var x = { f<A>(x: A) {} }; x.f.<>(42)");
  CheckInvalid("({ f<A>(x: A) {} }).f.(42)");
  CheckInvalid("({ f<A>(x: A) {} }).f.<>(42)");
  CheckInvalid("(o => o)({ f<A>(x: A) {} }).f.(42)");
  CheckInvalid("(o => o)({ f<A>(x: A) {} }).f.<>(42)");
})();
