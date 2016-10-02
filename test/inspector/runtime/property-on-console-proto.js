// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("Tests that property defined on console.__proto__ doesn't observable on other Objects.");

InspectorTest.evaluateInPage(`
function testFunction()
{
    var amountOfProperties = 0;
    for (var p in {})
        ++amountOfProperties;
    console.__proto__.debug = 239;
    for (var p in {})
        --amountOfProperties;
    return amountOfProperties;
}`);

InspectorTest.sendCommand("Runtime.evaluate", { "expression": "testFunction()" }, dumpResult);

function dumpResult(result)
{
  result.id = 0;
  InspectorTest.logObject(result);
  InspectorTest.completeTest();
}
