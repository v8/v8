// Copyright 2008 Google Inc. All Rights Reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --expose-debug-as debug
// The functions used for testing backtraces. They are at the top to make the
// testing of source line/column easier.
function f(x, y) {
  a=1;
};

function g() {
  new f(1);
};


// Get the Debug object exposed from the debug context global object.
Debug = debug.Debug

listenerCalled = false;
exception = false;

function safeEval(code) {
  try {
    return eval('(' + code + ')');
  } catch (e) {
    return undefined;
  }
}

function listener(event, exec_state, event_data, data) {
  try {
  if (event == Debug.DebugEvent.Break)
  {
    // The expected backtrace is
    // 0: f
    // 1: g
    // 2: [anonymous]
    
    // Get the debug command processor.
    var dcp = exec_state.debugCommandProcessor();

    // Get the backtrace.
    var json;
    json = '{"seq":0,"type":"request","command":"backtrace"}'
    var backtrace = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals(0, backtrace.fromFrame);
    assertEquals(3, backtrace.toFrame);
    assertEquals(3, backtrace.totalFrames);
    var frames = backtrace.frames;
    assertEquals(3, frames.length);
    for (var i = 0; i < frames.length; i++) {
      assertEquals('frame', frames[i].type);
    }
    assertEquals(0, frames[0].index);
    assertEquals("f", frames[0].func.name);
    assertEquals(1, frames[1].index);
    assertEquals("g", frames[1].func.name);
    assertEquals(2, frames[2].index);
    assertEquals("", frames[2].func.name);

    // Get backtrace with two frames.
    json = '{"seq":0,"type":"request","command":"backtrace","arguments":{"fromFrame":1,"toFrame":3}}'
    var backtrace = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals(1, backtrace.fromFrame);
    assertEquals(3, backtrace.toFrame);
    assertEquals(3, backtrace.totalFrames);
    var frames = backtrace.frames;
    assertEquals(2, frames.length);
    for (var i = 0; i < frames.length; i++) {
      assertEquals('frame', frames[i].type);
    }
    assertEquals(1, frames[0].index);
    assertEquals("g", frames[0].func.name);
    assertEquals(2, frames[1].index);
    assertEquals("", frames[1].func.name);

    // Get the individual frames.
    var frame;
    json = '{"seq":0,"type":"request","command":"frame"}'
    frame = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals(0, frame.index);
    assertEquals("f", frame.func.name);
    assertTrue(frame.constructCall);
    assertEquals(31, frame.line);
    assertEquals(3, frame.column);
    assertEquals(2, frame.arguments.length);
    assertEquals('x', frame.arguments[0].name);
    assertEquals('number', frame.arguments[0].value.type);
    assertEquals(1, frame.arguments[0].value.value);
    assertEquals('y', frame.arguments[1].name);
    assertEquals('undefined', frame.arguments[1].value.type);

    json = '{"seq":0,"type":"request","command":"frame","arguments":{"number":0}}'
    frame = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals(0, frame.index);
    assertEquals("f", frame.func.name);
    assertEquals(31, frame.line);
    assertEquals(3, frame.column);
    assertEquals(2, frame.arguments.length);
    assertEquals('x', frame.arguments[0].name);
    assertEquals('number', frame.arguments[0].value.type);
    assertEquals(1, frame.arguments[0].value.value);
    assertEquals('y', frame.arguments[1].name);
    assertEquals('undefined', frame.arguments[1].value.type);

    json = '{"seq":0,"type":"request","command":"frame","arguments":{"number":1}}'
    frame = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals(1, frame.index);
    assertEquals("g", frame.func.name);
    assertFalse(frame.constructCall);
    assertEquals(35, frame.line);
    assertEquals(2, frame.column);
    assertEquals(0, frame.arguments.length);

    json = '{"seq":0,"type":"request","command":"frame","arguments":{"number":2}}'
    frame = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals(2, frame.index);
    assertEquals("", frame.func.name);

    // Source slices for the individual frames (they all refer to this script).
    json = '{"seq":0,"type":"request","command":"source",' +
            '"arguments":{"frame":0,"fromLine":30,"toLine":32}}'
    source = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals("function f(x, y) {", source.source.substring(0, 18));
    assertEquals(30, source.fromLine);
    assertEquals(32, source.toLine);
    
    json = '{"seq":0,"type":"request","command":"source",' +
            '"arguments":{"frame":1,"fromLine":31,"toLine":32}}'
    source = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals("  a=1;", source.source.substring(0, 6));
    assertEquals(31, source.fromLine);
    assertEquals(32, source.toLine);
    
    json = '{"seq":0,"type":"request","command":"source",' +
            '"arguments":{"frame":2,"fromLine":35,"toLine":36}}'
    source = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals("  new f(1);", source.source.substring(0, 11));
    assertEquals(35, source.fromLine);
    assertEquals(36, source.toLine);
    
    // Test line interval way beyond this script will result in an error.
    json = '{"seq":0,"type":"request","command":"source",' +
            '"arguments":{"frame":0,"fromLine":10000,"toLine":20000}}'
    response = safeEval(dcp.processDebugJSONRequest(json));
    assertFalse(response.success);
    
    // Test without arguments.
    json = '{"seq":0,"type":"request","command":"source"}'
    source = safeEval(dcp.processDebugJSONRequest(json)).body;
    assertEquals(Debug.findScript(f).source, source.source);
    
    listenerCalled = true;
  }
  } catch (e) {
    exception = e
  };
};

// Add the debug event listener.
Debug.addListener(listener);

// Set a break point and call to invoke the debug event listener.
Debug.setBreakPoint(f, 0, 0);
g();

// Make sure that the debug event listener vas invoked.
assertFalse(exception, "exception in listener");
assertTrue(listenerCalled);

