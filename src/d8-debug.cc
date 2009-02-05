// Copyright 2008 the V8 project authors. All rights reserved.
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


#include "d8.h"
#include "d8-debug.h"


namespace v8 {


void HandleDebugEvent(DebugEvent event,
                      Handle<Object> exec_state,
                      Handle<Object> event_data,
                      Handle<Value> data) {
  HandleScope scope;

  // Check for handled event.
  if (event != Break && event != Exception && event != AfterCompile) {
    return;
  }

  TryCatch try_catch;

  // Print the event details.
  Handle<String> details = Shell::DebugEventToText(event_data);
  if (details->Length() == 0) {
    // Empty string is used to signal not to process this event.
    return;
  }
  String::Utf8Value str(details);
  printf("%s\n", *str);

  // Get the debug command processor.
  Local<String> fun_name = String::New("debugCommandProcessor");
  Local<Function> fun = Function::Cast(*exec_state->Get(fun_name));
  Local<Object> cmd_processor =
      Object::Cast(*fun->Call(exec_state, 0, NULL));
  if (try_catch.HasCaught()) {
    Shell::ReportException(&try_catch);
    return;
  }

  static const int kBufferSize = 256;
  bool running = false;
  while (!running) {
    char command[kBufferSize];
    printf("dbg> ");
    char* str = fgets(command, kBufferSize, stdin);
    if (str == NULL) break;

    // Ignore empty commands.
    if (strlen(command) == 0) continue;

    TryCatch try_catch;

    // Convert the debugger command to a JSON debugger request.
    Handle<Value> request =
        Shell::DebugCommandToJSONRequest(String::New(command));
    if (try_catch.HasCaught()) {
      Shell::ReportException(&try_catch);
      continue;
    }

    // If undefined is returned the command was handled internally and there is
    // no JSON to send.
    if (request->IsUndefined()) {
      continue;
    }

    Handle<String> fun_name;
    Handle<Function> fun;
    // All the functions used below take one argument.
    static const int kArgc = 1;
    Handle<Value> args[kArgc];

    // Invoke the JavaScript to convert the debug command line to a JSON
    // request, invoke the JSON request and convert the JSON respose to a text
    // representation.
    fun_name = String::New("processDebugRequest");
    fun = Handle<Function>::Cast(cmd_processor->Get(fun_name));
    args[0] = request;
    Handle<Value> response_val = fun->Call(cmd_processor, kArgc, args);
    if (try_catch.HasCaught()) {
      Shell::ReportException(&try_catch);
      continue;
    }
    Handle<String> response = Handle<String>::Cast(response_val);

    // Convert the debugger response into text details and the running state.
    Handle<Object> response_details = Shell::DebugResponseDetails(response);
    if (try_catch.HasCaught()) {
      Shell::ReportException(&try_catch);
      continue;
    }
    String::Utf8Value text_str(response_details->Get(String::New("text")));
    if (text_str.length() > 0) {
      printf("%s\n", *text_str);
    }
    running =
        response_details->Get(String::New("running"))->ToBoolean()->Value();
  }
}


}  // namespace v8
