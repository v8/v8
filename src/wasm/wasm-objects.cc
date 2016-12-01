// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-objects.h"

#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-text.h"

#define TRACE(...)                                      \
  do {                                                  \
    if (FLAG_trace_wasm_instances) PrintF(__VA_ARGS__); \
  } while (false)

#define TRACE_CHAIN(instance)        \
  do {                               \
    instance->PrintInstancesChain(); \
  } while (false)

using namespace v8::internal;
using namespace v8::internal::wasm;

#define DEFINE_ACCESSORS(Container, name, field, type) \
  type* Container::get_##name() {                      \
    return type::cast(GetInternalField(field));        \
  }                                                    \
  void Container::set_##name(type* value) {            \
    return SetInternalField(field, value);             \
  }

#define DEFINE_OPTIONAL_ACCESSORS(Container, name, field, type) \
  bool Container::has_##name() {                                \
    return !GetInternalField(field)->IsUndefined(GetIsolate()); \
  }                                                             \
  type* Container::get_##name() {                               \
    return type::cast(GetInternalField(field));                 \
  }                                                             \
  void Container::set_##name(type* value) {                     \
    return SetInternalField(field, value);                      \
  }

#define DEFINE_GETTER(Container, name, field, type) \
  type* Container::get_##name() { return type::cast(GetInternalField(field)); }

static uint32_t SafeUint32(Object* value) {
  if (value->IsSmi()) {
    int32_t val = Smi::cast(value)->value();
    CHECK_GE(val, 0);
    return static_cast<uint32_t>(val);
  }
  DCHECK(value->IsHeapNumber());
  HeapNumber* num = HeapNumber::cast(value);
  CHECK_GE(num->value(), 0.0);
  CHECK_LE(num->value(), kMaxUInt32);
  return static_cast<uint32_t>(num->value());
}

static int32_t SafeInt32(Object* value) {
  if (value->IsSmi()) {
    return Smi::cast(value)->value();
  }
  DCHECK(value->IsHeapNumber());
  HeapNumber* num = HeapNumber::cast(value);
  CHECK_GE(num->value(), Smi::kMinValue);
  CHECK_LE(num->value(), Smi::kMaxValue);
  return static_cast<int32_t>(num->value());
}

Handle<WasmModuleObject> WasmModuleObject::New(
    Isolate* isolate, Handle<WasmCompiledModule> compiled_module) {
  ModuleOrigin origin = compiled_module->module()->origin;

  Handle<JSObject> module_object;
  if (origin == ModuleOrigin::kWasmOrigin) {
    Handle<JSFunction> module_cons(
        isolate->native_context()->wasm_module_constructor());
    module_object = isolate->factory()->NewJSObject(module_cons);
    Handle<Symbol> module_sym(isolate->native_context()->wasm_module_sym());
    Object::SetProperty(module_object, module_sym, module_object, STRICT)
        .Check();
  } else {
    DCHECK(origin == ModuleOrigin::kAsmJsOrigin);
    Handle<Map> map = isolate->factory()->NewMap(
        JS_OBJECT_TYPE,
        JSObject::kHeaderSize + WasmModuleObject::kFieldCount * kPointerSize);
    module_object = isolate->factory()->NewJSObjectFromMap(map, TENURED);
  }
  module_object->SetInternalField(WasmModuleObject::kCompiledModule,
                                  *compiled_module);
  Handle<WeakCell> link_to_module =
      isolate->factory()->NewWeakCell(module_object);
  compiled_module->set_weak_wasm_module(link_to_module);
  return Handle<WasmModuleObject>::cast(module_object);
}

WasmModuleObject* WasmModuleObject::cast(Object* object) {
  DCHECK(object->IsJSObject());
  // TODO(titzer): brand check for WasmModuleObject.
  return reinterpret_cast<WasmModuleObject*>(object);
}

bool WasmModuleObject::IsWasmModuleObject(Object* object) {
  return object->IsJSObject() &&
         JSObject::cast(object)->GetInternalFieldCount() == kFieldCount;
}

DEFINE_GETTER(WasmModuleObject, compiled_module, kCompiledModule,
              WasmCompiledModule)

