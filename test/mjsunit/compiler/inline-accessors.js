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

// Flags: --allow-natives-syntax --inline-accessors

var accessorCallCount, setterArgument, setterValue, obj, forceDeopt;

// -----------------------------------------------------------------------------
// Helpers for testing inlining of getters.

function TestInlinedGetter(context, expected) {
  forceDeopt = 0;
  accessorCallCount = 0;

  assertEquals(expected, context());
  assertEquals(1, accessorCallCount);

  assertEquals(expected, context());
  assertEquals(2, accessorCallCount);

  %OptimizeFunctionOnNextCall(context);
  assertEquals(expected, context());
  assertEquals(3, accessorCallCount);

  %DeoptimizeFunction(context);
  %ClearFunctionTypeFeedback(context);
}


function TestGetterInAllContexts(obj, expected) {
  function value_context() {
    return obj.getterProperty;
  }
  TestInlinedGetter(value_context, expected);

  function test_context() {
    if (obj.getterProperty) {
      return 111;
    } else {
      return 222;
    }
  }
  TestInlinedGetter(test_context, expected ? 111 : 222);

  function effect_context() {
    obj.getterProperty;
    return 5678;
  }
  TestInlinedGetter(effect_context, 5678);
}

// -----------------------------------------------------------------------------
// Test getter returning something 'true'ish in all contexts.

function getter1() {
  assertSame(obj, this);
  accessorCallCount++;
  forceDeopt + 1;
  return 1234;
}

function ConstrG1() { }
obj = Object.defineProperty(new ConstrG1(), "getterProperty", { get: getter1 });
TestGetterInAllContexts(obj, 1234);
obj = Object.create(obj);
TestGetterInAllContexts(obj, 1234);

// -----------------------------------------------------------------------------
// Test getter returning false in all contexts.

function getter2() {
  assertSame(obj, this);
  accessorCallCount++;
  forceDeopt + 1;
  return false;
}

function ConstrG2() { }
obj = Object.defineProperty(new ConstrG2(), "getterProperty", { get: getter2 });
TestGetterInAllContexts(obj, false);
obj = Object.create(obj);
TestGetterInAllContexts(obj, false);

// -----------------------------------------------------------------------------
// Test getter without a return in all contexts.

function getter3() {
  assertSame(obj, this);
  accessorCallCount++;
  forceDeopt + 1;
}

function ConstrG3() { }
obj = Object.defineProperty(new ConstrG3(), "getterProperty", { get: getter3 });
TestGetterInAllContexts(obj, undefined);
obj = Object.create(obj);
TestGetterInAllContexts(obj, undefined);

// -----------------------------------------------------------------------------
// Test getter with too many arguments without a return in all contexts.

function getter4(a) {
  assertSame(obj, this);
  assertEquals(undefined, a);
  accessorCallCount++;
  forceDeopt + 1;
}

function ConstrG4() { }
obj = Object.defineProperty(new ConstrG4(), "getterProperty", { get: getter4 });
TestGetterInAllContexts(obj, undefined);
obj = Object.create(obj);
TestGetterInAllContexts(obj, undefined);

// -----------------------------------------------------------------------------
// Test getter with too many arguments with a return in all contexts.

function getter5(a) {
  assertSame(obj, this);
  assertEquals(undefined, a);
  accessorCallCount++;
  forceDeopt + 1;
  return 9876;
}

function ConstrG5() { }
obj = Object.defineProperty(new ConstrG5(), "getterProperty", { get: getter5 });
TestGetterInAllContexts(obj, 9876);
obj = Object.create(obj);
TestGetterInAllContexts(obj, 9876);

// -----------------------------------------------------------------------------
// Helpers for testing inlining of setters.

