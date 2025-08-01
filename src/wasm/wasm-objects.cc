// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/wasm-objects.h"

#if V8_TARGET_OS_LINUX
#include <sys/mman.h>
#include <sys/stat.h>
// `sys/mman.h defines `MAP_TYPE`, but `MAP_TYPE` also gets defined within V8.
// Since we don't need `sys/mman.h`'s `MAP_TYPE`, we undefine it immediately
// after the `#include`.
#undef MAP_TYPE
#endif  // V8_TARGET_OS_LINUX

#include <optional>

#include "src/base/iterator.h"
#include "src/base/vector.h"
#include "src/builtins/builtins-inl.h"
#include "src/compiler/wasm-compiler.h"
#include "src/debug/debug.h"
#include "src/logging/counters.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"
#include "src/objects/shared-function-info.h"
#include "src/roots/roots-inl.h"
#include "src/utils/utils.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/code-space-access.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/stacks.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-code-pointer-table-inl.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-export-wrapper-cache.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/wasm/wasm-value.h"

#if V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-runtime.h"
#endif  // V8_ENABLE_DRUMBRAKE

// Needs to be last so macros do not get undefined.
#include "src/objects/object-macros.h"

#define TRACE_IFT(...)              \
  do {                              \
    if (false) PrintF(__VA_ARGS__); \
  } while (false)

namespace v8 {
namespace internal {

namespace {

// Utilities for handling "uses" lists. We reserve one slot for the
// used length, then store pairs of (instance, table_index).
static constexpr int kReservedSlotOffset = 1;
void SetUsedLength(Tagged<ProtectedWeakFixedArray> uses, int length) {
  // {set} includes a DCHECK for sufficient capacity.
  uses->set(0, Smi::FromInt(length));
}
int GetUsedLength(Tagged<ProtectedWeakFixedArray> uses) {
  if (uses->length() == 0) return 0;
  return Cast<Smi>(uses->get(0)).value();
}
void SetEntry(Tagged<ProtectedWeakFixedArray> uses, int slot_index,
              Tagged<WasmTrustedInstanceData> user, int table_index) {
  DCHECK(slot_index & 1);
  uses->set(slot_index, MakeWeak(user));
  uses->set(slot_index + 1, Smi::FromInt(table_index));
}
// These are two separate functions because GCMole produces bogus warnings
// when we return a std::pair<A, B> and call it as `auto [a, b] = ...`.
Tagged<WasmTrustedInstanceData> GetInstance(
    Tagged<ProtectedWeakFixedArray> uses, int slot_index) {
  DCHECK(slot_index & 1);
  return Cast<WasmTrustedInstanceData>(
      uses->get(slot_index).GetHeapObjectAssumeWeak());
}
int GetTableIndex(Tagged<ProtectedWeakFixedArray> uses, int slot_index) {
  DCHECK(slot_index & 1);
  return Cast<Smi>(uses->get(slot_index + 1)).value();
}
void CopyEntry(Tagged<ProtectedWeakFixedArray> dst, int dst_index,
               Tagged<ProtectedWeakFixedArray> src, int src_index) {
  DCHECK(dst_index & 1);
  DCHECK(src_index & 1);
  // There shouldn't be a reason to copy cleared entries.
  DCHECK(
      IsWasmTrustedInstanceData(src->get(src_index).GetHeapObjectAssumeWeak()));
  DCHECK(IsSmi(src->get(src_index + 1)));
  dst->set(dst_index, src->get(src_index));
  dst->set(dst_index + 1, src->get(src_index + 1));
}

}  // namespace

// Import a few often used types from the wasm namespace.
using WasmFunction = wasm::WasmFunction;
using WasmModule = wasm::WasmModule;

// static
DirectHandle<WasmModuleObject> WasmModuleObject::New(
    Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
    DirectHandle<Script> script) {
  DirectHandle<Managed<wasm::NativeModule>> managed_native_module;
  if (script->type() == Script::Type::kWasm) {
    managed_native_module = direct_handle(
        Cast<Managed<wasm::NativeModule>>(script->wasm_managed_native_module()),
        isolate);
  } else {
    const WasmModule* module = native_module->module();
    size_t memory_estimate =
        native_module->committed_code_space() +
        wasm::WasmCodeManager::EstimateNativeModuleMetaDataSize(module);
    managed_native_module = Managed<wasm::NativeModule>::From(
        isolate, memory_estimate, std::move(native_module));
  }
  DirectHandle<WasmModuleObject> module_object = Cast<WasmModuleObject>(
      isolate->factory()->NewJSObject(isolate->wasm_module_constructor()));
  module_object->set_managed_native_module(*managed_native_module);
  module_object->set_script(*script);
  return module_object;
}

DirectHandle<String> WasmModuleObject::ExtractUtf8StringFromModuleBytes(
    Isolate* isolate, base::Vector<const uint8_t> wire_bytes,
    wasm::WireBytesRef ref, InternalizeString internalize) {
  base::Vector<const uint8_t> name_vec =
      wire_bytes.SubVector(ref.offset(), ref.end_offset());
  // UTF8 validation happens at decode time.
  DCHECK(unibrow::Utf8::ValidateEncoding(name_vec.begin(), name_vec.length()));
  auto* factory = isolate->factory();
  return internalize
             ? factory->InternalizeUtf8String(
                   base::Vector<const char>::cast(name_vec))
             : factory
                   ->NewStringFromUtf8(base::Vector<const char>::cast(name_vec))
                   .ToHandleChecked();
}

MaybeDirectHandle<String> WasmModuleObject::GetModuleNameOrNull(
    Isolate* isolate, DirectHandle<WasmModuleObject> module_object) {
  wasm::NativeModule* native_module = module_object->native_module();
  const WasmModule* module = native_module->module();
  if (!module->name.is_set()) return {};
  return ExtractUtf8StringFromModuleBytes(isolate, native_module->wire_bytes(),
                                          module->name, kNoInternalize);
}

MaybeDirectHandle<String> WasmModuleObject::GetFunctionNameOrNull(
    Isolate* isolate, DirectHandle<WasmModuleObject> module_object,
    uint32_t func_index) {
  wasm::NativeModule* native_module = module_object->native_module();
  const WasmModule* module = native_module->module();
  DCHECK_LT(func_index, module->functions.size());
  wasm::WireBytesRef name = module->lazily_generated_names.LookupFunctionName(
      wasm::ModuleWireBytes(native_module->wire_bytes()), func_index);
  if (!name.is_set()) return {};
  return ExtractUtf8StringFromModuleBytes(isolate, native_module->wire_bytes(),
                                          name, kNoInternalize);
}

base::Vector<const uint8_t> WasmModuleObject::GetRawFunctionName(
    int func_index) {
  if (func_index == wasm::kAnonymousFuncIndex) {
    return base::Vector<const uint8_t>({nullptr, 0});
  }
  wasm::NativeModule* native_mod = native_module();
  const WasmModule* module = native_mod->module();
  DCHECK_GT(module->functions.size(), func_index);
  wasm::ModuleWireBytes wire_bytes(native_mod->wire_bytes());
  wasm::WireBytesRef name_ref =
      module->lazily_generated_names.LookupFunctionName(wire_bytes, func_index);
  wasm::WasmName name = wire_bytes.GetNameOrNull(name_ref);
  return base::Vector<const uint8_t>::cast(name);
}

DirectHandle<WasmTableObject> WasmTableObject::New(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> trusted_data,
    wasm::ValueType type, wasm::CanonicalValueType canonical_type,
    uint32_t initial, bool has_maximum, uint64_t maximum,
    DirectHandle<Object> initial_value, wasm::AddressType address_type,
    DirectHandle<WasmDispatchTable>* out_dispatch_table) {
  CHECK(type.is_object_reference());

  DCHECK_LE(initial, wasm::max_table_size());
  DirectHandle<FixedArray> entries = isolate->factory()->NewFixedArray(initial);
  for (int i = 0; i < static_cast<int>(initial); ++i) {
    entries->set(i, *initial_value);
  }
  bool is_function_table = canonical_type.IsFunctionType();
  const bool shared = false;
  DirectHandle<WasmDispatchTable> dispatch_table =
      is_function_table ? isolate->factory()->NewWasmDispatchTable(
                              initial, canonical_type, shared)
                        : DirectHandle<WasmDispatchTable>{};

  DirectHandle<UnionOf<Undefined, Number, BigInt>> max =
      isolate->factory()->undefined_value();
  if (has_maximum) {
    if (address_type == wasm::AddressType::kI32) {
      DCHECK_GE(kMaxUInt32, maximum);
      max = isolate->factory()->NewNumber(maximum);
    } else {
      max = BigInt::FromUint64(isolate, maximum);
    }
  }

  DirectHandle<JSFunction> table_ctor(
      isolate->native_context()->wasm_table_constructor(), isolate);
  auto table_obj =
      Cast<WasmTableObject>(isolate->factory()->NewJSObject(table_ctor));
  DisallowGarbageCollection no_gc;

  if (!trusted_data.is_null()) {
    table_obj->set_trusted_data(*trusted_data);
  } else {
    table_obj->clear_trusted_data();
  }
  table_obj->set_entries(*entries);
  table_obj->set_current_length(initial);
  table_obj->set_maximum_length(*max);
  table_obj->set_raw_type(static_cast<int>(type.raw_bit_field()));
  table_obj->set_address_type(address_type);
  table_obj->set_padding_for_address_type_0(0);
  table_obj->set_padding_for_address_type_1(0);
#if TAGGED_SIZE_8_BYTES
  table_obj->set_padding_for_address_type_2(0);
#endif

  if (is_function_table) {
    DCHECK_EQ(table_obj->current_length(), dispatch_table->length());
    table_obj->set_trusted_dispatch_table(*dispatch_table);
    if (out_dispatch_table) *out_dispatch_table = dispatch_table;
  } else {
    table_obj->clear_trusted_dispatch_table();
  }
  return table_obj;
}

int WasmTableObject::Grow(Isolate* isolate, DirectHandle<WasmTableObject> table,
                          uint32_t count, DirectHandle<Object> init_value) {
  uint32_t old_size = table->current_length();
  if (count == 0) return old_size;  // Degenerate case: nothing to do.

  // Check if growing by {count} is valid.
  static_assert(wasm::kV8MaxWasmTableSize <= kMaxUInt32);
  uint64_t static_max_size = wasm::max_table_size();
  uint32_t max_size = static_cast<uint32_t>(std::min(
      static_max_size, table->maximum_length_u64().value_or(static_max_size)));
  DCHECK_LE(old_size, max_size);
  if (count > max_size - old_size) return -1;

  uint32_t new_size = old_size + count;
  // Even with 2x over-allocation, there should not be an integer overflow.
  static_assert(wasm::kV8MaxWasmTableSize <= kMaxInt / 2);
  DCHECK_GE(kMaxInt, new_size);
  int old_capacity = table->entries()->length();
  if (new_size > static_cast<uint32_t>(old_capacity)) {
    int grow = static_cast<int>(new_size) - old_capacity;
    // Grow at least by the old capacity, to implement exponential growing.
    grow = std::max(grow, old_capacity);
    // Never grow larger than the max size.
    grow = std::min(grow, static_cast<int>(max_size - old_capacity));
    auto new_store = isolate->factory()->CopyFixedArrayAndGrow(
        direct_handle(table->entries(), isolate), grow);
    table->set_entries(*new_store, WriteBarrierMode::UPDATE_WRITE_BARRIER);
  }

  if (table->has_trusted_dispatch_table()) {
    DirectHandle<WasmDispatchTable> dispatch_table(
        table->trusted_dispatch_table(isolate), isolate);
    DCHECK_EQ(old_size, dispatch_table->length());
    DirectHandle<WasmDispatchTable> new_dispatch_table =
        WasmDispatchTable::Grow(isolate, dispatch_table, new_size);
    if (!dispatch_table.is_identical_to(new_dispatch_table)) {
      table->set_trusted_dispatch_table(*new_dispatch_table);
    }
    DCHECK_EQ(new_size, table->trusted_dispatch_table(isolate)->length());

#if V8_ENABLE_DRUMBRAKE
    if (v8_flags.wasm_jitless) {
      Tagged<ProtectedWeakFixedArray> uses = dispatch_table->protected_uses();
      int used_length = GetUsedLength(uses);
      for (int i = kReservedSlotOffset; i < used_length; i += 2) {
        if (uses->get(i).IsCleared()) continue;
        Tagged<WasmTrustedInstanceData> instance = GetInstance(uses, i);
        if (instance->has_interpreter_object()) {
          int table_index = GetTableIndex(uses, i);
          wasm::WasmInterpreterRuntime::UpdateIndirectCallTable(
              isolate, direct_handle(instance->instance_object(), isolate),
              table_index);
        }
      }
    }
#endif  // V8_ENABLE_DRUMBRAKE
  }

  // Only update the current length after all allocations of sub-objects
  // (in particular: the new dispatch table) are done, so that heap verification
  // can assert that the dispatch table's length matches the table's length.
  table->set_current_length(new_size);

  for (uint32_t entry = old_size; entry < new_size; ++entry) {
    WasmTableObject::Set(isolate, table, entry, init_value);
  }
  return old_size;
}

MaybeDirectHandle<Object> WasmTableObject::JSToWasmElement(
    Isolate* isolate, DirectHandle<WasmTableObject> table,
    DirectHandle<Object> entry, const char** error_message) {
  const WasmModule* module = !table->has_trusted_data()
                                 ? nullptr
                                 : table->trusted_data(isolate)->module();
  return wasm::JSToWasmObject(isolate, module, entry, table->type(module),
                              error_message);
}

void WasmTableObject::SetFunctionTableEntry(Isolate* isolate,
                                            DirectHandle<WasmTableObject> table,
                                            int entry_index,
                                            DirectHandle<Object> entry) {
  if (IsWasmNull(*entry, isolate)) {
    table->ClearDispatchTable(entry_index);  // Degenerate case.
    table->entries()->set(entry_index, ReadOnlyRoots(isolate).wasm_null());
    return;
  }
  DCHECK(IsWasmFuncRef(*entry));
  DirectHandle<Object> external = WasmInternalFunction::GetOrCreateExternal(
      direct_handle(Cast<WasmFuncRef>(*entry)->internal(isolate), isolate));

  if (WasmExportedFunction::IsWasmExportedFunction(*external)) {
    auto exported_function = Cast<WasmExportedFunction>(external);
    auto func_data = exported_function->shared()->wasm_exported_function_data();
    DirectHandle<WasmTrustedInstanceData> target_instance_data(
        func_data->instance_data(), isolate);
    int func_index = func_data->function_index();
    const WasmModule* module = target_instance_data->module();
    SBXCHECK_BOUNDS(func_index, module->functions.size());
    auto* wasm_function = module->functions.data() + func_index;
    UpdateDispatchTable(isolate, table, entry_index, wasm_function,
                        target_instance_data
#if V8_ENABLE_DRUMBRAKE
                        ,
                        func_index
#endif  // V8_ENABLE_DRUMBRAKE
    );
  } else if (WasmJSFunction::IsWasmJSFunction(*external)) {
    UpdateDispatchTable(isolate, table, entry_index,
                        Cast<WasmJSFunction>(external));
  } else {
    DCHECK(WasmCapiFunction::IsWasmCapiFunction(*external));
    UpdateDispatchTable(isolate, table, entry_index,
                        Cast<WasmCapiFunction>(external));
  }
  table->entries()->set(entry_index, *entry);
}

// Note: This needs to be handlified because it can call {NewWasmImportData}.
void WasmTableObject::Set(Isolate* isolate, DirectHandle<WasmTableObject> table,
                          uint32_t index, DirectHandle<Object> entry) {
  // Callers need to perform bounds checks, type check, and error handling.
  DCHECK(table->is_in_bounds(index));

  DirectHandle<FixedArray> entries(table->entries(), isolate);
  // The FixedArray is addressed with int's.
  int entry_index = static_cast<int>(index);

  wasm::ValueType unsafe_type = table->unsafe_type();
  if (unsafe_type.has_index()) {
    DCHECK(table->has_trusted_data());
    const wasm::WasmModule* module = table->trusted_data(isolate)->module();
    if (module->has_signature(table->type(module).ref_index())) {
      SetFunctionTableEntry(isolate, table, entry_index, entry);
      return;
    }
    entries->set(entry_index, *entry);
    return;
  }
  switch (unsafe_type.generic_kind()) {
    case wasm::GenericKind::kExtern:
    case wasm::GenericKind::kString:
    case wasm::GenericKind::kStringViewWtf8:
    case wasm::GenericKind::kStringViewWtf16:
    case wasm::GenericKind::kStringViewIter:
    case wasm::GenericKind::kEq:
    case wasm::GenericKind::kStruct:
    case wasm::GenericKind::kArray:
    case wasm::GenericKind::kAny:
    case wasm::GenericKind::kI31:
    case wasm::GenericKind::kNone:
    case wasm::GenericKind::kNoFunc:
    case wasm::GenericKind::kNoExtern:
    case wasm::GenericKind::kExn:
    case wasm::GenericKind::kNoExn:
    case wasm::GenericKind::kCont:
    case wasm::GenericKind::kNoCont:
      entries->set(entry_index, *entry);
      return;
    case wasm::GenericKind::kFunc:
      SetFunctionTableEntry(isolate, table, entry_index, entry);
      return;
    case wasm::GenericKind::kBottom:
    case wasm::GenericKind::kTop:
    case wasm::GenericKind::kVoid:
    case wasm::GenericKind::kExternString:
      break;
  }
  UNREACHABLE();
}

DirectHandle<Object> WasmTableObject::Get(Isolate* isolate,
                                          DirectHandle<WasmTableObject> table,
                                          uint32_t index) {
  DirectHandle<FixedArray> entries(table->entries(), isolate);
  // Callers need to perform bounds checks and error handling.
  DCHECK(table->is_in_bounds(index));

  // The FixedArray is addressed with int's.
  int entry_index = static_cast<int>(index);

  DirectHandle<Object> entry(entries->get(entry_index), isolate);

  if (IsWasmNull(*entry, isolate)) return entry;
  if (IsWasmFuncRef(*entry)) return entry;

  wasm::ValueType unsafe_type = table->unsafe_type();
  if (unsafe_type.has_index()) {
    DCHECK(table->has_trusted_data());
    const WasmModule* module = table->trusted_data(isolate)->module();
    wasm::ModuleTypeIndex element_type = table->type(module).ref_index();
    if (module->has_array(element_type) || module->has_struct(element_type)) {
      return entry;
    }
    DCHECK(module->has_signature(element_type));
    // Fall through.
  } else {
    switch (unsafe_type.generic_kind()) {
      case wasm::GenericKind::kStringViewWtf8:
      case wasm::GenericKind::kStringViewWtf16:
      case wasm::GenericKind::kStringViewIter:
      case wasm::GenericKind::kExtern:
      case wasm::GenericKind::kString:
      case wasm::GenericKind::kEq:
      case wasm::GenericKind::kI31:
      case wasm::GenericKind::kStruct:
      case wasm::GenericKind::kArray:
      case wasm::GenericKind::kAny:
      case wasm::GenericKind::kNone:
      case wasm::GenericKind::kNoFunc:
      case wasm::GenericKind::kNoExtern:
      case wasm::GenericKind::kExn:
      case wasm::GenericKind::kNoExn:
      case wasm::GenericKind::kCont:
      case wasm::GenericKind::kNoCont:
        return entry;
      case wasm::GenericKind::kFunc:
        // Placeholder; handled below.
        break;
      case wasm::GenericKind::kBottom:
      case wasm::GenericKind::kTop:
      case wasm::GenericKind::kVoid:
      case wasm::GenericKind::kExternString:
        UNREACHABLE();
    }
  }

  // {entry} is not a valid entry in the table. It has to be a placeholder
  // for lazy initialization.
  DirectHandle<Tuple2> tuple = Cast<Tuple2>(entry);
  auto trusted_instance_data = direct_handle(
      Cast<WasmInstanceObject>(tuple->value1())->trusted_data(isolate),
      isolate);
  int function_index = Cast<Smi>(tuple->value2()).value();

  // Create a WasmInternalFunction and WasmFuncRef for the function if it does
  // not exist yet, and store it in the table.
  DirectHandle<WasmFuncRef> func_ref =
      WasmTrustedInstanceData::GetOrCreateFuncRef(
          isolate, trusted_instance_data, function_index);
  entries->set(entry_index, *func_ref);
  return func_ref;
}

void WasmTableObject::Fill(Isolate* isolate,
                           DirectHandle<WasmTableObject> table, uint32_t start,
                           DirectHandle<Object> entry, uint32_t count) {
  // Bounds checks must be done by the caller.
  DCHECK_LE(start, table->current_length());
  DCHECK_LE(count, table->current_length());
  DCHECK_LE(start + count, table->current_length());

  for (uint32_t i = 0; i < count; i++) {
    WasmTableObject::Set(isolate, table, start + i, entry);
  }
}

#if V8_ENABLE_SANDBOX || DEBUG
bool FunctionSigMatchesTable(wasm::CanonicalTypeIndex sig_id,
                             wasm::CanonicalValueType table_type) {
  DCHECK(table_type.is_object_reference());
  // When in-sandbox data is corrupted, we can't trust the statically
  // checked types; to prevent sandbox escapes, we have to verify actual
  // types before installing the dispatch table entry. There are three
  // alternative success conditions:
  // (1) Generic "funcref" tables can hold any function entry.
  if (!table_type.has_index() &&
      table_type.generic_kind() == wasm::GenericKind::kFunc) {
    return true;
  }
  // (2) Most function types are expected to be final, so they can be compared
  //     cheaply by canonicalized index equality.
  wasm::CanonicalTypeIndex canonical_table_type = table_type.ref_index();
  if (V8_LIKELY(sig_id == canonical_table_type)) return true;
  // (3) In the remaining cases, perform the full subtype check.
  return wasm::GetWasmEngine()->type_canonicalizer()->IsCanonicalSubtype(
      sig_id, canonical_table_type);
}
#endif  // V8_ENABLE_SANDBOX || DEBUG

// static
void WasmTableObject::UpdateDispatchTable(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    const wasm::WasmFunction* func,
    DirectHandle<WasmTrustedInstanceData> target_instance_data
#if V8_ENABLE_DRUMBRAKE
    ,
    int target_func_index
#endif  // V8_ENABLE_DRUMBRAKE
) {
  DirectHandle<TrustedObject> implicit_arg =
      func->imported
          // The function in the target instance was imported. Use its imports
          // table to look up the ref.
          ? direct_handle(Cast<TrustedObject>(
                              target_instance_data->dispatch_table_for_imports()
                                  ->implicit_arg(func->func_index)),
                          isolate)
          // For wasm functions, just pass the target instance data.
          : target_instance_data;

#if V8_ENABLE_DRUMBRAKE
  if (target_func_index <
      static_cast<int>(
          target_instance_data->module()->num_imported_functions)) {
    target_func_index = target_instance_data->imported_function_indices()->get(
        target_func_index);
  }
#endif  // V8_ENABLE_DRUMBRAKE

  const WasmModule* target_module = target_instance_data->module();
  wasm::CanonicalTypeIndex sig_id =
      target_module->canonical_sig_id(func->sig_index);
  DirectHandle<WasmDispatchTable> dispatch_table(
      table->trusted_dispatch_table(isolate), isolate);
  SBXCHECK(FunctionSigMatchesTable(sig_id, dispatch_table->table_type()));

  if (v8_flags.wasm_generic_wrapper && IsWasmImportData(*implicit_arg)) {
    auto import_data = Cast<WasmImportData>(implicit_arg);
    constexpr bool kShared = false;
    DirectHandle<WasmImportData> new_import_data =
        isolate->factory()->NewWasmImportData(import_data, kShared);
    new_import_data->set_call_origin(*dispatch_table);
    new_import_data->set_table_slot(entry_index);
    implicit_arg = new_import_data;
  }

  std::optional<std::shared_ptr<wasm::WasmImportWrapperHandle>> maybe_wrapper =
      target_instance_data->dispatch_table_for_imports()->MaybeGetWrapperHandle(
          func->func_index);
  if (maybe_wrapper) {
    CHECK(IsWasmImportData(*implicit_arg));
    dispatch_table->SetForWrapper(entry_index,
                                  Cast<WasmImportData>(*implicit_arg),
                                  *maybe_wrapper, sig_id,
#if V8_ENABLE_DRUMBRAKE
                                  target_func_index,
#endif
                                  WasmDispatchTable::kExistingEntry);
#if V8_ENABLE_DRUMBRAKE
  } else if (v8_flags.wasm_jitless) {
    DCHECK(v8_flags.wasm_jitless);
    if (IsWasmImportData(*implicit_arg)) {
      dispatch_table->SetForWrapper(
          entry_index, Cast<WasmImportData>(*implicit_arg), {}, sig_id,
          target_func_index, WasmDispatchTable::kExistingEntry);
    } else {
      dispatch_table->SetForNonWrapper(
          entry_index, Cast<WasmTrustedInstanceData>(*implicit_arg), {}, sig_id,
          target_func_index, WasmDispatchTable::kExistingEntry);
    }

    Tagged<ProtectedWeakFixedArray> uses = dispatch_table->protected_uses();
    int used_length = GetUsedLength(uses);
    for (int i = kReservedSlotOffset; i < used_length; i += 2) {
      if (uses->get(i).IsCleared()) continue;
      Tagged<WasmTrustedInstanceData> instance = GetInstance(uses, i);
      if (instance->has_interpreter_object()) {
        int table_index = GetTableIndex(uses, i);
        wasm::WasmInterpreterRuntime::UpdateIndirectCallTable(
            isolate, direct_handle(instance->instance_object(), isolate),
            table_index);
      }
    }
#endif  // V8_ENABLE_DRUMBRAKE
  } else {
    CHECK(IsWasmTrustedInstanceData(*implicit_arg));
    dispatch_table->SetForNonWrapper(
        entry_index, Cast<WasmTrustedInstanceData>(*implicit_arg),
        target_instance_data->GetCallTarget(func->func_index), sig_id,
#if V8_ENABLE_DRUMBRAKE
        target_func_index,
#endif
        WasmDispatchTable::kExistingEntry);
  }
}

// static
void WasmTableObject::UpdateDispatchTable(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    DirectHandle<WasmJSFunction> function) {
  Tagged<WasmJSFunctionData> function_data =
      function->shared()->wasm_js_function_data();
  wasm::CanonicalTypeIndex sig_id = function_data->sig_index();

  DirectHandle<WasmDispatchTable> dispatch_table(
      table->trusted_dispatch_table(isolate), isolate);
  SBXCHECK(FunctionSigMatchesTable(sig_id, dispatch_table->table_type()));

  std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle =
      function_data->offheap_data()->wrapper_handle();

  DirectHandle<WasmImportData> import_data(
      Cast<WasmImportData>(function_data->internal()->implicit_arg()), isolate);
#ifdef DEBUG
  Address call_target =
      wasm::GetProcessWideWasmCodePointerTable()
          ->GetEntrypointWithoutSignatureCheck(wrapper_handle->code_pointer());
#endif

  if (wrapper_handle->has_code()) {
    DCHECK_EQ(wrapper_handle->code()->instruction_start(), call_target);
  } else {
    const wasm::CanonicalSig* sig =
        wasm::GetWasmEngine()->type_canonicalizer()->LookupFunctionSignature(
            sig_id);
    // We still don't have a compiled wrapper. Allocate a new import_data
    // so we can store the proper call_origin for later wrapper tier-up.
    DCHECK(call_target ==
               Builtins::EmbeddedEntryOf(Builtin::kWasmToJsWrapperAsm) ||
           call_target ==
               Builtins::EmbeddedEntryOf(Builtin::kWasmToJsWrapperInvalidSig));
    constexpr bool kShared = false;
    wasm::Suspend suspend = function_data->GetSuspend();
    import_data = isolate->factory()->NewWasmImportData(
        function, suspend, MaybeDirectHandle<WasmTrustedInstanceData>{}, sig,
        kShared);
    import_data->SetIndexInTableAsCallOrigin(*dispatch_table, entry_index);
  }

  DCHECK(wrapper_handle->has_code() ||
         call_target ==
             Builtins::EmbeddedEntryOf(Builtin::kWasmToJsWrapperAsm) ||
         call_target ==
             Builtins::EmbeddedEntryOf(Builtin::kWasmToJsWrapperInvalidSig));
  dispatch_table->SetForWrapper(entry_index, *import_data, wrapper_handle,
                                sig_id,
#if V8_ENABLE_DRUMBRAKE
                                WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
                                WasmDispatchTable::kExistingEntry);
}

// static
void WasmTableObject::UpdateDispatchTable(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    DirectHandle<WasmCapiFunction> capi_function) {
  DirectHandle<WasmCapiFunctionData> func_data(
      capi_function->shared()->wasm_capi_function_data(), isolate);
  const wasm::CanonicalSig* sig = func_data->sig();
  DCHECK(wasm::GetTypeCanonicalizer()->Contains(sig));
  wasm::CanonicalTypeIndex sig_index = func_data->sig_index();

  wasm::WasmImportWrapperCache* cache = wasm::GetWasmImportWrapperCache();
  auto kind = wasm::ImportCallKind::kWasmToCapi;
  int param_count = static_cast<int>(sig->parameter_count());

  std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle =
      cache->GetCompiled(isolate, kind, sig_index, param_count,
                         wasm::kNoSuspend, sig);

  Tagged<WasmImportData> implicit_arg =
      Cast<WasmImportData>(func_data->internal()->implicit_arg());
  Tagged<WasmDispatchTable> dispatch_table =
      table->trusted_dispatch_table(isolate);
  SBXCHECK(FunctionSigMatchesTable(sig_index, dispatch_table->table_type()));
  dispatch_table->SetForWrapper(entry_index, implicit_arg, wrapper_handle,
                                sig_index,
#if V8_ENABLE_DRUMBRAKE
                                WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
                                WasmDispatchTable::kExistingEntry);
}

void WasmTableObject::ClearDispatchTable(int index) {
  DisallowGarbageCollection no_gc;
  Isolate* isolate = Isolate::Current();
  Tagged<WasmDispatchTable> dispatch_table = trusted_dispatch_table(isolate);
  dispatch_table->Clear(index, WasmDispatchTable::kExistingEntry);
#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    Tagged<ProtectedWeakFixedArray> uses = dispatch_table->protected_uses();
    int used_length = GetUsedLength(uses);
    for (int i = kReservedSlotOffset; i < used_length; i += 2) {
      if (uses->get(i).IsCleared()) continue;
      Tagged<WasmTrustedInstanceData> non_shared_instance_data =
          GetInstance(uses, i);
      if (non_shared_instance_data->has_interpreter_object()) {
        int table_index = GetTableIndex(uses, i);
        DirectHandle<WasmInstanceObject> instance_handle(
            non_shared_instance_data->instance_object(), isolate);
        wasm::WasmInterpreterRuntime::ClearIndirectCallCacheEntry(
            isolate, instance_handle, table_index, index);
      }
    }
  }
#endif  // V8_ENABLE_DRUMBRAKE
}

// static
void WasmTableObject::SetFunctionTablePlaceholder(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    int func_index) {
  // Put (instance, func_index) as a Tuple2 into the entry_index.
  // The {WasmExportedFunction} will be created lazily.
  // Allocate directly in old space as the tuples are typically long-lived, and
  // we create many of them, which would result in lots of GC when initializing
  // large tables.
  // TODO(42204563): Avoid crashing if the instance object is not available.
  CHECK(trusted_instance_data->has_instance_object());
  DirectHandle<Tuple2> tuple = isolate->factory()->NewTuple2(
      direct_handle(trusted_instance_data->instance_object(), isolate),
      direct_handle(Smi::FromInt(func_index), isolate), AllocationType::kOld);
  table->entries()->set(entry_index, *tuple);
}

// static
void WasmTableObject::GetFunctionTableEntry(
    Isolate* isolate, DirectHandle<WasmTableObject> table, int entry_index,
    bool* is_valid, bool* is_null,
    MaybeDirectHandle<WasmTrustedInstanceData>* instance_data,
    int* function_index, MaybeDirectHandle<WasmJSFunction>* maybe_js_function) {
#if DEBUG
  if (table->has_trusted_data()) {
    const wasm::WasmModule* module = table->trusted_data(isolate)->module();
    DCHECK(wasm::IsSubtypeOf(table->type(module), wasm::kWasmFuncRef, module));
  } else {
    // A function table defined outside a module may only have type exactly
    // {funcref}.
    DCHECK(table->unsafe_type() == wasm::kWasmFuncRef);
  }
  DCHECK_LT(entry_index, table->current_length());
#endif
  // We initialize {is_valid} with {true}. We may change it later.
  *is_valid = true;
  DirectHandle<Object> element(table->entries()->get(entry_index), isolate);

  *is_null = IsWasmNull(*element, isolate);
  if (*is_null) return;

  if (IsWasmFuncRef(*element)) {
    DirectHandle<WasmInternalFunction> internal{
        Cast<WasmFuncRef>(*element)->internal(isolate), isolate};
    element = WasmInternalFunction::GetOrCreateExternal(internal);
  }
  if (WasmExportedFunction::IsWasmExportedFunction(*element)) {
    auto target_func = Cast<WasmExportedFunction>(element);
    auto func_data = Cast<WasmExportedFunctionData>(
        target_func->shared()->wasm_exported_function_data());
    *instance_data = direct_handle(func_data->instance_data(), isolate);
    *function_index = func_data->function_index();
    *maybe_js_function = MaybeDirectHandle<WasmJSFunction>();
    return;
  }
  if (WasmJSFunction::IsWasmJSFunction(*element)) {
    *instance_data = MaybeDirectHandle<WasmTrustedInstanceData>();
    *maybe_js_function = Cast<WasmJSFunction>(element);
    return;
  }
  if (IsTuple2(*element)) {
    auto tuple = Cast<Tuple2>(element);
    *instance_data = direct_handle(
        Cast<WasmInstanceObject>(tuple->value1())->trusted_data(isolate),
        isolate);
    *function_index = Cast<Smi>(tuple->value2()).value();
    *maybe_js_function = MaybeDirectHandle<WasmJSFunction>();
    return;
  }
  *is_valid = false;
}

DirectHandle<WasmSuspendingObject> WasmSuspendingObject::New(
    Isolate* isolate, DirectHandle<JSReceiver> callable) {
  DirectHandle<JSFunction> suspending_ctor(
      isolate->native_context()->wasm_suspending_constructor(), isolate);
  auto suspending_obj = Cast<WasmSuspendingObject>(
      isolate->factory()->NewJSObject(suspending_ctor));
  suspending_obj->set_callable(*callable);
  return suspending_obj;
}

namespace {

void SetInstanceMemory(Tagged<WasmTrustedInstanceData> trusted_instance_data,
                       Tagged<JSArrayBuffer> buffer, int memory_index) {
  DisallowHeapAllocation no_gc;
  const WasmModule* module = trusted_instance_data->module();
  const wasm::WasmMemory& memory = module->memories[memory_index];

  bool is_wasm_module = module->origin == wasm::kWasmOrigin;
  bool use_trap_handler = memory.bounds_checks == wasm::kTrapHandler;
  // Asm.js does not use trap handling.
  CHECK_IMPLIES(use_trap_handler, is_wasm_module);
  // ArrayBuffers allocated for Wasm do always have a BackingStore.
  std::shared_ptr<BackingStore> backing_store = buffer->GetBackingStore();
  CHECK_IMPLIES(is_wasm_module, backing_store);
  CHECK_IMPLIES(is_wasm_module, backing_store->is_wasm_memory());
  // Wasm modules compiled to use the trap handler don't have bounds checks,
  // so they must have a memory that has guard regions.
  // Note: This CHECK can fail when in-sandbox corruption modified a
  // WasmMemoryObject. We currently believe that this would at worst
  // corrupt the contents of other Wasm memories or ArrayBuffers, but having
  // this CHECK in release mode is nice as an additional layer of defense.
  CHECK_IMPLIES(use_trap_handler, backing_store->has_guard_regions());
  // We checked this before, but a malicious worker thread with an in-sandbox
  // corruption primitive could have modified it since then.
  size_t byte_length = buffer->GetByteLength();
  SBXCHECK_GE(byte_length, memory.min_memory_size);

  trusted_instance_data->SetRawMemory(
      memory_index, reinterpret_cast<uint8_t*>(buffer->backing_store()),
      byte_length);

#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless &&
      trusted_instance_data->has_interpreter_object()) {
    AllowHeapAllocation allow_heap;
    Isolate* isolate = Isolate::Current();
    HandleScope scope(isolate);
    wasm::WasmInterpreterRuntime::UpdateMemoryAddress(
        direct_handle(trusted_instance_data->instance_object(), isolate));
  }
#endif  // V8_ENABLE_DRUMBRAKE
}

}  // namespace

