// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --harmony-destructuring --harmony-computed-property-names

(function TestObjectLiteralPattern() {
  var { x : x, y : y } = { x : 1, y : 2 };
  assertEquals(1, x);
  assertEquals(2, y);

  var {z} = { z : 3 };
  assertEquals(3, z);


  var sum = 0;
  for (var {z} = { z : 3 }; z != 0; z--) {
    sum += z;
  }
  assertEquals(6, sum);


  var log = [];
  var o = {
    get x() {
      log.push("x");
      return 0;
    },
    get y() {
      log.push("y");
      return {
        get z() { log.push("z"); return 1; }
      }
    }
  };
  var { x : x0, y : { z : z1 }, x : x1 } = o;
  assertSame(0, x0);
  assertSame(1, z1);
  assertSame(0, x1);
  assertArrayEquals(["x", "y", "z", "x"], log);
}());


(function TestObjectLiteralPatternInitializers() {
  var { x : x, y : y = 2 } = { x : 1 };
  assertEquals(1, x);
  assertEquals(2, y);

  var {z = 3} = {};
  assertEquals(3, z);

  var sum = 0;
  for (var {z = 3} = {}; z != 0; z--) {
    sum += z;
  }
  assertEquals(6, sum);

  var log = [];
  var o = {
    get x() {
      log.push("x");
      return undefined;
    },
    get y() {
      log.push("y");
      return {
        get z() { log.push("z"); return undefined; }
      }
    }
  };
  var { x : x0 = 0, y : { z : z1 = 1}, x : x1 = 0} = o;
  assertSame(0, x0);
  assertSame(1, z1);
  assertSame(0, x1);
  assertArrayEquals(["x", "y", "z", "x"], log);
}());


(function TestObjectLiteralPatternLexicalInitializers() {
  'use strict';
  let { x : x, y : y = 2 } = { x : 1 };
  assertEquals(1, x);
  assertEquals(2, y);

  let {z = 3} = {};
  assertEquals(3, z);

  let log = [];
  let o = {
    get x() {
      log.push("x");
      return undefined;
    },
    get y() {
      log.push("y");
      return {
        get z() { log.push("z"); return undefined; }
      }
    }
  };

  let { x : x0 = 0, y : { z : z1 = 1 }, x : x1 = 5} = o;
  assertSame(0, x0);
  assertSame(1, z1);
  assertSame(5, x1);
  assertArrayEquals(["x", "y", "z", "x"], log);

  let sum = 0;
  for (let {x = 0, z = 3} = {}; z != 0; z--) {
    assertEquals(0, x);
    sum += z;
  }
  assertEquals(6, sum);
}());


(function TestObjectLiteralPatternLexical() {
  'use strict';
  let { x : x, y : y } = { x : 1, y : 2 };
  assertEquals(1, x);
  assertEquals(2, y);

  let {z} = { z : 3 };
  assertEquals(3, z);

  let log = [];
  let o = {
    get x() {
      log.push("x");
      return 0;
    },
    get y() {
      log.push("y");
      return {
        get z() { log.push("z"); return 1; }
      }
    }
  };
  let { x : x0, y : { z : z1 }, x : x1 } = o;
  assertSame(0, x0);
  assertSame(1, z1);
  assertSame(0, x1);
  assertArrayEquals(["x", "y", "z", "x"], log);

  let sum = 0;
  for (let {x, z} = { x : 0, z : 3 }; z != 0; z--) {
    assertEquals(0, x);
    sum += z;
  }
  assertEquals(6, sum);
}());


(function TestObjectLiteralPatternLexicalConst() {
  'use strict';
  const { x : x, y : y } = { x : 1, y : 2 };
  assertEquals(1, x);
  assertEquals(2, y);

  assertThrows(function() { x++; }, TypeError);
  assertThrows(function() { y++; }, TypeError);

  const {z} = { z : 3 };
  assertEquals(3, z);

  for (const {x, z} = { x : 0, z : 3 }; z != 3 || x != 0;) {
    assertTrue(false);
  }
}());


(function TestFailingMatchesSloppy() {
  var {x, y} = {};
  assertSame(undefined, x);
  assertSame(undefined, y);

  var { x : { z1 }, y2} = { x : {}, y2 : 42 }
  assertSame(undefined, z1);
  assertSame(42, y2);
}());


(function TestFailingMatchesStrict() {
  'use strict';
  var {x, y} = {};
  assertSame(undefined, x);
  assertSame(undefined, y);

  var { x : { z1 }, y2} = { x : {}, y2 : 42 }
  assertSame(undefined, z1);
  assertSame(42, y2);

  {
    let {x1,y1} = {};
    assertSame(undefined, x1);
    assertSame(undefined, y1);

    let { x : { z1 }, y2} = { x : {}, y2 : 42 }
    assertSame(undefined, z1);
    assertSame(42, y2);
  }
}());


