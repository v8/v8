// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/messages.h"

#include <memory>

#include "src/api.h"
#include "src/execution.h"
#include "src/isolate-inl.h"
#include "src/keys.h"
#include "src/string-builder.h"
#include "src/wasm/wasm-module.h"

namespace v8 {
namespace internal {

MessageLocation::MessageLocation(Handle<Script> script, int start_pos,
                                 int end_pos)
    : script_(script), start_pos_(start_pos), end_pos_(end_pos) {}
MessageLocation::MessageLocation(Handle<Script> script, int start_pos,
                                 int end_pos, Handle<JSFunction> function)
    : script_(script),
      start_pos_(start_pos),
      end_pos_(end_pos),
      function_(function) {}
MessageLocation::MessageLocation() : start_pos_(-1), end_pos_(-1) {}

// If no message listeners have been registered this one is called
// by default.
void MessageHandler::DefaultMessageReport(Isolate* isolate,
                                          const MessageLocation* loc,
                                          Handle<Object> message_obj) {
  std::unique_ptr<char[]> str = GetLocalizedMessage(isolate, message_obj);
  if (loc == NULL) {
    PrintF("%s\n", str.get());
  } else {
    HandleScope scope(isolate);
    Handle<Object> data(loc->script()->name(), isolate);
    std::unique_ptr<char[]> data_str;
    if (data->IsString())
      data_str = Handle<String>::cast(data)->ToCString(DISALLOW_NULLS);
    PrintF("%s:%i: %s\n", data_str.get() ? data_str.get() : "<unknown>",
           loc->start_pos(), str.get());
  }
}


Handle<JSMessageObject> MessageHandler::MakeMessageObject(
    Isolate* isolate, MessageTemplate::Template message,
    MessageLocation* location, Handle<Object> argument,
    Handle<JSArray> stack_frames) {
  Factory* factory = isolate->factory();

  int start = -1;
  int end = -1;
  Handle<Object> script_handle = factory->undefined_value();
  if (location != NULL) {
    start = location->start_pos();
    end = location->end_pos();
    script_handle = Script::GetWrapper(location->script());
  } else {
    script_handle = Script::GetWrapper(isolate->factory()->empty_script());
  }

  Handle<Object> stack_frames_handle = stack_frames.is_null()
      ? Handle<Object>::cast(factory->undefined_value())
      : Handle<Object>::cast(stack_frames);

  Handle<JSMessageObject> message_obj = factory->NewJSMessageObject(
      message, argument, start, end, script_handle, stack_frames_handle);

  return message_obj;
}


void MessageHandler::ReportMessage(Isolate* isolate, MessageLocation* loc,
                                   Handle<JSMessageObject> message) {
  // We are calling into embedder's code which can throw exceptions.
  // Thus we need to save current exception state, reset it to the clean one
  // and ignore scheduled exceptions callbacks can throw.

  // We pass the exception object into the message handler callback though.
  Object* exception_object = isolate->heap()->undefined_value();
  if (isolate->has_pending_exception()) {
    exception_object = isolate->pending_exception();
  }
  Handle<Object> exception(exception_object, isolate);

  Isolate::ExceptionScope exception_scope(isolate);
  isolate->clear_pending_exception();
  isolate->set_external_caught_exception(false);

  // Turn the exception on the message into a string if it is an object.
  if (message->argument()->IsJSObject()) {
    HandleScope scope(isolate);
    Handle<Object> argument(message->argument(), isolate);

    MaybeHandle<Object> maybe_stringified;
    Handle<Object> stringified;
    // Make sure we don't leak uncaught internally generated Error objects.
    if (argument->IsJSError()) {
      maybe_stringified = Object::NoSideEffectsToString(isolate, argument);
    } else {
      v8::TryCatch catcher(reinterpret_cast<v8::Isolate*>(isolate));
      catcher.SetVerbose(false);
      catcher.SetCaptureMessage(false);

      maybe_stringified = Object::ToString(isolate, argument);
    }

    if (!maybe_stringified.ToHandle(&stringified)) {
      stringified = isolate->factory()->NewStringFromAsciiChecked("exception");
    }
    message->set_argument(*stringified);
  }

  v8::Local<v8::Message> api_message_obj = v8::Utils::MessageToLocal(message);
  v8::Local<v8::Value> api_exception_obj = v8::Utils::ToLocal(exception);

  Handle<TemplateList> global_listeners =
      isolate->factory()->message_listeners();
  int global_length = global_listeners->length();
  if (global_length == 0) {
    DefaultMessageReport(isolate, loc, message);
    if (isolate->has_scheduled_exception()) {
      isolate->clear_scheduled_exception();
    }
  } else {
    for (int i = 0; i < global_length; i++) {
      HandleScope scope(isolate);
      if (global_listeners->get(i)->IsUndefined(isolate)) continue;
      FixedArray* listener = FixedArray::cast(global_listeners->get(i));
      Foreign* callback_obj = Foreign::cast(listener->get(0));
      v8::MessageCallback callback =
          FUNCTION_CAST<v8::MessageCallback>(callback_obj->foreign_address());
      Handle<Object> callback_data(listener->get(1), isolate);
      {
        // Do not allow exceptions to propagate.
        v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
        callback(api_message_obj, callback_data->IsUndefined(isolate)
                                      ? api_exception_obj
                                      : v8::Utils::ToLocal(callback_data));
      }
      if (isolate->has_scheduled_exception()) {
        isolate->clear_scheduled_exception();
      }
    }
  }
}


Handle<String> MessageHandler::GetMessage(Isolate* isolate,
                                          Handle<Object> data) {
  Handle<JSMessageObject> message = Handle<JSMessageObject>::cast(data);
  Handle<Object> arg = Handle<Object>(message->argument(), isolate);
  return MessageTemplate::FormatMessage(isolate, message->type(), arg);
}

std::unique_ptr<char[]> MessageHandler::GetLocalizedMessage(
    Isolate* isolate, Handle<Object> data) {
  HandleScope scope(isolate);
  return GetMessage(isolate, data)->ToCString(DISALLOW_NULLS);
}

namespace {

MaybeHandle<Object> ConstructCallSite(Isolate* isolate,
                                      Handle<StackTraceFrame> frame) {
  // Create the JS object.

  Handle<JSFunction> target =
      handle(isolate->native_context()->callsite_function(), isolate);

  Handle<JSObject> obj;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, obj, JSObject::New(target, target),
                             Object);

