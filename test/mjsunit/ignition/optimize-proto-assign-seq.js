
// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --proto-assign-seq-opt
// Flags: --allow-natives-syntax
// Flags: --no-jitless
// Flags: --no-lazy-feedback-allocation
// Flags: --maglev --turbofan

function test_class_fast_path() {
  class test_class {
    constructor() { }
  }

  test_class.prototype.func = function () { return "test_class.prototype.func" };
  test_class.prototype.arrow_func = () => { return "test_class.prototype.arrow_func" };
  test_class.prototype.smi = 1
  test_class.prototype.str = "test_class.prototype.str"
  // TODO(rherouart): handle object and array literals
  // test_class.prototype.obj = {o:{smi:1, str:"str"}, smi:0};
  // test_class.prototype.arr = [0,1,2];
  return new test_class();
}

function assert_test_class_fast_path(test_instance){
  assertEquals(test_instance.func(), "test_class.prototype.func");
  assertEquals(test_instance.arrow_func(), "test_class.prototype.arrow_func");
  assertEquals(test_instance.smi, 1);
  assertEquals(test_instance.str, "test_class.prototype.str");
}

function test_function_fast_paths() {
  function test_function() { }


  test_function.prototype.func = function () { return "test_function.prototype.func" };
  test_function.prototype.arrow_func = () => { return "test_function.prototype.arrow_func" };
  test_function.prototype.smi = 1
  test_function.prototype.str = "test_function.prototype.str"

  return new test_function();
}

function assert_test_function_fast_paths(test_instance){
  assertEquals(test_instance.func(), "test_function.prototype.func");
  assertEquals(test_instance.arrow_func(), "test_function.prototype.arrow_func");
  assertEquals(test_instance.smi, 1);
  assertEquals(test_instance.str, "test_function.prototype.str");
}

function test_has_prototype_keys() {
  const test_object = {}
  Object.defineProperty(test_object, "prototype", {
    value: {}
  });

  test_object.prototype.func = function () { return "test_object.prototype.func" };
  test_object.prototype.arrow_func = () => { return "test_object.prototype.arrow_func" };
  test_object.prototype.smi = 1
  test_object.prototype.str = "test_object.prototype.str"

  return test_object;
}

function assert_test_has_prototype_keys(test_object){
  assertEquals(test_object.prototype.func(), "test_object.prototype.func");
  assertEquals(test_object.prototype.arrow_func(), "test_object.prototype.arrow_func");
  assertEquals(test_object.prototype.smi, 1);
  assertEquals(test_object.prototype.str, "test_object.prototype.str");
}

function test_arrow_function() {
  var test_arrow_func = () => { };

  Object.defineProperty(test_arrow_func, "prototype", {
    value: {}
  });

  test_arrow_func.prototype.func = function () { return "test_function.prototype.func" };
  test_arrow_func.prototype.arrow_func = () => { return "test_function.prototype.arrow_func" };
  test_arrow_func.prototype.smi = 1
  test_arrow_func.prototype.str = "test_function.prototype.str"
  return test_arrow_func;
}

function assert_test_arrow_function(test_arrow_func){
  assertEquals(test_arrow_func.prototype.func(), "test_function.prototype.func");
  assertEquals(test_arrow_func.prototype.arrow_func(), "test_function.prototype.arrow_func");
  assertEquals(test_arrow_func.prototype.smi, 1);
  assertEquals(test_arrow_func.prototype.str, "test_function.prototype.str");
}

function test_has_setters() {
  function test_function() { };

  Object.defineProperty(test_function.prototype, "key", {
    set(x) {
      test_function.prototype = {}
    },
  });

  test_function.prototype.func = function () { return "test_function.prototype.func" };
  test_function.prototype.arrow_func = () => { return "test_function.prototype.arrow_func" };
  test_function.prototype.key = "key"
  test_function.prototype.smi = 1
  test_function.prototype.str = "test_function.prototype.str"


  return new test_function();
}

