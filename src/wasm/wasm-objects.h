// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_OBJECTS_H_
#define V8_WASM_OBJECTS_H_

#include "src/objects-inl.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/managed.h"

namespace v8 {
namespace internal {
namespace wasm {
struct WasmModule;
}

class WasmCompiledModule;
class WasmDebugInfo;
class WasmInstanceObject;
class WasmInstanceWrapper;

#define DECLARE_CASTS(name)             \
  static bool Is##name(Object* object); \
  static name* cast(Object* object)

#define DECLARE_ACCESSORS(name, type) \
  type* get_##name();                 \
  void set_##name(type* value)

#define DECLARE_OPTIONAL_ACCESSORS(name, type) \
  bool has_##name();                           \
  type* get_##name();                          \
  void set_##name(type* value)

// Representation of a WebAssembly.Module JavaScript-level object.
class WasmModuleObject : public JSObject {
 public:
  // TODO(titzer): add the brand as an internal field instead of a property.
  enum Fields { kCompiledModule, kFieldCount };

  DECLARE_CASTS(WasmModuleObject);

  WasmCompiledModule* get_compiled_module();
  wasm::WasmModule* module();
  int num_functions();
  bool is_asm_js();
  int GetAsmWasmSourcePosition(int func_index, int byte_offset);
  WasmDebugInfo* debug_info();
  void set_debug_info(WasmDebugInfo* debug_info);
  MaybeHandle<String> GetFunctionName(Isolate* isolate, int func_index);

  static Handle<WasmModuleObject> New(
      Isolate* isolate, Handle<WasmCompiledModule> compiled_module);
};

// Representation of a WebAssembly.Table JavaScript-level object.
class WasmTableObject : public JSObject {
 public:
  // TODO(titzer): add the brand as an internal field instead of a property.
  enum Fields { kFunctions, kMaximum, kDispatchTables, kFieldCount };

  DECLARE_CASTS(WasmTableObject);
  DECLARE_ACCESSORS(functions, FixedArray);

  FixedArray* get_dispatch_tables();
  uint32_t current_length();
  uint32_t maximum_length();

  static Handle<WasmTableObject> New(Isolate* isolate, uint32_t initial,
                                     uint32_t maximum,
                                     Handle<FixedArray>* js_functions);
  static bool Grow(Handle<WasmTableObject> table, uint32_t count);
  static Handle<FixedArray> AddDispatchTable(
      Isolate* isolate, Handle<WasmTableObject> table,
      Handle<WasmInstanceObject> instance, int table_index,
      Handle<FixedArray> dispatch_table);
};

// Representation of a WebAssembly.Memory JavaScript-level object.
class WasmMemoryObject : public JSObject {
 public:
  // TODO(titzer): add the brand as an internal field instead of a property.
  enum Fields : uint8_t { kArrayBuffer, kMaximum, kInstancesLink, kFieldCount };

  DECLARE_CASTS(WasmMemoryObject);
  DECLARE_ACCESSORS(buffer, JSArrayBuffer);
  DECLARE_OPTIONAL_ACCESSORS(instances_link, WasmInstanceWrapper);

  void AddInstance(Isolate* isolate, Handle<WasmInstanceObject> object);
  void ResetInstancesLink(Isolate* isolate);
  uint32_t current_pages();
  int32_t maximum_pages();  // returns < 0 if there is no maximum

  static Handle<WasmMemoryObject> New(Isolate* isolate,
                                      Handle<JSArrayBuffer> buffer,
                                      int maximum);

  static bool Grow(Handle<WasmMemoryObject> memory, uint32_t count);
};

// Representation of a WebAssembly.Instance JavaScript-level object.
class WasmInstanceObject : public JSObject {
 public:
  // TODO(titzer): add the brand as an internal field instead of a property.
  enum Fields {
    kCompiledModule,
    kMemoryObject,
    kMemoryArrayBuffer,
    kGlobalsArrayBuffer,
    kDebugInfo,
    kWasmMemInstanceWrapper,
    kFieldCount
  };

