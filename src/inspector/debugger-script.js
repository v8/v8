/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
"use strict";

(function () {

var DebuggerScript = {};

/**
 * @param {!ExecutionState} execState
 * @param {!BreakpointInfo} info
 * @return {string|undefined}
 */
DebuggerScript.setBreakpoint = function(execState, info)
{
    var breakId = Debug.setScriptBreakPointById(info.sourceID, info.lineNumber, info.columnNumber, info.condition, undefined);
    var locations = Debug.findBreakPointActualLocations(breakId);
    if (!locations.length)
        return undefined;
    info.lineNumber = locations[0].line;
    info.columnNumber = locations[0].column;
    return breakId.toString();
}

/**
 * @param {!ExecutionState} execState
 * @param {!{breakpointId: number}} info
 */
DebuggerScript.removeBreakpoint = function(execState, info)
{
    Debug.findBreakPoint(info.breakpointId, true);
}

/**
 * @param {!ExecutionState} execState
 */
DebuggerScript.clearBreakpoints = function(execState)
{
    Debug.clearAllBreakPoints();
}

/**
 * @param {!Array<!BreakPoint>|undefined} breakpoints
 */
DebuggerScript.getBreakpointNumbers = function(breakpoints)
{
    var numbers = [];
    if (!breakpoints)
        return numbers;

    for (var i = 0; i < breakpoints.length; i++) {
        var breakpoint = breakpoints[i];
        var scriptBreakPoint = breakpoint.script_break_point();
        numbers.push(scriptBreakPoint ? scriptBreakPoint.number() : breakpoint.number());
    }
    return numbers;
}

return DebuggerScript;
})();