function assert_test_has_setters(test_instance) {
  assertEquals(Object.keys(test_instance.__proto__).length, 2);
  assertEquals(test_instance.smi, 1);
  assertEquals(test_instance.str, "test_function.prototype.str");
}


function test_prototype_proto_keys() {
  function test_function() { };
  var das_proto = {}
  Object.defineProperty(das_proto, "smi", {
    set(x) {
      test_function.prototype.str = "foo"
    },
    get() {
      return 0;
    }
  });

  test_function.prototype.func = function () { return "test_function.prototype.func" };
  test_function.prototype.arrow_func = () => { return "test_function.prototype.arrow_func" };
  test_function.prototype.__proto__ = das_proto
  test_function.prototype.str = "test_function.prototype.str"
  test_function.prototype.smi = 1

  return new test_function();
}

function assert_test_prototype_proto_keys(test_instance){
  assertEquals(test_instance.func(), "test_function.prototype.func");
  assertEquals(test_instance.arrow_func(), "test_function.prototype.arrow_func");
  assertEquals(test_instance.smi, 0);
  assertEquals(test_instance.str, "foo");
}

function test_feedback_vector_side_effect() {
  function outer() {
    function Class() { }
    Class.prototype.key_1 = function inner() {
      function tagged_template_literal(x) {
        return x;
      }
      return tagged_template_literal`abc`;
    }
    Class.prototype.key_2 = 1;
    Class.prototype.key_3 = 1;
    return new Class;
  }


  let inner_1 = outer();
  let inner_2 = outer();
  return [inner_1, inner_2]
}

function assert_test_feedback_vector_side_effect(inners){
  assertEquals(inners[0].key_1(), inners[1].key_1());
}

function test_assign_key_multiple_times() {
  function test_function() { };

  test_function.prototype.smi = function () { return "test_function.prototype.func" };
  test_function.prototype.smi = () => { return "test_function.prototype.arrow_func" };
  test_function.prototype.smi = 1

  return new test_function();
}

function assert_test_assign_key_multiple_times(x) {
    assertEquals(x.smi, 1);
}

function test_not_proto_assign_seq() {
  function test_function() { };
  test_function.prototype = []

  test_function.prototype["0"] = function () { return "test_function.prototype.func" };
  test_function.prototype["1"] = () => { return "test_function.prototype.arrow_func" };
  test_function.prototype["2"] = 1

  return test_function;
}

function assert_test_not_proto_assign_seq(test_function){
  assertEquals(test_function.prototype["0"](), "test_function.prototype.func");
  assertEquals(test_function.prototype["1"](), "test_function.prototype.arrow_func");
  assertEquals(test_function.prototype["2"], 1);
}

function test_prototype_read_only() {
  function test_function() { }

  Object.defineProperty(test_function.prototype, "key", { value: 0, writable: false })

  test_function.prototype.func = function () { return "test_function.prototype.func" };
  test_function.prototype.arrow_func = () => { return "test_function.prototype.arrow_func" };
  test_function.prototype.smi = 1
  test_function.prototype.str = "test_function.prototype.str"
  test_function.prototype.key = 1

  return new test_function();
}

function assert_test_prototype_read_only(test_instance) {
  assertEquals(test_instance.func(), "test_function.prototype.func");
  assertEquals(test_instance.arrow_func(), "test_function.prototype.arrow_func");
  assertEquals(test_instance.smi, 1);
  assertEquals(test_instance.str, "test_function.prototype.str");
  assertEquals(test_instance.key, 0);
}

function test_eval_return_last_set_property() {
  return eval(`
    function Foo(){}
    Foo.prototype.k1 = 1;
    Foo.prototype.k2 = 2;
    Foo.prototype.k3 = 3;
  `)
}

function assert_test_eval_return_last_set_property(result){
  assertEquals(3, result);
}