DirectHandle<WasmMemoryObject> WasmMemoryObject::New(
    Isolate* isolate, DirectHandle<JSArrayBuffer> buffer, int maximum,
    wasm::AddressType address_type) {
  DirectHandle<JSFunction> memory_ctor(
      isolate->native_context()->wasm_memory_constructor(), isolate);

  auto memory_object = Cast<WasmMemoryObject>(
      isolate->factory()->NewJSObject(memory_ctor, AllocationType::kOld));
  memory_object->set_array_buffer(*buffer);
  memory_object->set_maximum_pages(maximum);
  memory_object->set_address_type(address_type);
  memory_object->set_padding_for_address_type_0(0);
  memory_object->set_padding_for_address_type_1(0);
#if TAGGED_SIZE_8_BYTES
  memory_object->set_padding_for_address_type_2(0);
#endif
  memory_object->set_instances(ReadOnlyRoots{isolate}.empty_weak_array_list());

  if (buffer->is_resizable_by_js()) {
    memory_object->FixUpResizableArrayBuffer(*buffer);
  }

  std::shared_ptr<BackingStore> backing_store = buffer->GetBackingStore();
  if (buffer->is_shared()) {
    // Only Wasm memory can be shared (in contrast to asm.js memory).
    CHECK(backing_store && backing_store->is_wasm_memory());
    backing_store->AttachSharedWasmMemoryObject(isolate, memory_object);
  } else if (backing_store) {
    CHECK(!backing_store->is_shared());
  }

  // Memorize a link from the JSArrayBuffer to its owning WasmMemoryObject
  // instance.
  DirectHandle<Symbol> symbol =
      isolate->factory()->array_buffer_wasm_memory_symbol();
  Object::SetProperty(isolate, buffer, symbol, memory_object).Check();

  return memory_object;
}

MaybeDirectHandle<WasmMemoryObject> WasmMemoryObject::New(
    Isolate* isolate, int initial, int maximum, SharedFlag shared,
    wasm::AddressType address_type) {
  bool has_maximum = maximum != kNoMaximum;

  int engine_maximum = address_type == wasm::AddressType::kI64
                           ? static_cast<int>(wasm::max_mem64_pages())
                           : static_cast<int>(wasm::max_mem32_pages());

  if (initial > engine_maximum) return {};

#ifdef V8_TARGET_ARCH_32_BIT
  // On 32-bit platforms we need an heuristic here to balance overall memory
  // and address space consumption.
  constexpr int kGBPages = 1024 * 1024 * 1024 / wasm::kWasmPageSize;
  // We allocate the smallest of the following sizes, but at least the initial
  // size:
  // 1) the module-defined maximum;
  // 2) 1GB;
  // 3) the engine maximum;
  int allocation_maximum = std::min(kGBPages, engine_maximum);
  int heuristic_maximum;
  if (initial > kGBPages) {
    // We always allocate at least the initial size.
    heuristic_maximum = initial;
  } else if (has_maximum) {
    // We try to reserve the maximum, but at most the allocation_maximum to
    // avoid OOMs.
    heuristic_maximum = std::min(maximum, allocation_maximum);
  } else if (shared == SharedFlag::kShared) {
    // If shared memory has no maximum, we use the allocation_maximum as an
    // implicit maximum.
    heuristic_maximum = allocation_maximum;
  } else {
    // If non-shared memory has no maximum, we only allocate the initial size
    // and then grow with realloc.
    heuristic_maximum = initial;
  }
#else
  int heuristic_maximum =
      has_maximum ? std::min(engine_maximum, maximum) : engine_maximum;
#endif

  std::unique_ptr<BackingStore> backing_store =
      BackingStore::AllocateWasmMemory(isolate, initial, heuristic_maximum,
                                       address_type == wasm::AddressType::kI32
                                           ? WasmMemoryFlag::kWasmMemory32
                                           : WasmMemoryFlag::kWasmMemory64,
                                       shared);

  if (!backing_store) return {};

  DirectHandle<JSArrayBuffer> buffer =
      shared == SharedFlag::kShared
          ? isolate->factory()->NewJSSharedArrayBuffer(std::move(backing_store))
          : isolate->factory()->NewJSArrayBuffer(std::move(backing_store));

  return New(isolate, buffer, maximum, address_type);
}

