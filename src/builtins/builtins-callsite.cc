// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

#include "src/string-builder.h"
#include "src/wasm/wasm-module.h"

namespace v8 {
namespace internal {

#define CHECK_CALLSITE(recv, method)                                          \
  CHECK_RECEIVER(JSObject, recv, method);                                     \
  if (!JSReceiver::HasOwnProperty(                                            \
           recv, isolate->factory()->call_site_position_symbol())             \
           .FromMaybe(false)) {                                               \
    THROW_NEW_ERROR_RETURN_FAILURE(                                           \
        isolate,                                                              \
        NewTypeError(MessageTemplate::kCallSiteMethod,                        \
                     isolate->factory()->NewStringFromAsciiChecked(method))); \
  }

#define SET_CALLSITE_PROPERTY(target, key, value)        \
  RETURN_FAILURE_ON_EXCEPTION(                           \
      isolate, JSObject::SetOwnPropertyIgnoreAttributes( \
                   target, isolate->factory()->key(), value, DONT_ENUM))

BUILTIN(CallSiteConstructor) {
  HandleScope scope(isolate);
  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<HeapObject> new_target_obj = args.new_target();
  Handle<Object> receiver = args.atOrUndefined(isolate, 1);
  Handle<Object> fun = args.atOrUndefined(isolate, 2);
  Handle<Object> pos = args.atOrUndefined(isolate, 3);
  Handle<Object> strict_mode = args.atOrUndefined(isolate, 4);

  // Create the JS object.

  Handle<JSReceiver> new_target = new_target_obj->IsJSReceiver()
                                      ? Handle<JSReceiver>::cast(new_target_obj)
                                      : Handle<JSReceiver>::cast(target);

  Handle<JSObject> obj;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, obj,
                                     JSObject::New(target, new_target));

  // For wasm frames, receiver is the wasm object and fun is the function index
  // instead of an actual function.
  const bool is_wasm_object =
      receiver->IsJSObject() && wasm::IsWasmObject(JSObject::cast(*receiver));
  if (!fun->IsJSFunction() && !is_wasm_object) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCallSiteExpectsFunction,
                              Object::TypeOf(isolate, receiver),
                              Object::TypeOf(isolate, fun)));
  }

  if (is_wasm_object) {
    DCHECK(fun->IsSmi());
    DCHECK(wasm::GetNumberOfFunctions(JSObject::cast(*receiver)) >
           Smi::cast(*fun)->value());

    SET_CALLSITE_PROPERTY(obj, call_site_wasm_obj_symbol, receiver);
    SET_CALLSITE_PROPERTY(obj, call_site_wasm_func_index_symbol, fun);
  } else {
    DCHECK(fun->IsJSFunction());
    SET_CALLSITE_PROPERTY(obj, call_site_receiver_symbol, receiver);
    SET_CALLSITE_PROPERTY(obj, call_site_function_symbol, fun);
  }

  DCHECK(pos->IsSmi());
  SET_CALLSITE_PROPERTY(obj, call_site_position_symbol, pos);
  SET_CALLSITE_PROPERTY(
      obj, call_site_strict_symbol,
      isolate->factory()->ToBoolean(strict_mode->BooleanValue()));

  return *obj;
}

#undef SET_CALLSITE_PROPERTY

namespace {

Object* PositiveNumberOrNull(int value, Isolate* isolate) {
  if (value >= 0) return *isolate->factory()->NewNumberFromInt(value);
  return isolate->heap()->null_value();
}

}  // namespace

BUILTIN(CallSitePrototypeGetColumnNumber) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getColumnNumber");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return PositiveNumberOrNull(call_site.GetColumnNumber(), isolate);
}

