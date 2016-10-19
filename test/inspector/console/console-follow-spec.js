// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest.addScript(`
var self = this;

function checkPrototype() {
  const prototype1 = Object.getPrototypeOf(console);
  const prototype2 = Object.getPrototypeOf(prototype1);
  if (Object.getOwnPropertyNames(prototype1).length !== 0)
    return "false: The [[Prototype]] must have no properties";
  if (prototype2 !== Object.prototype)
    return "false: The [[Prototype]]'s [[Prototype]] must be %ObjectPrototype%";
  return "true";
}
`);

InspectorTest.runTestSuite([
  function consoleExistsOnGlobal(next) {
    Protocol.Runtime.evaluate({ expression: "self.hasOwnProperty(\"console\")", returnByValue: true})
      .then(message => InspectorTest.log(message.result.result.value))
      .then(next);
  },

  function consoleHasRightPropertyDescriptor(next) {
    Protocol.Runtime.evaluate({ expression: "Object.getOwnPropertyDescriptor(self, \"console\")", returnByValue: true})
      .then(dumpDescriptor)
      .then(next);

    function dumpDescriptor(message) {
      var value = message.result.result.value;
      value.value = "<value>";
      InspectorTest.logObject(value);
    }
  },

  function ConsoleNotExistsOnGlobal(next) {
    Protocol.Runtime.evaluate({ expression: "\"Console\" in self", returnByValue: true})
      .then(message => InspectorTest.log(message.result.result.value))
      .then(next);
  },

  function prototypeChainMustBeCorrect(next) {
    Protocol.Runtime.evaluate({ expression: "checkPrototype()", returnByValue: true })
      .then(message => InspectorTest.log(message.result.result.value))
      .then(next);
  }
]);
