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

RUNTIME_FUNCTION(Runtime_WasmGrowMemory) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  CONVERT_UINT32_ARG_CHECKED(delta_pages, 0);
  Handle<JSObject> module_instance;

  {
    // Get the module JSObject
    DisallowHeapAllocation no_allocation;
    const Address entry = Isolate::c_entry_fp(isolate->thread_local_top());
    Address pc =
        Memory::Address_at(entry + StandardFrameConstants::kCallerPCOffset);
    Code* code =
        isolate->inner_pointer_to_code_cache()->GetCacheEntry(pc)->code;
    Object* undefined = *isolate->factory()->undefined_value();
    Object* owning_instance = wasm::GetOwningWasmInstance(undefined, code);
    CHECK_NOT_NULL(owning_instance);
    CHECK_NE(owning_instance, undefined);
    module_instance = handle(JSObject::cast(owning_instance), isolate);
  }

  Address old_mem_start, new_mem_start;
  uint32_t old_size, new_size;

  // Get mem buffer associated with module object
  MaybeHandle<JSArrayBuffer> maybe_mem_buffer =
      wasm::GetInstanceMemory(isolate, module_instance);
  Handle<JSArrayBuffer> old_buffer;
  if (!maybe_mem_buffer.ToHandle(&old_buffer)) {
    // If module object does not have linear memory associated with it,
    // Allocate new array buffer of given size.
    old_mem_start = nullptr;
    old_size = 0;
    // TODO(gdeepti): Fix bounds check to take into account size of memtype.
    new_size = delta_pages * wasm::WasmModule::kPageSize;
    // The code generated in the wasm compiler guarantees this precondition.
    DCHECK(delta_pages <= wasm::WasmModule::kMaxMemPages);
    new_mem_start =
        static_cast<Address>(isolate->array_buffer_allocator()->Allocate(
            static_cast<uint32_t>(new_size)));
    if (new_mem_start == NULL) {
      return *isolate->factory()->NewNumberFromInt(-1);
    }
#if DEBUG
    // Double check the API allocator actually zero-initialized the memory.
    for (size_t i = old_size; i < new_size; i++) {
      DCHECK_EQ(0, new_mem_start[i]);
    }
#endif
  } else {
    old_mem_start = static_cast<Address>(old_buffer->backing_store());
    old_size = old_buffer->byte_length()->Number();
    // If the old memory was zero-sized, we should have been in the
    // "undefined" case above.
    DCHECK_NOT_NULL(old_mem_start);
    DCHECK_NE(0, old_size);

    new_size = old_size + delta_pages * wasm::WasmModule::kPageSize;
    if (new_size >
        wasm::WasmModule::kMaxMemPages * wasm::WasmModule::kPageSize) {
      return *isolate->factory()->NewNumberFromInt(-1);
    }
    new_mem_start =
        static_cast<Address>(isolate->array_buffer_allocator()->Allocate(
            static_cast<uint32_t>(new_size)));
    if (new_mem_start == NULL) {
      return *isolate->factory()->NewNumberFromInt(-1);
    }
#if DEBUG
    // Double check the API allocator actually zero-initialized the memory.
    for (size_t i = old_size; i < new_size; i++) {
      DCHECK_EQ(0, new_mem_start[i]);
    }
#endif
    // Copy contents of the old buffer to the new buffer
    memcpy(new_mem_start, old_mem_start, old_size);
  }

  Handle<JSArrayBuffer> buffer = isolate->factory()->NewJSArrayBuffer();
  JSArrayBuffer::Setup(buffer, isolate, false, new_mem_start, new_size);
  buffer->set_is_neuterable(false);

  // Set new buffer to be wasm memory

  wasm::SetInstanceMemory(module_instance, *buffer);

  CHECK(wasm::UpdateWasmModuleMemory(module_instance, old_mem_start,
                                     new_mem_start, old_size, new_size));

  return *isolate->factory()->NewNumberFromInt(old_size /
                                               wasm::WasmModule::kPageSize);
}

RUNTIME_FUNCTION(Runtime_WasmThrowTypeError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kWasmTrapTypeError));
}

RUNTIME_FUNCTION(Runtime_WasmThrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_SMI_ARG_CHECKED(lower, 0);
  CONVERT_SMI_ARG_CHECKED(upper, 1);

  const int32_t thrown_value = (upper << 16) | lower;

  return isolate->Throw(*isolate->factory()->NewNumberFromInt(thrown_value));
}

}  // namespace internal
}  // namespace v8