namespace {

Object* EvalFromFunctionName(Isolate* isolate, Handle<Script> script) {
  if (script->eval_from_shared()->IsUndefined(isolate))
    return *isolate->factory()->undefined_value();

  Handle<SharedFunctionInfo> shared(
      SharedFunctionInfo::cast(script->eval_from_shared()));
  // Find the name of the function calling eval.
  if (shared->name()->BooleanValue()) {
    return shared->name();
  }

  return shared->inferred_name();
}

Object* EvalFromScript(Isolate* isolate, Handle<Script> script) {
  if (script->eval_from_shared()->IsUndefined(isolate))
    return *isolate->factory()->undefined_value();

  Handle<SharedFunctionInfo> eval_from_shared(
      SharedFunctionInfo::cast(script->eval_from_shared()));
  return eval_from_shared->script()->IsScript()
             ? eval_from_shared->script()
             : *isolate->factory()->undefined_value();
}

MaybeHandle<String> FormatEvalOrigin(Isolate* isolate, Handle<Script> script) {
  Handle<Object> sourceURL = Script::GetNameOrSourceURL(script);
  if (!sourceURL->IsUndefined(isolate)) {
    DCHECK(sourceURL->IsString());
    return Handle<String>::cast(sourceURL);
  }

  IncrementalStringBuilder builder(isolate);
  builder.AppendCString("eval at ");

  Handle<Object> eval_from_function_name =
      handle(EvalFromFunctionName(isolate, script), isolate);
  if (eval_from_function_name->BooleanValue()) {
    Handle<String> str;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, str, Object::ToString(isolate, eval_from_function_name),
        String);
    builder.AppendString(str);
  } else {
    builder.AppendCString("<anonymous>");
  }

  Handle<Object> eval_from_script_obj =
      handle(EvalFromScript(isolate, script), isolate);
  if (eval_from_script_obj->IsScript()) {
    Handle<Script> eval_from_script =
        Handle<Script>::cast(eval_from_script_obj);
    builder.AppendCString(" (");
    if (eval_from_script->compilation_type() == Script::COMPILATION_TYPE_EVAL) {
      // Eval script originated from another eval.
      Handle<String> str;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, str, FormatEvalOrigin(isolate, eval_from_script), String);
      builder.AppendString(str);
    } else {
      DCHECK(eval_from_script->compilation_type() !=
             Script::COMPILATION_TYPE_EVAL);
      // eval script originated from "real" source.
      Handle<Object> name_obj = handle(eval_from_script->name(), isolate);
      if (eval_from_script->name()->IsString()) {
        builder.AppendString(Handle<String>::cast(name_obj));

        Script::PositionInfo info;
        if (eval_from_script->GetPositionInfo(script->GetEvalPosition(), &info,
                                              Script::NO_OFFSET)) {
          builder.AppendCString(":");

          Handle<String> str = isolate->factory()->NumberToString(
              handle(Smi::FromInt(info.line + 1), isolate));
          builder.AppendString(str);

          builder.AppendCString(":");

          str = isolate->factory()->NumberToString(
              handle(Smi::FromInt(info.column + 1), isolate));
          builder.AppendString(str);
        }
      } else {
        DCHECK(!eval_from_script->name()->IsString());
        builder.AppendCString("unknown source");
      }
    }
    builder.AppendCString(")");
  }

  Handle<String> result;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, result, builder.Finish(), String);
  return result;
}

MaybeHandle<Object> GetEvalOrigin(Isolate* isolate, Handle<JSObject> object) {
  CallSite call_site(isolate, object);
  if (call_site.IsWasm()) return isolate->factory()->undefined_value();

  // Retrieve the function's script object.

  Handle<Object> function_obj;
  Handle<Symbol> symbol = isolate->factory()->call_site_function_symbol();
  ASSIGN_RETURN_ON_EXCEPTION(isolate, function_obj,
                             JSObject::GetProperty(object, symbol), Object);

  DCHECK(function_obj->IsJSFunction());
  Handle<JSFunction> function = Handle<JSFunction>::cast(function_obj);
  Handle<Object> script = handle(function->shared()->script(), isolate);

  if (!script->IsScript()) {
    return isolate->factory()->undefined_value();
  }

  Handle<String> str;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, str, FormatEvalOrigin(isolate, Handle<Script>::cast(script)),
      String);

  return str;
}

}  // namespace