  RETURN_ON_EXCEPTION(
      isolate,
      JSObject::SetOwnPropertyIgnoreAttributes(
          obj, isolate->factory()->call_site_frame_symbol(), frame, DONT_ENUM),
      Object);

  return obj;
}

// Convert the raw frames as written by Isolate::CaptureSimpleStackTrace into
// a vector of JS CallSite objects.
MaybeHandle<JSArray> ToCallSites(Isolate* isolate,
                                 Handle<FixedArray> raw_stack_elements) {
  const int frame_count = raw_stack_elements->length();
  Handle<FixedArray> frames = isolate->factory()->NewFixedArray(frame_count);

  for (int i = 0; i < frame_count; i++) {
    auto frame = Handle<StackTraceFrame>::cast(
        FixedArray::get(*raw_stack_elements, i, isolate));

    Handle<Object> callsite;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, callsite,
                               ConstructCallSite(isolate, frame), JSArray);

    frames->set(i, *callsite);
  }

  return isolate->factory()->NewJSArrayWithElements(frames);
}

MaybeHandle<Object> AppendErrorString(Isolate* isolate, Handle<Object> error,
                                      IncrementalStringBuilder* builder) {
  MaybeHandle<String> err_str =
      ErrorUtils::ToString(isolate, Handle<Object>::cast(error));
  if (err_str.is_null()) {
    // Error.toString threw. Try to return a string representation of the thrown
    // exception instead.

    DCHECK(isolate->has_pending_exception());
    Handle<Object> pending_exception =
        handle(isolate->pending_exception(), isolate);
    isolate->clear_pending_exception();

    err_str = ErrorUtils::ToString(isolate, pending_exception);
    if (err_str.is_null()) {
      // Formatting the thrown exception threw again, give up.
      DCHECK(isolate->has_pending_exception());
      isolate->clear_pending_exception();

      builder->AppendCString("<error>");
    } else {
      // Formatted thrown exception successfully, append it.
      builder->AppendCString("<error: ");
      builder->AppendString(err_str.ToHandleChecked());
      builder->AppendCharacter('>');
    }
  } else {
    builder->AppendString(err_str.ToHandleChecked());
  }

  return error;
}

class PrepareStackTraceScope {
 public:
  explicit PrepareStackTraceScope(Isolate* isolate) : isolate_(isolate) {
    DCHECK(!isolate_->formatting_stack_trace());
    isolate_->set_formatting_stack_trace(true);
  }

