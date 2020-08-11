// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let {session, contextGroup, Protocol} = InspectorTest.start(
    'Tests that stepOver and stepInto correctly handle skipLists.');

function test(input) {
  debugger;
  var a = 4;
  var sum = 0;
  if (input > 0) {
    sum = a + input;
  }
  var b = 5;
  sum = add(sum, b);
  return sum;
}

function add(a, b) {
  return a + b;
}

contextGroup.addScript(`${test} //# sourceURL=test.js`);
contextGroup.addScript(`${add}`);

const first_non_debug_line_offset = 2;
const last_line_line_offset = 9;
const function_call_line_offset = 8;
const function_call_column_offset = 2;
const if_case_line_offset = 4;

Protocol.Debugger.enable();
runTest()
    .catch(reason => InspectorTest.log(`Failed: ${reason}`))
    .then(InspectorTest.completeTest);

async function runTest() {
  const response = await Protocol.Debugger.onceScriptParsed();
  const scriptId = response.params.scriptId;

  await checkValidSkipLists(scriptId);
  await checkInvalidSkipLists(scriptId);
}

async function checkInvalidSkipLists(scriptId) {
  InspectorTest.log('Test: start position has invalid column number');
  let skipList = [createLocationRange(
      scriptId, first_non_debug_line_offset, -1, last_line_line_offset, 0)];
  await testStepOver(skipList);

  InspectorTest.log('Test: start position has invalid line number');
  skipList =
      [createLocationRange(scriptId, -1, 0, first_non_debug_line_offset, 0)];
  await testStepOver(skipList);

  InspectorTest.log('Test: end position smaller than start position');
  skipList = [createLocationRange(
      scriptId, if_case_line_offset, 0, first_non_debug_line_offset, 0)];
  await testStepOver(skipList);

  InspectorTest.log('Test: skip list is not maximally merged');
  skipList = [
    createLocationRange(
        scriptId, first_non_debug_line_offset, 0, if_case_line_offset, 0),
    createLocationRange(
        scriptId, if_case_line_offset, 0, last_line_line_offset, 0)
  ];
  await testStepOver(skipList);

  InspectorTest.log('Test: skip list is not sorted');
  skipList = [
    createLocationRange(
        scriptId, function_call_line_offset, 0, last_line_line_offset, 0),
    createLocationRange(
        scriptId, first_non_debug_line_offset, 0, if_case_line_offset, 0)
  ];
  await testStepOver(skipList);
}

async function checkValidSkipLists(scriptId) {
  InspectorTest.log('Test: No skip list');
  await testStepOver([]);

  InspectorTest.log('Test: Skip lines');
  let skipList = [
    createLocationRange(
        scriptId, first_non_debug_line_offset, 0, if_case_line_offset, 0),
    createLocationRange(
        scriptId, function_call_line_offset, 0, last_line_line_offset, 0)
  ];
  await testStepOver(skipList);

  InspectorTest.log('Test: Start location is inclusive');
  skipList = [createLocationRange(
      scriptId, function_call_line_offset, function_call_column_offset,
      last_line_line_offset, 0)];
  await testStepOver(skipList);

  InspectorTest.log('Test: End location is exclusive');
  skipList = [createLocationRange(
      scriptId, first_non_debug_line_offset, 0, function_call_line_offset,
      function_call_column_offset)];
  await testStepOver(skipList);
}

async function testStepOver(skipList) {
  InspectorTest.log(
      `Testing step over with skipList: ${JSON.stringify(skipList)}`);
  Protocol.Runtime.evaluate({expression: 'test(5);'});
  while (true) {
    const pausedMsg = await Protocol.Debugger.oncePaused();
    const topCallFrame = pausedMsg.params.callFrames[0];
    printCallFrame(topCallFrame);
    if (topCallFrame.location.lineNumber == last_line_line_offset) break;

    const stepOverMsg = await Protocol.Debugger.stepOver({skipList});
    if (stepOverMsg.error) {
      InspectorTest.log(stepOverMsg.error.message);
      Protocol.Debugger.resume();
      return;
    }
  }
  await Protocol.Debugger.resume();
}

function createLocationRange(
    scriptId, startLine, startColumn, endLine, endColumn) {
  return {
    scriptId: scriptId,
        start: {lineNumber: startLine, columnNumber: startColumn},
        end: {lineNumber: endLine, columnNumber: endColumn}
  }
}

function printCallFrame(frame) {
  InspectorTest.log(
      frame.functionName + ': ' + frame.location.lineNumber + ':' +
      frame.location.columnNumber);
}
