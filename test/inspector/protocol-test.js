// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

InspectorTest = {};
InspectorTest._dumpInspectorProtocolMessages = false;
InspectorTest._commandsForLogging = new Set();

InspectorTest.createContextGroup = function() {
  var contextGroup = {};
  contextGroup.id = utils.createContextGroup();
  contextGroup.schedulePauseOnNextStatement = (reason, details) => utils.schedulePauseOnNextStatement(contextGroup.id, reason, details);
  contextGroup.cancelPauseOnNextStatement = () => utils.cancelPauseOnNextStatement(contextGroup.id);
  contextGroup.addScript = (string, lineOffset, columnOffset, url) => utils.compileAndRunWithOrigin(contextGroup.id, string, url || '', lineOffset || 0, columnOffset || 0, false);
  contextGroup.addModule = (string, url, lineOffset, columnOffset) => utils.compileAndRunWithOrigin(contextGroup.id, string, url, lineOffset || 0, columnOffset || 0, true);
  return contextGroup;
}

InspectorTest._sessions = new Map();
InspectorTest.createSession = function(contextGroup) {
  var session = {
    contextGroup: contextGroup,
    _dispatchTable: new Map(),
    _eventHandler: {},
    _requestId: 0,
  };
  session.Protocol = new Proxy({}, {
    get: function(target, agentName, receiver) {
      return new Proxy({}, {
        get: function(target, methodName, receiver) {
          const eventPattern = /^on(ce)?([A-Z][A-Za-z0-9]+)/;
          var match = eventPattern.exec(methodName);
          if (!match) {
            return args => session._sendCommandPromise(`${agentName}.${methodName}`, args || {});
          } else {
            var eventName = match[2];
            eventName = eventName.charAt(0).toLowerCase() + eventName.slice(1);
            if (match[1])
              return () => InspectorTest._waitForEventPromise(session, `${agentName}.${eventName}`);
            else
              return (listener) => { session._eventHandler[`${agentName}.${eventName}`] = listener };
          }
        }
      });
    }
  });
  session._dispatchMessage = messageString => {
    let messageObject = JSON.parse(messageString);
    if (InspectorTest._dumpInspectorProtocolMessages)
      utils.print("backend: " + JSON.stringify(messageObject));
    try {
      var messageId = messageObject["id"];
      if (typeof messageId === "number") {
        var handler = session._dispatchTable.get(messageId);
        if (handler) {
          handler(messageObject);
          session._dispatchTable.delete(messageId);
        }
      } else {
        var eventName = messageObject["method"];
        var eventHandler = session._eventHandler[eventName];
        if (session._scriptMap && eventName === "Debugger.scriptParsed")
          session._scriptMap.set(messageObject.params.scriptId, JSON.parse(JSON.stringify(messageObject.params)));
        if (eventName === "Debugger.scriptParsed" && messageObject.params.url === "wait-pending-tasks.js")
          return;
        if (eventHandler)
          eventHandler(messageObject);
      }
    } catch (e) {
      InspectorTest.log("Exception when dispatching message: " + e + "\n" + e.stack + "\n message = " + JSON.stringify(messageObject, null, 2));
      InspectorTest.completeTest();
    }
  };
  session.id = utils.connectSession(contextGroup.id, '', session._dispatchMessage.bind(session));
  InspectorTest._sessions.set(session.id, session);
  session.disconnect = () => utils.disconnectSession(session.id);
  session.reconnect = () => {
    InspectorTest._sessions.delete(session.id);
    var state = utils.disconnectSession(session.id);
    session.id = utils.connectSession(contextGroup.id, state, session._dispatchMessage.bind(session));
    InspectorTest._sessions.set(session.id, session);
  };
  session.sendRawCommand = (requestId, command, handler) => {
    if (InspectorTest._dumpInspectorProtocolMessages)
      utils.print("frontend: " + command);
    session._dispatchTable.set(requestId, handler);
    utils.sendMessageToBackend(session.id, command);
  }
  session._sendCommandPromise = (method, params) => {
    var requestId = ++session._requestId;
    var messageObject = { "id": requestId, "method": method, "params": params };
    var fulfillCallback;
    var promise = new Promise(fulfill => fulfillCallback = fulfill);
    if (InspectorTest._commandsForLogging.has(method)) {
      utils.print(method + ' called');
    }
    session.sendRawCommand(requestId, JSON.stringify(messageObject), fulfillCallback);
    return promise;
  }
  return session;
}