Handle<WasmTableObject> WasmTableObject::New(Isolate* isolate, uint32_t initial,
                                             uint32_t maximum,
                                             Handle<FixedArray>* js_functions) {
  Handle<JSFunction> table_ctor(
      isolate->native_context()->wasm_table_constructor());
  Handle<JSObject> table_obj = isolate->factory()->NewJSObject(table_ctor);
  *js_functions = isolate->factory()->NewFixedArray(initial);
  Object* null = isolate->heap()->null_value();
  for (int i = 0; i < static_cast<int>(initial); ++i) {
    (*js_functions)->set(i, null);
  }
  table_obj->SetInternalField(kFunctions, *(*js_functions));
  table_obj->SetInternalField(kMaximum,
                              static_cast<Object*>(Smi::FromInt(maximum)));

  Handle<FixedArray> dispatch_tables = isolate->factory()->NewFixedArray(0);
  table_obj->SetInternalField(kDispatchTables, *dispatch_tables);
  Handle<Symbol> table_sym(isolate->native_context()->wasm_table_sym());
  Object::SetProperty(table_obj, table_sym, table_obj, STRICT).Check();
  return Handle<WasmTableObject>::cast(table_obj);
}

DEFINE_GETTER(WasmTableObject, dispatch_tables, kDispatchTables, FixedArray)

Handle<FixedArray> WasmTableObject::AddDispatchTable(
    Isolate* isolate, Handle<WasmTableObject> table_obj,
    Handle<WasmInstanceObject> instance, int table_index,
    Handle<FixedArray> dispatch_table) {
  Handle<FixedArray> dispatch_tables(
      FixedArray::cast(table_obj->GetInternalField(kDispatchTables)), isolate);
  DCHECK_EQ(0, dispatch_tables->length() % 3);

  if (instance.is_null()) return dispatch_tables;
  // TODO(titzer): use weak cells here to avoid leaking instances.

  // Grow the dispatch table and add a new triple at the end.
  Handle<FixedArray> new_dispatch_tables =
      isolate->factory()->CopyFixedArrayAndGrow(dispatch_tables, 3);

  new_dispatch_tables->set(dispatch_tables->length() + 0, *instance);
  new_dispatch_tables->set(dispatch_tables->length() + 1,
                           Smi::FromInt(table_index));
  new_dispatch_tables->set(dispatch_tables->length() + 2, *dispatch_table);

  table_obj->SetInternalField(WasmTableObject::kDispatchTables,
                              *new_dispatch_tables);

  return new_dispatch_tables;
}

DEFINE_ACCESSORS(WasmTableObject, functions, kFunctions, FixedArray)

uint32_t WasmTableObject::current_length() { return get_functions()->length(); }

uint32_t WasmTableObject::maximum_length() {
  return SafeUint32(GetInternalField(kMaximum));
}

WasmTableObject* WasmTableObject::cast(Object* object) {
  DCHECK(object && object->IsJSObject());
  // TODO(titzer): brand check for WasmTableObject.
  return reinterpret_cast<WasmTableObject*>(object);
}

Handle<WasmMemoryObject> WasmMemoryObject::New(Isolate* isolate,
                                               Handle<JSArrayBuffer> buffer,
                                               int maximum) {
  Handle<JSFunction> memory_ctor(
      isolate->native_context()->wasm_memory_constructor());
  Handle<JSObject> memory_obj =
      isolate->factory()->NewJSObject(memory_ctor, TENURED);
  memory_obj->SetInternalField(kArrayBuffer, *buffer);
  memory_obj->SetInternalField(kMaximum,
                               static_cast<Object*>(Smi::FromInt(maximum)));
  Handle<Symbol> memory_sym(isolate->native_context()->wasm_memory_sym());
  Object::SetProperty(memory_obj, memory_sym, memory_obj, STRICT).Check();
  return Handle<WasmMemoryObject>::cast(memory_obj);
}

DEFINE_ACCESSORS(WasmMemoryObject, buffer, kArrayBuffer, JSArrayBuffer)
DEFINE_OPTIONAL_ACCESSORS(WasmMemoryObject, instances_link, kInstancesLink,
                          WasmInstanceWrapper)

uint32_t WasmMemoryObject::current_pages() {
  return SafeUint32(get_buffer()->byte_length()) / wasm::WasmModule::kPageSize;
}

int32_t WasmMemoryObject::maximum_pages() {
  return SafeInt32(GetInternalField(kMaximum));
}

