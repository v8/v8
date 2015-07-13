// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --strong-mode

"use strict";

function getClass() {
  class Foo {
    static get bar() { return 0 }
  }
  return Foo;
}

function getClassExpr() {
  return (class { static get bar() { return 0 } });
}

function getClassStrong() {
  "use strong";
  class Foo {
    static get bar() { return 0 }
  }
  return Foo;
}

function getClassExprStrong() {
  "use strong";
  return (class { static get bar() { return 0 } });
}

function addProperty(o) {
  o.baz = 1;
}

function convertPropertyToData(o) {
  assertTrue(o.hasOwnProperty("bar"));
  Object.defineProperty(o, "bar", { value: 1 });
}

assertDoesNotThrow(function(){addProperty(getClass())});
assertDoesNotThrow(function(){convertPropertyToData(getClass())});
assertDoesNotThrow(function(){addProperty(getClassExpr())});
assertDoesNotThrow(function(){convertPropertyToData(getClassExpr())});

assertThrows(function(){addProperty(getClassStrong())}, TypeError);
assertThrows(function(){convertPropertyToData(getClassStrong())}, TypeError);
assertThrows(function(){addProperty(getClassExprStrong())}, TypeError);
assertThrows(function(){convertPropertyToData(getClassExprStrong())},
             TypeError);

// Check strong classes don't freeze their parents.
(function() {
  "use strong";
  let parent = getClass();

  class Foo extends parent {
    static get bar() { return 0 }
  }

  assertThrows(function(){addProperty(Foo)}, TypeError);
  assertThrows(function(){convertPropertyToData(Foo)}, TypeError);
  assertDoesNotThrow(function(){addProperty(parent)});
  assertDoesNotThrow(function(){convertPropertyToData(parent)});
})();

// Check strong classes don't freeze their children.
(function() {
  let parent = getClassStrong();

  class Foo extends parent {
    static get bar() { return 0 }
  }

  assertThrows(function(){addProperty(parent)}, TypeError);
  assertThrows(function(){convertPropertyToData(parent)}, TypeError);
  assertDoesNotThrow(function(){addProperty(Foo)});
  assertDoesNotThrow(function(){convertPropertyToData(Foo)});
})();