function TestInlinedSetter(context, value, expected) {
  forceDeopt = 0;
  accessorCallCount = 0;
  setterArgument = value;

  assertEquals(expected, context(value));
  assertEquals(value, setterValue);
  assertEquals(1, accessorCallCount);

  assertEquals(expected, context(value));
  assertEquals(value, setterValue);
  assertEquals(2, accessorCallCount);

  %OptimizeFunctionOnNextCall(context);
  assertEquals(expected, context(value));
  assertEquals(value, setterValue);
  assertEquals(3, accessorCallCount);

  %DeoptimizeFunction(context);
  %ClearFunctionTypeFeedback(context);
}

function TestSetterInAllContexts(obj) {
  function value_context(value) {
    return obj.setterProperty = value;
  }
  TestInlinedSetter(value_context, 111, 111);

  function test_context(value) {
    if (obj.setterProperty = value) {
      return 333;
    } else {
      return 444;
    }
  }
  TestInlinedSetter(test_context, true, 333);
  TestInlinedSetter(test_context, false, 444);

  function effect_context(value) {
    obj.setterProperty = value;
    return 666;
  }
  TestInlinedSetter(effect_context, 555, 666);
}

// -----------------------------------------------------------------------------
// Test setter without a return in all contexts.

function setter1(value) {
  assertSame(obj, this);
  accessorCallCount++;
  forceDeopt + 1;
  setterValue = value;
}

function ConstrS1() { }
obj = Object.defineProperty(new ConstrS1(), "setterProperty", { set: setter1 });
TestSetterInAllContexts(obj);
obj = Object.create(obj);
TestSetterInAllContexts(obj);

// -----------------------------------------------------------------------------
// Test setter returning something different than the RHS in all contexts.

function setter2(value) {
  assertSame(obj, this);
  accessorCallCount++;
  forceDeopt + 1;
  setterValue = value;
  return 1000000;
}

function ConstrS2() { }
obj = Object.defineProperty(new ConstrS2(), "setterProperty", { set: setter2 });
TestSetterInAllContexts(obj);
obj = Object.create(obj);
TestSetterInAllContexts(obj);

// -----------------------------------------------------------------------------
// Test setter with too few arguments without a return in all contexts.

function setter3() {
  assertSame(obj, this);
  accessorCallCount++;
  forceDeopt + 1;
  setterValue = setterArgument;
}

function ConstrS3() { }
obj = Object.defineProperty(new ConstrS3(), "setterProperty", { set: setter3 });
TestSetterInAllContexts(obj);
obj = Object.create(obj);
TestSetterInAllContexts(obj);

// -----------------------------------------------------------------------------
// Test setter with too few arguments with a return in all contexts.

function setter4() {
  assertSame(obj, this);
  accessorCallCount++;
  forceDeopt + 1;
  setterValue = setterArgument;
  return 2000000;
}

function ConstrS4() { }
obj = Object.defineProperty(new ConstrS4(), "setterProperty", { set: setter4 });
TestSetterInAllContexts(obj);
obj = Object.create(obj);
TestSetterInAllContexts(obj);

// -----------------------------------------------------------------------------
// Test setter with too many arguments without a return in all contexts.

function setter5(value, foo) {
  assertSame(obj, this);
  assertEquals(undefined, foo);
  accessorCallCount++;
  forceDeopt + 1;
  setterValue = value;
}

function ConstrS5() { }
obj = Object.defineProperty(new ConstrS5(), "setterProperty", { set: setter5 });
TestSetterInAllContexts(obj);
obj = Object.create(obj);
TestSetterInAllContexts(obj);

// -----------------------------------------------------------------------------
// Test setter with too many arguments with a return in all contexts.

function setter6(value, foo) {
  assertSame(obj, this);
  assertEquals(undefined, foo);
  accessorCallCount++;
  forceDeopt + 1;
  setterValue = value;
  return 3000000;
}

function ConstrS6() { }
obj = Object.defineProperty(new ConstrS6(), "setterProperty", { set: setter6 });
TestSetterInAllContexts(obj);
obj = Object.create(obj);
TestSetterInAllContexts(obj);
