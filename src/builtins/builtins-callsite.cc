// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

#include "src/string-builder.h"
#include "src/wasm/wasm-module.h"

namespace v8 {
namespace internal {

#define CHECK_CALLSITE(recv, method)                                           \
  CHECK_RECEIVER(JSObject, recv, method);                                      \
  Handle<StackTraceFrame> frame;                                               \
  {                                                                            \
    Handle<Object> frame_obj;                                                  \
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(                                        \
        isolate, frame_obj,                                                    \
        JSObject::GetProperty(recv,                                            \
                              isolate->factory()->call_site_frame_symbol()));  \
    if (!frame_obj->IsStackTraceFrame()) {                                     \
      THROW_NEW_ERROR_RETURN_FAILURE(                                          \
          isolate, NewTypeError(MessageTemplate::kCallSiteMethod,              \
                                isolate->factory()->NewStringFromAsciiChecked( \
                                    method)));                                 \
    }                                                                          \
    frame = Handle<StackTraceFrame>::cast(frame_obj);                          \
  }

namespace {

Object* PositiveNumberOrNull(int value, Isolate* isolate) {
  if (value >= 0) return *isolate->factory()->NewNumberFromInt(value);
  return isolate->heap()->null_value();
}

}  // namespace

BUILTIN(CallSitePrototypeGetColumnNumber) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getColumnNumber");
  return PositiveNumberOrNull(frame->GetColumnNumber(), isolate);
}

BUILTIN(CallSitePrototypeGetEvalOrigin) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getEvalOrigin");
  return *frame->GetEvalOrigin();
}

BUILTIN(CallSitePrototypeGetFileName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getFileName");
  return *frame->GetFileName();
}

BUILTIN(CallSitePrototypeGetFunction) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getFunction");

  if (frame->IsStrict() || frame->IsWasmFrame()) {
    return *isolate->factory()->undefined_value();
  }

  return frame->function();
}

BUILTIN(CallSitePrototypeGetFunctionName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getFunctionName");
  return *frame->GetFunctionName();
}

BUILTIN(CallSitePrototypeGetLineNumber) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getLineNumber");
  return PositiveNumberOrNull(frame->GetLineNumber(), isolate);
}

BUILTIN(CallSitePrototypeGetMethodName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getMethodName");
  return *frame->GetMethodName();
}

BUILTIN(CallSitePrototypeGetPosition) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getPosition");
  return Smi::FromInt(frame->GetPosition());
}

BUILTIN(CallSitePrototypeGetScriptNameOrSourceURL) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getScriptNameOrSourceUrl");
  return *frame->GetScriptNameOrSourceUrl();
}

BUILTIN(CallSitePrototypeGetThis) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getThis");

  if (frame->IsStrict() || frame->ForceConstructor() || frame->IsWasmFrame()) {
    return *isolate->factory()->undefined_value();
  }

  return frame->receiver();
}

BUILTIN(CallSitePrototypeGetTypeName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getTypeName");
  return *frame->GetTypeName();
}

BUILTIN(CallSitePrototypeIsConstructor) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "isConstructor");
  return isolate->heap()->ToBoolean(frame->IsConstructor());
}

BUILTIN(CallSitePrototypeIsEval) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "isEval");
  return isolate->heap()->ToBoolean(frame->IsEval());
}

BUILTIN(CallSitePrototypeIsNative) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "isNative");
  return isolate->heap()->ToBoolean(frame->IsNative());
}

BUILTIN(CallSitePrototypeIsToplevel) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "isToplevel");
  return isolate->heap()->ToBoolean(frame->IsToplevel());
}

BUILTIN(CallSitePrototypeToString) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "toString");
  return *frame->ToString();
}

#undef CHECK_CALLSITE

}  // namespace internal
}  // namespace v8
