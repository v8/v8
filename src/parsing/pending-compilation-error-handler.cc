// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/parsing/pending-compilation-error-handler.h"

#include "src/ast/ast-value-factory.h"
#include "src/base/logging.h"
#include "src/debug/debug.h"
#include "src/execution/isolate.h"
#include "src/execution/messages.h"
#include "src/handles/handles.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

void PendingCompilationErrorHandler::MessageDetails::TransferOffThreadHandle(
    OffThreadIsolate* isolate) {
  DCHECK_NE(type_, kMainThreadHandle);
  if (type_ != kAstRawString) return;
  arg_transfer_handle_ = isolate->TransferHandle(arg_->string());
  type_ = kOffThreadTransferHandle;
}

Handle<String> PendingCompilationErrorHandler::MessageDetails::ArgumentString(
    Isolate* isolate) const {
  switch (type_) {
    case kAstRawString:
      return arg_->string();
    case kNone:
      return isolate->factory()->undefined_string();
    case kConstCharString:
      return isolate->factory()
          ->NewStringFromUtf8(CStrVector(char_arg_))
          .ToHandleChecked();
    case kMainThreadHandle:
      return arg_handle_;
    case kOffThreadTransferHandle:
      return arg_transfer_handle_.ToHandle();
  }
}

MessageLocation PendingCompilationErrorHandler::MessageDetails::GetLocation(
    Handle<Script> script) const {
  return MessageLocation(script, start_position_, end_position_);
}

void PendingCompilationErrorHandler::ReportMessageAt(int start_position,
                                                     int end_position,
                                                     MessageTemplate message,
                                                     const char* arg) {
  if (has_pending_error_) return;
  has_pending_error_ = true;

  error_details_ = MessageDetails(start_position, end_position, message, arg);
}

void PendingCompilationErrorHandler::ReportMessageAt(int start_position,
                                                     int end_position,
                                                     MessageTemplate message,
                                                     const AstRawString* arg) {
  if (has_pending_error_) return;
  has_pending_error_ = true;

  error_details_ = MessageDetails(start_position, end_position, message, arg);
}

void PendingCompilationErrorHandler::ReportWarningAt(int start_position,
                                                     int end_position,
                                                     MessageTemplate message,
                                                     const char* arg) {
  warning_messages_.emplace_front(
      MessageDetails(start_position, end_position, message, arg));
}

void PendingCompilationErrorHandler::ReportWarnings(Isolate* isolate,
                                                    Handle<Script> script) {
  DCHECK(!has_pending_error());

  for (const MessageDetails& warning : warning_messages_) {
    MessageLocation location = warning.GetLocation(script);
    Handle<String> argument = warning.ArgumentString(isolate);
    Handle<JSMessageObject> message =
        MessageHandler::MakeMessageObject(isolate, warning.message(), &location,
                                          argument, Handle<FixedArray>::null());
    message->set_error_level(v8::Isolate::kMessageWarning);
    MessageHandler::ReportMessage(isolate, &location, message);
  }
}

void PendingCompilationErrorHandler::ReportWarnings(OffThreadIsolate* isolate,
                                                    Handle<Script> script) {
  // Change any AstRawStrings to raw object pointers before the Ast Zone dies,
  // re-report later on the main thread.
  DCHECK(!has_pending_error());
  for (MessageDetails& warning : warning_messages_) {
    warning.TransferOffThreadHandle(isolate);
  }
}

void PendingCompilationErrorHandler::ReportErrors(
    Isolate* isolate, Handle<Script> script,
    AstValueFactory* ast_value_factory) {
  if (stack_overflow()) {
    isolate->StackOverflow();
  } else {
    DCHECK(has_pending_error());
    // Internalize ast values for throwing the pending error.
    ast_value_factory->Internalize(isolate);
    ThrowPendingError(isolate, script);
  }
}

void PendingCompilationErrorHandler::PrepareErrorsOffThread(
    OffThreadIsolate* isolate, Handle<Script> script,
    AstValueFactory* ast_value_factory) {
  if (!stack_overflow()) {
    DCHECK(has_pending_error());
    // Internalize ast values for later throwing the pending error.
    ast_value_factory->Internalize(isolate);
    error_details_.TransferOffThreadHandle(isolate);
  }
}

void PendingCompilationErrorHandler::ReportErrorsAfterOffThreadFinalization(
    Isolate* isolate, Handle<Script> script) {
  if (stack_overflow()) {
    isolate->StackOverflow();
  } else {
    DCHECK(has_pending_error());
    // Ast values should already be internalized.
    ThrowPendingError(isolate, script);
  }
}

void PendingCompilationErrorHandler::ThrowPendingError(Isolate* isolate,
                                                       Handle<Script> script) {
  if (!has_pending_error_) return;

  MessageLocation location = error_details_.GetLocation(script);
  Handle<String> argument = error_details_.ArgumentString(isolate);
  isolate->debug()->OnCompileError(script);

  Factory* factory = isolate->factory();
  Handle<JSObject> error =
      factory->NewSyntaxError(error_details_.message(), argument);
  isolate->ThrowAt(error, &location);
}

Handle<String> PendingCompilationErrorHandler::FormatErrorMessageForTest(
    Isolate* isolate) const {
  return MessageFormatter::Format(isolate, error_details_.message(),
                                  error_details_.ArgumentString(isolate));
}

}  // namespace internal
}  // namespace v8