  ~PrepareStackTraceScope() { isolate_->set_formatting_stack_trace(false); }

 private:
  Isolate* isolate_;

  DISALLOW_COPY_AND_ASSIGN(PrepareStackTraceScope);
};

}  // namespace

// static
MaybeHandle<Object> ErrorUtils::FormatStackTrace(Isolate* isolate,
                                                 Handle<JSObject> error,
                                                 Handle<Object> raw_stack) {
  // Extract the raw stack trace fixed array.

  DCHECK(raw_stack->IsJSArray());
  Handle<JSArray> raw_stack_array = Handle<JSArray>::cast(raw_stack);

  DCHECK(raw_stack_array->elements()->IsFixedArray());
  Handle<FixedArray> raw_stack_elements =
      handle(FixedArray::cast(raw_stack_array->elements()), isolate);

  // If there's a user-specified "prepareStackFrames" function, call it on the
  // frames and use its result.

  Handle<JSFunction> global_error = isolate->error_function();
  Handle<Object> prepare_stack_trace;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, prepare_stack_trace,
      JSFunction::GetProperty(isolate, global_error, "prepareStackTrace"),
      Object);

  const bool in_recursion = isolate->formatting_stack_trace();
  if (prepare_stack_trace->IsJSFunction() && !in_recursion) {
    PrepareStackTraceScope scope(isolate);

    // Create JS CallSite objects from the raw stack frame array.

    Handle<JSArray> frames;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, frames, ToCallSites(isolate, raw_stack_elements), Object);

    const int argc = 2;
    ScopedVector<Handle<Object>> argv(argc);
    argv[0] = error;
    argv[1] = frames;

    Handle<Object> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result, Execution::Call(isolate, prepare_stack_trace,
                                         global_error, argc, argv.start()),
        Object);

    return result;
  } else {
    IncrementalStringBuilder builder(isolate);

    RETURN_ON_EXCEPTION(isolate, AppendErrorString(isolate, error, &builder),
                        Object);

    for (int i = 0; i < raw_stack_elements->length(); i++) {
      auto frame = Handle<StackTraceFrame>::cast(
          FixedArray::get(*raw_stack_elements, i, isolate));

      builder.AppendCString("\n    at ");
      builder.AppendString(frame->ToString());
    }

    RETURN_RESULT(isolate, builder.Finish(), Object);
  }
}

Handle<String> MessageTemplate::FormatMessage(Isolate* isolate,
                                              int template_index,
                                              Handle<Object> arg) {
  Factory* factory = isolate->factory();
  Handle<String> result_string = Object::NoSideEffectsToString(isolate, arg);
  MaybeHandle<String> maybe_result_string = MessageTemplate::FormatMessage(
      template_index, result_string, factory->empty_string(),
      factory->empty_string());
  if (!maybe_result_string.ToHandle(&result_string)) {
    return factory->InternalizeOneByteString(STATIC_CHAR_VECTOR("<error>"));
  }
  // A string that has been obtained from JS code in this way is
  // likely to be a complicated ConsString of some sort.  We flatten it
  // here to improve the efficiency of converting it to a C string and
  // other operations that are likely to take place (see GetLocalizedMessage
  // for example).
  return String::Flatten(result_string);
}