WasmMemoryObject* WasmMemoryObject::cast(Object* object) {
  DCHECK(object && object->IsJSObject());
  // TODO(titzer): brand check for WasmMemoryObject.
  return reinterpret_cast<WasmMemoryObject*>(object);
}

void WasmMemoryObject::AddInstance(Isolate* isolate,
                                   Handle<WasmInstanceObject> instance) {
  Handle<WasmInstanceWrapper> instance_wrapper;
  if (has_instances_link()) {
    Handle<WasmInstanceWrapper> current_wrapper(get_instances_link());
    DCHECK(WasmInstanceWrapper::IsWasmInstanceWrapper(*current_wrapper));
    DCHECK(!current_wrapper->has_previous());
    instance_wrapper = WasmInstanceWrapper::New(isolate, instance);
    instance_wrapper->set_next_wrapper(*current_wrapper);
    current_wrapper->set_previous_wrapper(*instance_wrapper);
  } else {
    instance_wrapper = WasmInstanceWrapper::New(isolate, instance);
  }
  set_instances_link(*instance_wrapper);
  instance->set_instance_wrapper(*instance_wrapper);
}

void WasmMemoryObject::ResetInstancesLink(Isolate* isolate) {
  Handle<Object> undefined = isolate->factory()->undefined_value();
  SetInternalField(kInstancesLink, *undefined);
}

DEFINE_ACCESSORS(WasmInstanceObject, compiled_module, kCompiledModule,
                 WasmCompiledModule)
DEFINE_OPTIONAL_ACCESSORS(WasmInstanceObject, globals_buffer,
                          kGlobalsArrayBuffer, JSArrayBuffer)
DEFINE_OPTIONAL_ACCESSORS(WasmInstanceObject, memory_buffer, kMemoryArrayBuffer,
                          JSArrayBuffer)
DEFINE_OPTIONAL_ACCESSORS(WasmInstanceObject, memory_object, kMemoryObject,
                          WasmMemoryObject)
DEFINE_OPTIONAL_ACCESSORS(WasmInstanceObject, debug_info, kDebugInfo,
                          WasmDebugInfo)
DEFINE_OPTIONAL_ACCESSORS(WasmInstanceObject, instance_wrapper,
                          kWasmMemInstanceWrapper, WasmInstanceWrapper)

WasmModuleObject* WasmInstanceObject::module_object() {
  return WasmModuleObject::cast(*get_compiled_module()->wasm_module());
}

WasmModule* WasmInstanceObject::module() {
  return reinterpret_cast<WasmModuleWrapper*>(
             *get_compiled_module()->module_wrapper())
      ->get();
}

WasmInstanceObject* WasmInstanceObject::cast(Object* object) {
  DCHECK(IsWasmInstanceObject(object));
  return reinterpret_cast<WasmInstanceObject*>(object);
}

bool WasmInstanceObject::IsWasmInstanceObject(Object* object) {
  if (!object->IsObject()) return false;
  if (!object->IsJSObject()) return false;

  JSObject* obj = JSObject::cast(object);
  Isolate* isolate = obj->GetIsolate();
  if (obj->GetInternalFieldCount() != kFieldCount) {
    return false;
  }

  Object* mem = obj->GetInternalField(kMemoryArrayBuffer);
  if (!(mem->IsUndefined(isolate) || mem->IsJSArrayBuffer()) ||
      !WasmCompiledModule::IsWasmCompiledModule(
          obj->GetInternalField(kCompiledModule))) {
    return false;
  }

  // All checks passed.
  return true;
}

Handle<WasmInstanceObject> WasmInstanceObject::New(
    Isolate* isolate, Handle<WasmCompiledModule> compiled_module) {
  Handle<Map> map = isolate->factory()->NewMap(
      JS_OBJECT_TYPE, JSObject::kHeaderSize + kFieldCount * kPointerSize);
  Handle<WasmInstanceObject> instance(
      reinterpret_cast<WasmInstanceObject*>(
          *isolate->factory()->NewJSObjectFromMap(map, TENURED)),
      isolate);

  instance->SetInternalField(kCompiledModule, *compiled_module);
  instance->SetInternalField(kMemoryObject, isolate->heap()->undefined_value());
  return instance;
}

WasmInstanceObject* WasmExportedFunction::instance() {
  return WasmInstanceObject::cast(GetInternalField(kInstance));
}

int WasmExportedFunction::function_index() {
  return SafeInt32(GetInternalField(kIndex));
}

