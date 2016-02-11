// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-tailcalls
"use strict";

Error.prepareStackTrace = (e,s) => s;

function CheckStackTrace(expected) {
  var stack = (new Error()).stack;
  assertEquals("CheckStackTrace", stack[0].getFunctionName());
  for (var i = 0; i < expected.length; i++) {
    assertEquals(expected[i].name, stack[i + 1].getFunctionName());
  }
}

function f(expected_call_stack, a, b) {
  CheckStackTrace(expected_call_stack);
  return a;
}

function f_153(expected_call_stack, a) {
  CheckStackTrace(expected_call_stack);
  return 153;
}


// Tail call when caller does not have an arguments adaptor frame.
(function test() {
  // Caller and callee have same number of arguments.
  function f1(a) {
    CheckStackTrace([f1, test]);
    return 10 + a;
  }
  function g1(a) { return f1(2); }
  assertEquals(12, g1(1));

  // Caller has more arguments than callee.
  function f2(a) {
    CheckStackTrace([f2, test]);
    return 10 + a;
  }
  function g2(a, b, c) { return f2(2); }
  assertEquals(12, g2(1, 2, 3));

  // Caller has less arguments than callee.
  function f3(a, b, c) {
    CheckStackTrace([f3, test]);
    return 10 + a + b + c;
  }
  function g3(a) { return f3(2, 3, 4); }
  assertEquals(19, g3(1));

  // Callee has arguments adaptor frame.
  function f4(a, b, c) {
    CheckStackTrace([f4, test]);
    return 10 + a;
  }
  function g4(a) { return f4(2); }
  assertEquals(12, g4(1));
})();


// Tail call when caller has an arguments adaptor frame.
(function test() {
  // Caller and callee have same number of arguments.
  function f1(a) {
    CheckStackTrace([f1, test]);
    return 10 + a;
  }
  function g1(a) { return f1(2); }
  assertEquals(12, g1());

  // Caller has more arguments than callee.
  function f2(a) {
    CheckStackTrace([f2, test]);
    return 10 + a;
  }
  function g2(a, b, c) { return f2(2); }
  assertEquals(12, g2());

  // Caller has less arguments than callee.
  function f3(a, b, c) {
    CheckStackTrace([f3, test]);
    return 10 + a + b + c;
  }
  function g3(a) { return f3(2, 3, 4); }
  assertEquals(19, g3());

  // Callee has arguments adaptor frame.
  function f4(a, b, c) {
    CheckStackTrace([f4, test]);
    return 10 + a;
  }
  function g4(a) { return f4(2); }
  assertEquals(12, g4());
})();


// Tail call bound function when caller does not have an arguments
// adaptor frame.
(function test() {
  // Caller and callee have same number of arguments.
  function f1(a) {
    assertEquals(153, this.a);
    CheckStackTrace([f1, test]);
    return 10 + a;
  }
  var b1 = f1.bind({a: 153});
  function g1(a) { return b1(2); }
  assertEquals(12, g1(1));

  // Caller has more arguments than callee.
  function f2(a) {
    assertEquals(153, this.a);
    CheckStackTrace([f2, test]);
    return 10 + a;
  }
  var b2 = f2.bind({a: 153});
  function g2(a, b, c) { return b2(2); }
  assertEquals(12, g2(1, 2, 3));

  // Caller has less arguments than callee.
  function f3(a, b, c) {
    assertEquals(153, this.a);
    CheckStackTrace([f3, test]);
    return 10 + a + b + c;
  }
  var b3 = f3.bind({a: 153});
  function g3(a) { return b3(2, 3, 4); }
  assertEquals(19, g3(1));

  // Callee has arguments adaptor frame.
  function f4(a, b, c) {
    assertEquals(153, this.a);
    CheckStackTrace([f4, test]);
    return 10 + a;
  }
  var b4 = f4.bind({a: 153});
  function g4(a) { return b4(2); }
  assertEquals(12, g4(1));
})();