InspectorTest.logProtocolCommandCalls = (command) => InspectorTest._commandsForLogging.add(command);

InspectorTest.log = utils.print.bind(null);

InspectorTest.logMessage = function(originalMessage)
{
  var message = JSON.parse(JSON.stringify(originalMessage));
  if (message.id)
    message.id = "<messageId>";

  const nonStableFields = new Set(["objectId", "scriptId", "exceptionId", "timestamp",
    "executionContextId", "callFrameId", "breakpointId", "bindRemoteObjectFunctionId", "formatterObjectId" ]);
  var objects = [ message ];
  while (objects.length) {
    var object = objects.shift();
    for (var key in object) {
      if (nonStableFields.has(key))
        object[key] = `<${key}>`;
      else if (typeof object[key] === "string" && object[key].match(/\d+:\d+:\d+:debug/))
        object[key] = object[key].replace(/\d+/, '<scriptId>');
      else if (typeof object[key] === "object")
        objects.push(object[key]);
    }
  }

  InspectorTest.logObject(message);
  return originalMessage;
}

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

  dumpValue(object, "", title || "");
  InspectorTest.log(lines.join("\n"));
}

InspectorTest.logCallFrames = function(callFrames, session)
{
  session = session || InspectorTest.session;
  for (var frame of callFrames) {
    var functionName = frame.functionName || '(anonymous)';
    var url = frame.url ? frame.url : session._scriptMap.get(frame.location.scriptId).url;
    var lineNumber = frame.location ? frame.location.lineNumber : frame.lineNumber;
    var columnNumber = frame.location ? frame.location.columnNumber : frame.columnNumber;
    InspectorTest.log(`${functionName} (${url}:${lineNumber}:${columnNumber})`);
  }
}

InspectorTest.logSourceLocation = function(location, session)
{
  session = session || InspectorTest.session;
  var scriptId = location.scriptId;
  if (!session._scriptMap || !session._scriptMap.has(scriptId)) {
    InspectorTest.log("InspectorTest.setupScriptMap should be called before Protocol.Debugger.enable.");
    InspectorTest.completeTest();
  }
  var script = session._scriptMap.get(scriptId);
  if (!script.scriptSource) {
    return session.Protocol.Debugger.getScriptSource({ scriptId })
      .then(message => script.scriptSource = message.result.scriptSource)
      .then(dumpSourceWithLocation);
  }
  return Promise.resolve().then(dumpSourceWithLocation);

  function dumpSourceWithLocation() {
    var lines = script.scriptSource.split('\n');
    var line = lines[location.lineNumber];
    line = line.slice(0, location.columnNumber) + '#' + (line.slice(location.columnNumber) || '');
    lines[location.lineNumber] = line;
    lines = lines.filter(line => line.indexOf('//# sourceURL=') === -1);
    InspectorTest.log(lines.slice(Math.max(location.lineNumber - 1, 0), location.lineNumber + 2).join('\n'));
    InspectorTest.log('');
  }
}

InspectorTest.logSourceLocations = function(locations, session) {
  if (locations.length == 0) return Promise.resolve();
  return InspectorTest.logSourceLocation(locations[0], session)
      .then(() => InspectorTest.logSourceLocations(locations.splice(1), session));
}

InspectorTest.logAsyncStackTrace = function(asyncStackTrace, session)
{
  session = InspectorTest.session || session;
  while (asyncStackTrace) {
    if (asyncStackTrace.promiseCreationFrame) {
      var frame = asyncStackTrace.promiseCreationFrame;
      InspectorTest.log(`-- ${asyncStackTrace.description} (${frame.url
                        }:${frame.lineNumber}:${frame.columnNumber})--`);
    } else {
      InspectorTest.log(`-- ${asyncStackTrace.description} --`);
    }
    InspectorTest.logCallFrames(asyncStackTrace.callFrames, session);
    asyncStackTrace = asyncStackTrace.parent;
  }
}