void WasmMemoryObject::UseInInstance(
    Isolate* isolate, DirectHandle<WasmMemoryObject> memory,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    DirectHandle<WasmTrustedInstanceData> shared_trusted_instance_data,
    int memory_index_in_instance) {
  SetInstanceMemory(*trusted_instance_data, memory->array_buffer(),
                    memory_index_in_instance);
  if (!shared_trusted_instance_data.is_null()) {
    SetInstanceMemory(*shared_trusted_instance_data, memory->array_buffer(),
                      memory_index_in_instance);
  }
  DirectHandle<WeakArrayList> instances{memory->instances(), isolate};
  auto weak_instance_object = MaybeObjectDirectHandle::Weak(
      trusted_instance_data->instance_object(), isolate);
  instances = WeakArrayList::Append(isolate, instances, weak_instance_object);
  memory->set_instances(*instances);
}

void WasmMemoryObject::SetNewBuffer(Isolate* isolate,
                                    Tagged<JSArrayBuffer> new_buffer) {
  DisallowGarbageCollection no_gc;
  const bool new_buffer_is_resizable_by_js = new_buffer->is_resizable_by_js();
  if (new_buffer_is_resizable_by_js) {
    FixUpResizableArrayBuffer(*new_buffer);
  }
  Tagged<JSArrayBuffer> old_buffer = array_buffer();
  set_array_buffer(new_buffer);
  // Iterating and updating all instances is a slow operation, and is required
  // when the pointer to memory or size of memory changes.
  //
  // When refreshing the buffer for changing the resizability of the JS-exposed
  // (S)AB, both the data pointer and size stay the same, only the JS object
  // changes.
  //
  // When refreshing the buffer for growing a memory exposing a fixed-length
  // (S)AB (i.e. both the old and new buffers are !is_resizable_by_js()), the
  // size is changing, and updating the instances is needed.
  //
  // This function is never called in a way such that both the old and new
  // (S)ABs are resizable. Once a Wasm memory exposes a resizable (S)AB,
  // changing the size does not refresh the buffer.
  DCHECK(!old_buffer->is_resizable_by_js() ||
         !new_buffer->is_resizable_by_js());
  if (!old_buffer->is_resizable_by_js() && !new_buffer_is_resizable_by_js) {
    UpdateInstances(isolate);
  }
}

void WasmMemoryObject::UpdateInstances(Isolate* isolate) {
  DisallowGarbageCollection no_gc;
  Tagged<WeakArrayList> instances = this->instances();
  for (int i = 0, len = instances->length(); i < len; ++i) {
    Tagged<MaybeObject> elem = instances->Get(i);
    if (elem.IsCleared()) continue;
    Tagged<WasmInstanceObject> instance_object =
        Cast<WasmInstanceObject>(elem.GetHeapObjectAssumeWeak());
    Tagged<WasmTrustedInstanceData> trusted_data =
        instance_object->trusted_data(isolate);
    // TODO(clemens): Avoid the iteration by also remembering the memory index
    // if we ever see larger numbers of memories.
    Tagged<FixedArray> memory_objects = trusted_data->memory_objects();
    int num_memories = memory_objects->length();
    for (int mem_idx = 0; mem_idx < num_memories; ++mem_idx) {
      if (memory_objects->get(mem_idx) == *this) {
        SetInstanceMemory(trusted_data, array_buffer(), mem_idx);
      }
    }
  }
}

void WasmMemoryObject::FixUpResizableArrayBuffer(
    Tagged<JSArrayBuffer> new_buffer) {
  DCHECK(has_maximum_pages());
  DCHECK(new_buffer->is_resizable_by_js());
  DisallowGarbageCollection no_gc;
  if (new_buffer->is_shared()) new_buffer->set_byte_length(0);
  // Unlike JS-created resizable buffers, Wasm memories' backing store maximum
  // may differ from the exposed maximum.
  uintptr_t max_byte_length;
  if constexpr (kSystemPointerSize == 4) {
    // The spec says the maximum number of pages for 32-bit memories is 65536,
    // which means the maximum byte size is 65536 * 65536 (= 2^32), which is
    // UINT32_MAX+1. BackingStores, ArrayBuffers, and TypedArrays represent byte
    // lengths as uintptr_t, and UINT32_MAX+1 is not representable on 32bit.
    //
    // As a willful violation and gross hack, if we're exposing a Wasm memory
    // with an unrepresentable maximum, subtract one page size.
    uint64_t max_byte_length64 =
        static_cast<uint64_t>(maximum_pages()) * wasm::kWasmPageSize;
    if (max_byte_length64 > std::numeric_limits<uintptr_t>::max()) {
      max_byte_length64 =
          std::numeric_limits<uintptr_t>::max() - wasm::kWasmPageSize;
      CHECK(new_buffer->GetBackingStore()->max_byte_length() <=
            max_byte_length64);
    }
    max_byte_length = static_cast<uintptr_t>(max_byte_length64);
  } else {
    max_byte_length = maximum_pages() * wasm::kWasmPageSize;
  }
  new_buffer->set_max_byte_length(max_byte_length);
}

// static
DirectHandle<JSArrayBuffer> WasmMemoryObject::RefreshBuffer(
    Isolate* isolate, DirectHandle<WasmMemoryObject> memory_object,
    std::shared_ptr<BackingStore> new_backing_store) {
  // Detach old and create a new one with the new backing store.
  DirectHandle<JSArrayBuffer> old_buffer(memory_object->array_buffer(),
                                         isolate);
#ifdef DEBUG
  void* old_data_pointer = old_buffer->backing_store();
  size_t old_byte_length = old_buffer->byte_length();
#endif
  JSArrayBuffer::Detach(old_buffer, true).Check();
  DirectHandle<JSArrayBuffer> new_buffer =
      isolate->factory()->NewJSArrayBuffer(std::move(new_backing_store));
#ifdef DEBUG
  bool data_pointer_unchanged = new_buffer->backing_store() == old_data_pointer;
  bool byte_length_unchanged = new_buffer->byte_length() == old_byte_length;
  bool resizability_changed =
      old_buffer->is_resizable_by_js() != new_buffer->is_resizable_by_js();
  // SetNewBuffer only calls UpdateInstances if resizability didn't change,
  // which depends on resizability changing not changing the data pointer or
  // byte length. See comment in SetNewBuffer.
  DCHECK_IMPLIES(resizability_changed,
                 data_pointer_unchanged && byte_length_unchanged);
#endif
  memory_object->SetNewBuffer(isolate, *new_buffer);
  // Memorize a link from the JSArrayBuffer to its owning WasmMemoryObject
  // instance.
  DirectHandle<Symbol> symbol =
      isolate->factory()->array_buffer_wasm_memory_symbol();
  Object::SetProperty(isolate, new_buffer, symbol, memory_object).Check();
  return new_buffer;
}

// static
DirectHandle<JSArrayBuffer> WasmMemoryObject::RefreshSharedBuffer(
    Isolate* isolate, DirectHandle<WasmMemoryObject> memory_object,
    ResizableFlag resizable_by_js) {
  DirectHandle<JSArrayBuffer> old_buffer(memory_object->array_buffer(),
                                         isolate);
  std::shared_ptr<BackingStore> backing_store = old_buffer->GetBackingStore();
  // Wasm memory always has a BackingStore.
  CHECK_NOT_NULL(backing_store);
  CHECK(backing_store->is_wasm_memory());
  CHECK(backing_store->is_shared());

  // Keep a raw pointer to the backing store for a CHECK later one. Make it
  // {void*} so we do not accidentally try to use it for anything else.
  void* expected_backing_store = backing_store.get();

  DirectHandle<JSArrayBuffer> new_buffer =
      isolate->factory()->NewJSSharedArrayBuffer(std::move(backing_store));
  CHECK_EQ(expected_backing_store, new_buffer->GetBackingStore().get());
  if (resizable_by_js == ResizableFlag::kResizable) {
    new_buffer->set_is_resizable_by_js(true);
  }
  memory_object->SetNewBuffer(isolate, *new_buffer);
  // Memorize a link from the JSArrayBuffer to its owning WasmMemoryObject
  // instance.
  DirectHandle<Symbol> symbol =
      isolate->factory()->array_buffer_wasm_memory_symbol();
  Object::SetProperty(isolate, new_buffer, symbol, memory_object).Check();
  return new_buffer;
}

// static
int32_t WasmMemoryObject::Grow(Isolate* isolate,
                               DirectHandle<WasmMemoryObject> memory_object,
                               uint32_t pages) {
  TRACE_EVENT0("v8.wasm", "wasm.GrowMemory");
  DirectHandle<JSArrayBuffer> old_buffer(memory_object->array_buffer(),
                                         isolate);

  std::shared_ptr<BackingStore> backing_store = old_buffer->GetBackingStore();
  // Wasm memory can grow, and Wasm memory always has a backing store.
  DCHECK_NOT_NULL(backing_store);

  // Check for maximum memory size.
  // Note: The {wasm::max_mem_pages()} limit is already checked in
  // {BackingStore::CopyWasmMemory}, and is irrelevant for
  // {GrowWasmMemoryInPlace} because memory is never allocated with more
  // capacity than that limit.
  size_t old_size = old_buffer->GetByteLength();
  DCHECK_EQ(0, old_size % wasm::kWasmPageSize);
  size_t old_pages = old_size / wasm::kWasmPageSize;
  size_t max_pages = memory_object->is_memory64() ? wasm::max_mem64_pages()
                                                  : wasm::max_mem32_pages();
  if (memory_object->has_maximum_pages()) {
    max_pages = std::min(max_pages,
                         static_cast<size_t>(memory_object->maximum_pages()));
  }
  DCHECK_GE(max_pages, old_pages);
  if (pages > max_pages - old_pages) return -1;

  const bool must_grow_in_place =
      old_buffer->is_shared() || backing_store->has_guard_regions() ||
      backing_store->is_resizable_by_js() || pages == 0;
  const bool try_grow_in_place =
      must_grow_in_place || !v8_flags.stress_wasm_memory_moving;

  std::optional<size_t> result_inplace =
      try_grow_in_place
          ? backing_store->GrowWasmMemoryInPlace(isolate, pages, max_pages)
          : std::nullopt;
  if (must_grow_in_place && !result_inplace.has_value()) {
    // There are different limits per platform, thus crash if the correctness
    // fuzzer is running.
    if (v8_flags.correctness_fuzzer_suppressions) {
      FATAL("could not grow wasm memory");
    }
    return -1;
  }

  // Handle shared memory first.
  if (old_buffer->is_shared()) {
    DCHECK(result_inplace.has_value());
    backing_store->BroadcastSharedWasmMemoryGrow(isolate);
    if (!old_buffer->is_resizable_by_js()) {
      // Broadcasting the update should update this memory object too.
      CHECK_NE(*old_buffer, memory_object->array_buffer());
    }
    size_t new_pages = result_inplace.value() + pages;
    // If the allocation succeeded, then this can't possibly overflow:
    size_t new_byte_length = new_pages * wasm::kWasmPageSize;
    // This is a less than check, as it is not guaranteed that the SAB
    // length here will be equal to the stashed length above as calls to
    // grow the same memory object can come in from different workers.
    // It is also possible that a call to Grow was in progress when
    // handling this call.
    CHECK_LE(new_byte_length, memory_object->array_buffer()->GetByteLength());
    // As {old_pages} was read racefully, we return here the synchronized
    // value provided by {GrowWasmMemoryInPlace}, to provide the atomic
    // read-modify-write behavior required by the spec.
    return static_cast<int32_t>(result_inplace.value());  // success
  }

  size_t new_pages = old_pages + pages;
  // Check for overflow (should be excluded via {max_pages} above).
  DCHECK_LE(old_pages, new_pages);

  // Check if the non-shared memory could grow in-place.
  if (result_inplace.has_value()) {
    if (memory_object->array_buffer()->is_resizable_by_js()) {
      memory_object->array_buffer()->set_byte_length(new_pages *
                                                     wasm::kWasmPageSize);
      memory_object->UpdateInstances(isolate);
    } else {
      RefreshBuffer(isolate, memory_object, std::move(backing_store));
    }
    DCHECK_EQ(result_inplace.value(), old_pages);
    return static_cast<int32_t>(result_inplace.value());  // success
  }
  DCHECK(!memory_object->array_buffer()->is_resizable_by_js());

  // We should only try growing by copying if we actually grow.
  DCHECK_LT(old_pages, new_pages);

  // Try allocating a new backing store and copying.
  // To avoid overall quadratic complexity of many small grow operations, we
  // grow by at least 0.5 MB + 12.5% of the existing memory size.
  // These numbers are kept small because we must be careful about address
  // space consumption on 32-bit platforms.
  size_t min_growth = old_pages + 8 + (old_pages >> 3);
  // First apply {min_growth}, then {max_pages}. The order is important, because
  // {min_growth} can be bigger than {max_pages}, and in that case we want to
  // cap to {max_pages}.
  size_t new_capacity = std::min(max_pages, std::max(new_pages, min_growth));
  DCHECK_LE(new_pages, new_capacity);
  std::unique_ptr<BackingStore> new_backing_store =
      backing_store->CopyWasmMemory(isolate, new_pages, new_capacity,
                                    memory_object->is_memory64()
                                        ? WasmMemoryFlag::kWasmMemory64
                                        : WasmMemoryFlag::kWasmMemory32);
  if (!new_backing_store) {
    // Crash on out-of-memory if the correctness fuzzer is running.
    if (v8_flags.correctness_fuzzer_suppressions) {
      FATAL("could not grow wasm memory");
    }
    // Set the non-determinism flag in the WasmEngine.
    wasm::WasmEngine::set_had_nondeterminism();
    return -1;
  }

  RefreshBuffer(isolate, memory_object, std::move(new_backing_store));

  return static_cast<int32_t>(old_pages);  // success
}

// static
DirectHandle<JSArrayBuffer> WasmMemoryObject::ToFixedLengthBuffer(
    Isolate* isolate, DirectHandle<WasmMemoryObject> memory_object) {
  DirectHandle<JSArrayBuffer> old_buffer(memory_object->array_buffer(),
                                         isolate);
  DCHECK(old_buffer->is_resizable_by_js());
  if (old_buffer->is_shared()) {
    return RefreshSharedBuffer(isolate, memory_object,
                               ResizableFlag::kNotResizable);
  }
  std::shared_ptr<BackingStore> backing_store = old_buffer->GetBackingStore();
  DCHECK_NOT_NULL(backing_store);
  backing_store->MakeWasmMemoryResizableByJS(false);
  return RefreshBuffer(isolate, memory_object, std::move(backing_store));
}

// static
DirectHandle<JSArrayBuffer> WasmMemoryObject::ToResizableBuffer(
    Isolate* isolate, DirectHandle<WasmMemoryObject> memory_object) {
  // Resizable ArrayBuffers require a maximum size during creation. Mirror the
  // requirement when reflecting Wasm memory as a resizable buffer.
  DCHECK(memory_object->has_maximum_pages());
  DirectHandle<JSArrayBuffer> old_buffer(memory_object->array_buffer(),
                                         isolate);
  DCHECK(!old_buffer->is_resizable_by_js());
  if (old_buffer->is_shared()) {
    return RefreshSharedBuffer(isolate, memory_object,
                               ResizableFlag::kResizable);
  }
  std::shared_ptr<BackingStore> backing_store = old_buffer->GetBackingStore();
  DCHECK_NOT_NULL(backing_store);
  backing_store->MakeWasmMemoryResizableByJS(true);
  return RefreshBuffer(isolate, memory_object, std::move(backing_store));
}

MaybeDirectHandle<WasmMemoryMapDescriptor>
WasmMemoryMapDescriptor::NewFromAnonymous(Isolate* isolate, size_t length) {
#if V8_TARGET_OS_LINUX
  CHECK(v8_flags.experimental_wasm_memory_control);
  DirectHandle<JSFunction> descriptor_ctor(
      isolate->native_context()->wasm_memory_map_descriptor_constructor(),
      isolate);

  int file_descriptor = memfd_create("wasm_memory_map_descriptor", MFD_CLOEXEC);
  if (file_descriptor == -1) {
    return {};
  }
  int ret_val = ftruncate(file_descriptor, length);
  if (ret_val == -1) {
    return {};
  }

  return NewFromFileDescriptor(isolate, file_descriptor);
#else   // V8_TARGET_OS_LINUX
  return {};
#endif  // V8_TARGET_OS_LINUX
}

DirectHandle<WasmMemoryMapDescriptor>
WasmMemoryMapDescriptor::NewFromFileDescriptor(Isolate* isolate,
                                               int file_descriptor) {
  CHECK(v8_flags.experimental_wasm_memory_control);
  DirectHandle<JSFunction> descriptor_ctor(
      isolate->native_context()->wasm_memory_map_descriptor_constructor(),
      isolate);

  auto descriptor_object = Cast<WasmMemoryMapDescriptor>(
      isolate->factory()->NewJSObject(descriptor_ctor, AllocationType::kOld));

  descriptor_object->set_file_descriptor(file_descriptor);
  descriptor_object->set_memory(ClearedValue());
  descriptor_object->set_offset(0);
  descriptor_object->set_size(0);

  return descriptor_object;
}

size_t WasmMemoryMapDescriptor::MapDescriptor(
    DirectHandle<WasmMemoryObject> memory, size_t offset) {
#if V8_TARGET_OS_LINUX
  CHECK(v8_flags.experimental_wasm_memory_control);
  if (memory->array_buffer()->is_shared()) {
    // TODO(ahaas): Handle concurrent calls to `MapDescriptor`. To prevent
    // concurrency issues, we disable `MapDescriptor` for shared wasm memories
    // so far.
    return 0;
  }
  if (memory->is_memory64()) {
    // TODO(ahaas): Handle memory64. So far the offset in the
    // MemoryMapDescriptor is only an uint32. Either the offset has to be
    // interpreted as a wasm memory page, or be extended to an uint64.
    return 0;
  }

  uint8_t* target =
      reinterpret_cast<uint8_t*>(memory->array_buffer()->backing_store()) +
      offset;

  struct stat stat_for_size;
  if (fstat(this->file_descriptor(), &stat_for_size) == -1) {
    // Could not determine file size.
    return 0;
  }
  size_t size = RoundUp(stat_for_size.st_size,
                        GetArrayBufferPageAllocator()->AllocatePageSize());

  if (size + offset < size) {
    // Overflow
    return 0;
  }
  if (size + offset > memory->array_buffer()->GetByteLength()) {
    return 0;
  }

  void* ret_val = mmap(target, size, PROT_READ | PROT_WRITE,
                       MAP_FIXED | MAP_SHARED, this->file_descriptor(), 0);
  CHECK_NE(ret_val, MAP_FAILED);
  CHECK_EQ(ret_val, target);
  return size;
#else
  return 0;
#endif
}