// Tail call bound function when caller has an arguments adaptor frame.
(function test() {
  // Caller and callee have same number of arguments.
  function f1(a) {
    assertEquals(153, this.a);
    CheckStackTrace([f1, test]);
    return 10 + a;
  }
  var b1 = f1.bind({a: 153});
  function g1(a) { return b1(2); }
  assertEquals(12, g1());

  // Caller has more arguments than callee.
  function f2(a) {
    assertEquals(153, this.a);
    CheckStackTrace([f2, test]);
    return 10 + a;
  }
  var b2 = f2.bind({a: 153});
  function g2(a, b, c) { return b2(2); }
  assertEquals(12, g2());

  // Caller has less arguments than callee.
  function f3(a, b, c) {
    assertEquals(153, this.a);
    CheckStackTrace([f3, test]);
    return 10 + a + b + c;
  }
  var b3 = f3.bind({a: 153});
  function g3(a) { return b3(2, 3, 4); }
  assertEquals(19, g3());

  // Callee has arguments adaptor frame.
  function f4(a, b, c) {
    assertEquals(153, this.a);
    CheckStackTrace([f4, test]);
    return 10 + a;
  }
  var b4 = f4.bind({a: 153});
  function g4(a) { return b4(2); }
  assertEquals(12, g4());
})();


// Tail calling via various expressions.
(function test() {
  function g1(a) {
    return f([f, g1, test], false) || f([f, test], true);
  }
  assertEquals(true, g1());

  function g2(a) {
    return f([f, g2, test], true) && f([f, test], true);
  }
  assertEquals(true, g2());

  function g3(a) {
    return f([f, g3, test], 13), f([f, test], 153);
  }
  assertEquals(153, g3());
})();


// Test tail calls from try-catch-finally constructs.
(function test() {
  //
  // try-catch
  //
  function tc1(a) {
    try {
      f_153([f_153, tc1, test]);
      return f_153([f_153, tc1, test]);
    } catch(e) {
      f_153([f_153, tc1, test]);
    }
  }
  assertEquals(153, tc1());

  function tc2(a) {
    try {
      f_153([f_153, tc2, test]);
      throw new Error("boom");
    } catch(e) {
      f_153([f_153, tc2, test]);
      return f_153([f_153, test]);
    }
  }
  assertEquals(153, tc2());

  function tc3(a) {
    try {
      f_153([f_153, tc3, test]);
      throw new Error("boom");
    } catch(e) {
      f_153([f_153, tc3, test]);
    }
    f_153([f_153, tc3, test]);
    return f_153([f_153, test]);
  }
  assertEquals(153, tc3());

  //
  // try-finally
  //
  function tf1(a) {
    try {
      f_153([f_153, tf1, test]);
      return f_153([f_153, tf1, test]);
    } finally {
      f_153([f_153, tf1, test]);
    }
  }
  assertEquals(153, tf1());

  function tf2(a) {
    try {
      f_153([f_153, tf2, test]);
      throw new Error("boom");
    } finally {
      f_153([f_153, tf2, test]);
      return f_153([f_153, test]);
    }
  }
  assertEquals(153, tf2());

  function tf3(a) {
    try {
      f_153([f_153, tf3, test]);
    } finally {
      f_153([f_153, tf3, test]);
    }
    return f_153([f_153, test]);
  }
  assertEquals(153, tf3());

  //
  // try-catch-finally
  //
  function tcf1(a) {
    try {
      f_153([f_153, tcf1, test]);
      return f_153([f_153, tcf1, test]);
    } catch(e) {
    } finally {
      f_153([f_153, tcf1, test]);
    }
  }
  assertEquals(153, tcf1());

  function tcf2(a) {
    try {
      f_153([f_153, tcf2, test]);
      throw new Error("boom");
    } catch(e) {
      f_153([f_153, tcf2, test]);
      return f_153([f_153, tcf2, test]);
    } finally {
      f_153([f_153, tcf2, test]);
    }
  }
  assertEquals(153, tcf2());

  function tcf3(a) {
    try {
      f_153([f_153, tcf3, test]);
      throw new Error("boom");
    } catch(e) {
      f_153([f_153, tcf3, test]);
    } finally {
      f_153([f_153, tcf3, test]);
      return f_153([f_153, test]);
    }
  }
  assertEquals(153, tcf3());

  function tcf4(a) {
    try {
      f_153([f_153, tcf4, test]);
      throw new Error("boom");
    } catch(e) {
      f_153([f_153, tcf4, test]);
    } finally {
      f_153([f_153, tcf4, test]);
    }
    return f_153([f_153, test]);
  }
  assertEquals(153, tcf4());
})();
