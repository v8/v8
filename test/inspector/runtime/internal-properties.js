// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print('Checks internal properties in Runtime.getProperties output');

Protocol.Runtime.enable();
Protocol.Debugger.enable();

InspectorTest.runTestSuite([
  function generatorFunction(next) {
    checkExpression('(function* foo() { yield 1 })').then(next);
  },

  function regularFunction(next) {
    checkExpression('(function foo() {})').then(next);
  },

  function boxedObjects(next) {
    checkExpression('new Number(239)')
      .then(() => checkExpression('new Boolean(false)'))
      .then(() => checkExpression('new String(\'abc\')'))
      .then(() => checkExpression('Object(Symbol(42))'))
      .then(next);
  },

  function promise(next) {
    checkExpression('Promise.resolve(42)')
      .then(() => checkExpression('new Promise(() => undefined)'))
      .then(next);
  },

  function generatorObject(next) {
    checkExpression('(function* foo() { yield 1 })()')
      .then(next);
  },

  function iteratorObject(next) {
    checkExpression('(new Map([[1,2]])).entries()')
      .then(() => checkExpression('(new Set([[1,2]])).entries()'))
      .then(next);
  }
]);

function checkExpression(expression)
{
  InspectorTest.log(`expression: ${expression}`);
  return Protocol.Runtime.evaluate({ expression: expression })
    .then(message => Protocol.Runtime.getProperties({ objectId: message.result.result.objectId }))
    .then(message => { delete message.result.result; return message; })
    .then(InspectorTest.logMessage);
}
