// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

#include "src/bootstrapper.h"
#include "src/messages.h"
#include "src/property-descriptor.h"
#include "src/string-builder.h"

namespace v8 {
namespace internal {

// ES6 section 19.5.1.1 Error ( message )
BUILTIN(ErrorConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate,
      ConstructError(isolate, args.target<JSFunction>(),
                     Handle<Object>::cast(args.new_target()),
                     args.atOrUndefined(isolate, 1), SKIP_FIRST, false));
}

// static
BUILTIN(ErrorCaptureStackTrace) {
  HandleScope scope(isolate);
  Handle<Object> object_obj = args.atOrUndefined(isolate, 1);
  if (!object_obj->IsJSObject()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kInvalidArgument, object_obj));
  }
  Handle<JSObject> object = Handle<JSObject>::cast(object_obj);
  Handle<Object> caller = args.atOrUndefined(isolate, 2);
  FrameSkipMode mode = caller->IsJSFunction() ? SKIP_UNTIL_SEEN : SKIP_NONE;

  // Collect the stack trace.

  RETURN_FAILURE_ON_EXCEPTION(isolate,
                              isolate->CaptureAndSetDetailedStackTrace(object));

  // Eagerly format the stack trace and set the stack property.

  Handle<Object> stack_trace =
      isolate->CaptureSimpleStackTrace(object, mode, caller);
  if (!stack_trace->IsJSArray()) return *isolate->factory()->undefined_value();

  Handle<Object> formatted_stack_trace;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, formatted_stack_trace,
      FormatStackTrace(isolate, object, stack_trace));

  PropertyDescriptor desc;
  desc.set_configurable(true);
  desc.set_value(formatted_stack_trace);
  Maybe<bool> status = JSReceiver::DefineOwnProperty(
      isolate, object, isolate->factory()->stack_string(), &desc,
      Object::THROW_ON_ERROR);
  if (!status.IsJust()) return isolate->heap()->exception();
  CHECK(status.FromJust());
  return isolate->heap()->undefined_value();
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
BUILTIN(ErrorPrototypeToString) {
  HandleScope scope(isolate);

  // 1. Let O be the this value.
  // 2. If Type(O) is not Object, throw a TypeError exception.
  CHECK_RECEIVER(JSReceiver, receiver, "Error.prototype.toString");

  // 3. Let name be ? Get(O, "name").
  // 4. If name is undefined, let name be "Error"; otherwise let name be
  // ? ToString(name).
  Handle<String> name_key = isolate->factory()->name_string();
  Handle<String> name_default = isolate->factory()->Error_string();
  Handle<String> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, name,
      GetStringPropertyOrDefault(isolate, receiver, name_key, name_default));

  // 5. Let msg be ? Get(O, "message").
  // 6. If msg is undefined, let msg be the empty String; otherwise let msg be
  // ? ToString(msg).
  Handle<String> msg_key = isolate->factory()->message_string();
  Handle<String> msg_default = isolate->factory()->empty_string();
  Handle<String> msg;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, msg,
      GetStringPropertyOrDefault(isolate, receiver, msg_key, msg_default));

  // 7. If name is the empty String, return msg.
  // 8. If msg is the empty String, return name.
  if (name->length() == 0) return *msg;
  if (msg->length() == 0) return *name;

  // 9. Return the result of concatenating name, the code unit 0x003A (COLON),
  // the code unit 0x0020 (SPACE), and msg.
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(name);
  builder.AppendCString(": ");
  builder.AppendString(msg);
  RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
}

}  // namespace internal
}  // namespace v8
