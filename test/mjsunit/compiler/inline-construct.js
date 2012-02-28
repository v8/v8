// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --allow-natives-syntax --inline-construct

// Test inlining of constructor calls.

function TestInlinedConstructor(closure) {
  var result;
  var counter = { value:0 };
  result = closure(11, 12, counter);
  assertEquals(23, result);
  assertEquals(1, counter.value);
  result = closure(23, 19, counter);
  assertEquals(42, result);
  assertEquals(2, counter.value);
  %OptimizeFunctionOnNextCall(closure);
  result = closure(1, 42, counter)
  assertEquals(43, result);
  assertEquals(3, counter.value);
  result = closure("foo", "bar", counter)
  assertEquals("foobar", result)
  assertEquals(4, counter.value);
}


// Test constructor returning nothing in all contexts.
function c1(a, b, counter) {
  this.x = a + b;
  counter.value++;
}
function c1_value_context(a, b, counter) {
  var obj = new c1(a, b, counter);
  return obj.x;
}
function c1_test_context(a, b, counter) {
  if (!new c1(a, b, counter)) {
    assertUnreachable("should not happen");
  }
  return a + b;
}
function c1_effect_context(a, b, counter) {
  new c1(a, b, counter);
  return a + b;
}
TestInlinedConstructor(c1_value_context);
TestInlinedConstructor(c1_test_context);
TestInlinedConstructor(c1_effect_context);


// Test constructor returning an object in all contexts.
function c2(a, b, counter) {
  var obj = new Object();
  obj.x = a + b;
  counter.value++;
  return obj;
}
function c2_value_context(a, b, counter) {
  var obj = new c2(a, b, counter);
  return obj.x;
}
function c2_test_context(a, b, counter) {
  if (!new c2(a, b, counter)) {
    assertUnreachable("should not happen");
  }
  return a + b;
}
function c2_effect_context(a, b, counter) {
  new c2(a, b, counter);
  return a + b;
}
TestInlinedConstructor(c2_value_context);
TestInlinedConstructor(c2_test_context);
TestInlinedConstructor(c2_effect_context);


// Test constructor returning a primitive value in all contexts.
function c3(a, b, counter) {
  this.x = a + b;
  counter.value++;
  return "not an object";
}
function c3_value_context(a, b, counter) {
  var obj = new c3(a, b, counter);
  return obj.x;
}
function c3_test_context(a, b, counter) {
  if (!new c3(a, b, counter)) {
    assertUnreachable("should not happen");
  }
  return a + b;
}
function c3_effect_context(a, b, counter) {
  new c3(a, b, counter);
  return a + b;
}
TestInlinedConstructor(c3_value_context);
TestInlinedConstructor(c3_test_context);
TestInlinedConstructor(c3_effect_context);


// Test constructor called with too many arguments.
function c_too_many(a, b) {
  this.x = a + b;
}
function f_too_many(a, b, c) {
  var obj = new c_too_many(a, b, c);
  return obj.x;
}
assertEquals(23, f_too_many(11, 12, 1));
assertEquals(42, f_too_many(23, 19, 1));
%OptimizeFunctionOnNextCall(f_too_many);
assertEquals(43, f_too_many(1, 42, 1));
assertEquals("foobar", f_too_many("foo", "bar", "baz"))


// Test constructor called with too few arguments.
function c_too_few(a, b) {
  assertSame(undefined, b);
  this.x = a + 1;
}
function f_too_few(a) {
  var obj = new c_too_few(a);
  return obj.x;
}
assertEquals(12, f_too_few(11));
assertEquals(24, f_too_few(23));
%OptimizeFunctionOnNextCall(f_too_few);
assertEquals(2, f_too_few(1));
assertEquals("foo1", f_too_few("foo"))


// Test constructor that cannot be inlined.
function c_unsupported_syntax(a, b, counter) {
  try {
    this.x = a + b;
    counter.value++;
  } catch(e) {
    throw new Error();
  }
}
function f_unsupported_syntax(a, b, counter) {
  var obj = new c_unsupported_syntax(a, b, counter);
  return obj.x;
}
TestInlinedConstructor(f_unsupported_syntax);