bool WasmMemoryMapDescriptor::UnmapDescriptor() {
#if V8_TARGET_OS_LINUX
  CHECK(v8_flags.experimental_wasm_memory_control);
  DisallowGarbageCollection no_gc;

  i::Tagged<i::WasmMemoryObject> memory =
      Cast<i::WasmMemoryObject>(MakeStrong(this->memory()));
  if (memory.is_null()) {
    return true;
  }
  uint32_t offset = this->offset();
  uint32_t size = this->size();

  // The following checks already passed during `MapDescriptor`, and they should
  // still pass.
  CHECK(!memory->is_memory64());
  CHECK(!memory->array_buffer()->is_shared());
  CHECK_EQ(size % GetArrayBufferPageAllocator()->AllocatePageSize(), 0);
  CHECK_GE(size + offset, size);
  CHECK_LE(size + offset, memory->array_buffer()->byte_length());

  uint8_t* target =
      reinterpret_cast<uint8_t*>(memory->array_buffer()->backing_store()) +
      offset;

  void* ret_val = mmap(target, size, PROT_READ | PROT_WRITE,
                       MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  CHECK_NE(ret_val, MAP_FAILED);
  CHECK_EQ(ret_val, target);
  return true;
#else
  return false;
#endif
}

// static
MaybeDirectHandle<WasmGlobalObject> WasmGlobalObject::New(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> trusted_data,
    MaybeDirectHandle<JSArrayBuffer> maybe_untagged_buffer,
    MaybeDirectHandle<FixedArray> maybe_tagged_buffer, wasm::ValueType type,
    int32_t offset, bool is_mutable) {
  DirectHandle<JSFunction> global_ctor(
      isolate->native_context()->wasm_global_constructor(), isolate);
  auto global_obj =
      Cast<WasmGlobalObject>(isolate->factory()->NewJSObject(global_ctor));
  {
    // Disallow GC until all fields have acceptable types.
    DisallowGarbageCollection no_gc;
    if (!trusted_data.is_null()) {
      global_obj->set_trusted_data(*trusted_data);
    } else {
      global_obj->clear_trusted_data();
    }
    global_obj->set_type(type);
    global_obj->set_offset(offset);
    global_obj->set_is_mutable(is_mutable);
  }

  if (type.is_reference()) {
    DCHECK(maybe_untagged_buffer.is_null());
    DirectHandle<FixedArray> tagged_buffer;
    if (!maybe_tagged_buffer.ToHandle(&tagged_buffer)) {
      // If no buffer was provided, create one.
      tagged_buffer =
          isolate->factory()->NewFixedArray(1, AllocationType::kOld);
      CHECK_EQ(offset, 0);
    }
    global_obj->set_tagged_buffer(*tagged_buffer);
  } else {
    DCHECK(maybe_tagged_buffer.is_null());
    uint32_t type_size = type.value_kind_size();

    DirectHandle<JSArrayBuffer> untagged_buffer;
    if (!maybe_untagged_buffer.ToHandle(&untagged_buffer)) {
      MaybeDirectHandle<JSArrayBuffer> result =
          isolate->factory()->NewJSArrayBufferAndBackingStore(
              offset + type_size, InitializedFlag::kZeroInitialized);

      if (!result.ToHandle(&untagged_buffer)) {
        isolate->Throw(*isolate->factory()->NewRangeError(
            MessageTemplate::kOutOfMemory,
            isolate->factory()->NewStringFromAsciiChecked(
                "WebAssembly.Global")));
        return {};
      }
    }

    // Check that the offset is in bounds.
    CHECK_LE(offset + type_size, untagged_buffer->GetByteLength());

    global_obj->set_untagged_buffer(*untagged_buffer);
  }

  return global_obj;
}

FunctionTargetAndImplicitArg::FunctionTargetAndImplicitArg(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> target_instance_data,
    int target_func_index) {
  implicit_arg_ = target_instance_data;
  if (target_func_index <
      static_cast<int>(
          target_instance_data->module()->num_imported_functions)) {
    // The function in the target instance was imported. Load the ref from the
    // dispatch table for imports.
    implicit_arg_ = direct_handle(
        Cast<TrustedObject>(
            target_instance_data->dispatch_table_for_imports()->implicit_arg(
                target_func_index)),
        isolate);
#if V8_ENABLE_DRUMBRAKE
    target_func_index_ = target_instance_data->imported_function_indices()->get(
        target_func_index);
#endif  // V8_ENABLE_DRUMBRAKE
  } else {
    // The function in the target instance was not imported.
#if V8_ENABLE_DRUMBRAKE
    target_func_index_ = target_func_index;
#endif  // V8_ENABLE_DRUMBRAKE
  }
  call_target_ = target_instance_data->GetCallTarget(target_func_index);
}

void ImportedFunctionEntry::SetWasmToWrapper(
    Isolate* isolate, DirectHandle<JSReceiver> callable,
    std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle,
    wasm::Suspend suspend, const wasm::CanonicalSig* sig,
    wasm::CanonicalTypeIndex sig_id) {
#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    // Ignores wrapper_handle.
    DirectHandle<WasmImportData> import_data =
        isolate->factory()->NewWasmImportData(callable, suspend, instance_data_,
                                              sig, false /*kShared*/);
    {
      DisallowGarbageCollection no_gc;
      instance_data_->dispatch_table_for_imports()->SetForWrapper(
          index_, *import_data, {}, sig_id, -1,
          WasmDispatchTable::kExistingEntry);
    }
    instance_data_->imported_function_indices()->set(index_, -1);
    return;
  }
#endif  // V8_ENABLE_DRUMBRAKE

  TRACE_IFT("Import callable 0x%" PRIxPTR "[%d] = {callable=0x%" PRIxPTR
            ", target=%p}\n",
            instance_data_->ptr(), index_, callable->ptr(),
            wrapper_handle->has_code()
                ? nullptr
                : wrapper_handle->code()->instructions().begin());

#if DEBUG
  if (wrapper_handle->has_code()) {
    DCHECK(wrapper_handle->code()->kind() == wasm::WasmCode::kWasmToJsWrapper ||
           wrapper_handle->code()->kind() ==
               wasm::WasmCode::kWasmToCapiWrapper);
    DCHECK_EQ(wrapper_handle->code()->signature_hash(), sig->signature_hash());
  }
#endif  // DEBUG

  constexpr bool kShared = false;
  DirectHandle<WasmImportData> import_data =
      isolate->factory()->NewWasmImportData(callable, suspend, instance_data_,
                                            sig, kShared);

  if (!wrapper_handle->has_code()) {
    import_data->SetIndexInTableAsCallOrigin(
        instance_data_->dispatch_table_for_imports(), index_);
  }

  DisallowGarbageCollection no_gc;
  Tagged<WasmDispatchTable> dispatch_table =
      instance_data_->dispatch_table_for_imports();

  dispatch_table->SetForWrapper(index_, *import_data, std::move(wrapper_handle),
                                sig_id,
#if V8_ENABLE_DRUMBRAKE
                                WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
                                WasmDispatchTable::kNewEntry);
}

void ImportedFunctionEntry::SetWasmToWasm(
    Tagged<WasmTrustedInstanceData> target_instance_data,
    WasmCodePointer call_target, wasm::CanonicalTypeIndex sig_id
#if V8_ENABLE_DRUMBRAKE
    ,
    int exported_function_index
#endif  // V8_ENABLE_DRUMBRAKE
) {
  TRACE_IFT("Import Wasm 0x%" PRIxPTR "[%d] = {instance_data=0x%" PRIxPTR
            ", target=0x%" PRIxPTR "}\n",
            instance_data_->ptr(), index_, target_instance_data.ptr(),
            wasm::GetProcessWideWasmCodePointerTable()
                ->GetEntrypointWithoutSignatureCheck(call_target));
  DisallowGarbageCollection no_gc;
  Tagged<WasmDispatchTable> dispatch_table =
      instance_data_->dispatch_table_for_imports();
  dispatch_table->SetForNonWrapper(index_, target_instance_data, call_target,
                                   sig_id,
#if V8_ENABLE_DRUMBRAKE
                                   WasmDispatchTable::kInvalidFunctionIndex,
#endif  // V8_ENABLE_DRUMBRAKE
                                   WasmDispatchTable::kExistingEntry);

#if V8_ENABLE_DRUMBRAKE
  instance_data_->imported_function_indices()->set(index_,
                                                   exported_function_index);
#endif  // V8_ENABLE_DRUMBRAKE
}

// Returns an empty Tagged<Object>() if no callable is available, a JSReceiver
// otherwise.
Tagged<Object> ImportedFunctionEntry::maybe_callable() {
  Tagged<Object> data = implicit_arg();
  if (!IsWasmImportData(data)) return Tagged<Object>();
  return Cast<JSReceiver>(Cast<WasmImportData>(data)->callable());
}

Tagged<JSReceiver> ImportedFunctionEntry::callable() {
  return Cast<JSReceiver>(Cast<WasmImportData>(implicit_arg())->callable());
}

Tagged<Object> ImportedFunctionEntry::implicit_arg() {
  return instance_data_->dispatch_table_for_imports()->implicit_arg(index_);
}

WasmCodePointer ImportedFunctionEntry::target() {
  return instance_data_->dispatch_table_for_imports()->target(index_);
}

#if V8_ENABLE_DRUMBRAKE
int ImportedFunctionEntry::function_index_in_called_module() {
  return instance_data_->imported_function_indices()->get(index_);
}
#endif  // V8_ENABLE_DRUMBRAKE

// static
constexpr std::array<uint16_t, WasmTrustedInstanceData::kTaggedFieldsCount>
    WasmTrustedInstanceData::kTaggedFieldOffsets;
// static
constexpr std::array<const char*, WasmTrustedInstanceData::kTaggedFieldsCount>
    WasmTrustedInstanceData::kTaggedFieldNames;
// static
constexpr std::array<uint16_t, 6>
    WasmTrustedInstanceData::kProtectedFieldOffsets;
// static
constexpr std::array<const char*, 6>
    WasmTrustedInstanceData::kProtectedFieldNames;

void WasmTrustedInstanceData::SetRawMemory(int memory_index, uint8_t* mem_start,
                                           size_t mem_size) {
  CHECK_LT(memory_index, module()->memories.size());

  CHECK_LE(mem_size, module()->memories[memory_index].is_memory64()
                         ? wasm::max_mem64_bytes()
                         : wasm::max_mem32_bytes());
  // All memory bases and sizes are stored in a TrustedFixedAddressArray.
  Tagged<TrustedFixedAddressArray> bases_and_sizes = memory_bases_and_sizes();
  bases_and_sizes->set(memory_index * 2, reinterpret_cast<Address>(mem_start));
  bases_and_sizes->set(memory_index * 2 + 1, mem_size);
  // Memory 0 has fast-access fields.
  if (memory_index == 0) {
    set_memory0_start(mem_start);
    set_memory0_size(mem_size);
  }
}

#if V8_ENABLE_DRUMBRAKE
DirectHandle<Tuple2> WasmTrustedInstanceData::GetOrCreateInterpreterObject(
    DirectHandle<WasmInstanceObject> instance) {
  DCHECK(v8_flags.wasm_jitless);
  Isolate* isolate = Isolate::Current();
  DirectHandle<WasmTrustedInstanceData> trusted_data(
      instance->trusted_data(isolate), isolate);
  if (trusted_data->has_interpreter_object()) {
    return direct_handle(trusted_data->interpreter_object(), isolate);
  }
  DirectHandle<Tuple2> new_interpreter = WasmInterpreterObject::New(instance);
  DCHECK(trusted_data->has_interpreter_object());
  return new_interpreter;
}

DirectHandle<Tuple2> WasmTrustedInstanceData::GetInterpreterObject(
    DirectHandle<WasmInstanceObject> instance) {
  DCHECK(v8_flags.wasm_jitless);
  Isolate* isolate = Isolate::Current();
  DirectHandle<WasmTrustedInstanceData> trusted_data(
      instance->trusted_data(isolate), isolate);
  CHECK(trusted_data->has_interpreter_object());
  return direct_handle(trusted_data->interpreter_object(), isolate);
}
#endif  // V8_ENABLE_DRUMBRAKE

DirectHandle<WasmTrustedInstanceData> WasmTrustedInstanceData::New(
    Isolate* isolate, DirectHandle<WasmModuleObject> module_object,
    std::shared_ptr<wasm::NativeModule> native_module, bool shared) {
  // We don't read the NativeModule from the WasmModuleObject here to guard
  // against swapping attacks.
  DCHECK_EQ(native_module.get(), module_object->native_module());

  // Do first allocate all objects that will be stored in instance fields,
  // because otherwise we would have to allocate when the instance is not fully
  // initialized yet, which can lead to heap verification errors.
  const WasmModule* module = native_module->module();

  AllocationType allocation =
      shared ? AllocationType::kSharedOld : AllocationType::kYoung;

  int num_imported_functions = module->num_imported_functions;
  DirectHandle<WasmDispatchTable> dispatch_table_for_imports =
      isolate->factory()->NewWasmDispatchTable(num_imported_functions,
                                               wasm::kWasmFuncRef, shared);
  DirectHandle<FixedArray> well_known_imports =
      isolate->factory()->NewFixedArray(num_imported_functions, allocation);

  DirectHandle<FixedArray> func_refs =
      isolate->factory()->NewFixedArrayWithZeroes(
          static_cast<int>(module->functions.size()), allocation);

  int num_imported_mutable_globals = module->num_imported_mutable_globals;
  // The imported_mutable_globals is essentially a FixedAddressArray (storing
  // sandboxed pointers), but some entries (the indices for reference-type
  // globals) are accessed as 32-bit integers which is more convenient with a
  // raw ByteArray.
  DirectHandle<FixedAddressArray> imported_mutable_globals =
      FixedAddressArray::New(isolate, num_imported_mutable_globals, allocation);

  int num_data_segments = module->num_declared_data_segments;
  DirectHandle<FixedAddressArray> data_segment_starts =
      FixedAddressArray::New(isolate, num_data_segments, allocation);
  DirectHandle<FixedUInt32Array> data_segment_sizes =
      FixedUInt32Array::New(isolate, num_data_segments, allocation);

#if V8_ENABLE_DRUMBRAKE
  DirectHandle<FixedInt32Array> imported_function_indices =
      FixedInt32Array::New(isolate, num_imported_functions);
#endif  // V8_ENABLE_DRUMBRAKE

  static_assert(wasm::kV8MaxWasmMemories < kMaxInt / 2);
  int num_memories = static_cast<int>(module->memories.size());
  DirectHandle<FixedArray> memory_objects =
      isolate->factory()->NewFixedArray(num_memories, allocation);
  DirectHandle<TrustedFixedAddressArray> memory_bases_and_sizes =
      TrustedFixedAddressArray::New(
          isolate, 2 * num_memories,
          shared ? AllocationType::kSharedTrusted : AllocationType::kTrusted);

  // TODO(clemensb): Should we have singleton empty dispatch table in the
  // trusted space?
  DirectHandle<WasmDispatchTable> empty_dispatch_table =
      isolate->factory()->NewWasmDispatchTable(0, wasm::kWasmFuncRef, shared);
  DirectHandle<ProtectedFixedArray> empty_protected_fixed_array =
      isolate->factory()->empty_protected_fixed_array();

  // Use the same memory estimate as the (untrusted) Managed in
  // WasmModuleObject. This is not security critical, and we at least always
  // read the memory estimation of *some* NativeModule here.
  size_t estimated_size =
      module_object->managed_native_module()->estimated_size();
  DirectHandle<TrustedManaged<wasm::NativeModule>>
      trusted_managed_native_module = TrustedManaged<wasm::NativeModule>::From(
          isolate, estimated_size, native_module, shared);

  // Now allocate the WasmTrustedInstanceData.
  // During this step, no more allocations should happen because the instance is
  // incomplete yet, so we should not trigger heap verification at this point.
  DirectHandle<WasmTrustedInstanceData> trusted_data =
      isolate->factory()->NewWasmTrustedInstanceData(shared);
  {
    DisallowHeapAllocation no_gc;

    // Some constants:
    uint8_t* empty_backing_store_buffer =
        reinterpret_cast<uint8_t*>(EmptyBackingStoreBuffer());
    ReadOnlyRoots ro_roots{isolate};
    Tagged<FixedArray> empty_fixed_array = ro_roots.empty_fixed_array();

    trusted_data->set_dispatch_table_for_imports(*dispatch_table_for_imports);
    trusted_data->set_imported_mutable_globals(*imported_mutable_globals);
    trusted_data->set_dispatch_table0(*empty_dispatch_table);
    trusted_data->set_dispatch_tables(*empty_protected_fixed_array);
    trusted_data->set_shared_part(*trusted_data);  // TODO(14616): Good enough?
    trusted_data->set_data_segment_starts(*data_segment_starts);
    trusted_data->set_data_segment_sizes(*data_segment_sizes);
    trusted_data->set_element_segments(empty_fixed_array);
    trusted_data->set_managed_native_module(*trusted_managed_native_module);
    trusted_data->set_new_allocation_limit_address(
        isolate->heap()->NewSpaceAllocationLimitAddress());
    trusted_data->set_new_allocation_top_address(
        isolate->heap()->NewSpaceAllocationTopAddress());
    trusted_data->set_old_allocation_limit_address(
        isolate->heap()->OldSpaceAllocationLimitAddress());
    trusted_data->set_old_allocation_top_address(
        isolate->heap()->OldSpaceAllocationTopAddress());
    trusted_data->set_globals_start(empty_backing_store_buffer);
#if V8_ENABLE_DRUMBRAKE
    trusted_data->set_imported_function_indices(*imported_function_indices);
#endif  // V8_ENABLE_DRUMBRAKE
    if (!shared) trusted_data->set_native_context(*isolate->native_context());
    trusted_data->set_jump_table_start(native_module->jump_table_start());
    trusted_data->set_hook_on_function_call_address(
        isolate->debug()->hook_on_function_call_address());
    trusted_data->set_managed_object_maps(
        *isolate->factory()->empty_fixed_array());
    trusted_data->set_well_known_imports(*well_known_imports);
    trusted_data->set_func_refs(*func_refs);
    trusted_data->set_feedback_vectors(
        *isolate->factory()->empty_fixed_array());
    trusted_data->set_tiering_budget_array(
        native_module->tiering_budget_array());
    trusted_data->set_break_on_entry(module_object->script()->break_on_entry());
    trusted_data->InitDataSegmentArrays(native_module.get());
    trusted_data->set_memory0_start(empty_backing_store_buffer);
    trusted_data->set_memory0_size(0);
    trusted_data->set_memory_objects(*memory_objects);
    trusted_data->set_memory_bases_and_sizes(*memory_bases_and_sizes);
    trusted_data->set_stress_deopt_counter_address(
        ExternalReference::stress_deopt_count(isolate).address());

    for (int i = 0; i < num_memories; ++i) {
      memory_bases_and_sizes->set(
          2 * i, reinterpret_cast<Address>(empty_backing_store_buffer));
      memory_bases_and_sizes->set(2 * i + 1, 0);
    }
  }

  // Allocate the exports object, to be store in the instance object.
  DirectHandle<JSObject> exports_object =
      isolate->factory()->NewJSObjectWithNullProto();

  DirectHandle<WasmInstanceObject> instance_object;

  if (!shared) {
    // Allocate the WasmInstanceObject (JS wrapper).
    DirectHandle<JSFunction> instance_cons(
        isolate->native_context()->wasm_instance_constructor(), isolate);
    instance_object = Cast<WasmInstanceObject>(
        isolate->factory()->NewJSObject(instance_cons, AllocationType::kOld));
    instance_object->set_trusted_data(*trusted_data);
    instance_object->set_module_object(*module_object);
    instance_object->set_exports_object(*exports_object);
    trusted_data->set_instance_object(*instance_object);
  }

  // Insert the new instance into the scripts weak list of instances. This list
  // is used for breakpoints affecting all instances belonging to the script.
  if (module_object->script()->type() == Script::Type::kWasm &&
      !instance_object.is_null()) {
    DirectHandle<WeakArrayList> weak_instance_list(
        module_object->script()->wasm_weak_instance_list(), isolate);
    weak_instance_list =
        WeakArrayList::Append(isolate, weak_instance_list,
                              MaybeObjectDirectHandle::Weak(instance_object));
    module_object->script()->set_wasm_weak_instance_list(*weak_instance_list);
  }

  return trusted_data;
}

