// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/asmjs/asm-js.h"

#include "src/api-natives.h"
#include "src/api.h"
#include "src/asmjs/asm-typer.h"
#include "src/asmjs/asm-wasm-builder.h"
#include "src/assert-scope.h"
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
    compiled_module = result.val->CompileFunctions(isolate, thrower);
  }

  if (result.val) delete result.val;
  return compiled_module;
}

bool IsStdlibMemberValid(i::Isolate* isolate, Handle<JSReceiver> stdlib,
                         i::Handle<i::Object> member_id) {
  int32_t member_kind;
  if (!member_id->ToInt32(&member_kind)) {
    UNREACHABLE();
  }
  switch (member_kind) {
    case wasm::AsmTyper::StandardMember::kNone:
    case wasm::AsmTyper::StandardMember::kModule:
    case wasm::AsmTyper::StandardMember::kStdlib: {
      // Nothing to check for these.
      return true;
    }
    case wasm::AsmTyper::StandardMember::kNaN: {
      i::Handle<i::Name> name(isolate->factory()->InternalizeOneByteString(
          STATIC_CHAR_VECTOR("NaN")));
      i::MaybeHandle<i::Object> maybe_value =
          i::Object::GetProperty(stdlib, name);
      if (maybe_value.is_null()) {
        return false;
      }
      i::Handle<i::Object> value = maybe_value.ToHandleChecked();
      if (!value->IsNaN()) {
        return false;
      }
      return true;
    }
    case wasm::AsmTyper::StandardMember::kHeap:
    case wasm::AsmTyper::StandardMember::kFFI:
    case wasm::AsmTyper::StandardMember::kInfinity:
    case wasm::AsmTyper::StandardMember::kMathAcos:
    case wasm::AsmTyper::StandardMember::kMathAsin:
    case wasm::AsmTyper::StandardMember::kMathAtan:
    case wasm::AsmTyper::StandardMember::kMathCos:
    case wasm::AsmTyper::StandardMember::kMathSin:
    case wasm::AsmTyper::StandardMember::kMathTan:
    case wasm::AsmTyper::StandardMember::kMathExp:
    case wasm::AsmTyper::StandardMember::kMathLog:
    case wasm::AsmTyper::StandardMember::kMathCeil:
    case wasm::AsmTyper::StandardMember::kMathFloor:
    case wasm::AsmTyper::StandardMember::kMathSqrt:
    case wasm::AsmTyper::StandardMember::kMathAbs:
    case wasm::AsmTyper::StandardMember::kMathClz32:
    case wasm::AsmTyper::StandardMember::kMathMin:
    case wasm::AsmTyper::StandardMember::kMathMax:
    case wasm::AsmTyper::StandardMember::kMathAtan2:
    case wasm::AsmTyper::StandardMember::kMathPow:
    case wasm::AsmTyper::StandardMember::kMathImul:
    case wasm::AsmTyper::StandardMember::kMathFround:
    case wasm::AsmTyper::StandardMember::kMathE:
    case wasm::AsmTyper::StandardMember::kMathLN10:
    case wasm::AsmTyper::StandardMember::kMathLN2:
    case wasm::AsmTyper::StandardMember::kMathLOG2E:
    case wasm::AsmTyper::StandardMember::kMathLOG10E:
    case wasm::AsmTyper::StandardMember::kMathPI:
    case wasm::AsmTyper::StandardMember::kMathSQRT1_2:
    case wasm::AsmTyper::StandardMember::kMathSQRT2:
      // TODO(bradnelson) Actually check these.
      return true;
    default: { UNREACHABLE(); }
  }
  return false;
}

}  // namespace

MaybeHandle<FixedArray> AsmJs::ConvertAsmToWasm(ParseInfo* info) {
  ErrorThrower thrower(info->isolate(), "Asm.js -> WebAssembly conversion");
  wasm::AsmTyper typer(info->isolate(), info->zone(), *(info->script()),
                       info->literal());
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

  wasm::AsmTyper::StdlibSet uses = typer.StdlibUses();
  Handle<FixedArray> uses_array =
      info->isolate()->factory()->NewFixedArray(static_cast<int>(uses.size()));
  int count = 0;
  for (auto i : uses) {
    uses_array->set(count++, Smi::FromInt(i));
  }

  Handle<FixedArray> result = info->isolate()->factory()->NewFixedArray(3);
  result->set(0, *compiled.ToHandleChecked());
  result->set(1, *foreign_globals);
  result->set(2, *uses_array);
  return result;
}

bool AsmJs::IsStdlibValid(i::Isolate* isolate, Handle<FixedArray> wasm_data,
                          Handle<JSReceiver> stdlib) {
  i::Handle<i::FixedArray> uses(i::FixedArray::cast(wasm_data->get(2)));
  for (int i = 0; i < uses->length(); ++i) {
    if (!IsStdlibMemberValid(isolate, stdlib,
                             uses->GetValueChecked<i::Object>(isolate, i))) {
      return false;
    }
  }
  return true;
}

MaybeHandle<Object> AsmJs::InstantiateAsmWasm(i::Isolate* isolate,
                                              Handle<FixedArray> wasm_data,
                                              Handle<JSArrayBuffer> memory,
                                              Handle<JSReceiver> foreign) {
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
