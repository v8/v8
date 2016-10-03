// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest = {};
InspectorTest._dispatchTable = new Map();
InspectorTest._requestId = 0;
InspectorTest._dumpInspectorProtocolMessages = false;
InspectorTest.eventHandler = {};

InspectorTest.startDumpingProtocolMessages = function()
{
  InspectorTest._dumpInspectorProtocolMessages = true;
}

InspectorTest.sendCommand = function(method, params, handler)
{
  var requestId = ++InspectorTest._requestId;
  var messageObject = { "id": requestId, "method": method, "params": params };
  InspectorTest.sendRawCommand(requestId, JSON.stringify(messageObject), handler);
}

InspectorTest.sendRawCommand = function(requestId, command, handler)
{
  if (InspectorTest._dumpInspectorProtocolMessages)
    print("frontend: " + command);
  InspectorTest._dispatchTable.set(requestId, handler);
  sendMessageToBackend(command);
}

InspectorTest.sendCommandOrDie = function(command, properties, callback)
{
  InspectorTest.sendCommand(command, properties, commandCallback);
  function commandCallback(msg)
  {
    if (msg.error) {
      InspectorTest.log("ERROR: " + msg.error.message);
      InspectorTest.completeTest();
      return;
    }
    if (callback)
      callback(msg.result);
  }
}

InspectorTest.sendCommandPromise = function(method, params)
{
  return new Promise(fulfill => InspectorTest.sendCommand(method, params, fulfill));
}

InspectorTest.waitForEventPromise = function(eventName)
{
  return new Promise(fulfill => InspectorTest.eventHandler[eventName] = fullfillAndClearListener.bind(null, fulfill));

  function fullfillAndClearListener(fulfill, result)
  {
    delete InspectorTest.eventHandler[eventName];
    fulfill(result);
  }
}

InspectorTest.dispatchMessage = function(messageObject)
{
  if (InspectorTest._dumpInspectorProtocolMessages)
    print("backend: " + JSON.stringify(messageObject));
  try {
    var messageId = messageObject["id"];
    if (typeof messageId === "number") {
      var handler = InspectorTest._dispatchTable.get(messageId);
      if (handler) {
        handler(messageObject);
        InspectorTest._dispatchTable.delete(messageId);
      }
    } else {
      var eventName = messageObject["method"];
      var eventHandler = InspectorTest.eventHandler[eventName];
      if (eventHandler)
        eventHandler(messageObject);
    }
  } catch (e) {
    InspectorTest.log("Exception when dispatching message: " + e + "\n" + e.stack + "\n message = " + JSON.stringify(messageObject, null, 2));
    InspectorTest.completeTest();
  }
}

InspectorTest.log = print.bind(null);

InspectorTest.logObject = function(object, title)
{
  var lines = [];

  function dumpValue(value, prefix, prefixWithName)
  {
    if (typeof value === "object" && value !== null) {
      if (value instanceof Array)
        dumpItems(value, prefix, prefixWithName);
      else
        dumpProperties(value, prefix, prefixWithName);
    } else {
      lines.push(prefixWithName + String(value).replace(/\n/g, " "));
    }
  }

  function dumpProperties(object, prefix, firstLinePrefix)
  {
    prefix = prefix || "";
    firstLinePrefix = firstLinePrefix || prefix;
    lines.push(firstLinePrefix + "{");

    var propertyNames = Object.keys(object);
    propertyNames.sort();
    for (var i = 0; i < propertyNames.length; ++i) {
      var name = propertyNames[i];
      if (!object.hasOwnProperty(name))
        continue;
      var prefixWithName = "    " + prefix + name + " : ";
      dumpValue(object[name], "    " + prefix, prefixWithName);
    }
    lines.push(prefix + "}");
  }

  function dumpItems(object, prefix, firstLinePrefix)
  {
    prefix = prefix || "";
    firstLinePrefix = firstLinePrefix || prefix;
    lines.push(firstLinePrefix + "[");
    for (var i = 0; i < object.length; ++i)
      dumpValue(object[i], "    " + prefix, "    " + prefix + "[" + i + "] : ");
    lines.push(prefix + "]");
  }

  dumpValue(object, "", title);
  InspectorTest.log(lines.join("\n"));
}

InspectorTest.logMessage = function(message)
{
  if (message.id)
    message.id = 0;

  const nonStableFields = new Set(["objectId", "scriptId", "exceptionId"]);
  var objects = [ message ];
  while (objects.length) {
    var object = objects.shift();
    for (var key in object) {
      if (nonStableFields.has(key))
        object[key] = `<${key}>`;
      else if (typeof object[key] === "object")
        objects.push(object[key]);
    }
  }

  InspectorTest.logObject(message);
  return message;
}

InspectorTest.completeTest = quit.bind(null);

InspectorTest.completeTestAfterPendingTimeouts = function()
{
  InspectorTest.sendCommand("Runtime.evaluate", {
    expression: "new Promise(resolve => setTimeout(resolve, 0))",
    awaitPromise: true }, InspectorTest.completeTest);
}

InspectorTest.evaluateInPage = function(string, callback)
{
  InspectorTest.sendCommand("Runtime.evaluate", { "expression": string }, function(message) {
    if (message.error) {
      InspectorTest.log("Error while executing '" + string + "': " + message.error.message);
      InspectorTest.completeTest();
    }
    else if (callback)
      callback(message.result.result.value);
  });
};

InspectorTest.checkExpectation = function(fail, name, messageObject)
{
  if (fail === !!messageObject.error) {
    InspectorTest.log("PASS: " + name);
    return true;
  }

  InspectorTest.log("FAIL: " + name + ": " + JSON.stringify(messageObject));
  InspectorTest.completeTest();
  return false;
}
InspectorTest.expectedSuccess = InspectorTest.checkExpectation.bind(null, false);
InspectorTest.expectedError = InspectorTest.checkExpectation.bind(null, true);

InspectorTest.runTestSuite = function(testSuite)
{
  function nextTest()
  {
    if (!testSuite.length) {
      InspectorTest.completeTest();
      return;
    }
    var fun = testSuite.shift();
    InspectorTest.log("\nRunning test: " + fun.name);
    fun(nextTest);
  }
  nextTest();
}