void WasmTrustedInstanceData::InitDataSegmentArrays(
    const wasm::NativeModule* native_module) {
  const WasmModule* module = native_module->module();
  base::Vector<const uint8_t> wire_bytes = native_module->wire_bytes();
  uint32_t num_data_segments = module->num_declared_data_segments;
  // The number of declared data segments will be zero if there is no DataCount
  // section. These arrays will not be allocated nor initialized in that case,
  // since they cannot be used (since the validator checks that number of
  // declared data segments when validating the memory.init and memory.drop
  // instructions).
  DCHECK(num_data_segments == 0 ||
         num_data_segments == module->data_segments.size());
  for (uint32_t i = 0; i < num_data_segments; ++i) {
    const wasm::WasmDataSegment& segment = module->data_segments[i];
    // Initialize the pointer and size of passive segments.
    auto source_bytes = wire_bytes.SubVector(segment.source.offset(),
                                             segment.source.end_offset());
    data_segment_starts()->set(i,
                               reinterpret_cast<Address>(source_bytes.begin()));
    // Set the active segments to being already dropped, since memory.init on
    // a dropped passive segment and an active segment have the same
    // behavior.
    data_segment_sizes()->set(static_cast<int>(i),
                              segment.active ? 0 : source_bytes.length());
  }
}

WasmCodePointer WasmTrustedInstanceData::GetCallTarget(uint32_t func_index) {
  wasm::NativeModule* native_module = this->native_module();
  SBXCHECK_BOUNDS(func_index, native_module->num_functions());
  if (func_index < native_module->num_imported_functions()) {
    return dispatch_table_for_imports()->target(func_index);
  }

  if (v8_flags.wasm_jitless) {
    return wasm::kInvalidWasmCodePointer;
  }

  return native_module->GetCodePointerHandle(func_index);
}

// static
bool WasmTrustedInstanceData::CopyTableEntries(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    uint32_t table_dst_index, uint32_t table_src_index, uint32_t dst,
    uint32_t src, uint32_t count) {
  CHECK_LT(table_dst_index, trusted_instance_data->tables()->length());
  CHECK_LT(table_src_index, trusted_instance_data->tables()->length());
  auto table_dst =
      direct_handle(Cast<WasmTableObject>(
                        trusted_instance_data->tables()->get(table_dst_index)),
                    isolate);
  auto table_src =
      direct_handle(Cast<WasmTableObject>(
                        trusted_instance_data->tables()->get(table_src_index)),
                    isolate);
  uint32_t max_dst = table_dst->current_length();
  uint32_t max_src = table_src->current_length();
  bool copy_backward = src < dst;
  if (!base::IsInBounds(dst, count, max_dst) ||
      !base::IsInBounds(src, count, max_src)) {
    return false;
  }

  // no-op
  if ((dst == src && table_dst_index == table_src_index) || count == 0) {
    return true;
  }

  for (uint32_t i = 0; i < count; ++i) {
    uint32_t src_index = copy_backward ? (src + count - i - 1) : src + i;
    uint32_t dst_index = copy_backward ? (dst + count - i - 1) : dst + i;
    auto value = WasmTableObject::Get(isolate, table_src, src_index);
    WasmTableObject::Set(isolate, table_dst, dst_index, value);
  }
  return true;
}

// static
std::optional<MessageTemplate> WasmTrustedInstanceData::InitTableEntries(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    DirectHandle<WasmTrustedInstanceData> shared_trusted_instance_data,
    uint32_t table_index, uint32_t segment_index, uint32_t dst, uint32_t src,
    uint32_t count) {
  AccountingAllocator allocator;
  // This {Zone} will be used only by the temporary WasmFullDecoder allocated
  // down the line from this call. Therefore it is safe to stack-allocate it
  // here.
  Zone zone(&allocator, "LoadElemSegment");

  const WasmModule* module = trusted_instance_data->module();

  bool table_is_shared = module->tables[table_index].shared;
  bool segment_is_shared = module->elem_segments[segment_index].shared;

  DirectHandle<WasmTableObject> table_object(
      Cast<WasmTableObject>((table_is_shared ? shared_trusted_instance_data
                                             : trusted_instance_data)
                                ->tables()
                                ->get(table_index)),
      isolate);

  // If needed, try to lazily initialize the element segment.
  std::optional<MessageTemplate> opt_error = wasm::InitializeElementSegment(
      &zone, isolate, trusted_instance_data, shared_trusted_instance_data,
      segment_index);
  if (opt_error.has_value()) return opt_error;

  DirectHandle<FixedArray> elem_segment(
      Cast<FixedArray>((segment_is_shared ? shared_trusted_instance_data
                                          : trusted_instance_data)
                           ->element_segments()
                           ->get(segment_index)),
      isolate);
  if (!base::IsInBounds<uint64_t>(dst, count, table_object->current_length())) {
    return {MessageTemplate::kWasmTrapTableOutOfBounds};
  }
  if (!base::IsInBounds<uint64_t>(src, count, elem_segment->length())) {
    return {MessageTemplate::kWasmTrapElementSegmentOutOfBounds};
  }

  for (size_t i = 0; i < count; i++) {
    WasmTableObject::Set(
        isolate, table_object, static_cast<int>(dst + i),
        direct_handle(elem_segment->get(static_cast<int>(src + i)), isolate));
  }

  return {};
}

bool WasmTrustedInstanceData::try_get_func_ref(int index,
                                               Tagged<WasmFuncRef>* result) {
  Tagged<Object> val = func_refs()->get(index);
  if (IsSmi(val)) return false;
  *result = Cast<WasmFuncRef>(val);
  return true;
}

namespace {

V8_INLINE DirectHandle<WasmExportedFunction> CreateExportedFunction(
    Isolate* isolate, const WasmModule* module, int function_index,
    wasm::ModuleTypeIndex sig_index, DirectHandle<WasmFuncRef> func_ref,
    DirectHandle<WasmInternalFunction> internal_function,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data) {
  DCHECK_EQ(func_ref->internal(isolate), *internal_function);
  wasm::CanonicalTypeIndex canon_sig_id = module->canonical_sig_id(sig_index);
  const wasm::CanonicalSig* canon_sig =
      wasm::GetTypeCanonicalizer()->LookupFunctionSignature(canon_sig_id);
  // For now, we assume traditional behavior where the receiver is ignored.
  // If the corresponding bit in the WasmExportedFunctionData is flipped later,
  // we'll have to reset any existing compiled wrapper.
  bool receiver_is_first_param = false;
  DirectHandle<Code> wrapper_code = WasmExportedFunction::GetWrapper(
      isolate, canon_sig, canon_sig_id, receiver_is_first_param, module);
  int arity = static_cast<int>(canon_sig->parameter_count());
  DirectHandle<WasmExportedFunction> external = WasmExportedFunction::New(
      isolate, trusted_instance_data, func_ref, internal_function, arity,
      wrapper_code, module, function_index, canon_sig_id, canon_sig,
      wasm::kNoPromise);
  internal_function->set_external(*external);
  return external;
}

}  // namespace

DirectHandle<WasmFuncRef> WasmTrustedInstanceData::GetOrCreateFuncRef(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    int function_index, wasm::PrecreateExternal precreate_external) {
  bool shared = HeapLayout::InAnySharedSpace(*trusted_instance_data);
  Tagged<WasmFuncRef> existing_func_ref;
  if (trusted_instance_data->try_get_func_ref(function_index,
                                              &existing_func_ref)) {
    // Note: if {precreate_external}, we could add a check here that the
    // WasmExportedFunction exists as well. It's fine to skip that check on
    // this fast path, as {GetOrCreateExternal} will do what's necessary.
    return direct_handle(existing_func_ref, isolate);
  }

  const WasmModule* module = trusted_instance_data->module();
  bool is_import =
      function_index < static_cast<int>(module->num_imported_functions);
  wasm::ModuleTypeIndex sig_index = module->functions[function_index].sig_index;
  DirectHandle<TrustedObject> implicit_arg =
      is_import ? direct_handle(
                      Cast<TrustedObject>(
                          trusted_instance_data->dispatch_table_for_imports()
                              ->implicit_arg(function_index)),
                      isolate)
                : trusted_instance_data;
  // Imported Wasm functions should always have a cached func ref. Recreating
  // the `WasmInternalFunction` below does not work because the `function_index`
  // is an import index which cannot be interpreted in the context of the
  // imported `WasmTrustedInstanceData`.
  SBXCHECK(!is_import || Is<WasmImportData>(implicit_arg));

  // TODO(14034): Create funcref RTTs lazily?
  DirectHandle<Map> rtt{
      Cast<Map>(
          trusted_instance_data->managed_object_maps()->get(sig_index.index)),
      isolate};

  // Reuse the call target of the instance. In case of import wrappers, the
  // wrapper will automatically get tiered up together since it will use the
  // same CPT entry.
  DirectHandle<WasmInternalFunction> internal_function =
      isolate->factory()->NewWasmInternalFunction(
          implicit_arg, function_index, shared,
          trusted_instance_data->GetCallTarget(function_index));
  DirectHandle<WasmFuncRef> func_ref =
      isolate->factory()->NewWasmFuncRef(internal_function, rtt, shared);
  trusted_instance_data->func_refs()->set(function_index, *func_ref);

  if (precreate_external == wasm::kPrecreateExternal) {
    CreateExportedFunction(isolate, module, function_index, sig_index, func_ref,
                           internal_function, trusted_instance_data);
  }

  return func_ref;
}

bool WasmInternalFunction::try_get_external(Tagged<JSFunction>* result) {
  if (IsUndefined(external())) return false;
  *result = Cast<JSFunction>(external());
  return true;
}

// static
DirectHandle<JSFunction> WasmInternalFunction::GetOrCreateExternal(
    DirectHandle<WasmInternalFunction> internal) {
  Isolate* isolate = Isolate::Current();

  Tagged<JSFunction> existing_external;
  if (internal->try_get_external(&existing_external)) {
    return direct_handle(existing_external, isolate);
  }

  // {this} can either be:
  // - a declared function, i.e. {implicit_arg()} is a WasmTrustedInstanceData,
  // - or an imported callable, i.e. {implicit_arg()} is a WasmImportData which
  //   refers to the imported instance.
  // It cannot be a JS/C API function as for those, the external function is set
  // at creation.
  DirectHandle<TrustedObject> implicit_arg{internal->implicit_arg(), isolate};
  DirectHandle<WasmTrustedInstanceData> instance_data =
      IsWasmTrustedInstanceData(*implicit_arg)
          ? Cast<WasmTrustedInstanceData>(implicit_arg)
          : direct_handle(Cast<WasmImportData>(*implicit_arg)->instance_data(),
                          isolate);
  const WasmModule* module = instance_data->module();
  int function_index = internal->function_index();
  wasm::ModuleTypeIndex sig_index = module->functions[function_index].sig_index;

  DirectHandle<WasmFuncRef> func_ref{
      Cast<WasmFuncRef>(instance_data->func_refs()->get(function_index)),
      isolate};

  return CreateExportedFunction(isolate, module, function_index, sig_index,
                                func_ref, internal, instance_data);
}

// static
DirectHandle<Code> WasmExportedFunction::GetWrapper(
    Isolate* isolate, const wasm::CanonicalSig* sig,
    wasm::CanonicalTypeIndex sig_id, bool receiver_is_first_param,
    const WasmModule* module) {
#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    return isolate->builtins()->code_handle(
        Builtin::kGenericJSToWasmInterpreterWrapper);
  }
#endif  // V8_ENABLE_DRUMBRAKE
  Tagged<CodeWrapper> entry = wasm::WasmExportWrapperCache::Get(
      isolate, sig_id, receiver_is_first_param);
  if (!entry.is_null()) {
    return direct_handle(entry->code(isolate), isolate);
  }
  if (wasm::CanUseGenericJsToWasmWrapper(module, sig)) {
    if (v8_flags.stress_wasm_stack_switching) {
      return isolate->builtins()->code_handle(Builtin::kWasmStressSwitch);
    }
    return isolate->builtins()->code_handle(Builtin::kJSToWasmWrapper);
  }
  // Otherwise compile a wrapper.
  DirectHandle<Code> compiled =
      wasm::JSToWasmWrapperCompilationUnit::CompileJSToWasmWrapper(
          isolate, sig, sig_id, receiver_is_first_param);
  // This should have added an entry in the per-isolate cache.
  DCHECK_EQ(compiled->wrapper(), wasm::WasmExportWrapperCache::Get(
                                     isolate, sig_id, receiver_is_first_param));
  return compiled;
}

// static
void WasmExportedFunction::MarkAsReceiverIsFirstParam(
    Isolate* isolate, DirectHandle<WasmExportedFunction> exported_function) {
  Tagged<WasmExportedFunctionData> data =
      exported_function->shared()->wasm_exported_function_data();
  if (data->receiver_is_first_param() != 0) return;
  data->set_receiver_is_first_param(1);
  DirectHandle<WasmExportedFunctionData> data_handle(data, isolate);
  // Reset the wrapper code. If that's a compiled wrapper, it baked in the
  // bit we just flipped.
  DirectHandle<Code> wrapper =
      GetWrapper(isolate, data->sig(), data->sig_index(), true,
                 data->instance_data()->module());
  data = {};  // Might be stale due to GC.
  data_handle->set_wrapper_code(*wrapper);
  exported_function->UpdateCode(isolate, *wrapper);
}

void WasmImportData::SetIndexInTableAsCallOrigin(
    Tagged<WasmDispatchTable> table, int entry_index) {
  set_call_origin(table);
  set_table_slot(entry_index);
}

void WasmImportData::SetFuncRefAsCallOrigin(Tagged<WasmInternalFunction> func) {
  set_call_origin(func);
}

uint8_t* WasmTrustedInstanceData::GetGlobalStorage(
    const wasm::WasmGlobal& global) {
  DCHECK(!global.type.is_reference());
  if (global.mutability && global.imported) {
    return reinterpret_cast<uint8_t*>(
        imported_mutable_globals()->get_sandboxed_pointer(global.index));
  }
  return globals_start() + global.offset;
}

std::pair<Tagged<FixedArray>, uint32_t>
WasmTrustedInstanceData::GetGlobalBufferAndIndex(
    const wasm::WasmGlobal& global) {
  DisallowGarbageCollection no_gc;
  DCHECK(global.type.is_reference());
  if (global.mutability && global.imported) {
    Tagged<FixedArray> buffer =
        Cast<FixedArray>(imported_mutable_globals_buffers()->get(global.index));
    Address idx = imported_mutable_globals()->get(global.index);
    DCHECK_LE(idx, std::numeric_limits<uint32_t>::max());
    return {buffer, static_cast<uint32_t>(idx)};
  }
  return {tagged_globals_buffer(), global.offset};
}

wasm::WasmValue WasmTrustedInstanceData::GetGlobalValue(
    Isolate* isolate, const wasm::WasmGlobal& global) {
  DisallowGarbageCollection no_gc;
  if (global.type.is_reference()) {
    Tagged<FixedArray> global_buffer;  // The buffer of the global.
    uint32_t global_index = 0;         // The index into the buffer.
    std::tie(global_buffer, global_index) = GetGlobalBufferAndIndex(global);
    return wasm::WasmValue(
        direct_handle(global_buffer->get(global_index), isolate),
        module()->canonical_type(global.type));
  }
  Address ptr = reinterpret_cast<Address>(GetGlobalStorage(global));
  switch (global.type.kind()) {
#define CASE_TYPE(valuetype, ctype) \
  case wasm::valuetype:             \
    return wasm::WasmValue(base::ReadUnalignedValue<ctype>(ptr));
    FOREACH_WASMVALUE_CTYPES(CASE_TYPE)
#undef CASE_TYPE
    default:
      UNREACHABLE();
  }
}

const wasm::CanonicalStructType* WasmStruct::GcSafeType(Tagged<Map> map) {
  DCHECK_EQ(WASM_STRUCT_TYPE, map->instance_type());
  Tagged<HeapObject> raw = Cast<HeapObject>(map->constructor_or_back_pointer());
  // The {WasmTypeInfo} might be in the middle of being moved, which is why we
  // can't read its map for a checked cast. But we can rely on its native type
  // pointer being intact in the old location.
  Tagged<WasmTypeInfo> type_info = UncheckedCast<WasmTypeInfo>(raw);
  return wasm::GetTypeCanonicalizer()->LookupStruct(type_info->type_index());
}

DirectHandle<JSObject> WasmStruct::AllocatePrototype(
    Isolate* isolate, DirectHandle<JSPrototype> parent) {
  // Follow the example of {CreateClassPrototype} and create a map with no
  // in-object properties.
  // TODO(ishell): If we support caching the zero-in-object-properties map,
  // update this code.
  DirectHandle<Map> map = Map::Create(isolate, 0);
  map->set_is_prototype_map(true);
  Map::SetPrototype(isolate, map, parent);
  DirectHandle<JSObject> prototype =
      isolate->factory()->NewJSObjectFromMap(map);
  isolate->UpdateProtectorsOnSetPrototype(prototype, parent);
  return prototype;
}

// Allocates a Wasm Struct that is a descriptor for another type, leaving
// its fields uninitialized.
// Descriptor structs have a 1:1 relationship with the internal "RTT" (aka
// v8::internal::Map) of the struct type they are describing, so this RTT
// is allocated along with the descriptor below, and the links between them
// are set up. RTTs with custom descriptors always are subtypes of the
// canonical RTT for the same type, so that canonical RTT is installed as the
// super-RTT of the customized RTT.
// The RTT/map of the descriptor itself is provided by the caller as {map}.
//
// The eventual on-heap object structure will be something like the following,
// where (A) is the object returned by this function, and (B) is allocated
// along with it. There will likely be many instances of (C), and they will be
// allocated (much) later, by one or more {struct.new} instructions that
// take (A) as input and retrieve (B) from it.
// (D) is the {map} passed to this function.
//
//   Wasm struct (C):                    Wasm Descriptor Struct (A):
//   +-----------+                       +-----------+
//   | Map       |------\                | Map (D)   |
//   +-----------+      |                +-----------+
//   | hash      |      |                | hash      |
//   +-----------+      |       /--------| RTT       |
//   | fields... |      v  (B)  v        +-----------+
//   |           |   +-------------+     | fields... |
//   +-----------+   | Meta-map    |     |           |
//                   +-------------+     +-----------+
//                   | ...         |         ^
//                   | Descriptor  |---------/
//                   | ...         |
//                   +-------------+
// static
DirectHandle<WasmStruct> WasmStruct::AllocateDescriptorUninitialized(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> trusted_data,
    wasm::ModuleTypeIndex index, DirectHandle<Map> map,
    DirectHandle<Object> first_field) {
  const wasm::WasmModule* module = trusted_data->module();
  const wasm::TypeDefinition& type = module->type(index);
  DCHECK(type.is_descriptor());
  // TODO(jkummerow): Figure out support for shared objects.
  if (type.is_shared) UNIMPLEMENTED();
  wasm::CanonicalTypeIndex described_index =
      module->canonical_type_id(type.describes);
  DirectHandle<Map> rtt_parent{
      Cast<Map>(trusted_data->managed_object_maps()->get(type.describes.index)),
      isolate};
  DirectHandle<NativeContext> context(
      Cast<NativeContext>(trusted_data->native_context()), isolate);
  // There's always at least one supertype for {rtt_parent}.
  int num_supertypes = module->type(type.describes).subtyping_depth + 1;
  DirectHandle<Map> rtt = CreateStructMap(isolate, described_index, rtt_parent,
                                          num_supertypes, context);

  if (v8_flags.wasm_explicit_prototypes && !IsSmi(*first_field) &&
      IsWasmDescriptorOptions(Cast<HeapObject>(*first_field))) {
    DirectHandle<JSPrototype> prototype = direct_handle(
        Cast<JSReceiver>(
            Cast<WasmDescriptorOptions>(*first_field)->prototype()),
        isolate);
    Map::SetPrototype(isolate, rtt, prototype);
  }

  DirectHandle<WasmStruct> descriptor =
      isolate->factory()->NewWasmStructUninitialized(type.struct_type, map,
                                                     AllocationType::kOld);
  // The struct's body is uninitialized. As soon as we return, callers will
  // take care of that. Until then, no allocations are allowed.
  DisallowGarbageCollection no_gc;
  descriptor->set_described_rtt(*rtt);
  rtt->set_custom_descriptor(*descriptor);
  return descriptor;
}

