// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/stack-frame-info.h"

#include "src/objects/stack-frame-info-inl.h"

namespace v8 {
namespace internal {

// static
int StackTraceFrame::GetLineNumber(Handle<StackTraceFrame> frame) {
  int line = GetFrameInfo(frame)->line_number();
  return line != StackFrameBase::kNone ? line : Message::kNoLineNumberInfo;
}

// static
int StackTraceFrame::GetColumnNumber(Handle<StackTraceFrame> frame) {
  int column = GetFrameInfo(frame)->column_number();
  return column != StackFrameBase::kNone ? column : Message::kNoColumnInfo;
}

// static
int StackTraceFrame::GetScriptId(Handle<StackTraceFrame> frame) {
  int id = GetFrameInfo(frame)->script_id();
  return id != StackFrameBase::kNone ? id : Message::kNoScriptIdInfo;
}

// static
int StackTraceFrame::GetPromiseAllIndex(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->promise_all_index();
}

// static
Handle<Object> StackTraceFrame::GetFileName(Handle<StackTraceFrame> frame) {
  auto name = GetFrameInfo(frame)->script_name();
  return handle(name, frame->GetIsolate());
}

// static
Handle<Object> StackTraceFrame::GetScriptNameOrSourceUrl(
    Handle<StackTraceFrame> frame) {
  auto name = GetFrameInfo(frame)->script_name_or_source_url();
  return handle(name, frame->GetIsolate());
}

// static
Handle<Object> StackTraceFrame::GetFunctionName(Handle<StackTraceFrame> frame) {
  auto name = GetFrameInfo(frame)->function_name();
  return handle(name, frame->GetIsolate());
}

// static
Handle<Object> StackTraceFrame::GetMethodName(Handle<StackTraceFrame> frame) {
  auto name = GetFrameInfo(frame)->method_name();
  return handle(name, frame->GetIsolate());
}

// static
Handle<Object> StackTraceFrame::GetTypeName(Handle<StackTraceFrame> frame) {
  auto name = GetFrameInfo(frame)->type_name();
  return handle(name, frame->GetIsolate());
}

// static
Handle<Object> StackTraceFrame::GetEvalOrigin(Handle<StackTraceFrame> frame) {
  auto origin = GetFrameInfo(frame)->eval_origin();
  return handle(origin, frame->GetIsolate());
}

// static
Handle<Object> StackTraceFrame::GetWasmModuleName(
    Handle<StackTraceFrame> frame) {
  auto module = GetFrameInfo(frame)->wasm_module_name();
  return handle(module, frame->GetIsolate());
}

// static
bool StackTraceFrame::IsEval(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->is_eval();
}

// static
bool StackTraceFrame::IsConstructor(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->is_constructor();
}

// static
bool StackTraceFrame::IsWasm(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->is_wasm();
}

// static
bool StackTraceFrame::IsAsmJsWasm(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->is_asmjs_wasm();
}

// static
bool StackTraceFrame::IsUserJavaScript(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->is_user_java_script();
}

// static
bool StackTraceFrame::IsToplevel(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->is_toplevel();
}

// static
bool StackTraceFrame::IsAsync(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->is_async();
}

// static
bool StackTraceFrame::IsPromiseAll(Handle<StackTraceFrame> frame) {
  return GetFrameInfo(frame)->is_promise_all();
}

// static
Handle<StackFrameInfo> StackTraceFrame::GetFrameInfo(
    Handle<StackTraceFrame> frame) {
  if (frame->frame_info().IsUndefined()) InitializeFrameInfo(frame);
  return handle(StackFrameInfo::cast(frame->frame_info()), frame->GetIsolate());
}

// static
void StackTraceFrame::InitializeFrameInfo(Handle<StackTraceFrame> frame) {
  Isolate* isolate = frame->GetIsolate();
  Handle<StackFrameInfo> frame_info = isolate->factory()->NewStackFrameInfo(
      handle(FrameArray::cast(frame->frame_array()), isolate),
      frame->frame_index());
  frame->set_frame_info(*frame_info);

  // After initializing, we no longer need to keep a reference
  // to the frame_array.
  frame->set_frame_array(ReadOnlyRoots(isolate).undefined_value());
  frame->set_frame_index(-1);
}

Handle<FrameArray> GetFrameArrayFromStackTrace(Isolate* isolate,
                                               Handle<FixedArray> stack_trace) {
  // For the empty case, a empty FrameArray needs to be allocated so the rest
  // of the code doesn't has to be special cased everywhere.
  if (stack_trace->length() == 0) {
    return isolate->factory()->NewFrameArray(0);
  }

  // Retrieve the FrameArray from the first StackTraceFrame.
  DCHECK_GT(stack_trace->length(), 0);
  Handle<StackTraceFrame> frame(StackTraceFrame::cast(stack_trace->get(0)),
                                isolate);
  return handle(FrameArray::cast(frame->frame_array()), isolate);
}

}  // namespace internal
}  // namespace v8
