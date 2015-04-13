// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode
// Flags: --harmony-classes --harmony-arrow-functions

'use strong';

class C {}

let indirect_eval = eval;

function assertTypeError(script) { assertThrows(script, TypeError) }
function assertSyntaxError(script) { assertThrows(script, SyntaxError) }
function assertReferenceError(script) { assertThrows(script, ReferenceError) }

(function ImmutableClassBindings() {
  class D {}
  assertTypeError(function(){ indirect_eval("C = 0") });
  assertEquals('function', typeof C);
  assertEquals('function', typeof D);
  assertTypeError("'use strong'; (function f() {class E {}; E = 0})()");
})();

function constructor(body) {
  return "'use strong'; " +
      "(class extends Object { constructor() { " + body + " } })";
}

(function NoMissingSuper() {
  assertReferenceError(constructor(""));
  assertReferenceError(constructor("1"));
})();

(function NoNestedSuper() {
  assertSyntaxError(constructor("(super());"));
  assertSyntaxError(constructor("(() => super())();"));
  assertSyntaxError(constructor("{ super(); }"));
  assertSyntaxError(constructor("if (1) super();"));
})();

(function NoDuplicateSuper() {
  assertSyntaxError(constructor("super(), super();"));
  assertSyntaxError(constructor("super(); super();"));
  assertSyntaxError(constructor("super(); (super());"));
  assertSyntaxError(constructor("super(); { super() }"));
  assertSyntaxError(constructor("super(); (() => super())();"));
})();

(function NoReturnValue() {
  assertSyntaxError(constructor("return {};"));
  assertSyntaxError(constructor("return undefined;"));
  assertSyntaxError(constructor("{ return {}; }"));
  assertSyntaxError(constructor("if (1) return {};"));
})();

(function NoReturnBeforeSuper() {
  assertSyntaxError(constructor("return; super();"));
  assertSyntaxError(constructor("if (0) return; super();"));
  assertSyntaxError(constructor("{ return; } super();"));
})();