WasmExportedFunction* WasmExportedFunction::cast(Object* object) {
  DCHECK(object && object->IsJSFunction());
  DCHECK_EQ(Code::JS_TO_WASM_FUNCTION,
            JSFunction::cast(object)->code()->kind());
  // TODO(titzer): brand check for WasmExportedFunction.
  return reinterpret_cast<WasmExportedFunction*>(object);
}

Handle<WasmExportedFunction> WasmExportedFunction::New(
    Isolate* isolate, Handle<WasmInstanceObject> instance, Handle<String> name,
    Handle<Code> export_wrapper, int arity, int func_index) {
  DCHECK_EQ(Code::JS_TO_WASM_FUNCTION, export_wrapper->kind());
  Handle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfo(name, export_wrapper, false);
  shared->set_length(arity);
  shared->set_internal_formal_parameter_count(arity);
  Handle<JSFunction> function = isolate->factory()->NewFunction(
      isolate->wasm_function_map(), name, export_wrapper);
  function->set_shared(*shared);

  function->SetInternalField(kInstance, *instance);
  function->SetInternalField(kIndex, Smi::FromInt(func_index));
  return Handle<WasmExportedFunction>::cast(function);
}

Handle<WasmCompiledModule> WasmCompiledModule::New(
    Isolate* isolate, Handle<WasmModuleWrapper> module_wrapper) {
  Handle<FixedArray> ret =
      isolate->factory()->NewFixedArray(PropertyIndices::Count, TENURED);
  // WasmCompiledModule::cast would fail since module bytes are not set yet.
  Handle<WasmCompiledModule> compiled_module(
      reinterpret_cast<WasmCompiledModule*>(*ret), isolate);
  compiled_module->InitId();
  compiled_module->set_module_wrapper(module_wrapper);
  return compiled_module;
}

wasm::WasmModule* WasmCompiledModule::module() const {
  return reinterpret_cast<WasmModuleWrapper*>(ptr_to_module_wrapper())->get();
}

void WasmCompiledModule::InitId() {
#if DEBUG
  static uint32_t instance_id_counter = 0;
  set(kID_instance_id, Smi::FromInt(instance_id_counter++));
  TRACE("New compiled module id: %d\n", instance_id());
#endif
}