  DECLARE_CASTS(WasmInstanceObject);

  DECLARE_ACCESSORS(compiled_module, WasmCompiledModule);
  DECLARE_OPTIONAL_ACCESSORS(globals_buffer, JSArrayBuffer);
  DECLARE_OPTIONAL_ACCESSORS(memory_buffer, JSArrayBuffer);
  DECLARE_OPTIONAL_ACCESSORS(memory_object, WasmMemoryObject);
  DECLARE_OPTIONAL_ACCESSORS(debug_info, WasmDebugInfo);
  DECLARE_OPTIONAL_ACCESSORS(instance_wrapper, WasmInstanceWrapper);

  WasmModuleObject* module_object();
  wasm::WasmModule* module();

  static Handle<WasmInstanceObject> New(
      Isolate* isolate, Handle<WasmCompiledModule> compiled_module);
};

// Representation of an exported WASM function.
class WasmExportedFunction : public JSFunction {
 public:
  enum Fields { kInstance, kIndex, kFieldCount };

  DECLARE_CASTS(WasmExportedFunction);

  WasmInstanceObject* instance();
  int function_index();

  static Handle<WasmExportedFunction> New(Isolate* isolate,
                                          Handle<WasmInstanceObject> instance,
                                          Handle<String> name,
                                          Handle<Code> export_wrapper,
                                          int arity, int func_index);
};

class WasmCompiledModule : public FixedArray {
 public:
  enum Fields { kFieldCount };

