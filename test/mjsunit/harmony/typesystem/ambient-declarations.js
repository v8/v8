// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-types


load("test/mjsunit/harmony/typesystem/testgen.js");


function CheckValidAmbient(decl, full, preamble) {
  preamble = typeof(preamble) === "undefined" ? '' : preamble + '; ';
  CheckValid(preamble + "declare " + decl);
  if (typeof(full) === "undefined") full = decl;
  if (typeof(full) === "null") return;
  CheckValid(preamble + "declare " + decl + "; " + full);
}

function CheckInvalidAmbient(decl, full=null, preamble) {
  preamble = typeof(preamble) === "undefined" ? '' : preamble + '; ';
  CheckInvalid(preamble + "declare " + decl);
  if (typeof(full) === "undefined") full = decl;
  if (typeof(full) === "null") return;
  CheckInvalid(preamble + "declare " + decl + "; " + full);
}

function CheckInvalidAmbientInInnerScope(decl, full=null, preamble) {
  preamble = typeof(preamble) === "undefined" ? '' : preamble + '; ';
  CheckInvalid(preamble + "{ declare " + decl + "}");
  CheckInvalid(preamble + "try { declare " + decl + "} catch (x) {}");
  CheckInvalid(preamble + "function f() { declare " + decl + "}");
  CheckInvalid(preamble + "for (declare " + decl + " of []) {} }");
}


function CheckAmbientVariableDeclarations(valid, invalid=valid) {
  for (var key of ["var", "let", "const"]) {
    if (key != "const") {
      valid(key + " x");
      valid(key + " x: number");
      valid(key + " x, y");
      valid(key + " x: number, y: string");
    } else {
      valid(key + " x", key + " x = 42");
      valid(key + " x: number", key + " x: number = 42");
      valid(key + " x, y", key + " x = 42, y = 'hello'");
      valid(key + " x: number, y: string",
            key + " x: number = 42, y: string = 'hello'");
    }
    valid(key + " [x, y]", key + " [x, y] = [42, 'hello']");
    valid(key + " [x, y] : [number, string]",
          key + " [x, y] : [number, string] = [42, 'hello']");
    valid(key + " {a: x, b: y}",
          key + " {a: x, b: y} = {a: 42, b: 'hello'}");
    valid(key + " {a: x, b: y} : {a: number, b: string}",
          key + " {a: x, b: y} : {a: number, b: string} = {a: 42, b: 'hello'}");
    // Simple invalid declarations.
    invalid(key + " x : ()");
    // Initializers are not allowed in ambient variable declarations.
    invalid(key + " x = 42");
    invalid(key + " x: number = 42");
    invalid(key + " x = 42, y = 'hello'");
    invalid(key + " x: number = 42, y: string = 'hello'");
  }
}

(function TestAmbientVariableDeclarations() {
  CheckAmbientVariableDeclarations(CheckValidAmbient, CheckInvalidAmbient);
  CheckAmbientVariableDeclarations(CheckInvalidAmbientInInnerScope);
})();


function CheckAmbientFunctionDeclarations(valid, invalid=valid) {
  function check(decl) {
    valid(decl, decl + "{}");
    invalid(decl + "{}");  // Disallow bodies in ambient function declarations.
  }
  check("function f()");
  check("function f() : number");
  check("function f(x: number)");
  check("function f(x: number) : number");
  check("function f<A>()");
  check("function f<A>() : A");
  check("function f<A>(x: A)");
  check("function f<A, B>(x: A) : B");
  // We allow generators as well.
  check("function* f()");
  check("function* f() : number");
  check("function* f(x: number)");
  check("function* f(x: number) : number");
  check("function* f<A>()");
  check("function* f<A>() : A");
  check("function* f<A>(x: A)");
  check("function* f<A, B>(x: A) : B");
  // Simple invalid declarations.
  invalid("function f() : ()");
  invalid("function f(x? = 42) : ()");
  invalid("function f<>()");
  // The function's name must be present.
  invalid("function ()");
  invalid("function () : number");
  invalid("function (x: number)");
  invalid("function (x: number) : number");
  invalid("function <A>()");
  invalid("function <A>() : A");
  invalid("function <A>(x: A)");
  invalid("function <A, B>(x: A) : B");
}

(function TestAmbientFunctionDeclarations() {
  CheckAmbientFunctionDeclarations(CheckValidAmbient, CheckInvalidAmbient);
  CheckAmbientFunctionDeclarations(CheckInvalidAmbientInInnerScope);
})();


