// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

// If true, prints all messages sent and received by inspector.
const printProtocolMessages = false;

// The active wrapper instance.
let activeWrapper = undefined;

// Receiver function called by inspector, delegating to active wrapper.
function receive(message) {
  activeWrapper.receiveMessage(message);
}

class DebugWrapper {
  constructor() {
    // Message dictionary storing {id, message} pairs.
    this.receivedMessages = new Map();

    // Each message dispatched by the Debug wrapper is assigned a unique number
    // using nextMessageId.
    this.nextMessageId = 0;

    // The listener method called on certain events.
    this.listener = undefined;

    // TODO(jgruber): Determine which of these are still required and possible.
    // Debug events which can occur in the V8 JavaScript engine.
    this.DebugEvent = { Break: 1,
                        Exception: 2,
                        NewFunction: 3,
                        BeforeCompile: 4,
                        AfterCompile: 5,
                        CompileError: 6,
                        AsyncTaskEvent: 7
                      };

    // The different types of steps.
    this.StepAction = { StepOut: 0,
                        StepNext: 1,
                        StepIn: 2,
                        StepFrame: 3,
                      };

    // A copy of the scope types from runtime-debug.cc.
    // NOTE: these constants should be backward-compatible, so
    // add new ones to the end of this list.
    this.ScopeType = { Global:  0,
                       Local:   1,
                       With:    2,
                       Closure: 3,
                       Catch:   4,
                       Block:   5,
                       Script:  6,
                       Eval:    7,
                       Module:  8
                     };

    // Store the current script id so we can skip corresponding break events.
    this.thisScriptId = %FunctionGetScriptId(receive);

    // Register as the active wrapper.
    assertTrue(activeWrapper === undefined);
    activeWrapper = this;
  }

  enable() { this.sendMessageForMethodChecked("Debugger.enable"); }
  disable() { this.sendMessageForMethodChecked("Debugger.disable"); }

  setListener(listener) { this.listener = listener; }

  stepOver() { this.sendMessageForMethodChecked("Debugger.stepOver"); }
  stepInto() { this.sendMessageForMethodChecked("Debugger.stepInto"); }
  stepOut() { this.sendMessageForMethodChecked("Debugger.stepOut"); }

  // Returns the resulting breakpoint id.
  setBreakPoint(func, opt_line, opt_column, opt_condition) {
    assertTrue(%IsFunction(func));
    assertFalse(%FunctionIsAPIFunction(func));

    // TODO(jgruber): We handle only script breakpoints for now.
    // TODO(jgruber): Handle conditions.

    const scriptid = %FunctionGetScriptId(func);
    assertTrue(scriptid != -1);

    const offset = %FunctionGetScriptSourcePosition(func);
    const loc =
      %ScriptLocationFromLine2(scriptid, opt_line, opt_column, offset);

    const {msgid, msg} = this.createMessage(
        "Debugger.setBreakpoint",
        { location : { scriptId : scriptid.toString(),
                       lineNumber : loc.line,
                       columnNumber : loc.column
                     }
        });
    this.sendMessage(msg);

    const reply = this.takeReplyChecked(msgid);
    assertTrue(reply.result !== undefined);
    const breakid = reply.result.breakpointId;
    assertTrue(breakid !== undefined);

    return breakid;
  }

  clearBreakPoint(breakid) {
    const {msgid, msg} = this.createMessage(
        "Debugger.removeBreakpoint", { breakpointId : breakid });
    this.sendMessage(msg);
    this.takeReplyChecked(msgid);
  }

  // Returns the serialized result of the given expression. For example:
  // {"type":"number", "value":33, "description":"33"}.
  evaluate(frameid, expression) {
    const {msgid, msg} = this.createMessage(
        "Debugger.evaluateOnCallFrame",
        { callFrameId : frameid,
          expression : expression
        });
    this.sendMessage(msg);

    const reply = this.takeReplyChecked(msgid);
    return reply.result.result;
  }