wasm::WasmValue WasmStruct::GetFieldValue(uint32_t index) {
  const wasm::CanonicalStructType* type =
      wasm::GetTypeCanonicalizer()->LookupStruct(
          map()->wasm_type_info()->type_index());
  wasm::CanonicalValueType field_type = type->field(index);
  int field_offset = WasmStruct::kHeaderSize + type->field_offset(index);
  Address field_address = GetFieldAddress(field_offset);
  switch (field_type.kind()) {
#define CASE_TYPE(valuetype, ctype) \
  case wasm::valuetype:             \
    return wasm::WasmValue(base::ReadUnalignedValue<ctype>(field_address));
    CASE_TYPE(kI8, int8_t)
    CASE_TYPE(kI16, int16_t)
    FOREACH_WASMVALUE_CTYPES(CASE_TYPE)
#undef CASE_TYPE
    case wasm::kF16:
      return wasm::WasmValue(fp16_ieee_to_fp32_value(
          base::ReadUnalignedValue<uint16_t>(field_address)));
    case wasm::kRef:
    case wasm::kRefNull: {
      DirectHandle<Object> ref(TaggedField<Object>::load(*this, field_offset),
                               Isolate::Current());
      return wasm::WasmValue(ref, field_type);
    }
    case wasm::kVoid:
    case wasm::kTop:
    case wasm::kBottom:
      UNREACHABLE();
  }
}

wasm::WasmValue WasmArray::GetElement(uint32_t index) {
  wasm::CanonicalValueType element_type =
      map()->wasm_type_info()->element_type();
  int element_offset =
      WasmArray::kHeaderSize + index * element_type.value_kind_size();
  Address element_address = GetFieldAddress(element_offset);
  switch (element_type.kind()) {
#define CASE_TYPE(value_type, ctype) \
  case wasm::value_type:             \
    return wasm::WasmValue(base::ReadUnalignedValue<ctype>(element_address));
    CASE_TYPE(kI8, int8_t)
    CASE_TYPE(kI16, int16_t)
    FOREACH_WASMVALUE_CTYPES(CASE_TYPE)
#undef CASE_TYPE
    case wasm::kF16:
      return wasm::WasmValue(fp16_ieee_to_fp32_value(
          base::ReadUnalignedValue<uint16_t>(element_address)));
    case wasm::kRef:
    case wasm::kRefNull: {
      DirectHandle<Object> ref(TaggedField<Object>::load(*this, element_offset),
                               Isolate::Current());
      return wasm::WasmValue(ref, element_type);
    }
    case wasm::kVoid:
    case wasm::kTop:
    case wasm::kBottom:
      UNREACHABLE();
  }
}

void WasmArray::SetTaggedElement(uint32_t index, DirectHandle<Object> value,
                                 WriteBarrierMode mode) {
  DCHECK(map()->wasm_type_info()->element_type().is_reference());
  TaggedField<Object>::store(*this, element_offset(index), *value);
  CONDITIONAL_WRITE_BARRIER(*this, element_offset(index), *value, mode);
}

// static
DirectHandle<WasmTagObject> WasmTagObject::New(
    Isolate* isolate, const wasm::FunctionSig* sig,
    wasm::CanonicalTypeIndex type_index, DirectHandle<HeapObject> tag,
    DirectHandle<WasmTrustedInstanceData> trusted_data) {
  DirectHandle<JSFunction> tag_cons(
      isolate->native_context()->wasm_tag_constructor(), isolate);

  // Serialize the signature.
  DCHECK_EQ(0, sig->return_count());
  DCHECK_LE(sig->parameter_count(), std::numeric_limits<int>::max());
  int sig_size = static_cast<int>(sig->parameter_count());
  DirectHandle<PodArray<wasm::ValueType>> serialized_sig =
      PodArray<wasm::ValueType>::New(isolate, sig_size, AllocationType::kOld);
  int index = 0;  // Index into the {PodArray} above.
  for (wasm::ValueType param : sig->parameters()) {
    serialized_sig->set(index++, param);
  }

  DirectHandle<JSObject> tag_object =
      isolate->factory()->NewJSObject(tag_cons, AllocationType::kOld);
  DirectHandle<WasmTagObject> tag_wrapper = Cast<WasmTagObject>(tag_object);
  tag_wrapper->set_serialized_signature(*serialized_sig);
  tag_wrapper->set_canonical_type_index(type_index.index);
  tag_wrapper->set_tag(*tag);
  if (!trusted_data.is_null()) {
    tag_wrapper->set_trusted_data(*trusted_data);
  } else {
    tag_wrapper->clear_trusted_data();
  }

  return tag_wrapper;
}

DirectHandle<WasmDescriptorOptions> WasmDescriptorOptions::New(
    Isolate* isolate, DirectHandle<Object> prototype) {
  DirectHandle<JSFunction> ctor(
      isolate->native_context()->wasm_descriptor_options_constructor(),
      isolate);
  DirectHandle<WasmDescriptorOptions> desc =
      Cast<WasmDescriptorOptions>(isolate->factory()->NewJSObject(ctor));
  desc->set_prototype(*prototype);
  return desc;
}

bool WasmTagObject::MatchesSignature(wasm::CanonicalTypeIndex expected_index) {
  return wasm::CanonicalTypeIndex{static_cast<uint32_t>(
             this->canonical_type_index())} == expected_index;
}

const wasm::CanonicalSig* WasmCapiFunction::sig() const {
  return shared()->wasm_capi_function_data()->sig();
}

#ifdef DEBUG
WasmCodePointer WasmDispatchTableData::WrapperCodePointerForDebugging(
    int index) {
  auto it = wrappers_.find(index);
  CHECK_NE(it, wrappers_.end());
  return it->second->code_pointer();
}
#endif

std::optional<std::shared_ptr<wasm::WasmImportWrapperHandle>>
WasmDispatchTableData::MaybeGetWrapperHandle(int index) const {
  auto it = wrappers_.find(index);
  if (it == wrappers_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void WasmDispatchTableData::Add(
    int index, std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle) {
  auto [wrapper_cache, was_inserted] =
      wrappers_.emplace(index, std::move(wrapper_handle));
  USE(was_inserted);
  DCHECK(was_inserted);
}

void WasmDispatchTableData::Remove(int index) { wrappers_.erase(index); }

void WasmDispatchTable::SetForNonWrapper(
    int index, Tagged<Union<Smi, WasmTrustedInstanceData>> implicit_arg,
    WasmCodePointer call_target, wasm::CanonicalTypeIndex sig_id,
#if V8_ENABLE_DRUMBRAKE
    uint32_t function_index,
#endif  // V8_ENABLE_DRUMBRAKE
    NewOrExistingEntry new_or_existing) {
  if (implicit_arg == Smi::zero()) {
    DCHECK(v8_flags.wasm_jitless ||
           (wasm::kInvalidWasmCodePointer == call_target));
    Clear(index, new_or_existing);
    return;
  }

  SBXCHECK_BOUNDS(index, length());
  DCHECK(IsWasmTrustedInstanceData(implicit_arg));
  DCHECK(sig_id.valid());
  const int offset = OffsetOf(index);
  if (!v8_flags.wasm_jitless) {
    // When overwriting an existing entry, we must decrement the refcount
    // of any overwritten wrappers. When initializing an entry, we must not
    // read uninitialized memory.
    if (new_or_existing == kExistingEntry) {
      offheap_data()->Remove(index);
    }
    WriteField<uint32_t>(offset + kTargetBias, call_target.value());
  } else {
#if V8_ENABLE_DRUMBRAKE
    // Ignore call_target, not used in jitless mode.
    WriteField<int>(offset + kFunctionIndexBias, function_index);
#endif  // V8_ENABLE_DRUMBRAKE
  }
  WriteProtectedPointerField(offset + kImplicitArgBias,
                             Cast<TrustedObject>(implicit_arg));
  CONDITIONAL_WRITE_BARRIER(*this, offset + kImplicitArgBias, implicit_arg,
                            UPDATE_WRITE_BARRIER);
  WriteField<uint32_t>(offset + kSigBias, sig_id.index);
}

void WasmDispatchTable::SetForWrapper(
    int index, Tagged<WasmImportData> implicit_arg,
    std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle,
    wasm::CanonicalTypeIndex sig_id,
#if V8_ENABLE_DRUMBRAKE
    uint32_t function_index,
#endif  // V8_ENABLE_DRUMBRAKE
    NewOrExistingEntry new_or_existing) {
  DCHECK_NE(implicit_arg, Smi::zero());
  SBXCHECK(v8_flags.wasm_jitless || !wrapper_handle->has_code() ||
           !wrapper_handle->code()->is_dying());
  SBXCHECK_BOUNDS(index, length());
  DCHECK(sig_id.valid());
  const int offset = OffsetOf(index);
  WriteProtectedPointerField(offset + kImplicitArgBias,
                             Cast<TrustedObject>(implicit_arg));
  CONDITIONAL_WRITE_BARRIER(*this, offset + kImplicitArgBias, implicit_arg,
                            UPDATE_WRITE_BARRIER);
  if (!v8_flags.wasm_jitless) {
    // When overwriting an existing entry, we must decrement the refcount
    // of any overwritten wrappers. When initializing an entry, we must not
    // read uninitialized memory.
    if (new_or_existing == kExistingEntry) {
      offheap_data()->Remove(index);
    }
    WriteField<uint32_t>(offset + kTargetBias,
                         wrapper_handle->code_pointer().value());
    offheap_data()->Add(index, std::move(wrapper_handle));
  } else {
#if V8_ENABLE_DRUMBRAKE
    // Ignore call_target, not used in jitless mode.
    WriteField<int>(offset + kFunctionIndexBias, function_index);
#endif  // V8_ENABLE_DRUMBRAKE
  }

  WriteField<uint32_t>(offset + kSigBias, sig_id.index);
}

void WasmDispatchTable::Clear(int index, NewOrExistingEntry new_or_existing) {
  SBXCHECK_BOUNDS(index, length());
  const int offset = OffsetOf(index);
  // When clearing an existing entry, we must update the refcount of any
  // wrappers. When clear-initializing new entries, we must not read
  // uninitialized memory.
  if (new_or_existing == kExistingEntry) {
    offheap_data()->Remove(index);
  }
  ClearProtectedPointerField(offset + kImplicitArgBias);
  WriteField<uint32_t>(offset + kTargetBias,
                       wasm::kInvalidWasmCodePointer.value());
  WriteField<int>(offset + kSigBias, -1);
}

std::optional<std::shared_ptr<wasm::WasmImportWrapperHandle>>
WasmDispatchTable::MaybeGetWrapperHandle(int index) const {
  return offheap_data()->MaybeGetWrapperHandle(index);
}

// static
void WasmDispatchTable::AddUse(Isolate* isolate,
                               DirectHandle<WasmDispatchTable> dispatch_table,
                               DirectHandle<WasmTrustedInstanceData> instance,
                               int table_index) {
  Tagged<ProtectedWeakFixedArray> uses =
      MaybeGrowUsesList(isolate, dispatch_table);
  int cursor = GetUsedLength(uses);
  // {MaybeGrowUsesList} ensures that we have enough capacity.
  SetEntry(uses, cursor, *instance, table_index);
  SetUsedLength(uses, cursor + 2);
}

// static
Tagged<ProtectedWeakFixedArray> WasmDispatchTable::MaybeGrowUsesList(
    Isolate* isolate, DirectHandle<WasmDispatchTable> dispatch_table) {
  Tagged<ProtectedWeakFixedArray> uses = dispatch_table->protected_uses();
  int capacity = uses->length();
  if (capacity == 0) {
    constexpr int kInitialLength = 3;  // 1 slot + 1 pair.
    DirectHandle<ProtectedWeakFixedArray> new_uses =
        isolate->factory()->NewProtectedWeakFixedArray(kInitialLength);
    SetUsedLength(*new_uses, kReservedSlotOffset);
    dispatch_table->set_protected_uses(*new_uses);
    return *new_uses;
  }
  DCHECK_GT(uses->length(), 0);
  int used_length = GetUsedLength(uses);
  if (used_length < capacity) return uses;
  // Try to compact, grow if that doesn't free up enough space.
  int cleared_entries = 0;
  int write_cursor = kReservedSlotOffset;
  for (int i = kReservedSlotOffset; i < capacity; i += 2) {
    DCHECK(uses->get(i).IsWeakOrCleared());
    if (uses->get(i).IsCleared()) {
      cleared_entries++;
      continue;
    }
    if (write_cursor != i) {
      CopyEntry(uses, write_cursor, uses, i);
    }
    write_cursor += 2;
  }
  // We need at least one free entry. We want at least half the array to be
  // empty; each entry needs two slots.
  int min_free_entries = 1 + (capacity >> 2);
  if (cleared_entries >= min_free_entries) {
    SetUsedLength(uses, write_cursor);
    return uses;
  }
  // Grow by 50%, at least one entry.
  DirectHandle<ProtectedWeakFixedArray> uses_handle(uses, isolate);
  uses = {};
  int old_entries = capacity >> 1;  // Two slots per entry.
  int new_entries = std::max(old_entries + 1, old_entries + (old_entries >> 1));
  int new_capacity = new_entries * 2 + kReservedSlotOffset;
  DirectHandle<ProtectedWeakFixedArray> new_uses =
      isolate->factory()->NewProtectedWeakFixedArray(new_capacity);
  // The allocation could have triggered GC, freeing more entries.
  // The previous compaction's {write_cursor} is the new upper bound on
  // existing entries.
  used_length = write_cursor;
  DisallowGarbageCollection no_gc;
  uses = *uses_handle;
  write_cursor = kReservedSlotOffset;
  for (int i = kReservedSlotOffset; i < used_length; i += 2) {
    if (uses->get(i).IsCleared()) continue;
    CopyEntry(*new_uses, write_cursor, uses, i);
    write_cursor += 2;
  }
  SetUsedLength(*new_uses, write_cursor);
  dispatch_table->set_protected_uses(*new_uses);
  return *new_uses;
}

// static
DirectHandle<WasmDispatchTable> WasmDispatchTable::New(
    Isolate* isolate, int length, wasm::CanonicalValueType table_type,
    bool shared) {
  return isolate->factory()->NewWasmDispatchTable(length, table_type, shared);
}

// static
DirectHandle<WasmDispatchTable> WasmDispatchTable::Grow(
    Isolate* isolate, DirectHandle<WasmDispatchTable> old_table,
    uint32_t new_length) {
  uint32_t old_length = old_table->length();
  // This method should only be called if we actually grow. For sandbox
  // purposes we also want to ensure tables can never shrink below their
  // static minimum size.
  SBXCHECK_LT(old_length, new_length);

  uint32_t old_capacity = old_table->capacity();
  // Catch possible corruption. {new_length} is computed from untrusted data.
  SBXCHECK_LE(new_length, wasm::max_table_size());
  // {old_length} and {old_capacity} are read from trusted space, so we trust
  // them. The DCHECKs give fuzzers a chance to catch potential bugs.
  DCHECK_LE(old_length, wasm::max_table_size());
  DCHECK_LE(old_capacity, wasm::max_table_size());

  if (new_length < old_capacity) {
    RELEASE_WRITE_INT32_FIELD(*old_table, kLengthOffset, new_length);
    // All fields within the old capacity are already cleared (see below).
    return old_table;
  }

  // Grow table exponentially to guarantee amortized constant allocation and gc
  // time.
  uint32_t limit =
      std::min<uint32_t>(WasmDispatchTable::kMaxLength, wasm::max_table_size());
  uint32_t max_grow = limit - old_capacity;
  uint32_t min_grow = new_length - old_capacity;
  CHECK_LE(min_grow, max_grow);
  // Grow by old capacity, and at least by 8. Clamp to min_grow and max_grow.
  uint32_t exponential_grow = std::max(old_capacity, 8u);
  uint32_t grow = std::clamp(exponential_grow, min_grow, max_grow);
  uint32_t new_capacity = old_capacity + grow;
  DCHECK_LE(new_capacity, limit);
  DirectHandle<WasmDispatchTable> new_table =
      WasmDispatchTable::New(isolate, new_capacity, old_table->table_type(),
                             HeapLayout::InAnySharedSpace(*old_table));

  DisallowGarbageCollection no_gc;
  // Writing non-atomically is fine here because this is a freshly allocated
  // object.
  new_table->WriteField<int>(kLengthOffset, new_length);
  for (uint32_t i = 0; i < old_length; ++i) {
    WasmCodePointer call_target = old_table->target(i);
    // Update any stored call origins, so that future compiled wrappers
    // get installed into the new dispatch table.
    Tagged<Object> implicit_arg = old_table->implicit_arg(i);
    if (IsWasmImportData(implicit_arg)) {
      Tagged<WasmImportData> import_data = Cast<WasmImportData>(implicit_arg);
      // After installing a compiled wrapper, we don't set or update
      // call origins any more.
      if (import_data->has_call_origin()) {
        if (import_data->call_origin() == *old_table) {
          import_data->set_call_origin(*new_table);
        } else {
          DCHECK(v8_flags.wasm_jitless ||
                 wasm::GetWasmImportWrapperCache()->IsCompiledWrapper(
                     call_target));
        }
      }
    }

    if (implicit_arg == Smi::zero()) {
      new_table->Clear(i, kNewEntry);
      continue;
    }

    const int offset = OffsetOf(i);
    if (!v8_flags.wasm_jitless) {
      new_table->WriteField<uint32_t>(offset + kTargetBias,
                                      call_target.value());
    } else {
#if V8_ENABLE_DRUMBRAKE
      // Ignore call_target, not used in jitless mode.
      new_table->WriteField<int>(offset + kFunctionIndexBias,
                                 old_table->function_index(i));
#endif  // V8_ENABLE_DRUMBRAKE
    }
    new_table->WriteProtectedPointerField(offset + kImplicitArgBias,
                                          Cast<TrustedObject>(implicit_arg));
    CONDITIONAL_WRITE_BARRIER(*new_table, offset + kImplicitArgBias,
                              implicit_arg, UPDATE_WRITE_BARRIER);
    new_table->WriteField<uint32_t>(offset + kSigBias, old_table->sig(i).index);
  }

  new_table->offheap_data()->wrappers_ =
      std::move(old_table->offheap_data()->wrappers_);

  // Update users.
  Tagged<ProtectedWeakFixedArray> uses = old_table->protected_uses();
  new_table->set_protected_uses(uses);
  int used_length = GetUsedLength(uses);
  for (int i = kReservedSlotOffset; i < used_length; i += 2) {
    if (uses->get(i).IsCleared()) continue;
    Tagged<WasmTrustedInstanceData> instance = GetInstance(uses, i);
    int table_index = GetTableIndex(uses, i);
    DCHECK_EQ(instance->dispatch_tables()->get(table_index), *old_table);
    instance->dispatch_tables()->set(table_index, *new_table);
    if (table_index == 0) {
      DCHECK_EQ(instance->dispatch_table0(), *old_table);
      instance->set_dispatch_table0(*new_table);
    }
  }
  return new_table;
}

bool WasmCapiFunction::MatchesSignature(
    wasm::CanonicalTypeIndex other_canonical_sig_index) const {
#if DEBUG
  // TODO(14034): Change this if indexed types are allowed.
  for (wasm::CanonicalValueType type : this->sig()->all()) {
    CHECK(!type.has_index());
  }
#endif
  // TODO(14034): Check for subtyping instead if C API functions can define
  // signature supertype.
  return shared()->wasm_capi_function_data()->sig_index() ==
         other_canonical_sig_index;
}

// static
DirectHandle<WasmExceptionPackage> WasmExceptionPackage::New(
    Isolate* isolate, DirectHandle<WasmExceptionTag> exception_tag, int size) {
  DirectHandle<FixedArray> values = isolate->factory()->NewFixedArray(size);
  return New(isolate, exception_tag, values);
}

DirectHandle<WasmExceptionPackage> WasmExceptionPackage::New(
    Isolate* isolate, DirectHandle<WasmExceptionTag> exception_tag,
    DirectHandle<FixedArray> values) {
  DirectHandle<JSFunction> exception_cons(
      isolate->native_context()->wasm_exception_constructor(), isolate);
  DirectHandle<JSObject> exception =
      isolate->factory()->NewJSObject(exception_cons);
  exception->InObjectPropertyAtPut(kTagIndex, *exception_tag);
  exception->InObjectPropertyAtPut(kValuesIndex, *values);
  return Cast<WasmExceptionPackage>(exception);
}

// static
DirectHandle<Object> WasmExceptionPackage::GetExceptionTag(
    Isolate* isolate, DirectHandle<WasmExceptionPackage> exception_package) {
  DirectHandle<Object> tag;
  if (JSReceiver::GetProperty(isolate, exception_package,
                              isolate->factory()->wasm_exception_tag_symbol())
          .ToHandle(&tag)) {
    return tag;
  }
  return isolate->factory()->undefined_value();
}

// static
DirectHandle<Object> WasmExceptionPackage::GetExceptionValues(
    Isolate* isolate, DirectHandle<WasmExceptionPackage> exception_package) {
  DirectHandle<Object> values;
  if (JSReceiver::GetProperty(
          isolate, exception_package,
          isolate->factory()->wasm_exception_values_symbol())
          .ToHandle(&values)) {
    DCHECK_IMPLIES(!IsUndefined(*values), IsFixedArray(*values));
    return values;
  }
  return isolate->factory()->undefined_value();
}

void EncodeI32ExceptionValue(DirectHandle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint32_t value) {
  encoded_values->set((*encoded_index)++, Smi::FromInt(value >> 16));
  encoded_values->set((*encoded_index)++, Smi::FromInt(value & 0xffff));
}

void EncodeI64ExceptionValue(DirectHandle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint64_t value) {
  EncodeI32ExceptionValue(encoded_values, encoded_index,
                          static_cast<uint32_t>(value >> 32));
  EncodeI32ExceptionValue(encoded_values, encoded_index,
                          static_cast<uint32_t>(value));
}

void DecodeI32ExceptionValue(DirectHandle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint32_t* value) {
  uint32_t msb = Cast<Smi>(encoded_values->get((*encoded_index)++)).value();
  uint32_t lsb = Cast<Smi>(encoded_values->get((*encoded_index)++)).value();
  *value = (msb << 16) | (lsb & 0xffff);
}

void DecodeI64ExceptionValue(DirectHandle<FixedArray> encoded_values,
                             uint32_t* encoded_index, uint64_t* value) {
  uint32_t lsb = 0, msb = 0;
  DecodeI32ExceptionValue(encoded_values, encoded_index, &msb);
  DecodeI32ExceptionValue(encoded_values, encoded_index, &lsb);
  *value = (static_cast<uint64_t>(msb) << 32) | static_cast<uint64_t>(lsb);
}

bool UseGenericWasmToJSWrapper(wasm::ImportCallKind kind,
                               const wasm::CanonicalSig* sig,
                               wasm::Suspend suspend) {
  if (kind != wasm::ImportCallKind::kJSFunction) {
    return false;
  }
  DCHECK(wasm::IsJSCompatibleSignature(sig));
#if !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_ARM64 && !V8_TARGET_ARCH_ARM && \
    !V8_TARGET_ARCH_IA32 && !V8_TARGET_ARCH_RISCV64 &&                     \
    !V8_TARGET_ARCH_RISCV32 && !V8_TARGET_ARCH_PPC64 &&                    \
    !V8_TARGET_ARCH_S390X && !V8_TARGET_ARCH_LOONG64 && !V8_TARGET_ARCH_MIPS64
  return false;
#else
  if (suspend != wasm::Suspend::kNoSuspend) return false;

  return v8_flags.wasm_generic_wrapper;
#endif
}

#ifdef DEBUG

namespace {

constexpr uint32_t kBytesPerExceptionValuesArrayElement = 2;

size_t ComputeEncodedElementSize(wasm::ValueType type) {
  size_t byte_size = type.value_kind_size();
  DCHECK_EQ(byte_size % kBytesPerExceptionValuesArrayElement, 0);
  DCHECK_LE(1, byte_size / kBytesPerExceptionValuesArrayElement);
  return byte_size / kBytesPerExceptionValuesArrayElement;
}

}  // namespace

#endif  // DEBUG

// static
uint32_t WasmExceptionPackage::GetEncodedSize(const wasm::WasmTag* tag) {
  return GetEncodedSize(tag->sig);
}

// static
uint32_t WasmExceptionPackage::GetEncodedSize(const wasm::WasmTagSig* sig) {
  uint32_t encoded_size = 0;
  for (size_t i = 0; i < sig->parameter_count(); ++i) {
    switch (sig->GetParam(i).kind()) {
      case wasm::kI32:
      case wasm::kF32:
        DCHECK_EQ(2, ComputeEncodedElementSize(sig->GetParam(i)));
        encoded_size += 2;
        break;
      case wasm::kI64:
      case wasm::kF64:
        DCHECK_EQ(4, ComputeEncodedElementSize(sig->GetParam(i)));
        encoded_size += 4;
        break;
      case wasm::kS128:
        DCHECK_EQ(8, ComputeEncodedElementSize(sig->GetParam(i)));
        encoded_size += 8;
        break;
      case wasm::kRef:
      case wasm::kRefNull:
        encoded_size += 1;
        break;
      case wasm::kVoid:
      case wasm::kTop:
      case wasm::kBottom:
      case wasm::kI8:
      case wasm::kI16:
      case wasm::kF16:
        UNREACHABLE();
    }
  }
  return encoded_size;
}

bool WasmExportedFunction::IsWasmExportedFunction(Tagged<Object> object) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> js_function = Cast<JSFunction>(object);
  Tagged<Code> code = js_function->code(GetCurrentIsolateForSandbox());
  if (CodeKind::JS_TO_WASM_FUNCTION != code->kind() &&
#if V8_ENABLE_DRUMBRAKE
      code->builtin_id() != Builtin::kGenericJSToWasmInterpreterWrapper &&
#endif  // V8_ENABLE_DRUMBRAKE
      code->builtin_id() != Builtin::kJSToWasmWrapper &&
      code->builtin_id() != Builtin::kWasmPromising &&
      code->builtin_id() != Builtin::kWasmStressSwitch) {
    return false;
  }
  DCHECK(js_function->shared()->HasWasmExportedFunctionData(
      GetCurrentIsolateForSandbox()));
  return true;
}

bool WasmCapiFunction::IsWasmCapiFunction(Tagged<Object> object) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> js_function = Cast<JSFunction>(object);
  // TODO(jkummerow): Enable this when there is a JavaScript wrapper
  // able to call this function.
  // if (js_function->code()->kind() != CodeKind::WASM_TO_CAPI_FUNCTION) {
  //   return false;
  // }
  // DCHECK(js_function->shared()->HasWasmCapiFunctionData(
  //        GetCurrentIsolateForSandbox()));
  // return true;
  return js_function->shared()->HasWasmCapiFunctionData(
      GetCurrentIsolateForSandbox());
}

