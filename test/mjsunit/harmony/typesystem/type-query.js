// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/testgen.js");


function valid(type) {
  CheckValidType(type);
  CheckValidType(type + "[]");
  CheckValidType(type + " & any");
  CheckValidType(type + " | any");
  CheckValidType("(" + type + ")[]");
  CheckValidType("(a: " + type + ") => any");
}

function invalid(type) {
  CheckInvalidType(type);
  CheckInvalidType(type + "[]");
  CheckInvalidType(type + " & any");
  CheckInvalidType(type + " | any");
  CheckInvalidType("(" + type + ")[]");
  CheckInvalidType("(a: " + type + ") => any");
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
