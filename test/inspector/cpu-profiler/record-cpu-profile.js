// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

print("Test that profiler is able to record a profile. Also it tests that profiler returns an error when it unable to find the profile.");

InspectorTest.sendCommand("Profiler.enable", {});
InspectorTest.sendCommand("Profiler.start", {}, didStartFrontendProfile);
function didStartFrontendProfile(messageObject)
{
  if (!InspectorTest.expectedSuccess("startFrontendProfile", messageObject))
    return;
  InspectorTest.sendCommand("Runtime.evaluate", {expression: "console.profile('Profile 1');"}, didStartConsoleProfile);
}

function didStartConsoleProfile(messageObject)
{
  if (!InspectorTest.expectedSuccess("startConsoleProfile", messageObject))
    return;
  InspectorTest.sendCommand("Runtime.evaluate", {expression: "console.profileEnd('Profile 1');"}, didStopConsoleProfile);
}

function didStopConsoleProfile(messageObject)
{
  if (!InspectorTest.expectedSuccess("stopConsoleProfile", messageObject))
    return;
  InspectorTest.sendCommand("Profiler.stop", {}, didStopFrontendProfile);
}

function didStopFrontendProfile(messageObject)
{
  if (!InspectorTest.expectedSuccess("stoppedFrontendProfile", messageObject))
    return;
  InspectorTest.sendCommand("Profiler.start", {}, didStartFrontendProfile2);
}

function didStartFrontendProfile2(messageObject)
{
  if (!InspectorTest.expectedSuccess("startFrontendProfileSecondTime", messageObject))
    return;
  InspectorTest.sendCommand("Profiler.stop", {}, didStopFrontendProfile2);
}

function didStopFrontendProfile2(messageObject)
{
  InspectorTest.expectedSuccess("stopFrontendProfileSecondTime", messageObject)
  InspectorTest.completeTest();
}