function CheckAmbientClassDeclarations(valid, invalid=valid) {
  function check(check_header) {
    check_header("class C");
    check_header("class C extends B", "class B {}");
    check_header("class C implements I");
    check_header("class C implements I, J");
    check_header("class C extends B implements I", "class B {}");
    check_header("class C extends B implements I, J", "class B {}");
    check_header("class C<A, B>");
    check_header("class C<A, B> extends D", "class D {}");
    check_header("class C<A, B> implements I<A, B>");
    check_header("class C<A, B> extends D implements I<A, B>", "class D {}");
  }
  function good(members) {
    var ambient_body = "";
    var body = "";
    members.forEach(function([member, suffix='']) {
      if (suffix != "") suffix = " " + suffix;
      ambient_body += member + "; ";
      body += member + suffix + "; ";
    });
    check(function (header, preamble) {
      valid(header + " {" + ambient_body + "}",
            header + " {" + body + "}", preamble);
    });
  }
  function bad(member, suffix='') {
    check(function (header, preamble) {
      invalid(header + " {" + member + "}", null, preamble);
      if (suffix != "") {
        invalid(header + " {" + member + " " + suffix + "}", null, preamble);
      }
    });
  }
  // Test all possible valid members.
  good([
    // constructor
    ["constructor (x: number, y: boolean)", "{ this.x = x; this.y = y; }"],
    // variable members
    ["x"],
    ["y: boolean"],
    ["'one': number"],
    ["'2': boolean"],
    ["3: string"],
    // methods
    ["f1 () : number", "{ return 42; }"],
    ["f2 (a: number[]) : number", "{ return a[0]; }"],
    ["f3 (a: number[], b: number) : number", "{ return b || a[0]; }"],
    ["f4 <A, B>(a: A, b: B) : [A, B]", "{ return [a, b]; }"],
    // index members
    ["[x: string]"],
    ["[x: string] : number"],
    ["[x: number]"],
    ["[x: number] : boolean"]
  ]);
  // Test all possible valid static members.
  good([
    // variable members
    ["static x"],
    ["static y: boolean"],
    ["static 'one': number"],
    ["static '2': boolean"],
    ["static 3: string"],
    // methods
    ["static f1 () : number", "{ return 42; }"],
    ["static f2 (a: number[]) : number", "{ return a[0]; }"],
    ["static f3 (a: number[], b: number) : number", "{ return b || a[0]; }"],
    ["static f4 <A, B>(a: A, b: B) : [A, B]", "{ return [a, b]; }"]
  ]);
  // Invalid constructors.
  bad("constructor (a : number) : boolean", "{}"),
  bad("constructor <A>(a : A)", "{}");
  // Initializers in variable members are not allowed.
  bad("x = 42"),
  bad("x : number = 42"),
  bad("'four': number = 4"),
  bad("'5': boolean = false"),
  bad("6: string[] = [...['six', 'six', 'and', 'six']]"),
  // Getters and setters are not allowed, whether valid...
  bad("get x ()", "{ return 42; }");
  bad("get x () : number", "{ return 42; }");
  bad("set x (a)", "{}");
  bad("set x (a) : void", "{}");
  bad("set x (a : number)", "{}");
  bad("set x (a : number) : void", "{}");
  // ... or invalid.
  bad("get x (a)", "{ return 42; }");
  bad("get x (a) : number", "{ return 42; }");
  bad("get x (a, b)", "{ return 42; }");
  bad("get x (a, b) : number", "{ return 42; }");
  bad("get x (a : number)", "{ return 42; }");
  bad("get x (a : number) : number", "{ return 42; }");
  bad("get x <A>()", "{ return 42; }");
  bad("set x ()", "{}");
  bad("set x () : void", "{}");
  bad("set x (a : number, b : number)", "{}");
  bad("set x (a : number, b : number) : void", "{}");
  bad("set x (...rest)", "{}");
  bad("set x (...rest : string[]) : void", "{}");
  bad("set x <A>(a : A)", "{}");
}

(function TestAmbientClassDeclarations() {
  CheckAmbientClassDeclarations(CheckValidAmbient, CheckInvalidAmbient);
  CheckAmbientClassDeclarations(CheckInvalidAmbientInInnerScope);
})();

(function TestMoreInvalidAmbients() {
  CheckInvalidAmbient("type N = number");
  CheckInvalidAmbient("interface I {}");
  CheckInvalidAmbient("x = 42");
  CheckInvalidAmbient("{}");
  CheckInvalidAmbient("while (true) {}");
})();