BUILTIN(CallSitePrototypeGetEvalOrigin) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getEvalOrigin");
  RETURN_RESULT_OR_FAILURE(isolate, GetEvalOrigin(isolate, recv));
}

BUILTIN(CallSitePrototypeGetFileName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getFileName");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return *call_site.GetFileName();
}

namespace {

bool CallSiteIsStrict(Isolate* isolate, Handle<JSObject> receiver) {
  Handle<Object> strict;
  Handle<Symbol> symbol = isolate->factory()->call_site_strict_symbol();
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, strict,
                                     JSObject::GetProperty(receiver, symbol));
  return strict->BooleanValue();
}

}  // namespace

BUILTIN(CallSitePrototypeGetFunction) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getFunction");

  if (CallSiteIsStrict(isolate, recv))
    return *isolate->factory()->undefined_value();

  Handle<Symbol> symbol = isolate->factory()->call_site_function_symbol();
  RETURN_RESULT_OR_FAILURE(isolate, JSObject::GetProperty(recv, symbol));
}

BUILTIN(CallSitePrototypeGetFunctionName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getFunctionName");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return *call_site.GetFunctionName();
}

BUILTIN(CallSitePrototypeGetLineNumber) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getLineNumber");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());

  int line_number = call_site.IsWasm() ? call_site.wasm_func_index()
                                       : call_site.GetLineNumber();
  return PositiveNumberOrNull(line_number, isolate);
}

BUILTIN(CallSitePrototypeGetMethodName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getMethodName");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return *call_site.GetMethodName();
}

BUILTIN(CallSitePrototypeGetPosition) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getPosition");

  Handle<Symbol> symbol = isolate->factory()->call_site_position_symbol();
  RETURN_RESULT_OR_FAILURE(isolate, JSObject::GetProperty(recv, symbol));
}

BUILTIN(CallSitePrototypeGetScriptNameOrSourceURL) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getScriptNameOrSourceUrl");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return *call_site.GetScriptNameOrSourceUrl();
}

BUILTIN(CallSitePrototypeGetThis) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getThis");

  if (CallSiteIsStrict(isolate, recv))
    return *isolate->factory()->undefined_value();

  Handle<Object> receiver;
  Handle<Symbol> symbol = isolate->factory()->call_site_receiver_symbol();
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     JSObject::GetProperty(recv, symbol));

  if (*receiver == isolate->heap()->call_site_constructor_symbol())
    return *isolate->factory()->undefined_value();

  return *receiver;
}

namespace {

MaybeHandle<Object> GetTypeName(Isolate* isolate, Handle<JSObject> object) {
  Handle<Object> receiver;
  Handle<Symbol> symbol = isolate->factory()->call_site_receiver_symbol();
  ASSIGN_RETURN_ON_EXCEPTION(isolate, receiver,
                             JSObject::GetProperty(object, symbol), Object);

  // TODO(jgruber): Check for strict/constructor here as above.

  if (receiver->IsNull(isolate) || receiver->IsUndefined(isolate))
    return isolate->factory()->null_value();

  if (receiver->IsJSProxy()) return isolate->factory()->Proxy_string();

  Handle<JSReceiver> receiver_object =
      Object::ToObject(isolate, receiver).ToHandleChecked();
  return JSReceiver::GetConstructorName(receiver_object);
}

}  // namespace

BUILTIN(CallSitePrototypeGetTypeName) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "getTypeName");
  RETURN_RESULT_OR_FAILURE(isolate, GetTypeName(isolate, recv));
}

BUILTIN(CallSitePrototypeIsConstructor) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "isConstructor");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return isolate->heap()->ToBoolean(call_site.IsConstructor());
}