  static WasmCompiledModule* cast(Object* fixed_array) {
    SLOW_DCHECK(IsWasmCompiledModule(fixed_array));
    return reinterpret_cast<WasmCompiledModule*>(fixed_array);
  }

#define WCM_OBJECT_OR_WEAK(TYPE, NAME, ID)                           \
  Handle<TYPE> NAME() const { return handle(ptr_to_##NAME()); }      \
                                                                     \
  MaybeHandle<TYPE> maybe_##NAME() const {                           \
    if (has_##NAME()) return NAME();                                 \
    return MaybeHandle<TYPE>();                                      \
  }                                                                  \
                                                                     \
  TYPE* maybe_ptr_to_##NAME() const {                                \
    Object* obj = get(ID);                                           \
    if (!obj->Is##TYPE()) return nullptr;                            \
    return TYPE::cast(obj);                                          \
  }                                                                  \
                                                                     \
  TYPE* ptr_to_##NAME() const {                                      \
    Object* obj = get(ID);                                           \
    DCHECK(obj->Is##TYPE());                                         \
    return TYPE::cast(obj);                                          \
  }                                                                  \
                                                                     \
  void set_##NAME(Handle<TYPE> value) { set_ptr_to_##NAME(*value); } \
                                                                     \
  void set_ptr_to_##NAME(TYPE* value) { set(ID, value); }            \
                                                                     \
  bool has_##NAME() const { return get(ID)->Is##TYPE(); }            \
                                                                     \
  void reset_##NAME() { set_undefined(ID); }

#define WCM_OBJECT(TYPE, NAME) WCM_OBJECT_OR_WEAK(TYPE, NAME, kID_##NAME)

#define WCM_SMALL_NUMBER(TYPE, NAME)                               \
  TYPE NAME() const {                                              \
    return static_cast<TYPE>(Smi::cast(get(kID_##NAME))->value()); \
  }                                                                \
  void set_##NAME(TYPE value) { set(kID_##NAME, Smi::FromInt(value)); }

#define WCM_WEAK_LINK(TYPE, NAME)                        \
  WCM_OBJECT_OR_WEAK(WeakCell, weak_##NAME, kID_##NAME); \
                                                         \
  Handle<TYPE> NAME() const {                            \
    return handle(TYPE::cast(weak_##NAME()->value()));   \
  }

#define CORE_WCM_PROPERTY_TABLE(MACRO)                \
  MACRO(OBJECT, FixedArray, code_table)               \
  MACRO(OBJECT, Foreign, module_wrapper)              \
  /* For debugging: */                                \
  MACRO(OBJECT, SeqOneByteString, module_bytes)       \
  MACRO(OBJECT, Script, script)                       \
  MACRO(OBJECT, ByteArray, asm_js_offset_table)       \
  /* End of debugging stuff */                        \
  MACRO(OBJECT, FixedArray, function_tables)          \
  MACRO(OBJECT, FixedArray, empty_function_tables)    \
  MACRO(OBJECT, JSArrayBuffer, memory)                \
  MACRO(SMALL_NUMBER, uint32_t, min_mem_pages)        \
  MACRO(SMALL_NUMBER, uint32_t, max_mem_pages)        \
  MACRO(WEAK_LINK, WasmCompiledModule, next_instance) \
  MACRO(WEAK_LINK, WasmCompiledModule, prev_instance) \
  MACRO(WEAK_LINK, JSObject, owning_instance)         \
  MACRO(WEAK_LINK, JSObject, wasm_module)

#if DEBUG
#define DEBUG_ONLY_TABLE(MACRO) MACRO(SMALL_NUMBER, uint32_t, instance_id)
#else
#define DEBUG_ONLY_TABLE(IGNORE)
  uint32_t instance_id() const { return -1; }
#endif

#define WCM_PROPERTY_TABLE(MACRO) \
  CORE_WCM_PROPERTY_TABLE(MACRO)  \
  DEBUG_ONLY_TABLE(MACRO)

 private:
  enum PropertyIndices {
#define INDICES(IGNORE1, IGNORE2, NAME) kID_##NAME,
    WCM_PROPERTY_TABLE(INDICES) Count
#undef INDICES
  };

 public:
  static Handle<WasmCompiledModule> New(
      Isolate* isolate, Handle<Managed<wasm::WasmModule>> module_wrapper);

  static Handle<WasmCompiledModule> Clone(Isolate* isolate,
                                          Handle<WasmCompiledModule> module) {
    Handle<WasmCompiledModule> ret = Handle<WasmCompiledModule>::cast(
        isolate->factory()->CopyFixedArray(module));
    ret->InitId();
    ret->reset_weak_owning_instance();
    ret->reset_weak_next_instance();
    ret->reset_weak_prev_instance();
    return ret;
  }

  uint32_t mem_size() const;
  uint32_t default_mem_size() const;

  wasm::WasmModule* module() const;

#define DECLARATION(KIND, TYPE, NAME) WCM_##KIND(TYPE, NAME)
  WCM_PROPERTY_TABLE(DECLARATION)
#undef DECLARATION

  static bool IsWasmCompiledModule(Object* obj);

  void PrintInstancesChain();

  static void RecreateModuleWrapper(Isolate* isolate,
                                    Handle<FixedArray> compiled_module);

  // Get the function name of the function identified by the given index.
  // Returns a null handle if the function is unnamed or the name is not a valid
  // UTF-8 string.
  static MaybeHandle<String> GetFunctionName(
      Handle<WasmCompiledModule> compiled_module, uint32_t func_index);

  // Get the raw bytes of the function name of the function identified by the
  // given index.
  // Meant to be used for debugging or frame printing.
  // Does not allocate, hence gc-safe.
  Vector<const uint8_t> GetRawFunctionName(uint32_t func_index);

  // Return the byte offset of the function identified by the given index.
  // The offset will be relative to the start of the module bytes.
  // Returns -1 if the function index is invalid.
  int GetFunctionOffset(uint32_t func_index) const;

  // Returns the function containing the given byte offset.
  // Returns -1 if the byte offset is not contained in any function of this
  // module.
  int GetContainingFunction(uint32_t byte_offset) const;

  // Translate from byte offset in the module to function number and byte offset
  // within that function, encoded as line and column in the position info.
  // Returns true if the position is valid inside this module, false otherwise.
  bool GetPositionInfo(uint32_t position, Script::PositionInfo* info);

  // Get the asm.js source position from a byte offset.
  // Must only be called if the associated wasm object was created from asm.js.
  static int GetAsmJsSourcePosition(Handle<WasmCompiledModule> debug_info,
                                    uint32_t func_index, uint32_t byte_offset);

  // Compute the disassembly of a wasm function.
  // Returns the disassembly string and a list of <byte_offset, line, column>
  // entries, mapping wasm byte offsets to line and column in the disassembly.
  // The list is guaranteed to be ordered by the byte_offset.
  // Returns an empty string and empty vector if the function index is invalid.
  std::pair<std::string, std::vector<std::tuple<uint32_t, int, int>>>
  DisassembleFunction(int func_index);

 private:
  void InitId();

  DISALLOW_IMPLICIT_CONSTRUCTORS(WasmCompiledModule);
};

// TODO(clemensh): Extend this object for breakpoint support, or remove it.
class WasmDebugInfo : public FixedArray {
 public:
  enum class Fields { kFieldCount };

  static Handle<WasmDebugInfo> New(Handle<WasmInstanceObject> instance);

  static bool IsDebugInfo(Object* object);
  static WasmDebugInfo* cast(Object* object);

  WasmInstanceObject* wasm_instance();
};

class WasmInstanceWrapper : public FixedArray {
 public:
  static Handle<WasmInstanceWrapper> New(Isolate* isolate,
                                         Handle<WasmInstanceObject> instance);
  static WasmInstanceWrapper* cast(Object* fixed_array) {
    SLOW_DCHECK(IsWasmInstanceWrapper(fixed_array));
    return reinterpret_cast<WasmInstanceWrapper*>(fixed_array);
  }
  static bool IsWasmInstanceWrapper(Object* obj);
  bool has_instance() { return get(kWrapperInstanceObject)->IsWeakCell(); }
  Handle<WasmInstanceObject> instance_object() {
    Object* obj = get(kWrapperInstanceObject);
    DCHECK(obj->IsWeakCell());
    WeakCell* cell = WeakCell::cast(obj);
    DCHECK(cell->value()->IsJSObject());
    return handle(WasmInstanceObject::cast(cell->value()));
  }
  bool has_next() { return IsWasmInstanceWrapper(get(kNextInstanceWrapper)); }
  bool has_previous() {
    return IsWasmInstanceWrapper(get(kPreviousInstanceWrapper));
  }
  void set_instance_object(Handle<JSObject> instance, Isolate* isolate);
  void set_next_wrapper(Object* obj) {
    DCHECK(IsWasmInstanceWrapper(obj));
    set(kNextInstanceWrapper, obj);
  }
  void set_previous_wrapper(Object* obj) {
    DCHECK(IsWasmInstanceWrapper(obj));
    set(kPreviousInstanceWrapper, obj);
  }
  Handle<WasmInstanceWrapper> next_wrapper() {
    Object* obj = get(kNextInstanceWrapper);
    DCHECK(IsWasmInstanceWrapper(obj));
    return handle(WasmInstanceWrapper::cast(obj));
  }
  Handle<WasmInstanceWrapper> previous_wrapper() {
    Object* obj = get(kPreviousInstanceWrapper);
    DCHECK(IsWasmInstanceWrapper(obj));
    return handle(WasmInstanceWrapper::cast(obj));
  }
  void reset_next_wrapper() { set_undefined(kNextInstanceWrapper); }
  void reset_previous_wrapper() { set_undefined(kPreviousInstanceWrapper); }
  void reset() {
    for (int kID = 0; kID < kWrapperPropertyCount; kID++) set_undefined(kID);
  }

 private:
  enum {
    kWrapperInstanceObject,
    kNextInstanceWrapper,
    kPreviousInstanceWrapper,
    kWrapperPropertyCount
  };
};

#undef DECLARE_ACCESSORS
#undef DECLARE_OPTIONAL_ACCESSORS

}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_OBJECTS_H_
