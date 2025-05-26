// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Files: tools/clusterfuzz/js_fuzzer/resources/differential_fuzz_library.js

function test(expected, value) {
  assertEquals(expected, prettyPrinted(value));
}

// Primitives
test("undefined", undefined);
test("0", 0);
test("-0", -0);
test("42", 42);
test("42n", 42n);
test("2.3e-10", 2.3e-10);
test("Infinity", 1/0);
test("NaN", 0/0);
test('"foo"', "foo");

// Arrays
test("[]", []);
test("[1, 2, 3]", [1, 2, 3]);
test("[1, 2, , 3]", [1, 2, , 3]);
test('["a", "b", "c", "d"]', ["a", "b", "c", "d"]);

const arr = [];
arr[3] = 1;
arr[10] = 1
test("[, , , 1, , , , , , , 1]", arr);

// Arrays with properties
const arr2 = [, , 3, 4, 5];
arr2["0"] = 0;
arr2["a"] = 1;
test("[0, , 3, 4, 5]{a: 1}", arr2);

// Nested arrays
test("[[], []]", [[],[]]);
test("[[[1, 2, 3]], [4, 5], 6]", [[[1, 2, 3]],[4, 5], 6]);

// Objects
test("Object{}", {});
test("Object{}", new Object());
test('Object{a: "b"}', {a: "b"});
test("Object{1: 2, 3: 4}", {1: 2, 3: 4});

const cls = new class A {}();
cls["a"] = "b";
test('A{a: "b"}', cls);

const num = Object(1);
num[0] = 1;
test("Number(1){0: 1}", num);

const big = Object(1n);
big[0] = 1n;
test("BigInt(1n){0: 1n}", big);

const str = Object("1");
str[0] = "1";
test('String("1"){0: "1"}', str);

const bool = Object(true);
bool[0] = false;
test("Boolean(true){0: false}", bool);

const date = new Date(1748006413000);
date[0] = 1;
test("Date(1748006413000){0: 1}", date);

const reg = Object(/ab/);
reg[0] = 1;
test("/ab/{0: 1}", reg);

const i8a = new Int8Array([0, 1]);
i8a["a"] = 2;
test("Int8Array([0,1]){a: 2}", i8a);

const f64a = new Float64Array([0.1, 0.2]);
f64a["a"] = 0.3;
test("Float64Array([0.1,0.2]){a: 0.3}", f64a);

// Nested objects
test("Object{0: Object{}}", {0: {}});

// Nested arrays and objects
test("Object{0: [Object{}], 1: [Object{}]}", {0: [{}], 1: [{}]});

// Depth limit
test("[[[[...]]]]", [[[[[]]]]]);
test("Object{0: Object{0: Object{0: Object{0: ...}}}}",
   {0: {0: {0: {0: {}}}}});
test("Object{0: Object{0: Object{0: Object{0: ...}}, 1: [0, 1, [..., ...]]}}",
   {0: {0: {0: {0: {}}}, 1: [0, 1, [0, 1]]}});

// Functions
const fun = (a) => a + 1;
test("Fun{(a) => a + 1}", fun);

fun["a"] = [0, 1];
test("Fun{(a) => a + 1}{a: [0, 1]}", fun);

fun["a"] = function(a) {};
test("Fun{(a) => a + 1}{a: Fun{function(a) {}}}", fun);

// Sets
const set = new Set([0, 1, 2]);
set["a"] = 42;
test("Set[0, 1, 2]{a: 42}", set);

// Maps
const map = new Map([[0, 1], [2, 3]]);
map["a"] = 42;
test("Map{[[0, 1], [2, 3]]}{a: 42}", map);

// Proxies
const proxy = new Proxy({}, {});
proxy["a"] = 42;
test("Object{a: 42}", proxy);

// Some combinations
test("Set[Map{[[Object{}, []]]}]", new Set([new Map([[{}, []]])]));
test("Map{[[42, [Set[]]]]}", new Map([[42, [new Set([])]]]));

// Infinite nestings
const fun2 = (a) => a + 1;
fun2["a"] = new Set([fun2]);
test("Fun{(a) => a + 1}{a: Set[Fun{(a) => a + 1}{a: Set[...]}]}", fun2);

const map2 = new Map();
map2.set(0, map2);
test("Map{[[0, Map{[[0, Map{[[0, Map{[[..., ...]]}]]}]]}]]}", map2);

const map3 = new Map();
map3[0] = map3;
test("Map{[]}{0: Map{[]}{0: Map{[]}{0: Map{[]}{0: ...}}}}", map3);

const map4 = new Map();
map4.set(map4, 0);
test("Map{[[Map{[[Map{[[Map{[[..., ...]]}, 0]]}, 0]]}, 0]]}", map4);

const arr3 = [0];
arr3[1] = arr3;
test("[0, [0, [0, [..., ...]]]]", arr3);

const arr4 = [0];
arr4["a"] = arr4;
test("[0]{a: [0]{a: [0]{a: [...]{a: ...}}}}", arr4);

const arr5 = [];
arr5[0] = {0: arr5};
test("[Object{0: [Object{0: ...}]}]", arr5);