function test_null_prototype() {
  function test_function() { }

  test_function.prototype = null;
  assertThrows(() => {
    test_function.prototype.func = function () { return "test_function.prototype.func" };
    test_function.prototype.arrow_func = () => { return "test_function.prototype.arrow_func" };
    test_function.prototype.smi = 1
    test_function.prototype.str = "test_function.prototype.str"
    test_function.prototype.key = 1

    test_instance = new test_function();
    assertEquals(test_instance.func(), "test_function.prototype.func");
    assertEquals(test_instance.arrow_func(), "test_function.prototype.arrow_func");
    assertEquals(test_instance.smi, 1);
    assertEquals(test_instance.str, "test_function.prototype.str");
    assertEquals(test_instance.key, 0);
  }, TypeError);
}

function assert_test_null_prototype(test_instance){
}

function test_variable_proxy() {
  var calls = 0;
  let foo = {
    get prototype() {
      calls += 1;
      foo = {};
      return { prototype: {} };
    }
  };
  assertThrows(() => {
    foo.prototype.k1 = 1;
    foo.prototype.k2 = 2;
  }, TypeError);

  return [calls,foo]
}

function assert_test_variable_proxy(arr){
  assertEquals(arr[0], 1);
  assertEquals(Object.keys(arr[1]).length, 0);
}

function test_variable_proxy_eval() {
  var foo = function () { };
  (function inner_test() {

    eval("var foo = { prototype: { set k1(x) { calls += 1;foo = {}} }}");
    var calls = 0;
    assertThrows(() => {
      foo.prototype.k1 = 1;
      foo.prototype.k2 = 2;
    }, TypeError);
    assertEquals(calls, 1);
    assertEquals(Object.keys(foo).length, 0);
  })();
}

function assert_test_variable_proxy_eval(foo){

}

function test_different_left_most_var() {
  // Note: This code should not generate a SetPrototypeProperties Instruction
  function foo() { }
  function bar() { }

  foo.prototype.k1 = 1;
  bar.prototype.k1 = 2;
  foo.prototype.k2 = 3;
  bar.prototype.k2 = 4;

  return [foo,bar];
}

function assert_test_different_left_most_var(arr){
  assertEquals(arr[0].prototype.k1, 1);
  assertEquals(arr[1].prototype.k1, 2);
  assertEquals(arr[0].prototype.k2, 3);
  assertEquals(arr[1].prototype.k2, 4);
}

functions = [
  "test_null_prototype",
  "test_class_fast_path",
  "test_function_fast_paths",
  "test_has_prototype_keys",
  "test_arrow_function",
  "test_has_setters",
  "test_prototype_proto_keys",
  "test_feedback_vector_side_effect",
  "test_assign_key_multiple_times",
  "test_not_proto_assign_seq",
  "test_prototype_read_only",
  "test_eval_return_last_set_property",
  "test_variable_proxy_eval",
  "test_variable_proxy",
  "test_different_left_most_var"
];

functions.forEach(function (func, index) {
  console.log(func);
  globalThis["assert_"+func](globalThis[func]());
  %CompileBaseline(globalThis[func]);
  globalThis["assert_"+func](globalThis[func]());
  %PrepareFunctionForOptimization(globalThis[func]);
  globalThis["assert_"+func](globalThis[func]());
  globalThis["assert_"+func](globalThis[func]());
  %OptimizeMaglevOnNextCall(globalThis[func]);
  globalThis["assert_"+func](globalThis[func]());
  assertOptimized(globalThis[func]);
  assertTrue(isMaglevved(globalThis[func]));
  globalThis["assert_"+func](globalThis[func]());
  %OptimizeFunctionOnNextCall(globalThis[func]);
  globalThis["assert_"+func](globalThis[func]());
  assertOptimized(globalThis[func]);
  globalThis["assert_"+func](globalThis[func]());
});