(function TestTDZInIntializers() {
  'use strict';
  {
    let {x, y = x} = {x : 42, y : 27};
    assertSame(42, x);
    assertSame(27, y);
  }

  {
    let {x, y = x + 1} = { x : 42 };
    assertSame(42, x);
    assertSame(43, y);
  }
  assertThrows(function() {
    let {x = y, y} = { y : 42 };
  }, ReferenceError);

  {
    let {x, y = eval("x+1")} = {x:42};
    assertEquals(42, x);
    assertEquals(43, y);
  }

  {
    let {x = function() {return y+1;}, y} = {y:42};
    assertEquals(43, x());
    assertEquals(42, y);
  }
  {
    let {x = function() {return eval("y+1");}, y} = {y:42};
    assertEquals(43, x());
    assertEquals(42, y);
  }
}());


(function TestSideEffectsInInitializers() {
  var callCount = 0;
  function f(v) { callCount++; return v; }

  callCount = 0;
  var { x = f(42) } = { x : 27 };
  assertSame(27, x);
  assertEquals(0, callCount);

  callCount = 0;
  var { x = f(42) } = {};
  assertSame(42, x);
  assertEquals(1, callCount);
}());


(function TestMultipleAccesses() {
  assertThrows(
    "'use strict';"+
    "const {x,x} = {x:1};",
    SyntaxError);

  assertThrows(
    "'use strict';"+
    "let {x,x} = {x:1};",
     SyntaxError);

  (function() {
    var {x,x = 2} = {x : 1};
    assertSame(1, x);
  }());

  assertThrows(function () {
    'use strict';
    let {x = (function() { x = 2; }())} = {};
  }, ReferenceError);

  (function() {
    'use strict';
    let {x = (function() { x = 2; }())} = {x:1};
    assertSame(1, x);
  }());
}());


(function TestComputedNames() {
  var x = 1;
  var {[x]:y} = {1:2};
  assertSame(2, y);

  (function(){
    'use strict';
    let {[x]:y} = {1:2};
    assertSame(2, y);
  }());

  var callCount = 0;
  function foo(v) { callCount++; return v; }

  (function() {
    callCount = 0;
    var {[foo("abc")]:x} = {abc:42};
    assertSame(42, x);
    assertEquals(1, callCount);
  }());

  (function() {
    'use strict';
    callCount = 0;
    let {[foo("abc")]:x} = {abc:42};
    assertSame(42, x);
    assertEquals(1, callCount);
  }());

  (function() {
    callCount = 0;
    var {[foo("abc")]:x} = {};
    assertSame(undefined, x);
    assertEquals(1, callCount);
  }());

  (function() {
    'use strict';
    callCount = 0;
    let {[foo("abc")]:x} = {};
    assertSame(undefined, x);
    assertEquals(1, callCount);
  }());

  for (val of [null, undefined]) {
    callCount = 0;
    assertThrows(function() {
      var {[foo()]:x} = val;
    }, TypeError);
    assertEquals(0, callCount);

    callCount = 0;
    assertThrows(function() {
      'use strict';
      let {[foo()]:x} = val;
    }, TypeError);
    assertEquals(0, callCount);
  }

  var log = [];
  var o = {
    get x() { log.push("get x"); return 1; },
    get y() { log.push("get y"); return 2; }
  }
  function f(v) { log.push("f " + v); return v; }

  (function() {
    log = [];
    var { [f('x')]:x, [f('y')]:y } = o;
    assertSame(1, x);
    assertSame(2, y);
    assertArrayEquals(["f x", "get x", "f y", "get y"], log);
  }());

  (function() {
    'use strict';
    log = [];
    let { [f('x')]:x, [f('y')]:y } = o;
    assertSame(1, x);
    assertSame(2, y);
    assertArrayEquals(["f x", "get x", "f y", "get y"], log);
  }());

  (function() {
    'use strict';
    log = [];
    const { [f('x')]:x, [f('y')]:y } = o;
    assertSame(1, x);
    assertSame(2, y);
    assertArrayEquals(["f x", "get x", "f y", "get y"], log);
  }());
}());


(function TestExceptions() {
  for (var val of [null, undefined]) {
    assertThrows(function() { var {} = val; }, TypeError);
    assertThrows(function() { var {x} = val; }, TypeError);
    assertThrows(function() { var { x : {} } = { x : val }; }, TypeError);
    assertThrows(function() { 'use strict'; let {} = val; }, TypeError);
    assertThrows(function() { 'use strict'; let {x} = val; }, TypeError);
    assertThrows(function() { 'use strict'; let { x : {} } = { x : val }; },
                 TypeError);
  }
}());