const char* MessageTemplate::TemplateString(int template_index) {
  switch (template_index) {
#define CASE(NAME, STRING) \
  case k##NAME:            \
    return STRING;
    MESSAGE_TEMPLATES(CASE)
#undef CASE
    case kLastMessage:
    default:
      return NULL;
  }
}


MaybeHandle<String> MessageTemplate::FormatMessage(int template_index,
                                                   Handle<String> arg0,
                                                   Handle<String> arg1,
                                                   Handle<String> arg2) {
  Isolate* isolate = arg0->GetIsolate();
  const char* template_string = TemplateString(template_index);
  if (template_string == NULL) {
    isolate->ThrowIllegalOperation();
    return MaybeHandle<String>();
  }

  IncrementalStringBuilder builder(isolate);

  unsigned int i = 0;
  Handle<String> args[] = {arg0, arg1, arg2};
  for (const char* c = template_string; *c != '\0'; c++) {
    if (*c == '%') {
      // %% results in verbatim %.
      if (*(c + 1) == '%') {
        c++;
        builder.AppendCharacter('%');
      } else {
        DCHECK(i < arraysize(args));
        Handle<String> arg = args[i++];
        builder.AppendString(arg);
      }
    } else {
      builder.AppendCharacter(*c);
    }
  }

  return builder.Finish();
}

MaybeHandle<Object> ErrorUtils::Construct(
    Isolate* isolate, Handle<JSFunction> target, Handle<Object> new_target,
    Handle<Object> message, FrameSkipMode mode, Handle<Object> caller,
    bool suppress_detailed_trace) {
  // 1. If NewTarget is undefined, let newTarget be the active function object,
  // else let newTarget be NewTarget.

  Handle<JSReceiver> new_target_recv =
      new_target->IsJSReceiver() ? Handle<JSReceiver>::cast(new_target)
                                 : Handle<JSReceiver>::cast(target);

  // 2. Let O be ? OrdinaryCreateFromConstructor(newTarget, "%ErrorPrototype%",
  //    « [[ErrorData]] »).
  Handle<JSObject> err;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, err,
                             JSObject::New(target, new_target_recv), Object);

  // 3. If message is not undefined, then
  //  a. Let msg be ? ToString(message).
  //  b. Let msgDesc be the PropertyDescriptor{[[Value]]: msg, [[Writable]]:
  //     true, [[Enumerable]]: false, [[Configurable]]: true}.
  //  c. Perform ! DefinePropertyOrThrow(O, "message", msgDesc).
  // 4. Return O.

  if (!message->IsUndefined(isolate)) {
    Handle<String> msg_string;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, msg_string,
                               Object::ToString(isolate, message), Object);
    RETURN_ON_EXCEPTION(isolate, JSObject::SetOwnPropertyIgnoreAttributes(
                                     err, isolate->factory()->message_string(),
                                     msg_string, DONT_ENUM),
                        Object);
  }

  // Optionally capture a more detailed stack trace for the message.
  if (!suppress_detailed_trace) {
    RETURN_ON_EXCEPTION(isolate, isolate->CaptureAndSetDetailedStackTrace(err),
                        Object);
  }

  // Capture a simple stack trace for the stack property.
  RETURN_ON_EXCEPTION(isolate,
                      isolate->CaptureAndSetSimpleStackTrace(err, mode, caller),
                      Object);

  return err;
}

namespace {

MaybeHandle<String> GetStringPropertyOrDefault(Isolate* isolate,
                                               Handle<JSReceiver> recv,
                                               Handle<String> key,
                                               Handle<String> default_str) {
  Handle<Object> obj;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, obj, JSObject::GetProperty(recv, key),
                             String);