  // --- Internal methods. -----------------------------------------------------

  getNextMessageId() {
    return this.nextMessageId++;
  }

  createMessage(method, params) {
    const id = this.getNextMessageId();
    const msg = JSON.stringify({
      id: id,
      method: method,
      params: params,
    });
    return { msgid : id, msg: msg };
  }

  receiveMessage(message) {
    if (printProtocolMessages) print(message);

    const parsedMessage = JSON.parse(message);
    if (parsedMessage.id !== undefined) {
      this.receivedMessages.set(parsedMessage.id, parsedMessage);
    }

    this.dispatchMessage(parsedMessage);
  }

  sendMessage(message) {
    if (printProtocolMessages) print(message);
    send(message);
  }

  sendMessageForMethodChecked(method) {
    const {msgid, msg} = this.createMessage(method);
    this.sendMessage(msg);
    this.takeReplyChecked(msgid);
  }

  takeReplyChecked(msgid) {
    const reply = this.receivedMessages.get(msgid);
    assertTrue(reply !== undefined);
    this.receivedMessages.delete(msgid);
    return reply;
  }

  execStatePrepareStep(action) {
    switch(action) {
      case this.StepAction.StepOut: this.stepOut(); break;
      case this.StepAction.StepNext: this.stepOver(); break;
      case this.StepAction.StepIn: this.stepInto(); break;
      default: %AbortJS("Unsupported StepAction"); break;
    }
  }

  execStateScope(scope) {
    // TODO(jgruber): Mapping
    return { scopeType: () => scope.type,
             scopeObject: () => scope.object
           };
  }

  execStateFrame(frame) {
    const scriptid = parseInt(frame.location.scriptId);
    const line = frame.location.lineNumber;
    const column = frame.location.columnNumber;
    const loc = %ScriptLocationFromLine2(scriptid, line, column, 0);
    const func = { name : () => frame.functionName };
    return { sourceLineText : () => loc.sourceText,
             functionName : () => frame.functionName,
             func : () => func,
             scopeCount : () => frame.scopeChain.length,
             scope : (index) => this.execStateScope(frame.scopeChain[index])
           };
  }

  // --- Message handlers. -----------------------------------------------------

  dispatchMessage(message) {
    const method = message.method;
    if (method == "Debugger.paused") {
      this.handleDebuggerPaused(message);
    } else if (method == "Debugger.scriptParsed") {
      this.handleDebuggerScriptParsed(message);
    }
  }

  handleDebuggerPaused(message) {
    const params = message.params;

    // Skip break events in this file.
    if (params.callFrames[0].location.scriptId == this.thisScriptId) return;

    // TODO(jgruber): Arguments as needed.
    let execState = { frames : params.callFrames,
                      prepareStep : this.execStatePrepareStep.bind(this),
                      frame : (index) => this.execStateFrame(
                          index ? params.callFrames[index]
                                : params.callFrames[0]),
                      frameCount : () => params.callFrames.length
                    };
    let eventData = this.execStateFrame(params.callFrames[0]);
    this.invokeListener(this.DebugEvent.Break, execState, eventData);
  }

  handleDebuggerScriptParsed(message) {
    const params = message.params;
    let eventData = { scriptId : params.scriptId,
                      eventType : this.DebugEvent.AfterCompile
                    }

    // TODO(jgruber): Arguments as needed. Still completely missing exec_state,
    // and eventData used to contain the script mirror instead of its id.
    this.invokeListener(this.DebugEvent.AfterCompile, undefined, eventData,
                        undefined);
  }

  invokeListener(event, exec_state, event_data, data) {
    if (this.listener) {
      this.listener(event, exec_state, event_data, data);
    }
  }
}

// Simulate the debug object generated by --expose-debug-as debug.
var debug = { instance : undefined };
Object.defineProperty(debug, 'Debug', { get: function() {
  if (!debug.instance) {
    debug.instance = new DebugWrapper();
    debug.instance.enable();
  }
  return debug.instance;
}});
