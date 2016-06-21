// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api-natives.h"
#include "src/api.h"
#include "src/assert-scope.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/execution.h"
#include "src/factory.h"
#include "src/handles.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "src/parsing/parser.h"
#include "src/typing-asm.h"

#include "src/wasm/asm-wasm-builder.h"
#include "src/wasm/encoder.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-result.h"

typedef uint8_t byte;

using v8::internal::wasm::ErrorThrower;

namespace v8 {

namespace {
struct RawBuffer {
  const byte* start;
  const byte* end;
  size_t size() { return static_cast<size_t>(end - start); }
};

RawBuffer GetRawBufferSource(
    v8::Local<v8::Value> source, ErrorThrower* thrower) {
  const byte* start = nullptr;
  const byte* end = nullptr;

  if (source->IsArrayBuffer()) {
    // A raw array buffer was passed.
    Local<ArrayBuffer> buffer = Local<ArrayBuffer>::Cast(source);
    ArrayBuffer::Contents contents = buffer->GetContents();

    start = reinterpret_cast<const byte*>(contents.Data());
    end = start + contents.ByteLength();

    if (start == nullptr || end == start) {
      thrower->Error("ArrayBuffer argument is empty");
    }
  } else if (source->IsTypedArray()) {
    // A TypedArray was passed.
    Local<TypedArray> array = Local<TypedArray>::Cast(source);
    Local<ArrayBuffer> buffer = array->Buffer();

    ArrayBuffer::Contents contents = buffer->GetContents();

    start =
        reinterpret_cast<const byte*>(contents.Data()) + array->ByteOffset();
    end = start + array->ByteLength();

    if (start == nullptr || end == start) {
      thrower->Error("ArrayBuffer argument is empty");
    }
  } else {
    thrower->Error("Argument 0 must be an ArrayBuffer or Uint8Array");
  }

  return {start, end};
}

void VerifyModule(const v8::FunctionCallbackInfo<v8::Value>& args) {
  HandleScope scope(args.GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(args.GetIsolate());
  ErrorThrower thrower(isolate, "Wasm.verifyModule()");

  if (args.Length() < 1) {
    thrower.Error("Argument 0 must be a buffer source");
    return;
  }
  RawBuffer buffer = GetRawBufferSource(args[0], &thrower);
  if (thrower.error()) return;

  i::Zone zone(isolate->allocator());
  internal::wasm::ModuleResult result =
      internal::wasm::DecodeWasmModule(isolate, &zone, buffer.start, buffer.end,
                                       true, internal::wasm::kWasmOrigin);

  if (result.failed()) {
    thrower.Failed("", result);
  }

  if (result.val) delete result.val;
}

void VerifyFunction(const v8::FunctionCallbackInfo<v8::Value>& args) {
  HandleScope scope(args.GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(args.GetIsolate());
  ErrorThrower thrower(isolate, "Wasm.verifyFunction()");

  if (args.Length() < 1) {
    thrower.Error("Argument 0 must be a buffer source");
    return;
  }
  RawBuffer buffer = GetRawBufferSource(args[0], &thrower);
  if (thrower.error()) return;

  internal::wasm::FunctionResult result;
  {
    // Verification of a single function shouldn't allocate.
    i::DisallowHeapAllocation no_allocation;
    i::Zone zone(isolate->allocator());
    result = internal::wasm::DecodeWasmFunction(isolate, &zone, nullptr,
                                                buffer.start, buffer.end);
  }

  if (result.failed()) {
    thrower.Failed("", result);
  }

  if (result.val) delete result.val;
}

v8::internal::wasm::ZoneBuffer* TranslateAsmModule(
    i::ParseInfo* info, ErrorThrower* thrower,
    i::Handle<i::FixedArray>* foreign_args) {
  info->set_global();
  info->set_lazy(false);
  info->set_allow_lazy_parsing(false);
  info->set_toplevel(true);

  if (!i::Compiler::ParseAndAnalyze(info)) {
    return nullptr;
  }

  if (info->scope()->declarations()->length() == 0) {
    thrower->Error("Asm.js validation failed: no declarations in scope");
    return nullptr;
  }

  info->set_literal(
      info->scope()->declarations()->at(0)->AsFunctionDeclaration()->fun());

  v8::internal::AsmTyper typer(info->isolate(), info->zone(), *(info->script()),
                               info->literal());
  if (i::FLAG_enable_simd_asmjs) {
    typer.set_allow_simd(true);
  }
  if (!typer.Validate()) {
    thrower->Error("Asm.js validation failed: %s", typer.error_message());
    return nullptr;
  }

  v8::internal::wasm::AsmWasmBuilder builder(info->isolate(), info->zone(),
                                             info->literal(), &typer);

  return builder.Run(foreign_args);
}

i::MaybeHandle<i::JSObject> InstantiateModuleCommon(
    const v8::FunctionCallbackInfo<v8::Value>& args, const byte* start,
    const byte* end, ErrorThrower* thrower,
    internal::wasm::ModuleOrigin origin = i::wasm::kWasmOrigin) {
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(args.GetIsolate());

  // Decode but avoid a redundant pass over function bodies for verification.
  // Verification will happen during compilation.
  i::Zone zone(isolate->allocator());
  internal::wasm::ModuleResult result = internal::wasm::DecodeWasmModule(
      isolate, &zone, start, end, false, origin);

  i::MaybeHandle<i::JSObject> object;
  if (result.failed() && origin == internal::wasm::kAsmJsOrigin) {
    thrower->Error("Asm.js converted module failed to decode");
  } else if (result.failed()) {
    thrower->Failed("", result);
  } else {
    // Success. Instantiate the module and return the object.
    i::Handle<i::JSReceiver> ffi = i::Handle<i::JSObject>::null();
    if (args.Length() > 1 && args[1]->IsObject()) {
      Local<Object> obj = Local<Object>::Cast(args[1]);
      ffi = i::Handle<i::JSReceiver>::cast(v8::Utils::OpenHandle(*obj));
    }

    i::Handle<i::JSArrayBuffer> memory = i::Handle<i::JSArrayBuffer>::null();
    if (args.Length() > 2 && args[2]->IsArrayBuffer()) {
      Local<Object> obj = Local<Object>::Cast(args[2]);
      i::Handle<i::Object> mem_obj = v8::Utils::OpenHandle(*obj);
      memory = i::Handle<i::JSArrayBuffer>(i::JSArrayBuffer::cast(*mem_obj));
    }

    object = result.val->Instantiate(isolate, ffi, memory);
    if (!object.is_null()) {
      args.GetReturnValue().Set(v8::Utils::ToLocal(object.ToHandleChecked()));
    }
  }

  if (result.val) delete result.val;
  return object;
}

void InstantiateModuleFromAsm(const v8::FunctionCallbackInfo<v8::Value>& args) {
  HandleScope scope(args.GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(args.GetIsolate());
  ErrorThrower thrower(isolate, "Wasm.instantiateModuleFromAsm()");

  if (!args[0]->IsString()) {
    thrower.Error("Asm module text should be a string");
    return;
  }

  i::Factory* factory = isolate->factory();
  i::Zone zone(isolate->allocator());
  Local<String> source = Local<String>::Cast(args[0]);
  i::Handle<i::Script> script = factory->NewScript(Utils::OpenHandle(*source));
  i::ParseInfo info(&zone, script);

  i::Handle<i::Object> foreign;
  if (args.Length() > 1 && args[1]->IsObject()) {
    Local<Object> local_foreign = Local<Object>::Cast(args[1]);
    foreign = v8::Utils::OpenHandle(*local_foreign);
  }

  i::Handle<i::FixedArray> foreign_args;
  auto module = TranslateAsmModule(&info, &thrower, &foreign_args);
  if (module == nullptr) {
    return;
  }

  i::MaybeHandle<i::Object> maybe_module_object =
      InstantiateModuleCommon(args, module->begin(), module->end(), &thrower,
                              internal::wasm::kAsmJsOrigin);
  if (maybe_module_object.is_null()) {
    return;
  }

  i::Handle<i::Name> name =
      factory->NewStringFromStaticChars("__foreign_init__");

  i::Handle<i::Object> module_object = maybe_module_object.ToHandleChecked();
  i::MaybeHandle<i::Object> maybe_init =
      i::Object::GetProperty(module_object, name);
  DCHECK(!maybe_init.is_null());

  i::Handle<i::Object> init = maybe_init.ToHandleChecked();
  i::Handle<i::Object> undefined = isolate->factory()->undefined_value();
  i::Handle<i::Object>* foreign_args_array =
      new i::Handle<i::Object>[foreign_args->length()];
  for (int j = 0; j < foreign_args->length(); j++) {
    if (!foreign.is_null()) {
      i::MaybeHandle<i::Name> name = i::Object::ToName(
          isolate, i::Handle<i::Object>(foreign_args->get(j), isolate));
      if (!name.is_null()) {
        i::MaybeHandle<i::Object> val =
            i::Object::GetProperty(foreign, name.ToHandleChecked());
        if (!val.is_null()) {
          foreign_args_array[j] = val.ToHandleChecked();
          continue;
        }
      }
    }
    foreign_args_array[j] = undefined;
  }
  i::MaybeHandle<i::Object> retval = i::Execution::Call(
      isolate, init, undefined, foreign_args->length(), foreign_args_array);
  delete[] foreign_args_array;

  if (retval.is_null()) {
    thrower.Error(
        "WASM.instantiateModuleFromAsm(): foreign init function failed");
  }
}

void InstantiateModule(const v8::FunctionCallbackInfo<v8::Value>& args) {
  HandleScope scope(args.GetIsolate());
  i::Isolate* isolate = reinterpret_cast<i::Isolate*>(args.GetIsolate());
  ErrorThrower thrower(isolate, "Wasm.instantiateModule()");

  if (args.Length() < 1) {
    thrower.Error("Argument 0 must be a buffer source");
    return;
  }
  RawBuffer buffer = GetRawBufferSource(args[0], &thrower);
  if (buffer.start == nullptr) return;

  InstantiateModuleCommon(args, buffer.start, buffer.end, &thrower);
}


static i::MaybeHandle<i::JSObject> CreateModuleObject(
    v8::Isolate* isolate, const v8::Local<v8::Value> source,
    ErrorThrower* thrower) {
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);

  RawBuffer buffer = GetRawBufferSource(source, thrower);
  if (buffer.start == nullptr) return i::MaybeHandle<i::JSObject>();

  // TODO(rossberg): Once we can, do compilation here.
  DCHECK(source->IsArrayBuffer() || source->IsTypedArray());
  Local<Context> context = isolate->GetCurrentContext();
  i::Handle<i::Context> i_context = Utils::OpenHandle(*context);
  i::Handle<i::JSFunction> module_cons(i_context->wasm_module_constructor());
  i::Handle<i::JSObject> module_obj =
      i_isolate->factory()->NewJSObject(module_cons);
  i::Handle<i::Object> module_ref = Utils::OpenHandle(*source);
  i::Handle<i::Symbol> module_sym(i_context->wasm_module_sym());
  i::Object::SetProperty(module_obj, module_sym, module_ref, i::STRICT).Check();

  return module_obj;
}

void WebAssemblyCompile(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  HandleScope scope(isolate);
  ErrorThrower thrower(reinterpret_cast<i::Isolate*>(isolate),
                       "WebAssembly.compile()");

  if (args.Length() < 1) {
    thrower.Error("Argument 0 must be a buffer source");
    return;
  }
  i::MaybeHandle<i::JSObject> module_obj =
      CreateModuleObject(isolate, args[0], &thrower);
  if (module_obj.is_null()) return;

  Local<Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Promise::Resolver> resolver;
  if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return;
  resolver->Resolve(context, Utils::ToLocal(module_obj.ToHandleChecked()));

  v8::ReturnValue<v8::Value> return_value = args.GetReturnValue();
  return_value.Set(resolver->GetPromise());
}

void WebAssemblyModule(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  HandleScope scope(isolate);
  ErrorThrower thrower(reinterpret_cast<i::Isolate*>(isolate),
                       "WebAssembly.Module()");

  if (args.Length() < 1) {
    thrower.Error("Argument 0 must be a buffer source");
    return;
  }
  i::MaybeHandle<i::JSObject> module_obj =
      CreateModuleObject(isolate, args[0], &thrower);
  if (module_obj.is_null()) return;

  v8::ReturnValue<v8::Value> return_value = args.GetReturnValue();
  return_value.Set(Utils::ToLocal(module_obj.ToHandleChecked()));
}

void WebAssemblyInstance(const v8::FunctionCallbackInfo<v8::Value>& args) {
  HandleScope scope(args.GetIsolate());
  v8::Isolate* isolate = args.GetIsolate();
  ErrorThrower thrower(reinterpret_cast<i::Isolate*>(isolate),
                       "WebAssembly.Instance()");

  if (args.Length() < 1) {
    thrower.Error("Argument 0 must be a WebAssembly.Module");
    return;
  }
  Local<Context> context = isolate->GetCurrentContext();
  i::Handle<i::Context> i_context = Utils::OpenHandle(*context);
  i::Handle<i::Symbol> module_sym(i_context->wasm_module_sym());
  i::MaybeHandle<i::Object> source =
      i::Object::GetProperty(Utils::OpenHandle(*args[0]), module_sym);
  if (source.is_null()) return;

  RawBuffer buffer =
      GetRawBufferSource(Utils::ToLocal(source.ToHandleChecked()), &thrower);
  if (buffer.start == nullptr) return;

  InstantiateModuleCommon(args, buffer.start, buffer.end, &thrower);
}
}  // namespace

// TODO(titzer): we use the API to create the function template because the
// internal guts are too ugly to replicate here.
static i::Handle<i::FunctionTemplateInfo> NewTemplate(i::Isolate* i_isolate,
                                                      FunctionCallback func) {
  Isolate* isolate = reinterpret_cast<Isolate*>(i_isolate);
  Local<FunctionTemplate> local = FunctionTemplate::New(isolate, func);
  return v8::Utils::OpenHandle(*local);
}

namespace internal {
static Handle<String> v8_str(Isolate* isolate, const char* str) {
  return isolate->factory()->NewStringFromAsciiChecked(str);
}

static Handle<JSFunction> InstallFunc(Isolate* isolate, Handle<JSObject> object,
                                      const char* str, FunctionCallback func) {
  Handle<String> name = v8_str(isolate, str);
  Handle<FunctionTemplateInfo> temp = NewTemplate(isolate, func);
  Handle<JSFunction> function =
      ApiNatives::InstantiateFunction(temp).ToHandleChecked();
  PropertyAttributes attributes =
      static_cast<PropertyAttributes>(DONT_DELETE | READ_ONLY);
  JSObject::AddProperty(object, name, function, attributes);
  return function;
}

void WasmJs::Install(Isolate* isolate, Handle<JSGlobalObject> global) {
  Factory* factory = isolate->factory();

  // Setup wasm function map.
  Handle<Context> context(global->native_context(), isolate);
  InstallWasmFunctionMap(isolate, context);

  // Bind the experimental WASM object.
  // TODO(rossberg, titzer): remove once it's no longer needed.
  {
    Handle<String> name = v8_str(isolate, "Wasm");
    Handle<JSFunction> cons = factory->NewFunction(name);
    JSFunction::SetInstancePrototype(
        cons, Handle<Object>(context->initial_object_prototype(), isolate));
    cons->shared()->set_instance_class_name(*name);
    Handle<JSObject> wasm_object = factory->NewJSObject(cons, TENURED);
    PropertyAttributes attributes = static_cast<PropertyAttributes>(DONT_ENUM);
    JSObject::AddProperty(global, name, wasm_object, attributes);

    // Install functions on the WASM object.
    InstallFunc(isolate, wasm_object, "verifyModule", VerifyModule);
    InstallFunc(isolate, wasm_object, "verifyFunction", VerifyFunction);
    InstallFunc(isolate, wasm_object, "instantiateModule", InstantiateModule);
    InstallFunc(isolate, wasm_object, "instantiateModuleFromAsm",
                InstantiateModuleFromAsm);

    {
      // Add the Wasm.experimentalVersion property.
      Handle<String> name = v8_str(isolate, "experimentalVersion");
      PropertyAttributes attributes =
          static_cast<PropertyAttributes>(DONT_DELETE | READ_ONLY);
      Handle<Smi> value =
          Handle<Smi>(Smi::FromInt(wasm::kWasmVersion), isolate);
      JSObject::AddProperty(wasm_object, name, value, attributes);
    }
  }

  // Create private symbols.
  Handle<Symbol> module_sym = isolate->factory()->NewPrivateSymbol();
  Handle<Symbol> instance_sym = isolate->factory()->NewPrivateSymbol();
  context->set_wasm_module_sym(*module_sym);
  context->set_wasm_instance_sym(*instance_sym);

  // Bind the WebAssembly object.
  Handle<String> name = v8_str(isolate, "WebAssembly");
  Handle<JSFunction> cons = factory->NewFunction(name);
  JSFunction::SetInstancePrototype(
      cons, Handle<Object>(context->initial_object_prototype(), isolate));
  cons->shared()->set_instance_class_name(*name);
  Handle<JSObject> wasm_object = factory->NewJSObject(cons, TENURED);
  PropertyAttributes attributes = static_cast<PropertyAttributes>(DONT_ENUM);
  JSObject::AddProperty(global, name, wasm_object, attributes);

  // Install static methods on WebAssembly object.
  InstallFunc(isolate, wasm_object, "compile", WebAssemblyCompile);
  Handle<JSFunction> module_constructor =
      InstallFunc(isolate, wasm_object, "Module", WebAssemblyModule);
  Handle<JSFunction> instance_constructor =
      InstallFunc(isolate, wasm_object, "Instance", WebAssemblyInstance);
  context->set_wasm_module_constructor(*module_constructor);
  context->set_wasm_instance_constructor(*instance_constructor);
}

void WasmJs::InstallWasmFunctionMap(Isolate* isolate, Handle<Context> context) {
  if (!context->get(Context::WASM_FUNCTION_MAP_INDEX)->IsMap()) {
    // TODO(titzer): Move this to bootstrapper.cc??
    // TODO(titzer): Also make one for strict mode functions?
    Handle<Map> prev_map = Handle<Map>(context->sloppy_function_map(), isolate);

    InstanceType instance_type = prev_map->instance_type();
    int internal_fields = JSObject::GetInternalFieldCount(*prev_map);
    CHECK_EQ(0, internal_fields);
    int pre_allocated =
        prev_map->GetInObjectProperties() - prev_map->unused_property_fields();
    int instance_size;
    int in_object_properties;
    JSFunction::CalculateInstanceSizeHelper(instance_type, internal_fields + 1,
                                            0, &instance_size,
                                            &in_object_properties);

    int unused_property_fields = in_object_properties - pre_allocated;
    Handle<Map> map = Map::CopyInitialMap(
        prev_map, instance_size, in_object_properties, unused_property_fields);

    context->set_wasm_function_map(*map);
  }
}

}  // namespace internal
}  // namespace v8
