// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/d8.h"
#include "src/d8-debug.h"

namespace v8 {

void PrintPrompt(bool is_running) {
  const char* prompt = is_running? "> " : "dbg> ";
  printf("%s", prompt);
  fflush(stdout);
}


void HandleDebugEvent(const Debug::EventDetails& event_details) {
  // TODO(svenpanne) There should be a way to retrieve this in the callback.
  Isolate* isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);

  DebugEvent event = event_details.GetEvent();
  // Check for handled event.
  if (event != Break && event != Exception && event != AfterCompile) {
    return;
  }

  TryCatch try_catch(isolate);

  // Get the toJSONProtocol function on the event and get the JSON format.
  Local<String> to_json_fun_name =
      String::NewFromUtf8(isolate, "toJSONProtocol", NewStringType::kNormal)
          .ToLocalChecked();
  Local<Object> event_data = event_details.GetEventData();
  Local<Function> to_json_fun =
      Local<Function>::Cast(event_data->Get(isolate->GetCurrentContext(),
                                            to_json_fun_name).ToLocalChecked());
  Local<Value> event_json;
  if (!to_json_fun->Call(isolate->GetCurrentContext(), event_data, 0, NULL)
           .ToLocal(&event_json)) {
    Shell::ReportException(isolate, &try_catch);
    return;
  }

  // Print the event details.
  Local<Object> details =
      Shell::DebugMessageDetails(isolate, Local<String>::Cast(event_json));
  if (try_catch.HasCaught()) {
    Shell::ReportException(isolate, &try_catch);
    return;
  }
  String::Utf8Value str(
      details->Get(isolate->GetCurrentContext(),
                   String::NewFromUtf8(isolate, "text", NewStringType::kNormal)
                       .ToLocalChecked()).ToLocalChecked());
  if (str.length() == 0) {
    // Empty string is used to signal not to process this event.
    return;
  }
  printf("%s\n", *str);

  // Get the debug command processor.
  Local<String> fun_name =
      String::NewFromUtf8(isolate, "debugCommandProcessor",
                          NewStringType::kNormal).ToLocalChecked();
  Local<Object> exec_state = event_details.GetExecutionState();
  Local<Function> fun = Local<Function>::Cast(
      exec_state->Get(isolate->GetCurrentContext(), fun_name).ToLocalChecked());
  Local<Value> cmd_processor_value;
  if (!fun->Call(isolate->GetCurrentContext(), exec_state, 0, NULL)
           .ToLocal(&cmd_processor_value)) {
    Shell::ReportException(isolate, &try_catch);
    return;
  }
  Local<Object> cmd_processor = Local<Object>::Cast(cmd_processor_value);

  static const int kBufferSize = 256;
  bool running = false;
  while (!running) {
    char command[kBufferSize];
    PrintPrompt(running);
    char* str = fgets(command, kBufferSize, stdin);
    if (str == NULL) break;

    // Ignore empty commands.
    if (strlen(command) == 0) continue;

    TryCatch try_catch(isolate);

    // Convert the debugger command to a JSON debugger request.
    Local<Value> request = Shell::DebugCommandToJSONRequest(
        isolate, String::NewFromUtf8(isolate, command, NewStringType::kNormal)
                     .ToLocalChecked());
    if (try_catch.HasCaught()) {
      Shell::ReportException(isolate, &try_catch);
      continue;
    }

    // If undefined is returned the command was handled internally and there is
    // no JSON to send.
    if (request->IsUndefined()) {
      continue;
    }

    Local<String> fun_name;
    Local<Function> fun;
    // All the functions used below take one argument.
    static const int kArgc = 1;
    Local<Value> args[kArgc];

    // Invoke the JavaScript to convert the debug command line to a JSON
    // request, invoke the JSON request and convert the JSON respose to a text
    // representation.
    fun_name = String::NewFromUtf8(isolate, "processDebugRequest",
                                   NewStringType::kNormal).ToLocalChecked();
    fun = Local<Function>::Cast(cmd_processor->Get(isolate->GetCurrentContext(),
                                                   fun_name).ToLocalChecked());
    args[0] = request;
    Local<Value> response_val;
    if (!fun->Call(isolate->GetCurrentContext(), cmd_processor, kArgc, args)
             .ToLocal(&response_val)) {
      Shell::ReportException(isolate, &try_catch);
      continue;
    }
    Local<String> response = Local<String>::Cast(response_val);

    // Convert the debugger response into text details and the running state.
    Local<Object> response_details =
        Shell::DebugMessageDetails(isolate, response);
    if (try_catch.HasCaught()) {
      Shell::ReportException(isolate, &try_catch);
      continue;
    }
    String::Utf8Value text_str(
        response_details->Get(isolate->GetCurrentContext(),
                              String::NewFromUtf8(isolate, "text",
                                                  NewStringType::kNormal)
                                  .ToLocalChecked()).ToLocalChecked());
    if (text_str.length() > 0) {
      printf("%s\n", *text_str);
    }
    running = response_details->Get(isolate->GetCurrentContext(),
                                    String::NewFromUtf8(isolate, "running",
                                                        NewStringType::kNormal)
                                        .ToLocalChecked())
                  .ToLocalChecked()
                  ->ToBoolean(isolate->GetCurrentContext())
                  .ToLocalChecked()
                  ->Value();
  }
}

}  // namespace v8
