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
    this.receivedMessages = {};

    // Each message dispatched by the Debug wrapper is assigned a unique number
    // using nextMessageId.
    this.nextMessageId = 0;

    // The listener method called on certain events.
    this.listener = undefined;

    // TODO(jgruber): Determine which of these are still required and possible.
    // Debug events which can occur in the V8 JavaScript engine.
    this.DebugEvent = { Break: 1
                      , Exception: 2
                      , NewFunction: 3
                      , BeforeCompile: 4
                      , AfterCompile: 5
                      , CompileError: 6
                      , AsyncTaskEvent: 7
                      };

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
        { location : { scriptId : scriptid.toString()
                     , lineNumber : loc.line
                     , columnNumber : loc.column
                     }
        });
    this.sendMessage(msg);

    const reply = this.receivedMessages[msgid];
    const breakid = reply.result.breakpointId;
    assertTrue(breakid !== undefined);

    return breakid;
  }

  clearBreakPoint(breakid) {
    const {msgid, msg} = this.createMessage(
        "Debugger.removeBreakpoint", { breakpointId : breakid });
    this.sendMessage(msg);
    assertTrue(this.receivedMessages[msgid] !== undefined);
  }

  // Returns the serialized result of the given expression. For example:
  // {"type":"number", "value":33, "description":"33"}.
  evaluate(frameid, expression) {
    const {msgid, msg} = this.createMessage(
        "Debugger.evaluateOnCallFrame",
        { callFrameId : frameid
        , expression : expression
        });
    this.sendMessage(msg);

    const reply = this.receivedMessages[msgid];
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
    return {msgid: id, msg: msg};
  }

  receiveMessage(message) {
    if (printProtocolMessages) print(message);

    const parsedMessage = JSON.parse(message);
    if (parsedMessage.id !== undefined) {
      this.receivedMessages[parsedMessage.id] = parsedMessage;
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
    assertTrue(this.receivedMessages[msgid] !== undefined);
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

    // TODO(jgruber): Arguments as needed.
    let execState = { frames: params.callFrames };
    this.invokeListener(this.DebugEvent.Break, execState);
  }

  handleDebuggerScriptParsed(message) {
    const params = message.params;
    let eventData = { scriptId : params.scriptId
                    , eventType : this.DebugEvent.AfterCompile
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