DirectHandle<WasmCapiFunction> WasmCapiFunction::New(
    Isolate* isolate, Address call_target, DirectHandle<Foreign> embedder_data,
    wasm::CanonicalTypeIndex sig_index, const wasm::CanonicalSig* sig) {
  // TODO(jkummerow): Install a JavaScript wrapper. For now, calling
  // these functions directly is unsupported; they can only be called
  // from Wasm code.

  // To support simulator builds, we potentially have to redirect the
  // call target (which is an address pointing into the C++ binary).
  call_target = ExternalReference::Create(call_target).address();

  DirectHandle<Map> rtt = isolate->factory()->wasm_func_ref_map();
  DirectHandle<WasmCapiFunctionData> fun_data =
      isolate->factory()->NewWasmCapiFunctionData(
          call_target, embedder_data, BUILTIN_CODE(isolate, Illegal), rtt,
          sig_index, sig);
  DirectHandle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfoForWasmCapiFunction(fun_data);
  DirectHandle<JSFunction> result =
      Factory::JSFunctionBuilder{isolate, shared, isolate->native_context()}
          .Build();
  fun_data->internal()->set_external(*result);
  return Cast<WasmCapiFunction>(result);
}

DirectHandle<WasmExportedFunction> WasmExportedFunction::New(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> instance_data,
    DirectHandle<WasmFuncRef> func_ref,
    DirectHandle<WasmInternalFunction> internal_function, int arity,
    DirectHandle<Code> export_wrapper) {
  DCHECK(CodeKind::JS_TO_WASM_FUNCTION == export_wrapper->kind() ||
         (export_wrapper->is_builtin() &&
          (export_wrapper->builtin_id() == Builtin::kJSToWasmWrapper ||
#if V8_ENABLE_DRUMBRAKE
           export_wrapper->builtin_id() ==
               Builtin::kGenericJSToWasmInterpreterWrapper ||
#endif  // V8_ENABLE_DRUMBRAKE
           export_wrapper->builtin_id() == Builtin::kWasmPromising ||
           export_wrapper->builtin_id() == Builtin::kWasmStressSwitch)));
  int func_index = internal_function->function_index();
  wasm::Promise promise =
      export_wrapper->builtin_id() == Builtin::kWasmPromising
          ? wasm::kPromise
          : wasm::kNoPromise;
  const wasm::WasmModule* module = instance_data->module();
  wasm::CanonicalTypeIndex sig_id =
      module->canonical_sig_id(module->functions[func_index].sig_index);
  const wasm::CanonicalSig* sig =
      wasm::GetTypeCanonicalizer()->LookupFunctionSignature(sig_id);
  return New(isolate, instance_data, func_ref, internal_function, arity,
             export_wrapper, module, func_index, sig_id, sig, promise);
}

DirectHandle<WasmExportedFunction> WasmExportedFunction::New(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> instance_data,
    DirectHandle<WasmFuncRef> func_ref,
    DirectHandle<WasmInternalFunction> internal_function, int arity,
    DirectHandle<Code> export_wrapper, const wasm::WasmModule* module,
    int func_index, wasm::CanonicalTypeIndex sig_id,
    const wasm::CanonicalSig* sig, wasm::Promise promise) {
  Factory* factory = isolate->factory();
  DirectHandle<WasmExportedFunctionData> function_data =
      factory->NewWasmExportedFunctionData(
          export_wrapper, instance_data, func_ref, internal_function, sig,
          sig_id, v8_flags.wasm_wrapper_tiering_budget, promise);

#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    const wasm::FunctionSig* function_sig =
        reinterpret_cast<const wasm::FunctionSig*>(sig);
    uint32_t aligned_size =
        wasm::WasmBytecode::JSToWasmWrapperPackedArraySize(function_sig);
    bool hasRefArgs = wasm::WasmBytecode::RefArgsCount(function_sig) > 0;
    bool hasRefRets = wasm::WasmBytecode::RefRetsCount(function_sig) > 0;
    function_data->set_packed_args_size(
        wasm::WasmInterpreterRuntime::PackedArgsSizeField::encode(
            aligned_size) |
        wasm::WasmInterpreterRuntime::HasRefArgsField::encode(hasRefArgs) |
        wasm::WasmInterpreterRuntime::HasRefRetsField::encode(hasRefRets));
  }
#endif  // V8_ENABLE_DRUMBRAKE

  DirectHandle<SharedFunctionInfo> shared;
  DirectHandle<Map> function_map;
  bool is_asm_js_module = is_asmjs_module(module);
  if (is_asm_js_module) {
    // We can use the function name only for asm.js. For WebAssembly, the
    // function name is specified as the function_index.toString().
    DirectHandle<String> name;
    if (!WasmModuleObject::GetFunctionNameOrNull(
             isolate, direct_handle(instance_data->module_object(), isolate),
             func_index)
             .ToHandle(&name)) {
      name = factory->SizeToString(func_index);
    }
    shared = factory->NewSharedFunctionInfoForWasmExportedFunction(
        name, function_data, arity, kAdapt);
    if (module->origin == wasm::kAsmJsSloppyOrigin) {
      function_map = isolate->sloppy_function_map();
    } else {
      function_map = isolate->strict_function_map();
      shared->set_language_mode(LanguageMode::kStrict);
    }
  } else {
    DirectHandle<String> name = factory->SizeToString(func_index);
    shared = factory->NewSharedFunctionInfoForWasmExportedFunction(
        name, function_data, arity, kAdapt);
    function_map = isolate->wasm_exported_function_map();
  }

  DirectHandle<NativeContext> context(isolate->native_context());
  DirectHandle<JSFunction> js_function =
      Factory::JSFunctionBuilder{isolate, shared, context}
          .set_map(function_map)
          .Build();

  // According to the spec, exported functions should not have a [[Construct]]
  // method. This does not apply to functions exported from asm.js however.
  DCHECK_EQ(is_asm_js_module, IsConstructor(*js_function));
  if (instance_data->has_instance_object()) {
    shared->set_script(instance_data->module_object()->script(), kReleaseStore);
  } else {
    shared->set_script(*isolate->factory()->undefined_value(), kReleaseStore);
  }
  function_data->internal()->set_external(*js_function);
  return Cast<WasmExportedFunction>(js_function);
}

bool WasmExportedFunctionData::MatchesSignature(
    wasm::CanonicalTypeIndex other_canonical_type_index) {
  return wasm::GetTypeCanonicalizer()->IsCanonicalSubtype(
      sig_index(), other_canonical_type_index);
}

// static
std::unique_ptr<char[]> WasmExportedFunction::GetDebugName(
    const wasm::CanonicalSig* sig) {
  constexpr const char kPrefix[] = "js-to-wasm:";
  // prefix + parameters + delimiter + returns + zero byte
  size_t len = strlen(kPrefix) + sig->all().size() + 2;
  auto buffer = base::OwnedVector<char>::New(len);
  memcpy(buffer.begin(), kPrefix, strlen(kPrefix));
  PrintSignature(buffer.as_vector() + strlen(kPrefix), sig);
  return buffer.ReleaseData();
}

// static
bool WasmJSFunction::IsWasmJSFunction(Tagged<Object> object) {
  if (!IsJSFunction(object)) return false;
  Tagged<JSFunction> js_function = Cast<JSFunction>(object);
  return js_function->shared()->HasWasmJSFunctionData(
      GetCurrentIsolateForSandbox());
}

DirectHandle<Map> CreateStructMap(
    Isolate* isolate, wasm::CanonicalTypeIndex struct_index,
    DirectHandle<Map> opt_rtt_parent, int num_supertypes,
    DirectHandle<NativeContext> opt_native_context) {
  const wasm::CanonicalStructType* type =
      wasm::GetTypeCanonicalizer()->LookupStruct(struct_index);
  const bool shared = wasm::GetTypeCanonicalizer()->IsShared(struct_index);
  const int inobject_properties = 0;
  // We have to use the variable size sentinel because the instance size
  // stored directly in a Map is capped at 255 pointer sizes.
  const int map_instance_size = kVariableSizeSentinel;
  const InstanceType instance_type = WASM_STRUCT_TYPE;
  // TODO(jkummerow): If NO_ELEMENTS were supported, we could use that here.
  const ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND;
  const wasm::CanonicalValueType no_array_element = wasm::kWasmBottom;
  // If we had a CanonicalHeapType, we could use that here.
  wasm::CanonicalValueType heaptype = wasm::CanonicalValueType::Ref(
      struct_index, shared, wasm::RefTypeKind::kStruct);
  DirectHandle<WasmTypeInfo> type_info = isolate->factory()->NewWasmTypeInfo(
      heaptype, no_array_element, opt_rtt_parent, num_supertypes, shared);
  DirectHandle<Map> map;
  // TODO(manoskouk): Combine `shared` with contextful maps.
  if (shared) {
    map = isolate->factory()->NewMapWithMetaMap(
        isolate->factory()->meta_map(), instance_type, map_instance_size,
        elements_kind, inobject_properties, AllocationType::kSharedMap);
  } else if (opt_native_context.is_null()) {
    map = isolate->factory()->NewContextlessMap(
        instance_type, map_instance_size, elements_kind, inobject_properties);
  } else {
    map = isolate->factory()->NewContextfulMap(
        opt_native_context, instance_type, map_instance_size, elements_kind,
        inobject_properties);
  }
  map->set_wasm_type_info(*type_info);
  map->set_is_extensible(false);
  const int real_instance_size = WasmStruct::Size(type);
  WasmStruct::EncodeInstanceSizeInMap(real_instance_size, *map);
  return map;
}

DirectHandle<Map> CreateArrayMap(Isolate* isolate,
                                 wasm::CanonicalTypeIndex array_index,
                                 DirectHandle<Map> opt_rtt_parent,
                                 int num_supertypes) {
  const wasm::CanonicalArrayType* type =
      wasm::GetTypeCanonicalizer()->LookupArray(array_index);
  wasm::CanonicalValueType element_type = type->element_type();
  const bool shared = wasm::GetTypeCanonicalizer()->IsShared(array_index);
  const int inobject_properties = 0;
  const int map_instance_size = kVariableSizeSentinel;
  const InstanceType instance_type = WASM_ARRAY_TYPE;
  const ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND;
  wasm::CanonicalValueType heaptype = wasm::CanonicalValueType::Ref(
      array_index, shared, wasm::RefTypeKind::kArray);
  DirectHandle<WasmTypeInfo> type_info = isolate->factory()->NewWasmTypeInfo(
      heaptype, element_type, opt_rtt_parent, num_supertypes, shared);

  DirectHandle<Map> map =
      shared ? isolate->factory()->NewMapWithMetaMap(
                   isolate->factory()->meta_map(), instance_type,
                   map_instance_size, elements_kind, inobject_properties,
                   AllocationType::kSharedMap)
             : isolate->factory()->NewContextlessMap(
                   instance_type, map_instance_size, elements_kind,
                   inobject_properties);
  map->set_wasm_type_info(*type_info);
  map->SetInstanceDescriptors(isolate,
                              *isolate->factory()->empty_descriptor_array(), 0,
                              SKIP_WRITE_BARRIER);
  map->set_is_extensible(false);
  WasmArray::EncodeElementSizeInMap(element_type.value_kind_size(), *map);
  return map;
}

DirectHandle<Map> CreateFuncRefMap(Isolate* isolate,
                                   wasm::CanonicalTypeIndex type,
                                   DirectHandle<Map> opt_rtt_parent,
                                   int num_supertypes, bool shared) {
  const int inobject_properties = 0;
  const InstanceType instance_type = WASM_FUNC_REF_TYPE;
  const ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND;
  const wasm::CanonicalValueType no_array_element = wasm::kWasmBottom;
  wasm::CanonicalValueType heaptype =
      wasm::CanonicalValueType::Ref(type, shared, wasm::RefTypeKind::kFunction);
  DirectHandle<WasmTypeInfo> type_info = isolate->factory()->NewWasmTypeInfo(
      heaptype, no_array_element, opt_rtt_parent, num_supertypes, shared);
  constexpr int kInstanceSize = WasmFuncRef::kSize;
  DCHECK_EQ(
      kInstanceSize,
      Cast<Map>(isolate->root(RootIndex::kWasmFuncRefMap))->instance_size());
  DirectHandle<Map> map = isolate->factory()->NewContextlessMap(
      instance_type, kInstanceSize, elements_kind, inobject_properties,
      shared ? AllocationType::kSharedMap : AllocationType::kMap);
  map->set_wasm_type_info(*type_info);
  return map;
}

