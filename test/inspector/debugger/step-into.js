// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print('Checks possible break locations.');

InspectorTest.addScript(`

function testEval() {
  eval('// comment only');
  eval('// comment only\\n');
}

// function without return
function procedure() {
  var a = 1;
  var b = 2;
}

function testProcedure() {
  procedure();
}

function returnTrue() {
  return true;
}

function testIf() {
  var a;
  if (true) a = true;
  if (!a) {
    a = true;
  } else {
    a = false;
  }
  if (returnTrue()) {
    a = false;
  } else {
    a = true;
  }
}

function emptyFunction() {}

function testEmptyFunction() {
  emptyFunction();
}

function twoArguments(a1, a2) {
}

function testCallArguments() {
  twoArguments(emptyFunction(), emptyFunction());
}

function testNested() {
  function nested1() {
    function nested2() {
      function nested3() {
      }
      nested3();
      return;
    }
    return nested2();
  }
  nested1();
}

function return42() {
  return 42;
}

function returnCall() {
  return return42();
}

function testCallAtReturn() {
  return returnCall();
}

function returnObject() {
  return ({ foo: () => 42 });
}

function testWith() {
  with (returnObject()) {
    foo();
  }
  with({}) {
    return;
  }
}

function testForLoop() {
  for (var i = 0; i < 1; ++i) {}
  for (var i = 0; i < 1; ++i) i;
  for (var i = 0; i < 0; ++i) {}
}

function testForOfLoop() {
  for (var k of []) {}
  for (var k of [1]) k;
  var a = [];
  for (var k of a) {}
}

function testForInLoop() {
  var o = {};
  for (var k in o) {}
  for (var k in o) k;
  for (var k in { a:1 }) {}
  for (var k in { a:1 }) k;
}

function testSimpleExpressions() {
  1 + 2 + 3;
  var a = 1;
  ++a;
  a--;
}

Object.defineProperty(this, 'getterFoo', {
  get: () => return42
});

function testGetter() {
  getterFoo();
}

var obj = {
  foo: () => ({
    boo: () => return42
  })
};

function testChainedCalls() {
  obj.foo().boo()();
}

function testChainedWithNative() {
  Array.from([1]).concat([2]).map(v => v * 2);
}

function testPromiseThen() {
  return Promise.resolve().then(v => v * 2).then(v => v * 2);
}

function testSwitch() {
  for (var i = 0; i < 3; ++i) {
    switch(i) {
      case 0: continue;
      case 1: return42(); break;
      default: return;
    }
  }
}

function* idMaker() {
  yield 1;
  yield 2;
  yield 3;
}

function testGenerator() {
  var gen = idMaker();
  return42();
  gen.next().value;
  debugger;
  gen.next().value;
  return42();
  gen.next().value;
  return42();
  gen.next().value;
}

function throwException() {
  throw new Error();
}

function testCaughtException() {
  try {
    throwException()
  } catch (e) {
    return;
  }
}

function testClasses() {
  class Cat {
    constructor(name) {
      this.name = name;
    }

    speak() {
    }
  }
  class Lion extends Cat {
    constructor(name) {
      super(name);
    }

    speak() {
      super.speak();
    }
  }
  new Lion().speak();
}

async function asyncFoo() {
  await Promise.resolve().then(v => v * 2);
  return42();
  await asyncBoo();
}

async function asyncBoo() {
  await Promise.resolve();
}

async function testAsyncAwait() {
  await asyncFoo();
  await awaitBoo();
}

// TODO(kozyatinskiy): fix this.
async function testPromiseAsyncWithCode() {
  var nextTest;
  var testPromise = new Promise(resolve => nextTest = resolve);
  async function main() {
    async function foo() {
      var resolveNested;
      var p = new Promise(resolve => resolveNested = resolve);
      setTimeout(resolveNested, 0);
      await p;
    }
    setTimeout(returnCall, 0);
    await foo();
    await foo();
    nextTest();
  }
  main();
  return testPromise;
}

//# sourceURL=test.js`);

InspectorTest.setupScriptMap();
Protocol.Debugger.onPaused(message => {
  var frames = message.params.callFrames;
  if (frames.length === 1) {
    Protocol.Debugger.stepInto();
    return;
  }
  var scriptId = frames[0].location.scriptId;
  InspectorTest.log('break at:');
  InspectorTest.logCallFrameSourceLocation(frames[0])
    .then(() => Protocol.Debugger.stepInto());
});

Protocol.Debugger.enable();
Protocol.Runtime.evaluate({ expression: 'Object.keys(this).filter(name => name.indexOf(\'test\') === 0)', returnByValue: true })
  .then(runTests);

function runTests(message) {
  var tests = message.result.result.value;
  InspectorTest.runTestSuite(tests.map(test => eval(`(function ${test}(next) {
    Protocol.Runtime.evaluate({ expression: 'debugger; ${test}()', awaitPromise: ${test.indexOf('testPromise') === 0}})
      .then(next);
  })`)));
}
