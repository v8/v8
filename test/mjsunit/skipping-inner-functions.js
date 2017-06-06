// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --experimental-preparser-scope-analysis

(function TestBasicSkipping() {
  var result = 0;

  function lazy(ctxt_alloc_param) {
    var ctxt_alloc_var = 10;
    function skip_me() {
      result = ctxt_alloc_param + ctxt_alloc_var;
    }
    return skip_me;
  }
  // Test that parameters and variables of the outer function get context
  // allocated even if we skip the inner function.
  lazy(9)();
  assertEquals(19, result);
})();

(function TestSkippingFunctionWithEval() {
  var result = 0;

  function lazy(ctxt_alloc_param) {
    var ctxt_alloc_var = 10;
    function skip_me() {
      eval('result = ctxt_alloc_param + ctxt_alloc_var');
    }
    return skip_me;
  }
  // Test that parameters and variables of the outer function get context
  // allocated even if we skip the inner function.
  lazy(9)();
  assertEquals(19, result);
})();

(function TestCtxtAllocatingNonSimpleParams1() {
  var result = 0;

  function lazy([other_param1, ctxt_alloc_param, other_param2]) {
    function skip_me() {
      result = ctxt_alloc_param;
    }
    return skip_me;
  }
  // Test that parameters and variables of the outer function get context
  // allocated even if we skip the inner function.
  lazy([30, 29, 28])();
  assertEquals(29, result);
})();

(function TestCtxtAllocatingNonSimpleParams2() {
  var result = 0;

  function lazy({a: other_param1, b: ctxt_alloc_param, c: other_param2}) {
    function skip_me() {
      result = ctxt_alloc_param;
    }
    return skip_me;
  }
  // Test that parameters and variables of the outer function get context
  // allocated even if we skip the inner function.
  lazy({a: 31, b: 32, c: 33})();
  assertEquals(32, result);
})();

(function TestCtxtAllocatingNonSimpleParams3() {
  var result = 0;

  function lazy(...ctxt_alloc_param) {
    function skip_me() {
      result = ctxt_alloc_param;
    }
    return skip_me;
  }
  // Test that parameters and variables of the outer function get context
  // allocated even if we skip the inner function.
  lazy(34, 35)();
  assertEquals([34, 35], result);
})();

// Skippable top level functions.
let result = 0;
function lazy_top_level(ctxt_alloc_param) {
  let ctxt_alloc_var = 24;
  function skip_me() {
    result = ctxt_alloc_param + ctxt_alloc_var;
  }
  skip_me();
}

lazy_top_level(10);
assertEquals(34, result);