  Handle<String> str;
  if (obj->IsUndefined(isolate)) {
    str = default_str;
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, str, Object::ToString(isolate, obj),
                               String);
  }

  return str;
}

}  // namespace

// ES6 section 19.5.3.4 Error.prototype.toString ( )
MaybeHandle<String> ErrorUtils::ToString(Isolate* isolate,
                                         Handle<Object> receiver) {
  // 1. Let O be the this value.
  // 2. If Type(O) is not Object, throw a TypeError exception.
  if (!receiver->IsJSReceiver()) {
    return isolate->Throw<String>(isolate->factory()->NewTypeError(
        MessageTemplate::kIncompatibleMethodReceiver,
        isolate->factory()->NewStringFromAsciiChecked(
            "Error.prototype.toString"),
        receiver));
  }
  Handle<JSReceiver> recv = Handle<JSReceiver>::cast(receiver);

  // 3. Let name be ? Get(O, "name").
  // 4. If name is undefined, let name be "Error"; otherwise let name be
  // ? ToString(name).
  Handle<String> name_key = isolate->factory()->name_string();
  Handle<String> name_default = isolate->factory()->Error_string();
  Handle<String> name;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, name,
      GetStringPropertyOrDefault(isolate, recv, name_key, name_default),
      String);

  // 5. Let msg be ? Get(O, "message").
  // 6. If msg is undefined, let msg be the empty String; otherwise let msg be
  // ? ToString(msg).
  Handle<String> msg_key = isolate->factory()->message_string();
  Handle<String> msg_default = isolate->factory()->empty_string();
  Handle<String> msg;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, msg,
      GetStringPropertyOrDefault(isolate, recv, msg_key, msg_default), String);

  // 7. If name is the empty String, return msg.
  // 8. If msg is the empty String, return name.
  if (name->length() == 0) return msg;
  if (msg->length() == 0) return name;

  // 9. Return the result of concatenating name, the code unit 0x003A (COLON),
  // the code unit 0x0020 (SPACE), and msg.
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(name);
  builder.AppendCString(": ");
  builder.AppendString(msg);

  Handle<String> result;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result, builder.Finish(), String);
  return result;
}

namespace {

Handle<String> FormatMessage(Isolate* isolate, int template_index,
                             Handle<Object> arg0, Handle<Object> arg1,
                             Handle<Object> arg2) {
  Handle<String> arg0_str = Object::NoSideEffectsToString(isolate, arg0);
  Handle<String> arg1_str = Object::NoSideEffectsToString(isolate, arg1);
  Handle<String> arg2_str = Object::NoSideEffectsToString(isolate, arg2);

  isolate->native_context()->IncrementErrorsThrown();

  Handle<String> msg;
  if (!MessageTemplate::FormatMessage(template_index, arg0_str, arg1_str,
                                      arg2_str)
           .ToHandle(&msg)) {
    DCHECK(isolate->has_pending_exception());
    isolate->clear_pending_exception();
    return isolate->factory()->NewStringFromAsciiChecked("<error>");
  }

  return msg;
}

}  // namespace

// static
MaybeHandle<Object> ErrorUtils::MakeGenericError(
    Isolate* isolate, Handle<JSFunction> constructor, int template_index,
    Handle<Object> arg0, Handle<Object> arg1, Handle<Object> arg2,
    FrameSkipMode mode) {
  if (FLAG_clear_exceptions_on_js_entry) {
    // This function used to be implemented in JavaScript, and JSEntryStub
    // clears
    // any pending exceptions - so whenever we'd call this from C++, pending
    // exceptions would be cleared. Preserve this behavior.
    isolate->clear_pending_exception();
  }

  DCHECK(mode != SKIP_UNTIL_SEEN);

  Handle<Object> no_caller;
  Handle<String> msg = FormatMessage(isolate, template_index, arg0, arg1, arg2);
  return ErrorUtils::Construct(isolate, constructor, constructor, msg, mode,
                               no_caller, false);
}

}  // namespace internal
}  // namespace v8