bool WasmCompiledModule::IsWasmCompiledModule(Object* obj) {
  if (!obj->IsFixedArray()) return false;
  FixedArray* arr = FixedArray::cast(obj);
  if (arr->length() != PropertyIndices::Count) return false;
  Isolate* isolate = arr->GetIsolate();
#define WCM_CHECK_SMALL_NUMBER(TYPE, NAME) \
  if (!arr->get(kID_##NAME)->IsSmi()) return false;
#define WCM_CHECK_OBJECT_OR_WEAK(TYPE, NAME)         \
  if (!arr->get(kID_##NAME)->IsUndefined(isolate) && \
      !arr->get(kID_##NAME)->Is##TYPE())             \
    return false;
#define WCM_CHECK_OBJECT(TYPE, NAME) WCM_CHECK_OBJECT_OR_WEAK(TYPE, NAME)
#define WCM_CHECK_WEAK_LINK(TYPE, NAME) WCM_CHECK_OBJECT_OR_WEAK(WeakCell, NAME)
#define WCM_CHECK(KIND, TYPE, NAME) WCM_CHECK_##KIND(TYPE, NAME)
  WCM_PROPERTY_TABLE(WCM_CHECK)
#undef WCM_CHECK

  // All checks passed.
  return true;
}

void WasmCompiledModule::PrintInstancesChain() {
#if DEBUG
  if (!FLAG_trace_wasm_instances) return;
  for (WasmCompiledModule* current = this; current != nullptr;) {
    PrintF("->%d", current->instance_id());
    if (!current->has_weak_next_instance()) break;
    CHECK(!current->ptr_to_weak_next_instance()->cleared());
    current =
        WasmCompiledModule::cast(current->ptr_to_weak_next_instance()->value());
  }
  PrintF("\n");
#endif
}

uint32_t WasmCompiledModule::mem_size() const {
  return has_memory() ? memory()->byte_length()->Number() : default_mem_size();
}

uint32_t WasmCompiledModule::default_mem_size() const {
  return min_mem_pages() * WasmModule::kPageSize;
}

Vector<const uint8_t> WasmCompiledModule::GetRawFunctionName(
    uint32_t func_index) {
  DCHECK_GT(module()->functions.size(), func_index);
  WasmFunction& function = module()->functions[func_index];
  SeqOneByteString* bytes = ptr_to_module_bytes();
  DCHECK_GE(bytes->length(), function.name_offset);
  DCHECK_GE(bytes->length() - function.name_offset, function.name_length);
  return Vector<const uint8_t>(bytes->GetCharsAddress() + function.name_offset,
                               function.name_length);
}

int WasmCompiledModule::GetFunctionOffset(uint32_t func_index) const {
  std::vector<WasmFunction>& functions = module()->functions;
  if (static_cast<uint32_t>(func_index) >= functions.size()) return -1;
  DCHECK_GE(kMaxInt, functions[func_index].code_start_offset);
  return static_cast<int>(functions[func_index].code_start_offset);
}

int WasmCompiledModule::GetContainingFunction(uint32_t byte_offset) const {
  std::vector<WasmFunction>& functions = module()->functions;

  // Binary search for a function containing the given position.
  int left = 0;                                    // inclusive
  int right = static_cast<int>(functions.size());  // exclusive
  if (right == 0) return false;
  while (right - left > 1) {
    int mid = left + (right - left) / 2;
    if (functions[mid].code_start_offset <= byte_offset) {
      left = mid;
    } else {
      right = mid;
    }
  }
  // If the found function does not contains the given position, return -1.
  WasmFunction& func = functions[left];
  if (byte_offset < func.code_start_offset ||
      byte_offset >= func.code_end_offset) {
    return -1;
  }

  return left;
}

bool WasmCompiledModule::GetPositionInfo(uint32_t position,
                                         Script::PositionInfo* info) {
  int func_index = GetContainingFunction(position);
  if (func_index < 0) return false;

  WasmFunction& function = module()->functions[func_index];

  info->line = func_index;
  info->column = position - function.code_start_offset;
  info->line_start = function.code_start_offset;
  info->line_end = function.code_end_offset;
  return true;
}

namespace {
Handle<ByteArray> GetDecodedAsmJsOffsetTable(
    Handle<WasmCompiledModule> compiled_module, Isolate* isolate) {
  DCHECK(compiled_module->has_asm_js_offset_table());
  Handle<ByteArray> offset_table = compiled_module->asm_js_offset_table();

  // The last byte in the asm_js_offset_tables ByteArray tells whether it is
  // still encoded (0) or decoded (1).
  enum AsmJsTableType : int { Encoded = 0, Decoded = 1 };
  int table_type = offset_table->get(offset_table->length() - 1);
  DCHECK(table_type == Encoded || table_type == Decoded);
  if (table_type == Decoded) return offset_table;

  AsmJsOffsetsResult asm_offsets;
  {
    DisallowHeapAllocation no_gc;
    const byte* bytes_start = offset_table->GetDataStartAddress();
    const byte* bytes_end = bytes_start + offset_table->length() - 1;
    asm_offsets = wasm::DecodeAsmJsOffsets(bytes_start, bytes_end);
  }
  // Wasm bytes must be valid and must contain asm.js offset table.
  DCHECK(asm_offsets.ok());
  DCHECK_GE(kMaxInt, asm_offsets.val.size());
  int num_functions = static_cast<int>(asm_offsets.val.size());
  int num_imported_functions =
      static_cast<int>(compiled_module->module()->num_imported_functions);
  DCHECK_EQ(compiled_module->module()->functions.size(),
            static_cast<size_t>(num_functions) + num_imported_functions);
  // One byte to encode that this is a decoded table.
  int total_size = 1;
  for (int func = 0; func < num_functions; ++func) {
    size_t new_size = asm_offsets.val[func].size() * 2 * kIntSize;
    DCHECK_LE(new_size, static_cast<size_t>(kMaxInt) - total_size);
    total_size += static_cast<int>(new_size);
  }
  Handle<ByteArray> decoded_table =
      isolate->factory()->NewByteArray(total_size, TENURED);
  decoded_table->set(total_size - 1, AsmJsTableType::Decoded);
  compiled_module->set_asm_js_offset_table(decoded_table);

  int idx = 0;
  std::vector<WasmFunction>& wasm_funs = compiled_module->module()->functions;
  for (int func = 0; func < num_functions; ++func) {
    std::vector<std::pair<int, int>>& func_asm_offsets = asm_offsets.val[func];
    if (func_asm_offsets.empty()) continue;
    int func_offset =
        wasm_funs[num_imported_functions + func].code_start_offset;
    for (std::pair<int, int> p : func_asm_offsets) {
      // Byte offsets must be strictly monotonously increasing:
      DCHECK(idx == 0 ||
             func_offset + p.first > decoded_table->get_int(idx - 2));
      decoded_table->set_int(idx++, func_offset + p.first);
      decoded_table->set_int(idx++, p.second);
    }
  }
  DCHECK_EQ(total_size, idx * kIntSize + 1);
  return decoded_table;
}
}  // namespace

int WasmCompiledModule::GetAsmJsSourcePosition(
    Handle<WasmCompiledModule> compiled_module, uint32_t func_index,
    uint32_t byte_offset) {
  Isolate* isolate = compiled_module->GetIsolate();
  Handle<ByteArray> offset_table =
      GetDecodedAsmJsOffsetTable(compiled_module, isolate);

  DCHECK_LT(func_index, compiled_module->module()->functions.size());
  uint32_t func_code_offset =
      compiled_module->module()->functions[func_index].code_start_offset;
  uint32_t total_offset = func_code_offset + byte_offset;

  // Binary search for the total byte offset.
  int left = 0;                                       // inclusive
  int right = offset_table->length() / kIntSize / 2;  // exclusive
  DCHECK_LT(left, right);
  while (right - left > 1) {
    int mid = left + (right - left) / 2;
    int mid_entry = offset_table->get_int(2 * mid);
    DCHECK_GE(kMaxInt, mid_entry);
    if (static_cast<uint32_t>(mid_entry) <= total_offset) {
      left = mid;
    } else {
      right = mid;
    }
  }
  // There should be an entry for each position that could show up on the stack
  // trace:
  DCHECK_EQ(total_offset,
            static_cast<uint32_t>(offset_table->get_int(2 * left)));
  return offset_table->get_int(2 * left + 1);
}

std::pair<std::string, std::vector<std::tuple<uint32_t, int, int>>>
WasmCompiledModule::DisassembleFunction(int func_index) {
  DisallowHeapAllocation no_gc;

  if (func_index < 0 ||
      static_cast<uint32_t>(func_index) >= module()->functions.size())
    return {};

  SeqOneByteString* module_bytes_str = ptr_to_module_bytes();
  Vector<const byte> module_bytes(module_bytes_str->GetChars(),
                                  module_bytes_str->length());

  std::ostringstream disassembly_os;
  std::vector<std::tuple<uint32_t, int, int>> offset_table;

  PrintWasmText(module(), module_bytes, static_cast<uint32_t>(func_index),
                disassembly_os, &offset_table);

  return {disassembly_os.str(), std::move(offset_table)};
}

Handle<WasmInstanceWrapper> WasmInstanceWrapper::New(
    Isolate* isolate, Handle<WasmInstanceObject> instance) {
  Handle<FixedArray> array =
      isolate->factory()->NewFixedArray(kWrapperPropertyCount, TENURED);
  Handle<WasmInstanceWrapper> instance_wrapper(
      reinterpret_cast<WasmInstanceWrapper*>(*array), isolate);
  instance_wrapper->set_instance_object(instance, isolate);
  return instance_wrapper;
}

bool WasmInstanceWrapper::IsWasmInstanceWrapper(Object* obj) {
  if (!obj->IsFixedArray()) return false;
  Handle<FixedArray> array = handle(FixedArray::cast(obj));
  if (array->length() != kWrapperPropertyCount) return false;
  if (!array->get(kWrapperInstanceObject)->IsWeakCell()) return false;
  Isolate* isolate = array->GetIsolate();
  if (!array->get(kNextInstanceWrapper)->IsUndefined(isolate) &&
      !array->get(kNextInstanceWrapper)->IsFixedArray())
    return false;
  if (!array->get(kPreviousInstanceWrapper)->IsUndefined(isolate) &&
      !array->get(kPreviousInstanceWrapper)->IsFixedArray())
    return false;
  return true;
}

void WasmInstanceWrapper::set_instance_object(Handle<JSObject> instance,
                                              Isolate* isolate) {
  Handle<WeakCell> cell = isolate->factory()->NewWeakCell(instance);
  set(kWrapperInstanceObject, *cell);
}
