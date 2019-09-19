// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

new class extends Object {
  constructor() {
    super(); this.foo = 1;
  }
}

{
  class Throws extends Object {
    t = 1;
    constructor(t = this.t) {
      super();
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      this.x = true;
      super();
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      super(this);
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      super(eval("this"));
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      super(eval("(() => this)()"));
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor(t = eval("this")) {
      super();
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      super();
      super();
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      if (false) { super(); }
      this.x = true;
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      super((4 * this.t));
    }
  }
  assertThrows(() => new Throws, ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      super((() => this)());
    }
  }
  assertThrows(() => new Throws(), ReferenceError);
}

{
  class Throws extends Object {
    constructor() {
      super((()=>{ var x = ()=> this; return x(); })())
    }
  }
  assertThrows(() => new Throws(), ReferenceError);
}

{
  class C extends null {
    constructor() {
      super();
    }
  }
  assertThrows(() => new C(), TypeError);
}

{
  class C extends Object {
    constructor() {
      super();
      (() => {
        this;
      })();
    }
  }
  new C();
}

{
  var count = 0;

  class A {
    constructor() {
      count++;
    }
    increment() {
      count++;
    }
  }

  class B extends A {
    constructor() {
      super();
      (_ => super.increment())();
    }
  }
  new B();
  assertEquals(count, 2);
}