InspectorTest.completeTest = () => Protocol.Debugger.disable().then(() => utils.quit());

InspectorTest.completeTestAfterPendingTimeouts = function()
{
  InspectorTest.waitPendingTasks().then(InspectorTest.completeTest);
}

InspectorTest.waitPendingTasks = function()
{
  var promises = [];
  for (var session of InspectorTest._sessions.values())
    promises.push(session.Protocol.Runtime.evaluate({ expression: "new Promise(r => setTimeout(r, 0))//# sourceURL=wait-pending-tasks.js", awaitPromise: true }));
  return Promise.all(promises);
}

InspectorTest.startDumpingProtocolMessages = function()
{
  InspectorTest._dumpInspectorProtocolMessages = true;
}

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

InspectorTest.setupScriptMap = function(session) {
  session = session || InspectorTest.session;
  if (session._scriptMap)
    return;
  session._scriptMap = new Map();
}

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

InspectorTest.runAsyncTestSuite = async function(testSuite) {
  for (var test of testSuite) {
    InspectorTest.log("\nRunning test: " + test.name);
    try {
      await test();
    } catch (e) {
      utils.print(e.stack);
    }
  }
  InspectorTest.completeTest();
}

InspectorTest._waitForEventPromise = function(session, eventName)
{
  return new Promise(fulfill => session._eventHandler[eventName] = fullfillAndClearListener.bind(null, fulfill));

  function fullfillAndClearListener(fulfill, result)
  {
    delete session._eventHandler[eventName];
    fulfill(result);
  }
}

InspectorTest.setupInjectedScriptEnvironment = function(debug, session) {
  session = session || InspectorTest.session;
  let scriptSource = '';
  // First define all getters on Object.prototype.
  let injectedScriptSource = utils.read('src/inspector/injected-script-source.js');
  let getterRegex = /\.[a-zA-Z0-9]+/g;
  let match;
  let getters = new Set();
  while (match = getterRegex.exec(injectedScriptSource)) {
    getters.add(match[0].substr(1));
  }
  scriptSource += `(function installSettersAndGetters() {
    let defineProperty = Object.defineProperty;
    let ObjectPrototype = Object.prototype;\n`;
  scriptSource += Array.from(getters).map(getter => `
    defineProperty(ObjectPrototype, '${getter}', {
      set() { debugger; throw 42; }, get() { debugger; throw 42; },
      __proto__: null
    });
  `).join('\n') + '})();';
  session.contextGroup.addScript(scriptSource);

  if (debug) {
    InspectorTest.log('WARNING: InspectorTest.setupInjectedScriptEnvironment with debug flag for debugging only and should not be landed.');
    InspectorTest.log('WARNING: run test with --expose-inspector-scripts flag to get more details.');
    InspectorTest.log('WARNING: you can additionally comment rjsmin in xxd.py to get unminified injected-script-source.js.');
    InspectorTest.setupScriptMap(session);
    sesison.Protocol.Debugger.enable();
    session.Protocol.Debugger.onPaused(message => {
      let callFrames = message.params.callFrames;
      InspectorTest.logSourceLocations(callFrames.map(frame => frame.location), session);
    })
  }
}

try {
  InspectorTest.contextGroup = InspectorTest.createContextGroup();
  InspectorTest.session = InspectorTest.createSession(InspectorTest.contextGroup);
  this.Protocol = InspectorTest.session.Protocol;
  InspectorTest.addScript = InspectorTest.contextGroup.addScript.bind(InspectorTest.contextGroup);
  InspectorTest.addModule = InspectorTest.contextGroup.addModule.bind(InspectorTest.contextGroup);
  InspectorTest.loadScript = fileName => InspectorTest.addScript(utils.read(fileName));
} catch (e) {
  utils.print(e.stack);
}
