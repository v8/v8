// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-class-fields
"use strict";

{
  class C {
    a;
  }

  assertEquals(undefined, C.a);

  let c = new C;
  let descriptor = Object.getOwnPropertyDescriptor(c, 'a');
  assertTrue(c.hasOwnProperty('a'));
  assertTrue(descriptor.writable);
  assertTrue(descriptor.enumerable);
  assertTrue(descriptor.configurable);
  assertEquals(undefined, c.a);
}

{
  let x = 'a';
  class C {
    a;
    b = x;
    c = 1;
    hasOwnProperty() { return 1;}
    static [x] = 2;
    static b = 3;
    static d;
  }

  assertEquals(2, C.a);
  assertEquals(3, C.b);
  assertEquals(undefined, C.d);
  assertEquals(undefined, C.c);

  let c = new C;
  assertEquals(undefined, c.a);
  assertEquals('a', c.b);
  assertEquals(1, c.c);
  assertEquals(undefined, c.d);
  assertEquals(1, c.hasOwnProperty());
}

{
  class C {
    x = Object.freeze(this);
    c = 42;
  }
  assertThrows(() => { new C; }, TypeError);
}

{
  class C {
    c = this;
    d = () => this;
  }

  let c = new C;
  assertEquals(c, c.c);
  assertEquals(c, c.d());

  assertEquals(undefined, C.c);
  assertEquals(undefined, C.d);
}

{
  class C {
    c = 1;
    d = this.c;
  }

  let c = new C;
  assertEquals(1, c.c);
  assertEquals(1, c.d);

  assertEquals(undefined, C.c);
  assertEquals(undefined, C.d);
}

{
  class C {
    b = 1;
    c = () => this.b;
  }

  let c = new C;
  assertEquals(1, c.b);
  assertEquals(1, c.c());

  assertEquals(undefined, C.c);
  assertEquals(undefined, C.b);
}

{
  let x = 'a';
  class C {
    b = 1;
    c = () => this.b;
    e = () => x;
  }

  let c = new C;
  assertEquals(1, c.b);
  assertEquals('a', c.e());

  let a = {b : 2 };
  assertEquals(1, c.c.call(a));

  assertEquals(undefined, C.b);
  assertEquals(undefined, C.c);
}

{
  let x = 'a';
  class C {
    c = 1;
    d = function() { return this.c; };
    e = function() { return x; };
  }

  let c = new C;
  assertEquals(1, c.c);
  assertEquals(1, c.d());
  assertEquals('a', c.e());

  c.c = 2;
  assertEquals(2, c.d());

  let a = {c : 3 };
  assertEquals(3, c.d.call(a));

  assertThrows(c.d.bind(undefined));

  assertEquals(undefined, C.c);
  assertEquals(undefined, C.d);
  assertEquals(undefined, C.e);
}

{
  class C {
    c = function() { return 1 };
  }

  let c = new C;
  assertEquals('c', c.c.name);
}

{
  let d = function() { return new.target; }
  class C {
    c = d;
  }

  let c = new C;
  assertEquals(undefined, c.c());
  assertEquals(new d, new c.c());
}

{
  class C {
    c = () => new.target;
  }

  let c = new C;
  assertEquals(undefined, c.c());
}

{
  let run = false;
  class C {
    c = () => {
      let b;
      class A {
        constructor() {
          b = new.target;
        }
      };
      new A;
      run = true;
      assertEquals(A, b);
    }
  }

  let c = new C;
  c.c();
  assertTrue(run);
}

{
  class C {
    c = new.target;
  }

  let c = new C;
  assertEquals(undefined, c.c);
}

{
  class B {
    c = 1;
  }

  class C extends B {}

  let c = new C;
  assertEquals(1, c.c);
}

{
  assertThrows(() => {
    class C {
      c = new C;
    }
    let c = new C;
  });
}

(function test() {
  function makeC() {
    var x = 1;

    return class {
      a = () => () => x;
    }
  }

  let C = makeC();
  let c = new C;
  let f = c.a();
  assertEquals(1, f());
})()

{
  let c1 = "c";
  class C {
    ["a"] = 1;
    ["b"];
    [c1];
  }

  let c = new C;
  assertEquals(1, c.a);
  assertEquals(undefined, c.b);
  assertEquals(undefined, c.c1);
}

{
  let log = [];
  function run(i) {
    log.push(i);
    return i;
  }

  class C {
    [run(1)] = run(7);
    [run(2)] = run(8);
    [run(3)]() { run(9);}
    static [run(4)] = run(6);
    [run(5)]() { throw new Error('should not execute');};
  }

  let c = new C;
  c[3]();
  assertEquals([1, 2, 3, 4, 5, 6, 7, 8, 9], log);
}

function x() {
  // This tests lazy parsing.
  return function() {
    let log = [];
    function run(i) {
      log.push(i);
      return i;
    }

    class C {
      [run(1)] = run(7);
      [run(2)] = run(8);
      [run(3)]() { run(9);}
      static [run(4)] = run(6);
      [run(5)]() { throw new Error('should not execute');};
    }

    let c = new C;
    c[3]();
    assertEquals([1, 2, 3, 4, 5, 6, 7, 8, 9], log);
  }
}
x();

{
  class C {}
  class D {
    [C];
  }

  let d = new D;
  assertThrows(() => { class X { [X] } let x = new X;});
  assertEquals(undefined, d[C]);
}
