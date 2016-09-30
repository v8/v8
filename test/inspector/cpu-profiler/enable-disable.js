// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("Test that profiling can only be started when Profiler was enabled and that Profiler.disable command will stop recording all profiles.");

InspectorTest.sendCommand("Profiler.start", {}, didFailToStartWhenDisabled);
disallowConsoleProfiles();

function disallowConsoleProfiles()
{
  InspectorTest.eventHandler["Profiler.consoleProfileStarted"] = function(messageObject)
  {
    InspectorTest.log("FAIL: console profile started " + JSON.stringify(messageObject, null, 4));
  }
  InspectorTest.eventHandler["Profiler.consoleProfileFinished"] = function(messageObject)
  {
    InspectorTest.log("FAIL: unexpected profile received " + JSON.stringify(messageObject, null, 4));
  }
}
function allowConsoleProfiles()
{
  InspectorTest.eventHandler["Profiler.consoleProfileStarted"] = function(messageObject)
  {
    InspectorTest.log("PASS: console initiated profile started");
  }
  InspectorTest.eventHandler["Profiler.consoleProfileFinished"] = function(messageObject)
  {
    InspectorTest.log("PASS: console initiated profile received");
  }
}
function didFailToStartWhenDisabled(messageObject)
{
  if (!InspectorTest.expectedError("didFailToStartWhenDisabled", messageObject))
    return;
  allowConsoleProfiles();
  InspectorTest.sendCommand("Profiler.enable", {});
  InspectorTest.sendCommand("Profiler.start", {}, didStartFrontendProfile);
}
function didStartFrontendProfile(messageObject)
{
  if (!InspectorTest.expectedSuccess("didStartFrontendProfile", messageObject))
    return;
  InspectorTest.sendCommand("Runtime.evaluate", {expression: "console.profile('p1');"}, didStartConsoleProfile);
}

function didStartConsoleProfile(messageObject)
{
  if (!InspectorTest.expectedSuccess("didStartConsoleProfile", messageObject))
    return;
  InspectorTest.sendCommand("Profiler.disable", {}, didDisableProfiler);
}

function didDisableProfiler(messageObject)
{
  if (!InspectorTest.expectedSuccess("didDisableProfiler", messageObject))
    return;
  InspectorTest.sendCommand("Profiler.enable", {});
  InspectorTest.sendCommand("Profiler.stop", {}, didStopFrontendProfile);
}

function didStopFrontendProfile(messageObject)
{
  if (!InspectorTest.expectedError("no front-end initiated profiles found", messageObject))
    return;
  disallowConsoleProfiles();
  InspectorTest.sendCommand("Runtime.evaluate", {expression: "console.profileEnd();"}, didStopConsoleProfile);
}

function didStopConsoleProfile(messageObject)
{
  if (!InspectorTest.expectedSuccess("didStopConsoleProfile", messageObject))
    return;
  InspectorTest.completeTest();
}