BUILTIN(CallSitePrototypeIsEval) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "isEval");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return isolate->heap()->ToBoolean(call_site.IsEval());
}

BUILTIN(CallSitePrototypeIsNative) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "isNative");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return isolate->heap()->ToBoolean(call_site.IsNative());
}

BUILTIN(CallSitePrototypeIsToplevel) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "isToplevel");

  CallSite call_site(isolate, recv);
  CHECK(call_site.IsJavaScript() || call_site.IsWasm());
  return isolate->heap()->ToBoolean(call_site.IsToplevel());
}

namespace {

bool IsNonEmptyString(Handle<Object> object) {
  return (object->IsString() && String::cast(*object)->length() > 0);
}

MaybeHandle<JSObject> AppendWasmToString(Isolate* isolate,
                                         Handle<JSObject> recv,
                                         CallSite* call_site,
                                         IncrementalStringBuilder* builder) {
  Handle<Object> name = call_site->GetFunctionName();
  if (name->IsNull(isolate)) {
    builder->AppendCString("<WASM UNNAMED>");
  } else {
    DCHECK(name->IsString());
    builder->AppendString(Handle<String>::cast(name));
  }

  builder->AppendCString(" (<WASM>[");

  Handle<String> ix = isolate->factory()->NumberToString(
      handle(Smi::FromInt(call_site->wasm_func_index()), isolate));
  builder->AppendString(ix);

  builder->AppendCString("]+");

  Handle<Object> pos;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, pos, JSObject::GetProperty(
                        recv, isolate->factory()->call_site_position_symbol()),
      JSObject);
  DCHECK(pos->IsNumber());
  builder->AppendString(isolate->factory()->NumberToString(pos));
  builder->AppendCString(")");

  return recv;
}

MaybeHandle<JSObject> AppendFileLocation(Isolate* isolate,
                                         Handle<JSObject> recv,
                                         CallSite* call_site,
                                         IncrementalStringBuilder* builder) {
  if (call_site->IsNative()) {
    builder->AppendCString("native");
    return recv;
  }

  Handle<Object> file_name = call_site->GetScriptNameOrSourceUrl();
  if (!file_name->IsString() && call_site->IsEval()) {
    Handle<Object> eval_origin;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, eval_origin,
                               GetEvalOrigin(isolate, recv), JSObject);
    DCHECK(eval_origin->IsString());
    builder->AppendString(Handle<String>::cast(eval_origin));
    builder->AppendCString(", ");  // Expecting source position to follow.
  }

  if (IsNonEmptyString(file_name)) {
    builder->AppendString(Handle<String>::cast(file_name));
  } else {
    // Source code does not originate from a file and is not native, but we
    // can still get the source position inside the source string, e.g. in
    // an eval string.
    builder->AppendCString("<anonymous>");
  }

  int line_number = call_site->GetLineNumber();
  if (line_number != -1) {
    builder->AppendCharacter(':');
    Handle<String> line_string = isolate->factory()->NumberToString(
        handle(Smi::FromInt(line_number), isolate), isolate);
    builder->AppendString(line_string);

    int column_number = call_site->GetColumnNumber();
    if (column_number != -1) {
      builder->AppendCharacter(':');
      Handle<String> column_string = isolate->factory()->NumberToString(
          handle(Smi::FromInt(column_number), isolate), isolate);
      builder->AppendString(column_string);
    }
  }

  return recv;
}

int StringIndexOf(Isolate* isolate, Handle<String> subject,
                  Handle<String> pattern) {
  if (pattern->length() > subject->length()) return -1;
  return String::IndexOf(isolate, subject, pattern, 0);
}

