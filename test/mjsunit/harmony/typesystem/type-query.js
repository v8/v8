// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types

function CheckValid(type) {
  // print("V:", type);
  assertDoesNotThrow("'use types'; var x: " + type + ";");
}

function CheckInvalid(type) {
  // print("I:", type);
  assertThrows("'use types'; var x: " + type + ";", SyntaxError);
}

function valid(type) {
  CheckValid(type);
  CheckValid(type + "[]");
  CheckValid(type + " & any");
  CheckValid(type + " | any");
  CheckValid("(" + type + ")[]");
  CheckValid("(a: " + type + ") => any");
}

function invalid(type) {
  CheckInvalid(type);
  CheckInvalid(type + "[]");
  CheckInvalid(type + " & any");
  CheckInvalid(type + " | any");
  CheckInvalid("(" + type + ")[]");
  CheckInvalid("(a: " + type + ") => any");
}

(function TestTypeQueries() {
  valid("typeof x");
  valid("typeof x.a");
  valid("typeof x.something.something_else");
  valid("typeof x.a.b.c.d.e.f.g.h.i.j.k.l");

  invalid("typeof");
  invalid("typeof x.");
  invalid("typeof x.a.b.c.d.e.f.g.h.i.j.k.l.");
  invalid("typeof x => any");
})();
