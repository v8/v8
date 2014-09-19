// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-classes


(function TestSuperNamedLoads() {
  function Base() { }
  function Derived() {
    this.derivedDataProperty = "xxx";
  }
  Derived.prototype = Object.create(Base.prototype);

  function fBase() { return "Base " + this.toString(); }

  Base.prototype.f = fBase.toMethod(Base.prototype);

  function fDerived() {
     assertEquals("Base this is Derived", super.f());
     assertEquals(15, super.x);
     assertEquals(27, this.x);

     return "Derived"
  }

  Base.prototype.x = 15;
  Base.prototype.toString = function() { return "this is Base"; };
  Derived.prototype.toString = function() { return "this is Derived"; };
  Derived.prototype.x = 27;
  Derived.prototype.f = fDerived.toMethod(Derived.prototype);

  assertEquals("Base this is Base", new Base().f());
  assertEquals("Derived", new Derived().f());
}());

(function TestSuperKeywordNonMethod() {
  function f() {
    super.unknown();
  }

  assertThrows(f, ReferenceError);
}());


(function TestGetter() {
  function Base() {}
  var derived;
  Base.prototype = {
    constructor: Base,
    get x() {
      assertSame(this, derived);
      return this._x;
    },
    _x: 'base'
  };

  function Derived() {}
  Derived.__proto__ = Base;
  Derived.prototype = {
    __proto__: Base.prototype,
    constructor: Derived,
    _x: 'derived'
  };
  Derived.prototype.testGetter = function() {
    return super.x;
  }.toMethod(Derived.prototype);
  derived = new Derived();
  assertEquals('derived', derived.testGetter());
}());

/*
 * TODO[dslomov]: named stores and keyed loads/stores not implemented yet.
(function TestSetter() {
  function Base() {}
  Base.prototype = {
    constructor: Base,
    get x() {
      return this._x;
    },
    set x(v) {
      this._x = v;
    },
    _x: 'base'
  };

  function Derived() {}
  Derived.__proto__ = Base;
  Derived.prototype = {
    __proto__: Base.prototype,
    constructor: Derived,
    _x: 'derived'
  };
  Derived.prototype.testSetter = function() {
      super.x = 'foobar';
    }.toMethod(Derived.prototype);
  var d = new Derived();
  d.testSetter();
  assertEquals('base', Base.prototype._x);
  assertEquals('foobar', d._x);
}());


(function TestKeyedGetter() {
  function Base() {}
  Base.prototype = {
    constructor: Base,
    _x: 'base'
  };

  Object.defineProperty(Base.prototype, '0',
        { get: function() { return this._x; } });

  function Derived() {}
  Derived.__proto__ = Base;
  Derived.prototype = {
    __proto__: Base.prototype,
    constructor: Derived,
    _x: 'derived'
  };
  Derived.prototype.testGetter = function() {
      return super[0];
    }.toMethod(Derived.prototype);
  assertEquals('derived', new Derived()[0]);
  // assertEquals('derived', new Derived().testGetter());
}());
*/