DirectHandle<Map> CreateContRefMap(Isolate* isolate,
                                   wasm::CanonicalTypeIndex type) {
  const int inobject_properties = 0;
  const InstanceType instance_type = WASM_CONTINUATION_OBJECT_TYPE;
  const ElementsKind elements_kind = TERMINAL_FAST_ELEMENTS_KIND;
  const wasm::CanonicalValueType no_array_element = wasm::kWasmBottom;
  wasm::CanonicalValueType heaptype =
      wasm::CanonicalValueType::Ref(type, false, wasm::RefTypeKind::kCont);
  DirectHandle<WasmTypeInfo> type_info = isolate->factory()->NewWasmTypeInfo(
      heaptype, no_array_element, {}, 0, false);
  constexpr int kInstanceSize = WasmContinuationObject::kSize;
  DCHECK_EQ(kInstanceSize,
            Cast<Map>(isolate->root(RootIndex::kWasmContinuationObjectMap))
                ->instance_size());
  DirectHandle<Map> map = isolate->factory()->NewContextlessMap(
      instance_type, kInstanceSize, elements_kind, inobject_properties,
      AllocationType::kMap);
  map->set_wasm_type_info(*type_info);
  return map;
}

DirectHandle<WasmJSFunction> WasmJSFunction::New(
    Isolate* isolate, const wasm::FunctionSig* sig,
    DirectHandle<JSReceiver> callable, wasm::Suspend suspend) {
  DCHECK_LE(sig->all().size(), kMaxInt);
  int parameter_count = static_cast<int>(sig->parameter_count());
  Factory* factory = isolate->factory();

  DirectHandle<Map> rtt;
  DirectHandle<NativeContext> context(isolate->native_context());

  static_assert(wasm::kMaxCanonicalTypes <= kMaxInt);
  // TODO(clemensb): Merge the next two lines into a single call.
  wasm::CanonicalTypeIndex sig_id =
      wasm::GetTypeCanonicalizer()->AddRecursiveGroup(sig);
  const wasm::CanonicalSig* canonical_sig =
      wasm::GetTypeCanonicalizer()->LookupFunctionSignature(sig_id);

  wasm::TypeCanonicalizer::PrepareForCanonicalTypeId(isolate, sig_id);

  DirectHandle<WeakFixedArray> canonical_rtts(
      isolate->heap()->wasm_canonical_rtts(), isolate);

  Tagged<MaybeObject> maybe_canonical_map = canonical_rtts->get(sig_id.index);

  if (!maybe_canonical_map.IsCleared()) {
    rtt = direct_handle(
        Cast<Map>(maybe_canonical_map.GetHeapObjectAssumeWeak()), isolate);
  } else {
    rtt = CreateFuncRefMap(isolate, sig_id, DirectHandle<Map>(), 0, false);
    canonical_rtts->set(sig_id.index, MakeWeak(*rtt));
  }

  int expected_arity = parameter_count;
  wasm::ImportCallKind kind;
  if (IsJSFunction(*callable)) {
    Tagged<SharedFunctionInfo> shared = Cast<JSFunction>(callable)->shared();
    if (shared->HasWasmFunctionData(isolate)) {
      kind = wasm::ImportCallKind::kUseCallBuiltin;
    } else {
      expected_arity =
          shared->internal_formal_parameter_count_without_receiver();
      kind = wasm::ImportCallKind::kJSFunction;
    }
  } else {
    kind = wasm::ImportCallKind::kUseCallBuiltin;
  }

  wasm::WasmImportWrapperCache* cache = wasm::GetWasmImportWrapperCache();
  // Initialize the import wrapper cache if that hasn't happened yet.
  cache->LazyInitialize(isolate);
  std::shared_ptr<wasm::WasmImportWrapperHandle> wrapper_handle =
      cache->Get(isolate, kind, sig_id, expected_arity, suspend, canonical_sig);

  bool should_clear_call_origin = wrapper_handle->has_code();

  DirectHandle<Code> js_to_js_wrapper_code =
      wasm::IsJSCompatibleSignature(canonical_sig)
          ? isolate->builtins()->code_handle(Builtin::kJSToJSWrapper)
          : isolate->builtins()->code_handle(Builtin::kJSToJSWrapperInvalidSig);

  DirectHandle<WasmJSFunctionData> function_data =
      factory->NewWasmJSFunctionData(sig_id, callable, js_to_js_wrapper_code,
                                     rtt, suspend, wasm::kNoPromise,
                                     wrapper_handle);
  DirectHandle<WasmInternalFunction> internal_function{
      function_data->internal(), isolate};

  // Some later DCHECKs assume that we don't have a {call_origin} when
  // the function already uses a compiled wrapper.
  if (should_clear_call_origin) {
    Cast<WasmImportData>(internal_function->implicit_arg())
        ->clear_call_origin();
  }

  DirectHandle<String> name = factory->Function_string();
  if (IsJSFunction(*callable)) {
    name = JSFunction::GetDebugName(isolate, Cast<JSFunction>(callable));
    name = String::Flatten(isolate, name);
  }
  DirectHandle<SharedFunctionInfo> shared =
      factory->NewSharedFunctionInfoForWasmJSFunction(name, function_data);
  shared->set_internal_formal_parameter_count(
      JSParameterCount(parameter_count));
  DirectHandle<JSFunction> js_function =
      Factory::JSFunctionBuilder{isolate, shared, context}
          .set_map(isolate->wasm_exported_function_map())
          .Build();
  internal_function->set_external(*js_function);
  return Cast<WasmJSFunction>(js_function);
}

Tagged<JSReceiver> WasmJSFunctionData::GetCallable() const {
  return Cast<JSReceiver>(
      Cast<WasmImportData>(internal()->implicit_arg())->callable());
}

wasm::Suspend WasmJSFunctionData::GetSuspend() const {
  return Cast<WasmImportData>(internal()->implicit_arg())->suspend();
}

const wasm::CanonicalSig* WasmJSFunctionData::GetSignature() const {
  return wasm::GetWasmEngine()->type_canonicalizer()->LookupFunctionSignature(
      sig_index());
}

bool WasmJSFunctionData::MatchesSignature(
    wasm::CanonicalTypeIndex other_canonical_sig_index) const {
#if DEBUG
  // TODO(14034): Change this if indexed types are allowed.
  const wasm::CanonicalSig* sig = GetSignature();
  for (wasm::CanonicalValueType type : sig->all()) CHECK(!type.has_index());
#endif
  // TODO(14034): Check for subtyping instead if WebAssembly.Function can define
  // signature supertype.
  return sig_index() == other_canonical_sig_index;
}

bool WasmExternalFunction::IsWasmExternalFunction(Tagged<Object> object) {
  return WasmExportedFunction::IsWasmExportedFunction(object) ||
         WasmJSFunction::IsWasmJSFunction(object) ||
         WasmCapiFunction::IsWasmCapiFunction(object);
}

DirectHandle<WasmExceptionTag> WasmExceptionTag::New(Isolate* isolate,
                                                     int index) {
  auto result = Cast<WasmExceptionTag>(isolate->factory()->NewStruct(
      WASM_EXCEPTION_TAG_TYPE, AllocationType::kOld));
  result->set_index(index);
  return result;
}

Handle<AsmWasmData> AsmWasmData::New(
    Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
    DirectHandle<HeapNumber> uses_bitset) {
  const WasmModule* module = native_module->module();
  size_t memory_estimate =
      wasm::WasmCodeManager::EstimateNativeModuleCodeSize(module) +
      wasm::WasmCodeManager::EstimateNativeModuleMetaDataSize(module);
  DirectHandle<Managed<wasm::NativeModule>> managed_native_module =
      Managed<wasm::NativeModule>::From(isolate, memory_estimate,
                                        std::move(native_module));
  auto result = Cast<AsmWasmData>(
      isolate->factory()->NewStruct(ASM_WASM_DATA_TYPE, AllocationType::kOld));
  result->set_managed_native_module(*managed_native_module);
  result->set_uses_bitset(*uses_bitset);
  return result;
}

namespace {
constexpr int32_t kInt31MaxValue = 0x3fffffff;
constexpr int32_t kInt31MinValue = -kInt31MaxValue - 1;

// Tries to canonicalize a HeapNumber to an i31ref Smi. Returns the original
// HeapNumber if it fails.
DirectHandle<Object> CanonicalizeHeapNumber(DirectHandle<Object> number,
                                            Isolate* isolate, bool is_shared) {
  auto heap_number = Cast<HeapNumber>(number);
  double double_value = heap_number->value();
  if (double_value >= kInt31MinValue && double_value <= kInt31MaxValue &&
      !IsMinusZero(double_value) &&
      double_value == FastI2D(FastD2I(double_value))) {
    return direct_handle(Smi::FromInt(FastD2I(double_value)), isolate);
  }
  if (is_shared && !HeapLayout::InWritableSharedSpace(*heap_number)) {
    return isolate->factory()->NewHeapNumber<AllocationType::kSharedOld>(
        double_value);
  }
  return number;
}

// Tries to canonicalize a Smi into an i31 Smi. Returns a HeapNumber if it
// fails.
DirectHandle<Object> CanonicalizeSmi(DirectHandle<Object> smi, Isolate* isolate,
                                     bool is_shared) {
  if constexpr (SmiValuesAre31Bits()) return smi;

  int32_t value = Cast<Smi>(*smi).value();

  if (value <= kInt31MaxValue && value >= kInt31MinValue) {
    return smi;
  } else if (is_shared) {
    return isolate->factory()->NewHeapNumber<AllocationType::kSharedOld>(value);
  } else {
    return isolate->factory()->NewHeapNumber(value);
  }
}
}  // namespace

namespace wasm {
namespace {
inline bool CheckExpectedSharedness(Isolate* isolate,
                                    DirectHandle<Object> value,
                                    CanonicalValueType expected,
                                    const char** error_message) {
  if (v8_flags.experimental_wasm_shared && (*value).IsHeapObject()) {
    Tagged<HeapObject> heap_obj = (*value).cast<HeapObject>();
    if (expected.is_shared() != HeapLayout::InWritableSharedSpace(heap_obj)) {
      *error_message =
          expected.is_shared()
              ? "unshared object is not allowed for shared heap types"
              : "shared object is not allowed for unshared heap types";
      return false;
    }
  }
  return true;
}

inline bool ConvertToSharedIfExpected(Isolate* isolate,
                                      DirectHandle<Object>* value,
                                      CanonicalValueType expected,
                                      const char** error_message) {
  if (v8_flags.experimental_wasm_shared && expected.is_shared() &&
      (**value).IsHeapObject()) {
    Tagged<HeapObject> heap_obj = (**value).cast<HeapObject>();
    if (!HeapLayout::InWritableSharedSpace(heap_obj)) {
      if (!Object::Share(isolate, *value, ShouldThrow::kDontThrow)
               .ToHandle(value)) {
        *error_message = "unshared object is not allowed for shared heap types";
        return false;
      }
    }
  }
  return true;
}
}  // namespace

MaybeDirectHandle<Object> JSToWasmObject(Isolate* isolate,
                                         DirectHandle<Object> value,
                                         CanonicalValueType expected,
                                         const char** error_message) {
  DCHECK(expected.is_object_reference());
  if (expected.kind() == kRefNull && IsNull(*value, isolate)) {
    switch (expected.heap_representation()) {
      case HeapType::kStringViewWtf8:
        *error_message = "stringview_wtf8 has no JS representation";
        return {};
      case HeapType::kStringViewWtf16:
        *error_message = "stringview_wtf16 has no JS representation";
        return {};
      case HeapType::kStringViewIter:
        *error_message = "stringview_iter has no JS representation";
        return {};
      case HeapType::kExn:
        *error_message = "invalid type (ref null exn)";
        return {};
      case HeapType::kNoExn:
        *error_message = "invalid type (ref null noexn)";
        return {};
      case HeapType::kNoCont:
        *error_message = "invalid type (ref null nocont)";
        return {};
      case HeapType::kCont:
        *error_message = "invalid type (ref null cont)";
        return {};
      default:
        return expected.use_wasm_null() ? isolate->factory()->wasm_null()
                                        : value;
    }
  }

  switch (expected.heap_representation_non_shared()) {
    case HeapType::kFunc: {
      if (!(WasmExternalFunction::IsWasmExternalFunction(*value) ||
            WasmCapiFunction::IsWasmCapiFunction(*value))) {
        *error_message =
            "function-typed object must be null (if nullable) or a Wasm "
            "function object";
        return {};
      }
      return direct_handle(
          Cast<JSFunction>(*value)->shared()->wasm_function_data()->func_ref(),
          isolate);
    }
    case HeapType::kExtern: {
      if (!ConvertToSharedIfExpected(isolate, &value, expected,
                                     error_message)) {
        return {};
      }
      if (!IsNull(*value, isolate)) return value;
      *error_message = "null is not allowed for (ref extern)";
      return {};
    }
    case HeapType::kAny: {
      if (IsSmi(*value)) {
        return CanonicalizeSmi(value, isolate, expected.is_shared());
      }
      if (IsHeapNumber(*value)) {
        return CanonicalizeHeapNumber(value, isolate, expected.is_shared());
      }
      if (!ConvertToSharedIfExpected(isolate, &value, expected,
                                     error_message)) {
        return {};
      }
      if (!IsNull(*value, isolate)) return value;
      *error_message = "null is not allowed for (ref any)";
      return {};
    }
    case HeapType::kExn:
      *error_message = "invalid type (ref exn)";
      return {};
    case HeapType::kCont:
      *error_message = "invalid type (ref cont)";
      return {};
    case HeapType::kStruct: {
      if (!CheckExpectedSharedness(isolate, value, expected, error_message)) {
        return {};
      }
      if (IsWasmStruct(*value)) {
        return value;
      }
      *error_message =
          "structref object must be null (if nullable) or a wasm struct";
      return {};
    }
    case HeapType::kArray: {
      if (!CheckExpectedSharedness(isolate, value, expected, error_message)) {
        return {};
      }
      if (IsWasmArray(*value)) {
        return value;
      }
      *error_message =
          "arrayref object must be null (if nullable) or a wasm array";
      return {};
    }
    case HeapType::kEq: {
      if (IsSmi(*value)) {
        DirectHandle<Object> truncated =
            CanonicalizeSmi(value, isolate, expected.is_shared());
        if (IsSmi(*truncated)) return truncated;
      } else if (IsHeapNumber(*value)) {
        DirectHandle<Object> truncated =
            CanonicalizeHeapNumber(value, isolate, expected.is_shared());
        if (IsSmi(*truncated)) return truncated;
      } else if (IsWasmStruct(*value) || IsWasmArray(*value)) {
        if (!CheckExpectedSharedness(isolate, value, expected, error_message)) {
          return {};
        }
        return value;
      }
      *error_message =
          "eqref object must be null (if nullable), or a wasm "
          "struct/array, or a Number that fits in i31ref range";
      return {};
    }
    case HeapType::kI31: {
      if (IsSmi(*value)) {
        DirectHandle<Object> truncated =
            CanonicalizeSmi(value, isolate, expected.is_shared());
        if (IsSmi(*truncated)) return truncated;
      } else if (IsHeapNumber(*value)) {
        DirectHandle<Object> truncated =
            CanonicalizeHeapNumber(value, isolate, expected.is_shared());
        if (IsSmi(*truncated)) return truncated;
      }
      *error_message =
          "i31ref object must be null (if nullable) or a Number that fits "
          "in i31ref range";
      return {};
    }
    case HeapType::kString:
      if (IsString(*value)) return value;
      *error_message = "wrong type (expected a string)";
      return {};
    case HeapType::kStringViewWtf8:
      *error_message = "stringview_wtf8 has no JS representation";
      return {};
    case HeapType::kStringViewWtf16:
      *error_message = "stringview_wtf16 has no JS representation";
      return {};
    case HeapType::kStringViewIter:
      *error_message = "stringview_iter has no JS representation";
      return {};
    case HeapType::kNoFunc:
    case HeapType::kNoExtern:
    case HeapType::kNoExn:
    case HeapType::kNoCont:
    case HeapType::kNone: {
      *error_message = "only null allowed for null types";
      return {};
    }
    default: {
      DCHECK(expected.has_index());
      CanonicalTypeIndex canonical_index = expected.ref_index();
      auto type_canonicalizer = GetWasmEngine()->type_canonicalizer();

      if (WasmExportedFunction::IsWasmExportedFunction(*value)) {
        Tagged<WasmExportedFunction> function =
            Cast<WasmExportedFunction>(*value);
        CanonicalTypeIndex real_type_index =
            function->shared()->wasm_exported_function_data()->sig_index();
        if (!type_canonicalizer->IsCanonicalSubtype(real_type_index,
                                                    canonical_index)) {
          *error_message =
              "assigned exported function has to be a subtype of the "
              "expected type";
          return {};
        }
        return direct_handle(Cast<WasmExternalFunction>(*value)->func_ref(),
                             isolate);
      } else if (WasmJSFunction::IsWasmJSFunction(*value)) {
        if (!Cast<WasmJSFunction>(*value)
                 ->shared()
                 ->wasm_js_function_data()
                 ->MatchesSignature(canonical_index)) {
          *error_message =
              "assigned WebAssembly.Function has to be a subtype of the "
              "expected type";
          return {};
        }
        return direct_handle(Cast<WasmExternalFunction>(*value)->func_ref(),
                             isolate);
      } else if (WasmCapiFunction::IsWasmCapiFunction(*value)) {
        if (!Cast<WasmCapiFunction>(*value)->MatchesSignature(
                canonical_index)) {
          *error_message =
              "assigned C API function has to be a subtype of the expected "
              "type";
          return {};
        }
        return direct_handle(Cast<WasmExternalFunction>(*value)->func_ref(),
                             isolate);
      } else if (IsWasmStruct(*value) || IsWasmArray(*value)) {
        DirectHandle<WasmObject> wasm_obj = Cast<WasmObject>(value);
        Tagged<WasmTypeInfo> type_info = wasm_obj->map()->wasm_type_info();
        CanonicalTypeIndex actual_type = type_info->type_index();
        if (!type_canonicalizer->IsCanonicalSubtype(actual_type,
                                                    canonical_index)) {
          *error_message = "object is not a subtype of expected type";
          return {};
        }
        return value;
      } else {
        *error_message = "JS object does not match expected wasm type";
        return {};
      }
    }
  }
}

// Utility which canonicalizes {expected} in addition.
MaybeDirectHandle<Object> JSToWasmObject(Isolate* isolate,
                                         const WasmModule* module,
                                         DirectHandle<Object> value,
                                         ValueType expected,
                                         const char** error_message) {
  CanonicalValueType canonical;
  if (expected.has_index()) {
    canonical = module->canonical_type(expected);
  } else {
    canonical = CanonicalValueType{expected};
  }
  return JSToWasmObject(isolate, value, canonical, error_message);
}

DirectHandle<Object> WasmToJSObject(Isolate* isolate,
                                    DirectHandle<Object> value) {
  if (IsWasmNull(*value)) {
    return isolate->factory()->null_value();
  } else if (IsWasmFuncRef(*value)) {
    return i::WasmInternalFunction::GetOrCreateExternal(i::direct_handle(
        i::Cast<i::WasmFuncRef>(*value)->internal(isolate), isolate));
  } else {
    return value;
  }
}

// The WasmArray header is not a multiple of 8 bytes. For shared i64 arrays each
// element needs to be 8 byte aligned for atomic accesses. Therefore shared
// arrays use the kDoubleUnaligned alignment. If the header size changes to a
// multiple of 8 bytes, shared arrays should be allocated using kDoubleAligned
// instead.
// Note that for 64 bit no-pointer-compression builds, kDoubleUnAligned performs
// aligned(!) allocations instead, so we manually align the kHeaderSize there.
// Needed changes in case the header size changes to a multiple of 8:
// - objects-inl.h: HeapObject::RequiredAlignment
// - wasm.tq: WasmAllocateSharedArray_Uninitialized
// LINT.IfChange
static_assert(WasmArray::kHeaderSize % kDoubleSize ==
              (kTaggedSize != kDoubleSize ? 4 : 0));
// LINT.ThenChange(src/objects/objects-inl.h, src/builtins/wasm.tq)

}  // namespace wasm

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"
#undef TRACE_IFT
