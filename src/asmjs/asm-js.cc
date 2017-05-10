// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/asmjs/asm-js.h"

#include "src/api-natives.h"
#include "src/api.h"
#include "src/asmjs/asm-names.h"
#include "src/asmjs/asm-parser.h"
#include "src/assert-scope.h"
#include "src/ast/ast.h"
#include "src/base/platform/elapsed-timer.h"
#include "src/compilation-info.h"
#include "src/execution.h"
#include "src/factory.h"
#include "src/handles.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects.h"

#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {

const char* const AsmJs::kSingleFunctionName = "__single_function__";

namespace {
enum WasmDataEntries {
  kWasmDataCompiledModule,
  kWasmDataUsesArray,
  kWasmDataEntryCount,
};

Handle<Object> StdlibMathMember(Isolate* isolate, Handle<JSReceiver> stdlib,
                                Handle<Name> name) {
  Handle<Name> math_name(
      isolate->factory()->InternalizeOneByteString(STATIC_CHAR_VECTOR("Math")));
  Handle<Object> math = JSReceiver::GetDataProperty(stdlib, math_name);
  if (!math->IsJSReceiver()) return isolate->factory()->undefined_value();
  Handle<JSReceiver> math_receiver = Handle<JSReceiver>::cast(math);
  Handle<Object> value = JSReceiver::GetDataProperty(math_receiver, name);
  return value;
}

bool IsStdlibMemberValid(Isolate* isolate, Handle<JSReceiver> stdlib,
                         wasm::AsmJsParser::StandardMember member,
                         bool* is_typed_array) {
  switch (member) {
    case wasm::AsmJsParser::StandardMember::kInfinity: {
      Handle<Name> name = isolate->factory()->infinity_string();
      Handle<Object> value = JSReceiver::GetDataProperty(stdlib, name);
      return value->IsNumber() && std::isinf(value->Number());
    }
    case wasm::AsmJsParser::StandardMember::kNaN: {
      Handle<Name> name = isolate->factory()->nan_string();
      Handle<Object> value = JSReceiver::GetDataProperty(stdlib, name);
      return value->IsNaN();
    }
#define STDLIB_MATH_FUNC(fname, FName, ignore1, ignore2)            \
  case wasm::AsmJsParser::StandardMember::kMath##FName: {           \
    Handle<Name> name(isolate->factory()->InternalizeOneByteString( \
        STATIC_CHAR_VECTOR(#fname)));                               \
    Handle<Object> value = StdlibMathMember(isolate, stdlib, name); \
    if (!value->IsJSFunction()) return false;                       \
    Handle<JSFunction> func = Handle<JSFunction>::cast(value);      \
    return func->shared()->code() ==                                \
           isolate->builtins()->builtin(Builtins::kMath##FName);    \
  }
      STDLIB_MATH_FUNCTION_LIST(STDLIB_MATH_FUNC)
#undef STDLIB_MATH_FUNC
#define STDLIB_MATH_CONST(cname, const_value)                       \
  case wasm::AsmJsParser::StandardMember::kMath##cname: {           \
    Handle<Name> name(isolate->factory()->InternalizeOneByteString( \
        STATIC_CHAR_VECTOR(#cname)));                               \
    Handle<Object> value = StdlibMathMember(isolate, stdlib, name); \
    return value->IsNumber() && value->Number() == const_value;     \
  }
      STDLIB_MATH_VALUE_LIST(STDLIB_MATH_CONST)
#undef STDLIB_MATH_CONST
#define STDLIB_ARRAY_TYPE(fname, FName)                               \
  case wasm::AsmJsParser::StandardMember::k##FName: {                 \
    *is_typed_array = true;                                           \
    Handle<Name> name(isolate->factory()->InternalizeOneByteString(   \
        STATIC_CHAR_VECTOR(#FName)));                                 \
    Handle<Object> value = JSReceiver::GetDataProperty(stdlib, name); \
    if (!value->IsJSFunction()) return false;                         \
    Handle<JSFunction> func = Handle<JSFunction>::cast(value);        \
    return func.is_identical_to(isolate->fname());                    \
  }
      STDLIB_ARRAY_TYPE(int8_array_fun, Int8Array)
      STDLIB_ARRAY_TYPE(uint8_array_fun, Uint8Array)
      STDLIB_ARRAY_TYPE(int16_array_fun, Int16Array)
      STDLIB_ARRAY_TYPE(uint16_array_fun, Uint16Array)
      STDLIB_ARRAY_TYPE(int32_array_fun, Int32Array)
      STDLIB_ARRAY_TYPE(uint32_array_fun, Uint32Array)
      STDLIB_ARRAY_TYPE(float32_array_fun, Float32Array)
      STDLIB_ARRAY_TYPE(float64_array_fun, Float64Array)
#undef STDLIB_ARRAY_TYPE
  }
  UNREACHABLE();
  return false;
}

}  // namespace

MaybeHandle<FixedArray> AsmJs::CompileAsmViaWasm(CompilationInfo* info) {
  wasm::ZoneBuffer* module = nullptr;
  wasm::ZoneBuffer* asm_offsets = nullptr;
  Handle<FixedArray> uses_array;
  base::ElapsedTimer asm_wasm_timer;
  asm_wasm_timer.Start();
  size_t asm_wasm_zone_start = info->zone()->allocation_size();
  {
    wasm::AsmJsParser parser(info->isolate(), info->zone(), info->script(),
                             info->literal()->start_position(),
                             info->literal()->end_position());
    if (!parser.Run()) {
      DCHECK(!info->isolate()->has_pending_exception());
      if (!FLAG_suppress_asm_messages) {
        MessageLocation location(info->script(), parser.failure_location(),
                                 parser.failure_location());
        Handle<String> message =
            info->isolate()
                ->factory()
                ->NewStringFromUtf8(CStrVector(parser.failure_message()))
                .ToHandleChecked();
        Handle<JSMessageObject> error_message =
            MessageHandler::MakeMessageObject(
                info->isolate(), MessageTemplate::kAsmJsInvalid, &location,
                message, Handle<FixedArray>::null());
        error_message->set_error_level(v8::Isolate::kMessageWarning);
        MessageHandler::ReportMessage(info->isolate(), &location,
                                      error_message);
      }
      return MaybeHandle<FixedArray>();
    }
    Zone* zone = info->zone();
    module = new (zone) wasm::ZoneBuffer(zone);
    parser.module_builder()->WriteTo(*module);
    asm_offsets = new (zone) wasm::ZoneBuffer(zone);
    parser.module_builder()->WriteAsmJsOffsetTable(*asm_offsets);
    uses_array = info->isolate()->factory()->NewFixedArray(
        static_cast<int>(parser.stdlib_uses()->size()));
    int count = 0;
    for (auto i : *parser.stdlib_uses()) {
      uses_array->set(count++, Smi::FromInt(i));
    }
  }

  double asm_wasm_time = asm_wasm_timer.Elapsed().InMillisecondsF();
  size_t asm_wasm_zone = info->zone()->allocation_size() - asm_wasm_zone_start;
  if (FLAG_trace_asm_parser) {
    PrintF("[asm.js translation successful: time=%0.3fms, zone=%" PRIuS "KB]\n",
           asm_wasm_time, asm_wasm_zone / KB);
  }

  Vector<const byte> asm_offsets_vec(asm_offsets->begin(),
                                     static_cast<int>(asm_offsets->size()));

  base::ElapsedTimer compile_timer;
  compile_timer.Start();
  wasm::ErrorThrower thrower(info->isolate(),
                             "Asm.js -> WebAssembly conversion");
  MaybeHandle<JSObject> compiled = SyncCompileTranslatedAsmJs(
      info->isolate(), &thrower,
      wasm::ModuleWireBytes(module->begin(), module->end()), info->script(),
      asm_offsets_vec);
  DCHECK(!compiled.is_null());
  DCHECK(!thrower.error());
  double compile_time = compile_timer.Elapsed().InMillisecondsF();
  DCHECK_GE(module->end(), module->begin());
  uintptr_t wasm_size = module->end() - module->begin();

  Handle<FixedArray> result =
      info->isolate()->factory()->NewFixedArray(kWasmDataEntryCount);
  result->set(kWasmDataCompiledModule, *compiled.ToHandleChecked());
  result->set(kWasmDataUsesArray, *uses_array);

  MessageLocation location(info->script(), info->literal()->position(),
                           info->literal()->position());
  char text[100];
  int length;
  if (FLAG_predictable) {
    length = base::OS::SNPrintF(text, arraysize(text), "success");
  } else {
    length = base::OS::SNPrintF(
        text, arraysize(text),
        "success, asm->wasm: %0.3f ms, compile: %0.3f ms, %" PRIuPTR " bytes",
        asm_wasm_time, compile_time, wasm_size);
  }
  DCHECK_NE(-1, length);
  USE(length);
  Handle<String> stext(info->isolate()->factory()->InternalizeUtf8String(text));
  Handle<JSMessageObject> message = MessageHandler::MakeMessageObject(
      info->isolate(), MessageTemplate::kAsmJsCompiled, &location, stext,
      Handle<FixedArray>::null());
  message->set_error_level(v8::Isolate::kMessageInfo);
  if (!FLAG_suppress_asm_messages && FLAG_trace_asm_time) {
    MessageHandler::ReportMessage(info->isolate(), &location, message);
  }

  return result;
}

MaybeHandle<Object> AsmJs::InstantiateAsmWasm(Isolate* isolate,
                                              Handle<SharedFunctionInfo> shared,
                                              Handle<FixedArray> wasm_data,
                                              Handle<JSReceiver> stdlib,
                                              Handle<JSReceiver> foreign,
                                              Handle<JSArrayBuffer> memory) {
  base::ElapsedTimer instantiate_timer;
  instantiate_timer.Start();
  Handle<FixedArray> stdlib_uses(
      FixedArray::cast(wasm_data->get(kWasmDataUsesArray)));
  Handle<WasmModuleObject> module(
      WasmModuleObject::cast(wasm_data->get(kWasmDataCompiledModule)));

  // Check that all used stdlib members are valid.
  bool stdlib_use_of_typed_array_present = false;
  for (int i = 0; i < stdlib_uses->length(); ++i) {
    if (stdlib.is_null()) return MaybeHandle<Object>();
    int member_id = Smi::cast(stdlib_uses->get(i))->value();
    wasm::AsmJsParser::StandardMember member =
        static_cast<wasm::AsmJsParser::StandardMember>(member_id);
    if (!IsStdlibMemberValid(isolate, stdlib, member,
                             &stdlib_use_of_typed_array_present)) {
      return MaybeHandle<Object>();
    }
  }

  // Create the ffi object for foreign functions {"": foreign}.
  Handle<JSObject> ffi_object;
  if (!foreign.is_null()) {
    Handle<JSFunction> object_function = Handle<JSFunction>(
        isolate->native_context()->object_function(), isolate);
    ffi_object = isolate->factory()->NewJSObject(object_function);
    JSObject::AddProperty(ffi_object, isolate->factory()->empty_string(),
                          foreign, NONE);
  }

  // Check that a valid heap buffer is provided if required.
  if (stdlib_use_of_typed_array_present) {
    if (memory.is_null()) return MaybeHandle<Object>();
    size_t size = NumberToSize(memory->byte_length());
    // TODO(mstarzinger): We currently only limit byte length of the buffer to
    // be a multiple of 8, we should enforce the stricter spec limits here.
    if (size % FixedTypedArrayBase::kMaxElementSize != 0) {
      return MaybeHandle<Object>();
    }
  }

  wasm::ErrorThrower thrower(isolate, "Asm.js -> WebAssembly instantiation");
  MaybeHandle<Object> maybe_module_object =
      wasm::SyncInstantiate(isolate, &thrower, module, ffi_object, memory);
  if (maybe_module_object.is_null()) {
    thrower.Reset();  // Ensure exceptions do not propagate.
    return MaybeHandle<Object>();
  }
  DCHECK(!thrower.error());
  Handle<Object> module_object = maybe_module_object.ToHandleChecked();

  Handle<Name> single_function_name(
      isolate->factory()->InternalizeUtf8String(AsmJs::kSingleFunctionName));
  MaybeHandle<Object> single_function =
      Object::GetProperty(module_object, single_function_name);
  if (!single_function.is_null() &&
      !single_function.ToHandleChecked()->IsUndefined(isolate)) {
    return single_function;
  }

  int position = shared->start_position();
  Handle<Script> script(Script::cast(shared->script()));
  MessageLocation location(script, position, position);
  char text[50];
  int length;
  if (FLAG_predictable) {
    length = base::OS::SNPrintF(text, arraysize(text), "success");
  } else {
    length = base::OS::SNPrintF(text, arraysize(text), "success, %0.3f ms",
                                instantiate_timer.Elapsed().InMillisecondsF());
  }
  DCHECK_NE(-1, length);
  USE(length);
  Handle<String> stext(isolate->factory()->InternalizeUtf8String(text));
  Handle<JSMessageObject> message = MessageHandler::MakeMessageObject(
      isolate, MessageTemplate::kAsmJsInstantiated, &location, stext,
      Handle<FixedArray>::null());
  message->set_error_level(v8::Isolate::kMessageInfo);
  if (!FLAG_suppress_asm_messages && FLAG_trace_asm_time) {
    MessageHandler::ReportMessage(isolate, &location, message);
  }

  Handle<String> exports_name =
      isolate->factory()->InternalizeUtf8String("exports");
  return Object::GetProperty(module_object, exports_name);
}

}  // namespace internal
}  // namespace v8