// Returns true iff
// 1. the subject ends with '.' + pattern, or
// 2. subject == pattern.
bool StringEndsWithMethodName(Isolate* isolate, Handle<String> subject,
                              Handle<String> pattern) {
  if (String::Equals(subject, pattern)) return true;

  FlatStringReader subject_reader(isolate, String::Flatten(subject));
  FlatStringReader pattern_reader(isolate, String::Flatten(pattern));

  int pattern_index = pattern_reader.length() - 1;
  int subject_index = subject_reader.length() - 1;
  for (int i = 0; i <= pattern_reader.length(); i++) {  // Iterate over len + 1.
    if (subject_index < 0) {
      return false;
    }

    const uc32 subject_char = subject_reader.Get(subject_index);
    if (i == pattern_reader.length()) {
      if (subject_char != '.') return false;
    } else if (subject_char != pattern_reader.Get(pattern_index)) {
      return false;
    }

    pattern_index--;
    subject_index--;
  }

  return true;
}

MaybeHandle<JSObject> AppendMethodCall(Isolate* isolate, Handle<JSObject> recv,
                                       CallSite* call_site,
                                       IncrementalStringBuilder* builder) {
  Handle<Object> type_name;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, type_name, GetTypeName(isolate, recv),
                             JSObject);
  Handle<Object> method_name = call_site->GetMethodName();
  Handle<Object> function_name = call_site->GetFunctionName();

  if (IsNonEmptyString(function_name)) {
    Handle<String> function_string = Handle<String>::cast(function_name);
    if (type_name->IsString()) {
      Handle<String> type_string = Handle<String>::cast(type_name);
      bool starts_with_type_name =
          (StringIndexOf(isolate, function_string, type_string) == 0);
      if (!starts_with_type_name) {
        builder->AppendString(type_string);
        builder->AppendCharacter('.');
      }
    }
    builder->AppendString(function_string);

    if (IsNonEmptyString(method_name)) {
      Handle<String> method_string = Handle<String>::cast(method_name);
      if (!StringEndsWithMethodName(isolate, function_string, method_string)) {
        builder->AppendCString(" [as ");
        builder->AppendString(method_string);
        builder->AppendCharacter(']');
      }
    }
  } else {
    builder->AppendString(Handle<String>::cast(type_name));
    builder->AppendCharacter('.');
    if (IsNonEmptyString(method_name)) {
      builder->AppendString(Handle<String>::cast(method_name));
    } else {
      builder->AppendCString("<anonymous>");
    }
  }

  return recv;
}

}  // namespace

BUILTIN(CallSitePrototypeToString) {
  HandleScope scope(isolate);
  CHECK_CALLSITE(recv, "toString");

  IncrementalStringBuilder builder(isolate);

  CallSite call_site(isolate, recv);
  if (call_site.IsWasm()) {
    RETURN_FAILURE_ON_EXCEPTION(
        isolate, AppendWasmToString(isolate, recv, &call_site, &builder));
    RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
  }

  DCHECK(!call_site.IsWasm());
  Handle<Object> function_name = call_site.GetFunctionName();

  const bool is_toplevel = call_site.IsToplevel();
  const bool is_constructor = call_site.IsConstructor();
  const bool is_method_call = !(is_toplevel || is_constructor);

  if (is_method_call) {
    RETURN_FAILURE_ON_EXCEPTION(
        isolate, AppendMethodCall(isolate, recv, &call_site, &builder));
  } else if (is_constructor) {
    builder.AppendCString("new ");
    if (IsNonEmptyString(function_name)) {
      builder.AppendString(Handle<String>::cast(function_name));
    } else {
      builder.AppendCString("<anonymous>");
    }
  } else if (IsNonEmptyString(function_name)) {
    builder.AppendString(Handle<String>::cast(function_name));
  } else {
    RETURN_FAILURE_ON_EXCEPTION(
        isolate, AppendFileLocation(isolate, recv, &call_site, &builder));
    RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
  }

  builder.AppendCString(" (");
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, AppendFileLocation(isolate, recv, &call_site, &builder));
  builder.AppendCString(")");

  RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
}

#undef CHECK_CALLSITE

}  // namespace internal
}  // namespace v8
