// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_WASM_OBJECTS_INL_H_
#define V8_WASM_WASM_OBJECTS_INL_H_

#include "src/heap/heap-inl.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {

// Has to be the last include (doesn't have include guards)
#include "src/objects/object-macros.h"

CAST_ACCESSOR(WasmCompiledModule)
CAST_ACCESSOR(WasmDebugInfo)
CAST_ACCESSOR(WasmInstanceObject)
CAST_ACCESSOR(WasmMemoryObject)
CAST_ACCESSOR(WasmModuleObject)
CAST_ACCESSOR(WasmSharedModuleData)
CAST_ACCESSOR(WasmTableObject)

#define OPTIONAL_ACCESSORS(holder, name, type, offset)           \
  bool holder::has_##name() {                                    \
    return !READ_FIELD(this, offset)->IsUndefined(GetIsolate()); \
  }                                                              \
  ACCESSORS(holder, name, type, offset)

// WasmModuleObject
ACCESSORS(WasmModuleObject, compiled_module, WasmCompiledModule,
          kCompiledModuleOffset)

// WasmTableObject
ACCESSORS(WasmTableObject, functions, FixedArray, kFunctionsOffset)
ACCESSORS(WasmTableObject, maximum_length, Object, kMaximumLengthOffset)
ACCESSORS(WasmTableObject, dispatch_tables, FixedArray, kDispatchTablesOffset)

// WasmMemoryObject
ACCESSORS(WasmMemoryObject, array_buffer, JSArrayBuffer, kArrayBufferOffset)
SMI_ACCESSORS(WasmMemoryObject, maximum_pages, kMaximumPagesOffset)
OPTIONAL_ACCESSORS(WasmMemoryObject, instances, FixedArrayOfWeakCells,
                   kInstancesOffset)

// WasmInstanceObject
ACCESSORS(WasmInstanceObject, wasm_context, Managed<WasmContext>,
          kWasmContextOffset)
ACCESSORS(WasmInstanceObject, compiled_module, WasmCompiledModule,
          kCompiledModuleOffset)
ACCESSORS(WasmInstanceObject, exports_object, JSObject, kExportsObjectOffset)
OPTIONAL_ACCESSORS(WasmInstanceObject, memory_object, WasmMemoryObject,
                   kMemoryObjectOffset)
ACCESSORS(WasmInstanceObject, globals_buffer, JSArrayBuffer,
          kGlobalsBufferOffset)
OPTIONAL_ACCESSORS(WasmInstanceObject, debug_info, WasmDebugInfo,
                   kDebugInfoOffset)
OPTIONAL_ACCESSORS(WasmInstanceObject, table_object, WasmTableObject,
                   kTableObjectOffset)
OPTIONAL_ACCESSORS(WasmInstanceObject, function_tables, FixedArray,
                   kFunctionTablesOffset)
ACCESSORS(WasmInstanceObject, directly_called_instances, FixedArray,
          kDirectlyCalledInstancesOffset)
ACCESSORS(WasmInstanceObject, js_imports_table, FixedArray,
          kJsImportsTableOffset)

// WasmSharedModuleData
ACCESSORS(WasmSharedModuleData, module_wrapper, Object, kModuleWrapperOffset)
ACCESSORS(WasmSharedModuleData, module_bytes, SeqOneByteString,
          kModuleBytesOffset)
ACCESSORS(WasmSharedModuleData, script, Script, kScriptOffset)
OPTIONAL_ACCESSORS(WasmSharedModuleData, asm_js_offset_table, ByteArray,
                   kAsmJsOffsetTableOffset)
OPTIONAL_ACCESSORS(WasmSharedModuleData, breakpoint_infos, FixedArray,
                   kBreakPointInfosOffset)
OPTIONAL_ACCESSORS(WasmSharedModuleData, lazy_compilation_orchestrator, Foreign,
                   kLazyCompilationOrchestratorOffset)
void WasmSharedModuleData::reset_breakpoint_infos() {
  DCHECK(IsWasmSharedModuleData());
  WRITE_FIELD(this, kBreakPointInfosOffset, GetHeap()->undefined_value());
}

// WasmDebugInfo
ACCESSORS(WasmDebugInfo, wasm_instance, WasmInstanceObject, kInstanceOffset)
ACCESSORS(WasmDebugInfo, interpreter_handle, Object, kInterpreterHandleOffset)
ACCESSORS(WasmDebugInfo, interpreted_functions, Object,
          kInterpretedFunctionsOffset)
