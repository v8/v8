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

// TODO(jgruber): Determine which of these are still required and possible.
// Debug events which can occur in the V8 JavaScript engine.
const DebugEvent = { Break: 1,
                     Exception: 2,
                     NewFunction: 3,
                     BeforeCompile: 4,
                     AfterCompile: 5,
                     CompileError: 6,
                     AsyncTaskEvent: 7 };

class DebugWrapper {
  constructor() {
    // Message dictionary storing {id, message} pairs.
    this.receivedMessages = {};

    // Each message dispatched by the Debug wrapper is assigned a unique number
    // using nextMessageId.
    this.nextMessageId = 0;

    // The listener method called on certain events.
    this.listener = () => undefined;

    // Register as the active wrapper.
    assertTrue(activeWrapper === undefined);
    activeWrapper = this;
  }

  enable() {
    const {msgid, msg} = this.createMessage("Debugger.enable");
    this.sendMessage(msg);
    assertTrue(this.receivedMessages[msgid] !== undefined);
  }

  disable() {
    const {msgid, msg} = this.createMessage("Debugger.disable");
    this.sendMessage(msg);
    assertTrue(this.receivedMessages[msgid] !== undefined);
  }

  setListener(listener) {
    this.listener = listener;
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

  // --- Message handlers. -----------------------------------------------------

  dispatchMessage(message) {
    const method = message.method;
    if (method == "Debugger.scriptParsed") {
      this.handleDebuggerScriptParsed(message);
    }
  }

  handleDebuggerScriptParsed(message) {
    const params = message.params;
    let eventData = { scriptId : params.scriptId
                    , eventType : DebugEvent.AfterCompile
                    }

    // TODO(jgruber): Arguments as needed. Still completely missing exec_state,
    // and eventData used to contain the script mirror instead of its id.
    this.listener(DebugEvent.AfterCompile, undefined, eventData, undefined);
  }
}
