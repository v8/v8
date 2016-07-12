// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/asmjs/asm-js.h"

#include "src/api-natives.h"
#include "src/api.h"
#include "src/asmjs/asm-wasm-builder.h"
#include "src/asmjs/typing-asm.h"
#include "src/assert-scope.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/execution.h"
#include "src/factory.h"
#include "src/handles.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "src/parsing/parser.h"

#include "src/wasm/encoder.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-result.h"

typedef uint8_t byte;

using v8::internal::wasm::ErrorThrower;

namespace v8 {
namespace internal {

namespace {
i::MaybeHandle<i::FixedArray> CompileModule(
    i::Isolate* isolate, const byte* start, const byte* end,
    ErrorThrower* thrower,
    internal::wasm::ModuleOrigin origin = i::wasm::kWasmOrigin) {
  // Decode but avoid a redundant pass over function bodies for verification.
  // Verification will happen during compilation.
  i::Zone zone(isolate->allocator());
  internal::wasm::ModuleResult result = internal::wasm::DecodeWasmModule(
      isolate, &zone, start, end, false, origin);

  i::MaybeHandle<i::FixedArray> compiled_module;
  if (result.failed() && origin == internal::wasm::kAsmJsOrigin) {
    thrower->Error("Asm.js converted module failed to decode");
  } else if (result.failed()) {
    thrower->Failed("", result);
  } else {
    compiled_module = result.val->CompileFunctions(isolate);
  }

  if (result.val) delete result.val;
  return compiled_module;
}

}  // namespace

MaybeHandle<FixedArray> AsmJs::ConvertAsmToWasm(ParseInfo* info) {
  ErrorThrower thrower(info->isolate(), "Asm.js -> WebAssembly conversion");
  AsmTyper typer(info->isolate(), info->zone(), *(info->script()),
                 info->literal());
  typer.set_fixed_signature(true);
  if (i::FLAG_enable_simd_asmjs) {
    typer.set_allow_simd(true);
  }
  if (!typer.Validate()) {
    DCHECK(!info->isolate()->has_pending_exception());
    PrintF("Validation of asm.js module failed: %s", typer.error_message());
    return MaybeHandle<FixedArray>();
  }
  v8::internal::wasm::AsmWasmBuilder builder(info->isolate(), info->zone(),
                                             info->literal(), &typer);
  i::Handle<i::FixedArray> foreign_globals;
  auto module = builder.Run(&foreign_globals);

  i::MaybeHandle<i::FixedArray> compiled =
      CompileModule(info->isolate(), module->begin(), module->end(), &thrower,
                    internal::wasm::kAsmJsOrigin);
  DCHECK(!compiled.is_null());

  Handle<FixedArray> result = info->isolate()->factory()->NewFixedArray(2);
  result->set(0, *compiled.ToHandleChecked());
  result->set(1, *foreign_globals);
  return result;
}

MaybeHandle<Object> AsmJs::InstantiateAsmWasm(i::Isolate* isolate,
                                              Handle<FixedArray> wasm_data,
                                              Handle<JSArrayBuffer> memory,
                                              Handle<JSObject> foreign) {
  i::Handle<i::FixedArray> compiled(i::FixedArray::cast(wasm_data->get(0)));
  i::Handle<i::FixedArray> foreign_globals(
      i::FixedArray::cast(wasm_data->get(1)));

  ErrorThrower thrower(isolate, "Asm.js -> WebAssembly instantiation");

  i::MaybeHandle<i::JSObject> maybe_module_object =
      i::wasm::WasmModule::Instantiate(isolate, compiled, foreign, memory);
  if (maybe_module_object.is_null()) {
    return MaybeHandle<Object>();
  }

  i::Handle<i::Name> name(isolate->factory()->InternalizeOneByteString(
      STATIC_CHAR_VECTOR("__foreign_init__")));

  i::Handle<i::Object> module_object = maybe_module_object.ToHandleChecked();
  i::MaybeHandle<i::Object> maybe_init =
      i::Object::GetProperty(module_object, name);
  DCHECK(!maybe_init.is_null());

  i::Handle<i::Object> init = maybe_init.ToHandleChecked();
  i::Handle<i::Object> undefined(isolate->heap()->undefined_value(), isolate);
  i::Handle<i::Object>* foreign_args_array =
      new i::Handle<i::Object>[foreign_globals->length()];
  for (int j = 0; j < foreign_globals->length(); j++) {
    if (!foreign.is_null()) {
      i::MaybeHandle<i::Name> name = i::Object::ToName(
          isolate, i::Handle<i::Object>(foreign_globals->get(j), isolate));
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
      isolate, init, undefined, foreign_globals->length(), foreign_args_array);
  delete[] foreign_args_array;

  if (retval.is_null()) {
    thrower.Error(
        "WASM.instantiateModuleFromAsm(): foreign init function failed");
    return MaybeHandle<Object>();
  }
  return maybe_module_object;
}

}  // namespace internal
}  // namespace v8