OPTIONAL_ACCESSORS(WasmDebugInfo, locals_names, FixedArray, kLocalsNamesOffset)
OPTIONAL_ACCESSORS(WasmDebugInfo, c_wasm_entries, FixedArray,
                   kCWasmEntriesOffset)
OPTIONAL_ACCESSORS(WasmDebugInfo, c_wasm_entry_map, Managed<wasm::SignatureMap>,
                   kCWasmEntryMapOffset)

#undef OPTIONAL_ACCESSORS

#define WCM_OBJECT_OR_WEAK(TYPE, NAME, OFFSET, TYPE_CHECK)   \
  bool WasmCompiledModule::has_##NAME() const {              \
    Object* value = READ_FIELD(this, OFFSET);                \
    return TYPE_CHECK;                                       \
  }                                                          \
                                                             \
  void WasmCompiledModule::reset_##NAME() {                  \
    WRITE_FIELD(this, OFFSET, GetHeap()->undefined_value()); \
  }                                                          \
                                                             \
  ACCESSORS_CHECKED2(WasmCompiledModule, NAME, TYPE, OFFSET, TYPE_CHECK, true)

#define WCM_OBJECT(TYPE, NAME, OFFSET) \
  WCM_OBJECT_OR_WEAK(TYPE, NAME, OFFSET, value->Is##TYPE())

#define WCM_SMALL_CONST_NUMBER(TYPE, NAME, OFFSET)                  \
  TYPE WasmCompiledModule::NAME() const {                           \
    return static_cast<TYPE>(Smi::ToInt(READ_FIELD(this, OFFSET))); \
  }                                                                 \
                                                                    \
  void WasmCompiledModule::set_##NAME(TYPE value) {                 \
    WRITE_FIELD(this, OFFSET, Smi::FromInt(value));                 \
  }

#define WCM_WEAK_LINK(TYPE, NAME, OFFSET)                                \
  WCM_OBJECT_OR_WEAK(WeakCell, weak_##NAME, OFFSET, value->IsWeakCell()) \
                                                                         \
  TYPE* WasmCompiledModule::NAME() const {                               \
    DCHECK(!weak_##NAME()->cleared());                                   \
    return TYPE::cast(weak_##NAME()->value());                           \
  }

// WasmCompiledModule
WCM_OBJECT(WasmSharedModuleData, shared, kSharedOffset)
WCM_WEAK_LINK(Context, native_context, kNativeContextOffset)
WCM_OBJECT(FixedArray, export_wrappers, kExportWrappersOffset)
WCM_OBJECT(FixedArray, weak_exported_functions, kWeakExportedFunctionsOffset)
WCM_OBJECT(WasmCompiledModule, next_instance, kNextInstanceOffset)
WCM_OBJECT(WasmCompiledModule, prev_instance, kPrevInstanceOffset)
WCM_WEAK_LINK(WasmInstanceObject, owning_instance, kOwningInstanceOffset)
WCM_WEAK_LINK(WasmModuleObject, wasm_module, kWasmModuleOffset)
WCM_OBJECT(FixedArray, source_positions, kSourcePositionsOffset)
WCM_OBJECT(Foreign, native_module, kNativeModuleOffset)
WCM_OBJECT(FixedArray, lazy_compile_data, kLazyCompileDataOffset)
WCM_SMALL_CONST_NUMBER(bool, use_trap_handler, kUseTrapHandlerOffset)
WCM_OBJECT(FixedArray, code_table, kCodeTableOffset)
WCM_OBJECT(FixedArray, function_tables, kFunctionTablesOffset)
WCM_OBJECT(FixedArray, empty_function_tables, kEmptyFunctionTablesOffset)
ACCESSORS(WasmCompiledModule, raw_next_instance, Object, kNextInstanceOffset);
ACCESSORS(WasmCompiledModule, raw_prev_instance, Object, kPrevInstanceOffset);

#undef WCM_OBJECT_OR_WEAK
#undef WCM_OBJECT
#undef WCM_SMALL_CONST_NUMBER
#undef WCM_WEAK_LINK

uint32_t WasmTableObject::current_length() { return functions()->length(); }

bool WasmMemoryObject::has_maximum_pages() { return maximum_pages() >= 0; }

void WasmCompiledModule::ReplaceCodeTableForTesting(
    Handle<FixedArray> testing_table) {
  set_code_table(*testing_table);
}

#include "src/objects/object-macros-undef.h"

}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_WASM_OBJECTS_INL_H_
