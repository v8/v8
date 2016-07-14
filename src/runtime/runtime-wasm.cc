// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/assembler.h"
#include "src/compiler/wasm-compiler.h"
#include "src/conversions.h"
#include "src/debug/debug.h"
#include "src/factory.h"
#include "src/frames-inl.h"
#include "src/objects-inl.h"
#include "src/v8memory.h"
#include "src/wasm/wasm-module.h"

namespace v8 {
namespace internal {

namespace {
const int kWasmMemArrayBuffer = 2;
}

RUNTIME_FUNCTION(Runtime_WasmGrowMemory) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  uint32_t delta_pages = 0;
  CHECK(args[0]->ToUint32(&delta_pages));
  Handle<JSObject> module_object;

  {
    // Get the module JSObject
    DisallowHeapAllocation no_allocation;
    const Address entry = Isolate::c_entry_fp(isolate->thread_local_top());
    Address pc =
        Memory::Address_at(entry + StandardFrameConstants::kCallerPCOffset);
    Code* code =
        isolate->inner_pointer_to_code_cache()->GetCacheEntry(pc)->code;
    FixedArray* deopt_data = code->deoptimization_data();
    DCHECK(deopt_data->length() == 2);
    module_object = Handle<JSObject>::cast(handle(deopt_data->get(0), isolate));
    CHECK(!module_object->IsNull(isolate));
  }

  Address old_mem_start, new_mem_start;
  uint32_t old_size, new_size;

  // Get mem buffer associated with module object
  Handle<Object> obj(module_object->GetInternalField(kWasmMemArrayBuffer),
                     isolate);

  if (obj->IsUndefined(isolate)) {
    // If module object does not have linear memory associated with it,
    // Allocate new array buffer of given size.
    old_mem_start = nullptr;
    old_size = 0;
    // TODO(gdeepti): Fix bounds check to take into account size of memtype.
    new_size = delta_pages * wasm::WasmModule::kPageSize;
    if (delta_pages > wasm::WasmModule::kMaxMemPages) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewRangeError(MessageTemplate::kWasmTrapMemOutOfBounds));
    }
    new_mem_start =
        static_cast<Address>(isolate->array_buffer_allocator()->Allocate(
            static_cast<uint32_t>(new_size)));
    if (new_mem_start == NULL) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewRangeError(MessageTemplate::kWasmTrapMemAllocationFail));
    }
#if DEBUG
    // Double check the API allocator actually zero-initialized the memory.
    for (size_t i = old_size; i < new_size; i++) {
      DCHECK_EQ(0, new_mem_start[i]);
    }
#endif
  } else {
    Handle<JSArrayBuffer> old_buffer = Handle<JSArrayBuffer>::cast(obj);
    old_mem_start = static_cast<Address>(old_buffer->backing_store());
    old_size = old_buffer->byte_length()->Number();
    // If the old memory was zero-sized, we should have been in the
    // "undefined" case above.
    DCHECK_NOT_NULL(old_mem_start);
    DCHECK_NE(0, old_size);

    new_size = old_size + delta_pages * wasm::WasmModule::kPageSize;
    if (new_size >
        wasm::WasmModule::kMaxMemPages * wasm::WasmModule::kPageSize) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewRangeError(MessageTemplate::kWasmTrapMemOutOfBounds));
    }
    new_mem_start = static_cast<Address>(realloc(old_mem_start, new_size));
    if (new_mem_start == NULL) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewRangeError(MessageTemplate::kWasmTrapMemAllocationFail));
    }
    old_buffer->set_is_external(true);
    isolate->heap()->UnregisterArrayBuffer(*old_buffer);
    // Zero initializing uninitialized memory from realloc
    memset(new_mem_start + old_size, 0, new_size - old_size);
  }

  Handle<JSArrayBuffer> buffer = isolate->factory()->NewJSArrayBuffer();
  JSArrayBuffer::Setup(buffer, isolate, false, new_mem_start, new_size);
  buffer->set_is_neuterable(false);

  // Set new buffer to be wasm memory
  module_object->SetInternalField(kWasmMemArrayBuffer, *buffer);

  CHECK(wasm::UpdateWasmModuleMemory(module_object, old_mem_start,
                                     new_mem_start, old_size, new_size));

  return *isolate->factory()->NewNumberFromUint(old_size /
                                                wasm::WasmModule::kPageSize);
}

RUNTIME_FUNCTION(Runtime_JITSingleFunction) {
  const int fixed_args = 6;

  HandleScope scope(isolate);
  DCHECK_LE(fixed_args, args.length());
  CONVERT_SMI_ARG_CHECKED(base, 0);
  CONVERT_SMI_ARG_CHECKED(length, 1);
  CONVERT_SMI_ARG_CHECKED(index, 2);
  CONVERT_ARG_HANDLE_CHECKED(FixedArray, function_table, 3);
  CONVERT_UINT32_ARG_CHECKED(sig_index, 4);
  CONVERT_SMI_ARG_CHECKED(return_count, 5);

  Handle<JSObject> module_object;

  {
    // Get the module JSObject
    DisallowHeapAllocation no_allocation;
    const Address entry = Isolate::c_entry_fp(isolate->thread_local_top());
    Address pc =
        Memory::Address_at(entry + StandardFrameConstants::kCallerPCOffset);
    Code* code =
        isolate->inner_pointer_to_code_cache()->GetCacheEntry(pc)->code;
    FixedArray* deopt_data = code->deoptimization_data();
    DCHECK(deopt_data->length() == 2);
    module_object = Handle<JSObject>::cast(handle(deopt_data->get(0), isolate));
    CHECK(!module_object->IsNull(isolate));
  }

  // Get mem buffer associated with module object
  Handle<Object> obj(module_object->GetInternalField(kWasmMemArrayBuffer),
                     isolate);

  if (obj->IsUndefined(isolate)) {
    return isolate->heap()->undefined_value();
  }

  Handle<JSArrayBuffer> mem_buffer = Handle<JSArrayBuffer>::cast(obj);

  wasm::WasmModule module(reinterpret_cast<byte*>(mem_buffer->backing_store()));
  wasm::ErrorThrower thrower(isolate, "JITSingleFunction");
  wasm::ModuleEnv module_env;
  module_env.module = &module;
  module_env.instance = nullptr;
  module_env.origin = wasm::kWasmOrigin;

  uint32_t signature_size = args.length() - fixed_args;
  wasm::LocalType* sig_types = new wasm::LocalType[signature_size];

  for (uint32_t i = 0; i < signature_size; ++i) {
    CONVERT_SMI_ARG_CHECKED(sig_type, i + fixed_args);
    sig_types[i] = static_cast<wasm::LocalType>(sig_type);
  }
  wasm::FunctionSig sig(return_count, signature_size - return_count, sig_types);

  wasm::WasmFunction func;
  func.sig = &sig;
  func.func_index = index;
  func.sig_index = sig_index;
  func.name_offset = 0;
  func.name_length = 0;
  func.code_start_offset = base;
  func.code_end_offset = base + length;

  Handle<Code> code = compiler::WasmCompilationUnit::CompileWasmFunction(
      &thrower, isolate, &module_env, &func);

  delete[] sig_types;
  if (thrower.error()) {
    return isolate->heap()->undefined_value();
  }

  function_table->set(index, Smi::FromInt(sig_index));
  function_table->set(index + function_table->length() / 2, *code);

  return isolate->heap()->undefined_value();
}
}  // namespace internal
}  // namespace v8
