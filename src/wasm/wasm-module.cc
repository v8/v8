// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/base/atomic-utils.h"
#include "src/code-stubs.h"

#include "src/macro-assembler.h"
#include "src/objects.h"
#include "src/property-descriptor.h"
#include "src/simulator.h"
#include "src/snapshot/snapshot.h"
#include "src/v8.h"

#include "src/wasm/ast-decoder.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-function-name-table.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-result.h"

#include "src/compiler/wasm-compiler.h"

namespace v8 {
namespace internal {
namespace wasm {

#define TRACE(...)                                      \
  do {                                                  \
    if (FLAG_trace_wasm_instances) PrintF(__VA_ARGS__); \
  } while (false)

#define TRACE_CHAIN(instance)        \
  do {                               \
    instance->PrintInstancesChain(); \
  } while (false)

namespace {

static const int kPlaceholderMarker = 1000000000;

enum JSFunctionExportInternalField {
  kInternalModuleInstance,
  kInternalArity,
  kInternalSignature
};

// Internal constants for the layout of the module object.
enum WasmInstanceObjectFields {
  kWasmCompiledModule = 0,
  kWasmModuleFunctionTable,
  kWasmModuleCodeTable,
  kWasmMemArrayBuffer,
  kWasmGlobalsArrayBuffer,
  // TODO(clemensh): Remove function name array, extract names from module
  // bytes.
  kWasmFunctionNamesArray,
  kWasmModuleBytesString,
  kWasmDebugInfo,
  kWasmNumImportedFunctions,
  kWasmModuleInternalFieldCount
};

enum WasmImportData {
  kImportKind,         // Smi. an ExternalKind
  kImportGlobalType,   // Smi. Type for globals.
  kImportIndex,        // Smi. index for the import.
  kModuleName,         // String
  kFunctionName,       // maybe String
  kOutputCount,        // Smi. an uint32_t
  kSignature,          // ByteArray. A copy of the data in FunctionSig
  kWasmImportDataSize  // Sentinel value.
};

enum WasmExportData {
  kExportKind,         // Smi. an ExternalKind
  kExportGlobalType,   // Smi. Type for globals.
  kExportName,         // String
  kExportArity,        // Smi, an int
  kExportIndex,        // Smi, an uint32_t
  kExportedSignature,  // ByteArray. A copy of the data in FunctionSig
  kWasmExportDataSize  // Sentinel value.
};

enum WasmGlobalInitData {
  kGlobalInitKind,   // 0 = constant, 1 = global index
  kGlobalInitType,   // Smi. Type for globals.
  kGlobalInitIndex,  // Smi, an uint32_t
  kGlobalInitValue,  // Number.
  kWasmGlobalInitDataSize
};

enum WasmSegmentInfo {
  kDestInitKind,        // 0 = constant, 1 = global index
  kDestAddrValue,       // Smi. an uint32_t
  kSourceSize,          // Smi. an uint32_t
  kWasmSegmentInfoSize  // Sentinel value.
};

enum WasmIndirectFunctionTableData {
  kSize,                              // Smi. an uint32_t
  kTable,                             // FixedArray of indirect function table
  kWasmIndirectFunctionTableDataSize  // Sentinel value.
};

byte* raw_buffer_ptr(MaybeHandle<JSArrayBuffer> buffer, int offset) {
  return static_cast<byte*>(buffer.ToHandleChecked()->backing_store()) + offset;
}

uint32_t GetMinModuleMemSize(const WasmModule* module) {
  return WasmModule::kPageSize * module->min_mem_pages;
}

void SaveDataSegmentInfo(Factory* factory, const WasmModule* module,
                         Handle<WasmCompiledModule> compiled_module) {
  Handle<FixedArray> segments = factory->NewFixedArray(
      static_cast<int>(module->data_segments.size()), TENURED);
  uint32_t data_size = 0;
  for (const WasmDataSegment& segment : module->data_segments) {
    if (segment.source_size == 0) continue;
    data_size += segment.source_size;
  }
  Handle<ByteArray> data = factory->NewByteArray(data_size, TENURED);

  uint32_t last_insertion_pos = 0;
  for (uint32_t i = 0; i < module->data_segments.size(); ++i) {
    const WasmDataSegment& segment = module->data_segments[i];
    if (segment.source_size == 0) continue;
    Handle<ByteArray> js_segment =
        factory->NewByteArray(kWasmSegmentInfoSize * sizeof(uint32_t), TENURED);
    // TODO(titzer): add support for global offsets for dest_addr
    CHECK_EQ(WasmInitExpr::kI32Const, segment.dest_addr.kind);
    js_segment->set_int(kDestAddrValue, segment.dest_addr.val.i32_const);
    js_segment->set_int(kSourceSize, segment.source_size);
    segments->set(i, *js_segment);
    data->copy_in(last_insertion_pos,
                  module->module_start + segment.source_offset,
                  segment.source_size);
    last_insertion_pos += segment.source_size;
  }
  compiled_module->set_data_segments_info(segments);
  compiled_module->set_data_segments(data);
}

void PatchFunctionTable(Handle<Code> code,
                        Handle<FixedArray> old_indirect_table,
                        Handle<FixedArray> new_indirect_table) {
  for (RelocIterator it(*code, 1 << RelocInfo::EMBEDDED_OBJECT); !it.done();
       it.next()) {
    if (it.rinfo()->target_object() == *old_indirect_table) {
      it.rinfo()->set_target_object(*new_indirect_table);
    }
  }
}

Handle<JSArrayBuffer> NewArrayBuffer(Isolate* isolate, size_t size) {
  if (size > (WasmModule::kMaxMemPages * WasmModule::kPageSize)) {
    // TODO(titzer): lift restriction on maximum memory allocated here.
    return Handle<JSArrayBuffer>::null();
  }
  void* memory = isolate->array_buffer_allocator()->Allocate(size);
  if (memory == nullptr) {
    return Handle<JSArrayBuffer>::null();
  }

#if DEBUG
  // Double check the API allocator actually zero-initialized the memory.
  const byte* bytes = reinterpret_cast<const byte*>(memory);
  for (size_t i = 0; i < size; ++i) {
    DCHECK_EQ(0, bytes[i]);
  }
#endif

  Handle<JSArrayBuffer> buffer = isolate->factory()->NewJSArrayBuffer();
  JSArrayBuffer::Setup(buffer, isolate, false, memory, static_cast<int>(size));
  buffer->set_is_neuterable(false);
  return buffer;
}

void RelocateInstanceCode(Handle<JSObject> instance, Address old_start,
                          Address start, uint32_t prev_size,
                          uint32_t new_size) {
  Handle<FixedArray> functions = Handle<FixedArray>(
      FixedArray::cast(instance->GetInternalField(kWasmModuleCodeTable)));
  for (int i = 0; i < functions->length(); ++i) {
    Handle<Code> function = Handle<Code>(Code::cast(functions->get(i)));
    AllowDeferredHandleDereference embedding_raw_address;
    int mask = (1 << RelocInfo::WASM_MEMORY_REFERENCE) |
               (1 << RelocInfo::WASM_MEMORY_SIZE_REFERENCE);
    for (RelocIterator it(*function, mask); !it.done(); it.next()) {
      it.rinfo()->update_wasm_memory_reference(old_start, start, prev_size,
                                               new_size);
    }
  }
}

void RelocateGlobals(Handle<JSObject> instance, Address old_start,
                     Address globals_start) {
  Handle<FixedArray> functions = Handle<FixedArray>(
      FixedArray::cast(instance->GetInternalField(kWasmModuleCodeTable)));
  uint32_t function_count = static_cast<uint32_t>(functions->length());
  for (uint32_t i = 0; i < function_count; ++i) {
    Handle<Code> function = Handle<Code>(Code::cast(functions->get(i)));
    AllowDeferredHandleDereference embedding_raw_address;
    int mask = 1 << RelocInfo::WASM_GLOBAL_REFERENCE;
    for (RelocIterator it(*function, mask); !it.done(); it.next()) {
      it.rinfo()->update_wasm_global_reference(old_start, globals_start);
    }
  }
}

Handle<Code> CreatePlaceholder(Factory* factory, uint32_t index,
                               Code::Kind kind) {
  // Create a placeholder code object and encode the corresponding index in
  // the {constant_pool_offset} field of the code object.
  // TODO(titzer): placeholder code objects are somewhat dangerous.
  static byte buffer[] = {0, 0, 0, 0, 0, 0, 0, 0};  // fake instructions.
  static CodeDesc desc = {
      buffer, arraysize(buffer), arraysize(buffer), 0, 0, nullptr, 0, nullptr};
  Handle<Code> code = factory->NewCode(desc, Code::KindField::encode(kind),
                                       Handle<Object>::null());
  code->set_constant_pool_offset(static_cast<int>(index) + kPlaceholderMarker);
  return code;
}

bool LinkFunction(Handle<Code> unlinked,
                  std::vector<Handle<Code>>& code_table) {
  bool modified = false;
  int mode_mask = RelocInfo::ModeMask(RelocInfo::CODE_TARGET);
  AllowDeferredHandleDereference embedding_raw_address;
  for (RelocIterator it(*unlinked, mode_mask); !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsCodeTarget(mode)) {
      Code* target =
          Code::GetCodeFromTargetAddress(it.rinfo()->target_address());
      if (target->constant_pool_offset() < kPlaceholderMarker) continue;
      switch (target->kind()) {
        case Code::WASM_FUNCTION:        // fall through
        case Code::WASM_TO_JS_FUNCTION:  // fall through
        case Code::JS_TO_WASM_FUNCTION: {
          // Patch direct calls to placeholder code objects.
          uint32_t index = target->constant_pool_offset() - kPlaceholderMarker;
          Handle<Code> new_target = code_table[index];
          if (target != *new_target) {
            it.rinfo()->set_target_address(new_target->instruction_start(),
                                           UPDATE_WRITE_BARRIER,
                                           SKIP_ICACHE_FLUSH);
            modified = true;
          }
          break;
        }
        default:
          break;
      }
    }
  }
  return modified;
}

void FlushICache(Isolate* isolate, Handle<FixedArray> functions) {
  for (int i = 0; i < functions->length(); ++i) {
    Handle<Code> code = functions->GetValueChecked<Code>(isolate, i);
    Assembler::FlushICache(isolate, code->instruction_start(),
                           code->instruction_size());
  }
}

// Fetches the compilation unit of a wasm function and executes its parallel
// phase.
bool FetchAndExecuteCompilationUnit(
    Isolate* isolate,
    std::vector<compiler::WasmCompilationUnit*>* compilation_units,
    std::queue<compiler::WasmCompilationUnit*>* executed_units,
    base::Mutex* result_mutex, base::AtomicNumber<size_t>* next_unit) {
  DisallowHeapAllocation no_allocation;
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;
  DisallowCodeDependencyChange no_dependency_change;

  // - 1 because AtomicIncrement returns the value after the atomic increment.
  size_t index = next_unit->Increment(1) - 1;
  if (index >= compilation_units->size()) {
    return false;
  }

  compiler::WasmCompilationUnit* unit = compilation_units->at(index);
  if (unit != nullptr) {
    unit->ExecuteCompilation();
    base::LockGuard<base::Mutex> guard(result_mutex);
    executed_units->push(unit);
  }
  return true;
}

class WasmCompilationTask : public CancelableTask {
 public:
  WasmCompilationTask(
      Isolate* isolate,
      std::vector<compiler::WasmCompilationUnit*>* compilation_units,
      std::queue<compiler::WasmCompilationUnit*>* executed_units,
      base::Semaphore* on_finished, base::Mutex* result_mutex,
      base::AtomicNumber<size_t>* next_unit)
      : CancelableTask(isolate),
        isolate_(isolate),
        compilation_units_(compilation_units),
        executed_units_(executed_units),
        on_finished_(on_finished),
        result_mutex_(result_mutex),
        next_unit_(next_unit) {}

  void RunInternal() override {
    while (FetchAndExecuteCompilationUnit(isolate_, compilation_units_,
                                          executed_units_, result_mutex_,
                                          next_unit_)) {
    }
    on_finished_->Signal();
  }

  Isolate* isolate_;
  std::vector<compiler::WasmCompilationUnit*>* compilation_units_;
  std::queue<compiler::WasmCompilationUnit*>* executed_units_;
  base::Semaphore* on_finished_;
  base::Mutex* result_mutex_;
  base::AtomicNumber<size_t>* next_unit_;
};

static void RecordStats(Isolate* isolate, Code* code) {
  isolate->counters()->wasm_generated_code_size()->Increment(code->body_size());
  isolate->counters()->wasm_reloc_size()->Increment(
      code->relocation_info()->length());
}

static void RecordStats(Isolate* isolate, Handle<FixedArray> functions) {
  DisallowHeapAllocation no_gc;
  for (int i = 0; i < functions->length(); ++i) {
    RecordStats(isolate, Code::cast(functions->get(i)));
  }
}

Address GetGlobalStartAddressFromCodeTemplate(Object* undefined,
                                              JSObject* owner) {
  Address old_address = nullptr;
  Object* stored_value = owner->GetInternalField(kWasmGlobalsArrayBuffer);
  if (stored_value != undefined) {
    old_address = static_cast<Address>(
        JSArrayBuffer::cast(stored_value)->backing_store());
  }
  return old_address;
}

Handle<FixedArray> EncodeImports(Factory* factory, const WasmModule* module) {
  Handle<FixedArray> ret = factory->NewFixedArray(
      static_cast<int>(module->import_table.size()), TENURED);

  for (size_t i = 0; i < module->import_table.size(); ++i) {
    const WasmImport& import = module->import_table[i];
    Handle<FixedArray> encoded_import =
        factory->NewFixedArray(kWasmImportDataSize, TENURED);
    encoded_import->set(kImportKind, Smi::FromInt(import.kind));
    encoded_import->set(kImportIndex, Smi::FromInt(import.index));

    // Add the module and function name.
    WasmName module_name = module->GetNameOrNull(import.module_name_offset,
                                                 import.module_name_length);
    WasmName function_name = module->GetNameOrNull(import.field_name_offset,
                                                   import.field_name_length);

    Handle<String> module_name_string =
        factory->InternalizeUtf8String(module_name);
    encoded_import->set(kModuleName, *module_name_string);
    if (!function_name.is_empty()) {
      Handle<String> function_name_string =
          factory->InternalizeUtf8String(function_name);
      encoded_import->set(kFunctionName, *function_name_string);
    }

    switch (import.kind) {
      case kExternalFunction: {
        // Encode the signature into the import.
        FunctionSig* fsig = module->functions[import.index].sig;
        Handle<ByteArray> sig = factory->NewByteArray(
            static_cast<int>(fsig->parameter_count() + fsig->return_count()),
            TENURED);
        sig->copy_in(0, reinterpret_cast<const byte*>(fsig->raw_data()),
                     sig->length());
        encoded_import->set(
            kOutputCount, Smi::FromInt(static_cast<int>(fsig->return_count())));
        encoded_import->set(kSignature, *sig);
        break;
      }
      case kExternalTable:
        // Nothing extra required for imported tables.
        break;
      case kExternalMemory:
        // Nothing extra required for imported memories.
        break;
      case kExternalGlobal: {
        // Encode the offset and the global type into the import.
        const WasmGlobal& global = module->globals[import.index];
        TRACE("import[%zu].type = %s\n", i, WasmOpcodes::TypeName(global.type));
        encoded_import->set(
            kImportGlobalType,
            Smi::FromInt(WasmOpcodes::LocalTypeCodeFor(global.type)));
        encoded_import->set(kImportIndex, Smi::FromInt(global.offset));
        break;
      }
    }
    ret->set(static_cast<int>(i), *encoded_import);
  }
  return ret;
}

void InitializeParallelCompilation(
    Isolate* isolate, const std::vector<WasmFunction>& functions,
    std::vector<compiler::WasmCompilationUnit*>& compilation_units,
    ModuleEnv& module_env, ErrorThrower* thrower) {
  for (uint32_t i = FLAG_skip_compiling_wasm_funcs; i < functions.size(); ++i) {
    const WasmFunction* func = &functions[i];
    compilation_units[i] =
        func->imported ? nullptr : new compiler::WasmCompilationUnit(
                                       thrower, isolate, &module_env, func, i);
  }
}

uint32_t* StartCompilationTasks(
    Isolate* isolate,
    std::vector<compiler::WasmCompilationUnit*>& compilation_units,
    std::queue<compiler::WasmCompilationUnit*>& executed_units,
    base::Semaphore* pending_tasks, base::Mutex& result_mutex,
    base::AtomicNumber<size_t>& next_unit) {
  const size_t num_tasks =
      Min(static_cast<size_t>(FLAG_wasm_num_compilation_tasks),
          V8::GetCurrentPlatform()->NumberOfAvailableBackgroundThreads());
  uint32_t* task_ids = new uint32_t[num_tasks];
  for (size_t i = 0; i < num_tasks; ++i) {
    WasmCompilationTask* task =
        new WasmCompilationTask(isolate, &compilation_units, &executed_units,
                                pending_tasks, &result_mutex, &next_unit);
    task_ids[i] = task->id();
    V8::GetCurrentPlatform()->CallOnBackgroundThread(
        task, v8::Platform::kShortRunningTask);
  }
  return task_ids;
}

void WaitForCompilationTasks(Isolate* isolate, uint32_t* task_ids,
                             base::Semaphore* pending_tasks) {
  const size_t num_tasks =
      Min(static_cast<size_t>(FLAG_wasm_num_compilation_tasks),
          V8::GetCurrentPlatform()->NumberOfAvailableBackgroundThreads());
  for (size_t i = 0; i < num_tasks; ++i) {
    // If the task has not started yet, then we abort it. Otherwise we wait for
    // it to finish.
    if (!isolate->cancelable_task_manager()->TryAbort(task_ids[i])) {
      pending_tasks->Wait();
    }
  }
}

void FinishCompilationUnits(
    std::queue<compiler::WasmCompilationUnit*>& executed_units,
    std::vector<Handle<Code>>& results, base::Mutex& result_mutex) {
  while (true) {
    compiler::WasmCompilationUnit* unit = nullptr;
    {
      base::LockGuard<base::Mutex> guard(&result_mutex);
      if (executed_units.empty()) {
        break;
      }
      unit = executed_units.front();
      executed_units.pop();
    }
    int j = unit->index();
    results[j] = unit->FinishCompilation();
    delete unit;
  }
}

void CompileInParallel(Isolate* isolate, const WasmModule* module,
                       std::vector<Handle<Code>>& functions,
                       ErrorThrower* thrower, ModuleEnv* module_env) {
  // Data structures for the parallel compilation.
  std::vector<compiler::WasmCompilationUnit*> compilation_units(
      module->functions.size());
  std::queue<compiler::WasmCompilationUnit*> executed_units;

  //-----------------------------------------------------------------------
  // For parallel compilation:
  // 1) The main thread allocates a compilation unit for each wasm function
  //    and stores them in the vector {compilation_units}.
  // 2) The main thread spawns {WasmCompilationTask} instances which run on
  //    the background threads.
  // 3.a) The background threads and the main thread pick one compilation
  //      unit at a time and execute the parallel phase of the compilation
  //      unit. After finishing the execution of the parallel phase, the
  //      result is enqueued in {executed_units}.
  // 3.b) If {executed_units} contains a compilation unit, the main thread
  //      dequeues it and finishes the compilation.
  // 4) After the parallel phase of all compilation units has started, the
  //    main thread waits for all {WasmCompilationTask} instances to finish.
  // 5) The main thread finishes the compilation.

  // Turn on the {CanonicalHandleScope} so that the background threads can
  // use the node cache.
  CanonicalHandleScope canonical(isolate);

  // 1) The main thread allocates a compilation unit for each wasm function
  //    and stores them in the vector {compilation_units}.
  InitializeParallelCompilation(isolate, module->functions, compilation_units,
                                *module_env, thrower);

  // Objects for the synchronization with the background threads.
  base::Mutex result_mutex;
  base::AtomicNumber<size_t> next_unit(
      static_cast<size_t>(FLAG_skip_compiling_wasm_funcs));

  // 2) The main thread spawns {WasmCompilationTask} instances which run on
  //    the background threads.
  std::unique_ptr<uint32_t[]> task_ids(StartCompilationTasks(
      isolate, compilation_units, executed_units, module->pending_tasks.get(),
      result_mutex, next_unit));

  // 3.a) The background threads and the main thread pick one compilation
  //      unit at a time and execute the parallel phase of the compilation
  //      unit. After finishing the execution of the parallel phase, the
  //      result is enqueued in {executed_units}.
  while (FetchAndExecuteCompilationUnit(isolate, &compilation_units,
                                        &executed_units, &result_mutex,
                                        &next_unit)) {
    // 3.b) If {executed_units} contains a compilation unit, the main thread
    //      dequeues it and finishes the compilation unit. Compilation units
    //      are finished concurrently to the background threads to save
    //      memory.
    FinishCompilationUnits(executed_units, functions, result_mutex);
  }
  // 4) After the parallel phase of all compilation units has started, the
  //    main thread waits for all {WasmCompilationTask} instances to finish.
  WaitForCompilationTasks(isolate, task_ids.get(), module->pending_tasks.get());
  // Finish the compilation of the remaining compilation units.
  FinishCompilationUnits(executed_units, functions, result_mutex);
}

void CompileSequentially(Isolate* isolate, const WasmModule* module,
                         std::vector<Handle<Code>>& functions,
                         ErrorThrower* thrower, ModuleEnv* module_env) {
  DCHECK(!thrower->error());

  for (uint32_t i = FLAG_skip_compiling_wasm_funcs;
       i < module->functions.size(); ++i) {
    const WasmFunction& func = module->functions[i];
    if (func.imported) continue;  // Imports are compiled at instantiation time.

    WasmName str = module->GetName(func.name_offset, func.name_length);
    Handle<Code> code = Handle<Code>::null();
    // Compile the function.
    code = compiler::WasmCompilationUnit::CompileWasmFunction(
        thrower, isolate, module_env, &func);
    if (code.is_null()) {
      thrower->Error("Compilation of #%d:%.*s failed.", i, str.length(),
                     str.start());
      break;
    }
      // Install the code into the linker table.
    functions[i] = code;
  }
}

void PatchDirectCalls(Handle<FixedArray> old_functions,
                      Handle<FixedArray> new_functions, int start) {
  DCHECK_EQ(new_functions->length(), old_functions->length());

  DisallowHeapAllocation no_gc;
  std::map<Code*, Code*> old_to_new_code;
  for (int i = 0; i < new_functions->length(); ++i) {
    old_to_new_code.insert(std::make_pair(Code::cast(old_functions->get(i)),
                                          Code::cast(new_functions->get(i))));
  }
  int mode_mask = RelocInfo::ModeMask(RelocInfo::CODE_TARGET);
  AllowDeferredHandleDereference embedding_raw_address;
  for (int i = start; i < new_functions->length(); ++i) {
    Code* wasm_function = Code::cast(new_functions->get(i));
    for (RelocIterator it(wasm_function, mode_mask); !it.done(); it.next()) {
      Code* old_code =
          Code::GetCodeFromTargetAddress(it.rinfo()->target_address());
      if (old_code->kind() == Code::WASM_TO_JS_FUNCTION ||
          old_code->kind() == Code::WASM_FUNCTION) {
        auto found = old_to_new_code.find(old_code);
        DCHECK(found != old_to_new_code.end());
        Code* new_code = found->second;
        if (new_code != old_code) {
          it.rinfo()->set_target_address(new_code->instruction_start(),
                                         UPDATE_WRITE_BARRIER,
                                         SKIP_ICACHE_FLUSH);
        }
      }
    }
  }
}

static void ResetCompiledModule(Isolate* isolate, JSObject* owner,
                                WasmCompiledModule* compiled_module) {
  TRACE("Resetting %d\n", compiled_module->instance_id());
  Object* undefined = *isolate->factory()->undefined_value();
  uint32_t old_mem_size = compiled_module->has_heap()
                              ? compiled_module->mem_size()
                              : compiled_module->default_mem_size();
  uint32_t default_mem_size = compiled_module->default_mem_size();
  Object* mem_start = compiled_module->ptr_to_heap();
  Address old_mem_address = nullptr;
  Address globals_start =
      GetGlobalStartAddressFromCodeTemplate(undefined, owner);

  if (old_mem_size > 0) {
    CHECK_NE(mem_start, undefined);
    old_mem_address =
        static_cast<Address>(JSArrayBuffer::cast(mem_start)->backing_store());
  }
  int mode_mask = RelocInfo::ModeMask(RelocInfo::WASM_MEMORY_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::WASM_MEMORY_SIZE_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::WASM_GLOBAL_REFERENCE);

  Object* fct_obj = compiled_module->ptr_to_code_table();
  if (fct_obj != nullptr && fct_obj != undefined &&
      (old_mem_size > 0 || globals_start != nullptr)) {
    FixedArray* functions = FixedArray::cast(fct_obj);
    for (int i = 0; i < functions->length(); ++i) {
      Code* code = Code::cast(functions->get(i));
      bool changed = false;
      for (RelocIterator it(code, mode_mask); !it.done(); it.next()) {
        RelocInfo::Mode mode = it.rinfo()->rmode();
        if (RelocInfo::IsWasmMemoryReference(mode) ||
            RelocInfo::IsWasmMemorySizeReference(mode)) {
          it.rinfo()->update_wasm_memory_reference(
              old_mem_address, nullptr, old_mem_size, default_mem_size);
          changed = true;
        } else {
          CHECK(RelocInfo::IsWasmGlobalReference(mode));
          it.rinfo()->update_wasm_global_reference(globals_start, nullptr);
          changed = true;
        }
      }
      if (changed) {
        Assembler::FlushICache(isolate, code->instruction_start(),
                               code->instruction_size());
      }
    }
  }
  compiled_module->reset_heap();
}

static void InstanceFinalizer(const v8::WeakCallbackInfo<void>& data) {
  JSObject** p = reinterpret_cast<JSObject**>(data.GetParameter());
  JSObject* owner = *p;
  WasmCompiledModule* compiled_module =
      WasmCompiledModule::cast(owner->GetInternalField(kWasmCompiledModule));
  TRACE("Finalizing %d {\n", compiled_module->instance_id());
  Isolate* isolate = reinterpret_cast<Isolate*>(data.GetIsolate());
  DCHECK(compiled_module->has_weak_module_object());
  WeakCell* weak_module_obj = compiled_module->ptr_to_weak_module_object();

  // weak_module_obj may have been cleared, meaning the module object
  // was GC-ed. In that case, there won't be any new instances created,
  // and we don't need to maintain the links between instances.
  if (!weak_module_obj->cleared()) {
    JSObject* module_obj = JSObject::cast(weak_module_obj->value());
    WasmCompiledModule* current_template =
        WasmCompiledModule::cast(module_obj->GetInternalField(0));

    TRACE("chain before {\n");
    TRACE_CHAIN(current_template);
    TRACE("}\n");

    DCHECK(!current_template->has_weak_prev_instance());
    WeakCell* next = compiled_module->ptr_to_weak_next_instance();
    WeakCell* prev = compiled_module->ptr_to_weak_prev_instance();

    if (current_template == compiled_module) {
      if (next == nullptr) {
        ResetCompiledModule(isolate, owner, compiled_module);
      } else {
        DCHECK(next->value()->IsFixedArray());
        module_obj->SetInternalField(0, next->value());
        DCHECK_NULL(prev);
        WasmCompiledModule::cast(next->value())->reset_weak_prev_instance();
      }
    } else {
      DCHECK(!(prev == nullptr && next == nullptr));
      // the only reason prev or next would be cleared is if the
      // respective objects got collected, but if that happened,
      // we would have relinked the list.
      if (prev != nullptr) {
        DCHECK(!prev->cleared());
        if (next == nullptr) {
          WasmCompiledModule::cast(prev->value())->reset_weak_next_instance();
        } else {
          WasmCompiledModule::cast(prev->value())
              ->set_ptr_to_weak_next_instance(next);
        }
      }
      if (next != nullptr) {
        DCHECK(!next->cleared());
        if (prev == nullptr) {
          WasmCompiledModule::cast(next->value())->reset_weak_prev_instance();
        } else {
          WasmCompiledModule::cast(next->value())
              ->set_ptr_to_weak_prev_instance(prev);
        }
      }
    }
    TRACE("chain after {\n");
    TRACE_CHAIN(WasmCompiledModule::cast(module_obj->GetInternalField(0)));
    TRACE("}\n");
  }
  compiled_module->reset_weak_owning_instance();
  GlobalHandles::Destroy(reinterpret_cast<Object**>(p));
  TRACE("}\n");
}

Handle<FixedArray> SetupIndirectFunctionTable(
    Isolate* isolate, Handle<FixedArray> wasm_functions,
    Handle<FixedArray> indirect_table_template,
    Handle<FixedArray> tables_to_replace) {
  Factory* factory = isolate->factory();
  Handle<FixedArray> cloned_indirect_tables =
      factory->CopyFixedArray(indirect_table_template);
  for (int i = 0; i < cloned_indirect_tables->length(); ++i) {
    Handle<FixedArray> orig_metadata =
        cloned_indirect_tables->GetValueChecked<FixedArray>(isolate, i);
    Handle<FixedArray> cloned_metadata = factory->CopyFixedArray(orig_metadata);
    cloned_indirect_tables->set(i, *cloned_metadata);

    Handle<FixedArray> orig_table =
        cloned_metadata->GetValueChecked<FixedArray>(isolate, kTable);
    Handle<FixedArray> cloned_table = factory->CopyFixedArray(orig_table);
    cloned_metadata->set(kTable, *cloned_table);
    // Patch the cloned code to refer to the cloned kTable.
    Handle<FixedArray> table_to_replace =
        tables_to_replace->GetValueChecked<FixedArray>(isolate, i)
            ->GetValueChecked<FixedArray>(isolate, kTable);
    for (int fct_index = 0; fct_index < wasm_functions->length(); ++fct_index) {
      Handle<Code> wasm_function =
          wasm_functions->GetValueChecked<Code>(isolate, fct_index);
      PatchFunctionTable(wasm_function, table_to_replace, cloned_table);
    }
  }
  return cloned_indirect_tables;
}

}  // namespace

const char* SectionName(WasmSectionCode code) {
  switch (code) {
    case kUnknownSectionCode:
      return "Unknown";
    case kTypeSectionCode:
      return "Type";
    case kImportSectionCode:
      return "Import";
    case kFunctionSectionCode:
      return "Function";
    case kTableSectionCode:
      return "Table";
    case kMemorySectionCode:
      return "Memory";
    case kGlobalSectionCode:
      return "Global";
    case kExportSectionCode:
      return "Export";
    case kStartSectionCode:
      return "Start";
    case kCodeSectionCode:
      return "Code";
    case kElementSectionCode:
      return "Element";
    case kDataSectionCode:
      return "Data";
    case kNameSectionCode:
      return "Name";
    default:
      return "<unknown>";
  }
}

std::ostream& operator<<(std::ostream& os, const WasmModule& module) {
  os << "WASM module with ";
  os << (module.min_mem_pages * module.kPageSize) << " min mem";
  os << (module.max_mem_pages * module.kPageSize) << " max mem";
  os << module.functions.size() << " functions";
  os << module.functions.size() << " globals";
  os << module.functions.size() << " data segments";
  return os;
}

std::ostream& operator<<(std::ostream& os, const WasmFunction& function) {
  os << "WASM function with signature " << *function.sig;

  os << " code bytes: "
     << (function.code_end_offset - function.code_start_offset);
  return os;
}

std::ostream& operator<<(std::ostream& os, const WasmFunctionName& pair) {
  os << "#" << pair.function_->func_index << ":";
  if (pair.function_->name_offset > 0) {
    if (pair.module_) {
      WasmName name = pair.module_->GetName(pair.function_->name_offset,
                                            pair.function_->name_length);
      os.write(name.start(), name.length());
    } else {
      os << "+" << pair.function_->func_index;
    }
  } else {
    os << "?";
  }
  return os;
}

Handle<JSFunction> WrapExportCodeAsJSFunction(
    Isolate* isolate, Handle<Code> export_code, Handle<String> name, int arity,
    MaybeHandle<ByteArray> maybe_signature, Handle<JSObject> module_instance) {
  Handle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfo(name, export_code, false);
  shared->set_length(arity);
  shared->set_internal_formal_parameter_count(arity);
  Handle<JSFunction> function = isolate->factory()->NewFunction(
      isolate->wasm_function_map(), name, export_code);
  function->set_shared(*shared);

  function->SetInternalField(kInternalModuleInstance, *module_instance);
  // add another Internal Field as the function arity
  function->SetInternalField(kInternalArity, Smi::FromInt(arity));
  // add another Internal Field as the signature of the foreign function
  Handle<ByteArray> signature;
  if (maybe_signature.ToHandle(&signature)) {
    function->SetInternalField(kInternalSignature, *signature);
  }
  return function;
}

Object* GetOwningWasmInstance(Code* code) {
  DCHECK(code->kind() == Code::WASM_FUNCTION);
  DisallowHeapAllocation no_gc;
  FixedArray* deopt_data = code->deoptimization_data();
  DCHECK_NOT_NULL(deopt_data);
  DCHECK(deopt_data->length() == 2);
  Object* weak_link = deopt_data->get(0);
  if (!weak_link->IsWeakCell()) return nullptr;
  WeakCell* cell = WeakCell::cast(weak_link);
  return cell->value();
}

uint32_t GetNumImportedFunctions(Handle<JSObject> wasm_object) {
  return static_cast<uint32_t>(
      Smi::cast(wasm_object->GetInternalField(kWasmNumImportedFunctions))
          ->value());
}

WasmModule::WasmModule(byte* module_start)
    : module_start(module_start),
      module_end(nullptr),
      min_mem_pages(0),
      max_mem_pages(0),
      mem_export(false),
      start_function_index(-1),
      origin(kWasmOrigin),
      globals_size(0),
      num_imported_functions(0),
      num_declared_functions(0),
      num_exported_functions(0),
      pending_tasks(new base::Semaphore(0)) {}

void EncodeInit(const WasmModule* module, Factory* factory,
                Handle<FixedArray> entry, int kind_index, int value_index,
                const WasmInitExpr& expr) {
  entry->set(kind_index, Smi::FromInt(0));

  Handle<Object> value;
  switch (expr.kind) {
    case WasmInitExpr::kGlobalIndex: {
      TRACE("  kind = 1, global index %u\n", expr.val.global_index);
      entry->set(kind_index, Smi::FromInt(1));
      uint32_t offset = module->globals[expr.val.global_index].offset;
      entry->set(value_index, Smi::FromInt(offset));
      return;
    }
    case WasmInitExpr::kI32Const:
      TRACE("  kind = 0, i32 = %d\n", expr.val.i32_const);
      value = factory->NewNumber(expr.val.i32_const);
      break;
    case WasmInitExpr::kI64Const:
      // TODO(titzer): implement initializers for i64 globals.
      UNREACHABLE();
      break;
    case WasmInitExpr::kF32Const:
      TRACE("  kind = 0, f32 = %f\n", expr.val.f32_const);
      value = factory->NewNumber(expr.val.f32_const);
      break;
    case WasmInitExpr::kF64Const:
      TRACE("  kind = 0, f64 = %lf\n", expr.val.f64_const);
      value = factory->NewNumber(expr.val.f64_const);
      break;
    default:
      UNREACHABLE();
  }
  entry->set(value_index, *value);
}

MaybeHandle<WasmCompiledModule> WasmModule::CompileFunctions(
    Isolate* isolate, ErrorThrower* thrower) const {
  Factory* factory = isolate->factory();

  MaybeHandle<WasmCompiledModule> nothing;

  WasmModuleInstance temp_instance(this);
  temp_instance.context = isolate->native_context();
  temp_instance.mem_size = GetMinModuleMemSize(this);
  temp_instance.mem_start = nullptr;
  temp_instance.globals_start = nullptr;

  MaybeHandle<FixedArray> indirect_table =
      function_tables.size()
          ? factory->NewFixedArray(static_cast<int>(function_tables.size()),
                                   TENURED)
          : MaybeHandle<FixedArray>();
  for (uint32_t i = 0; i < function_tables.size(); ++i) {
    Handle<FixedArray> values = wasm::BuildFunctionTable(isolate, i, this);
    temp_instance.function_tables[i] = values;

    Handle<FixedArray> metadata = isolate->factory()->NewFixedArray(
        kWasmIndirectFunctionTableDataSize, TENURED);
    metadata->set(kSize, Smi::FromInt(function_tables[i].size));
    metadata->set(kTable, *values);
    indirect_table.ToHandleChecked()->set(i, *metadata);
  }

  HistogramTimerScope wasm_compile_module_time_scope(
      isolate->counters()->wasm_compile_module_time());

  ModuleEnv module_env;
  module_env.module = this;
  module_env.instance = &temp_instance;
  module_env.origin = origin;

  // The {code_table} array contains import wrappers and functions (which
  // are both included in {functions.size()}, and export wrappers.
  int code_table_size =
      static_cast<int>(functions.size() + num_exported_functions);
  Handle<FixedArray> code_table =
      factory->NewFixedArray(static_cast<int>(code_table_size), TENURED);

  // Initialize the code table with placeholders.
  for (uint32_t i = 0; i < functions.size(); ++i) {
    Code::Kind kind = Code::WASM_FUNCTION;
    if (i < num_imported_functions) kind = Code::WASM_TO_JS_FUNCTION;
    Handle<Code> placeholder = CreatePlaceholder(factory, i, kind);
    code_table->set(static_cast<int>(i), *placeholder);
    temp_instance.function_code[i] = placeholder;
  }

  isolate->counters()->wasm_functions_per_module()->AddSample(
      static_cast<int>(functions.size()));
  if (!FLAG_trace_wasm_decoder && FLAG_wasm_num_compilation_tasks != 0) {
    // Avoid a race condition by collecting results into a second vector.
    std::vector<Handle<Code>> results;
    results.reserve(temp_instance.function_code.size());
    for (size_t i = 0; i < temp_instance.function_code.size(); ++i) {
      results.push_back(temp_instance.function_code[i]);
    }
    CompileInParallel(isolate, this, results, thrower, &module_env);

    for (size_t i = 0; i < results.size(); ++i) {
      temp_instance.function_code[i] = results[i];
    }
  } else {
    CompileSequentially(isolate, this, temp_instance.function_code, thrower,
                        &module_env);
  }
  if (thrower->error()) return nothing;

  // At this point, compilation has completed. Update the code table.
  for (size_t i = FLAG_skip_compiling_wasm_funcs;
       i < temp_instance.function_code.size(); ++i) {
    Code* code = *temp_instance.function_code[i];
    code_table->set(static_cast<int>(i), code);
  }

  // Link the functions in the module.
  for (size_t i = FLAG_skip_compiling_wasm_funcs;
       i < temp_instance.function_code.size(); ++i) {
    Handle<Code> code = temp_instance.function_code[i];
    bool modified = LinkFunction(code, temp_instance.function_code);
    if (modified) {
      // TODO(mtrofin): do we need to flush the cache here?
      Assembler::FlushICache(isolate, code->instruction_start(),
                             code->instruction_size());
    }
  }

  // Create the compiled module object, and populate with compiled functions
  // and information needed at instantiation time. This object needs to be
  // serializable. Instantiation may occur off a deserialized version of this
  // object.
  Handle<WasmCompiledModule> ret =
      WasmCompiledModule::New(isolate, min_mem_pages, globals_size, origin);
  ret->set_code_table(code_table);
  if (!indirect_table.is_null()) {
    ret->set_indirect_function_tables(indirect_table.ToHandleChecked());
  }

  // Create and set import data.
  Handle<FixedArray> imports = EncodeImports(factory, this);
  ret->set_imports(imports);

  // Create and set export data.
  int export_size = static_cast<int>(export_table.size());
  if (export_size > 0) {
    Handle<FixedArray> exports = factory->NewFixedArray(export_size, TENURED);
    int index = 0;
    int func_index = 0;

    for (const WasmExport& exp : export_table) {
      if (thrower->error()) return nothing;
      Handle<FixedArray> encoded_export =
          factory->NewFixedArray(kWasmExportDataSize, TENURED);
      WasmName str = GetName(exp.name_offset, exp.name_length);
      Handle<String> name = factory->InternalizeUtf8String(str);
      encoded_export->set(kExportKind, Smi::FromInt(exp.kind));
      encoded_export->set(kExportName, *name);
      encoded_export->set(kExportIndex,
                          Smi::FromInt(static_cast<int>(exp.index)));
      exports->set(index, *encoded_export);

      switch (exp.kind) {
        case kExternalFunction: {
          // Copy the signature and arity.
          FunctionSig* funcSig = functions[exp.index].sig;
          Handle<ByteArray> exportedSig = factory->NewByteArray(
              static_cast<int>(funcSig->parameter_count() +
                               funcSig->return_count()),
              TENURED);
          exportedSig->copy_in(
              0, reinterpret_cast<const byte*>(funcSig->raw_data()),
              exportedSig->length());
          encoded_export->set(kExportedSignature, *exportedSig);
          encoded_export->set(
              kExportArity,
              Smi::FromInt(static_cast<int>(funcSig->parameter_count())));

          // Compile a wrapper for an exported function.
          Handle<Code> code =
              code_table->GetValueChecked<Code>(isolate, exp.index);
          Handle<Code> export_code = compiler::CompileJSToWasmWrapper(
              isolate, &module_env, code, exp.index);
          int code_table_index =
              static_cast<int>(functions.size() + func_index);
          code_table->set(code_table_index, *export_code);
          encoded_export->set(kExportIndex, Smi::FromInt(code_table_index));
          ++func_index;
        }
        case kExternalTable:
          // Nothing special about exported tables.
          break;
        case kExternalMemory:
          // Nothing special about exported tables.
          break;
        case kExternalGlobal: {
          // Encode the global type and the global offset.
          const WasmGlobal& global = globals[exp.index];
          encoded_export->set(
              kExportGlobalType,
              Smi::FromInt(WasmOpcodes::LocalTypeCodeFor(global.type)));
          encoded_export->set(kExportIndex, Smi::FromInt(global.offset));
          break;
        }
      }
      ++index;
    }
    ret->set_exports(exports);
  }

  // Create and set init data.
  int init_size = static_cast<int>(globals.size());
  if (init_size > 0) {
    Handle<FixedArray> inits = factory->NewFixedArray(init_size, TENURED);
    int index = 0;
    for (const WasmGlobal& global : globals) {
      // Skip globals that have no initializer (e.g. imported ones).
      if (global.init.kind == WasmInitExpr::kNone) continue;

      Handle<FixedArray> encoded_init =
          factory->NewFixedArray(kWasmGlobalInitDataSize, TENURED);
      inits->set(index, *encoded_init);
      TRACE("init[%d].type = %s\n", index, WasmOpcodes::TypeName(global.type));

      encoded_init->set(
          kGlobalInitType,
          Smi::FromInt(WasmOpcodes::LocalTypeCodeFor(global.type)));
      encoded_init->set(kGlobalInitIndex, Smi::FromInt(global.offset));
      EncodeInit(this, factory, encoded_init, kGlobalInitKind, kGlobalInitValue,
                 global.init);
      ++index;
    }
    inits->Shrink(index);
    ret->set_inits(inits);
  }

  // Record data for startup function.
  if (start_function_index >= 0) {
    HandleScope scope(isolate);
    Handle<FixedArray> startup_data =
        factory->NewFixedArray(kWasmExportDataSize, TENURED);
    startup_data->set(kExportArity, Smi::FromInt(0));
    startup_data->set(kExportIndex, Smi::FromInt(start_function_index));
    ret->set_startup_function(startup_data);
  }

  // TODO(wasm): saving the module bytes for debugging is wasteful. We should
  // consider downloading this on-demand.
  {
    size_t module_bytes_len = module_end - module_start;
    DCHECK_LE(module_bytes_len, static_cast<size_t>(kMaxInt));
    Vector<const uint8_t> module_bytes_vec(module_start,
                                           static_cast<int>(module_bytes_len));
    Handle<String> module_bytes_string =
        factory->NewStringFromOneByte(module_bytes_vec, TENURED)
            .ToHandleChecked();
    ret->set_module_bytes(module_bytes_string);
  }

  Handle<ByteArray> function_name_table =
      BuildFunctionNamesTable(isolate, module_env.module);
  ret->set_function_names(function_name_table);
  if (data_segments.size() > 0) SaveDataSegmentInfo(factory, this, ret);
  DCHECK_EQ(ret->default_mem_size(), temp_instance.mem_size);
  return ret;
}

// A helper class to simplify instantiating a module from a compiled module.
// It closes over the {Isolate}, the {ErrorThrower}, the {WasmCompiledModule},
// etc.
class WasmInstanceBuilder {
 public:
  WasmInstanceBuilder(Isolate* isolate, ErrorThrower* thrower,
                      Handle<JSObject> module_object, Handle<JSReceiver> ffi,
                      Handle<JSArrayBuffer> memory)
      : isolate_(isolate),
        thrower_(thrower),
        module_object_(module_object),
        ffi_(ffi),
        memory_(memory) {}

  // Build an instance, in all of its glory.
  MaybeHandle<JSObject> Build() {
    MaybeHandle<JSObject> nothing;
    HistogramTimerScope wasm_instantiate_module_time_scope(
        isolate_->counters()->wasm_instantiate_module_time());
    Factory* factory = isolate_->factory();

    //--------------------------------------------------------------------------
    // Reuse the compiled module (if no owner), otherwise clone.
    //--------------------------------------------------------------------------
    Handle<FixedArray> code_table;
    Handle<FixedArray> old_code_table;
    Handle<JSObject> owner;
    // If we don't clone, this will be null(). Otherwise, this will
    // be a weak link to the original. If we lose the original to GC,
    // this will be a cleared. We'll link the instances chain last.
    MaybeHandle<WeakCell> link_to_original;

    TRACE("Starting new module instantiation\n");
    {
      Handle<WasmCompiledModule> original(
          WasmCompiledModule::cast(module_object_->GetInternalField(0)),
          isolate_);
      // Always make a new copy of the code_table, since the old_code_table
      // may still have placeholders for imports.
      old_code_table = original->code_table();
      code_table = factory->CopyFixedArray(old_code_table);

      if (original->has_weak_owning_instance()) {
        WeakCell* tmp = original->ptr_to_weak_owning_instance();
        DCHECK(!tmp->cleared());
        // There is already an owner, clone everything.
        owner = Handle<JSObject>(JSObject::cast(tmp->value()), isolate_);
        // Insert the latest clone in front.
        TRACE("Cloning from %d\n", original->instance_id());
        compiled_module_ = WasmCompiledModule::Clone(isolate_, original);
        // Replace the strong reference to point to the new instance here.
        // This allows any of the other instances, including the original,
        // to be collected.
        module_object_->SetInternalField(0, *compiled_module_);
        compiled_module_->set_weak_module_object(
            original->weak_module_object());
        link_to_original = factory->NewWeakCell(original);
        // Don't link to original here. We remember the original
        // as a weak link. If that link isn't clear by the time we finish
        // instantiating this instance, then we link it at that time.
        compiled_module_->reset_weak_next_instance();

        // Clone the code for WASM functions and exports.
        for (int i = 0; i < code_table->length(); ++i) {
          Handle<Code> orig_code =
              code_table->GetValueChecked<Code>(isolate_, i);
          switch (orig_code->kind()) {
            case Code::WASM_TO_JS_FUNCTION:
              // Imports will be overwritten with newly compiled wrappers.
              break;
            case Code::JS_TO_WASM_FUNCTION:
            case Code::WASM_FUNCTION: {
              Handle<Code> code = factory->CopyCode(orig_code);
              code_table->set(i, *code);
              break;
            }
            default:
              UNREACHABLE();
          }
        }
        RecordStats(isolate_, code_table);
      } else {
        // There was no owner, so we can reuse the original.
        compiled_module_ = original;
        TRACE("Reusing existing instance %d\n",
              compiled_module_->instance_id());
      }
      compiled_module_->set_code_table(code_table);
    }

    //--------------------------------------------------------------------------
    // Allocate the instance object.
    //--------------------------------------------------------------------------
    Handle<Map> map = factory->NewMap(
        JS_OBJECT_TYPE,
        JSObject::kHeaderSize + kWasmModuleInternalFieldCount * kPointerSize);
    Handle<JSObject> instance = factory->NewJSObjectFromMap(map, TENURED);
    instance->SetInternalField(kWasmModuleCodeTable, *code_table);

    //--------------------------------------------------------------------------
    // Set up the memory for the new instance.
    //--------------------------------------------------------------------------
    MaybeHandle<JSArrayBuffer> old_memory;
    // TODO(titzer): handle imported memory properly.

    uint32_t min_mem_pages = compiled_module_->min_memory_pages();
    isolate_->counters()->wasm_min_mem_pages_count()->AddSample(min_mem_pages);
    // TODO(wasm): re-enable counter for max_mem_pages when we use that field.

    if (memory_.is_null() && min_mem_pages > 0) {
      memory_ = AllocateMemory(min_mem_pages);
      if (memory_.is_null()) return nothing;  // failed to allocate memory
    }

    if (!memory_.is_null()) {
      instance->SetInternalField(kWasmMemArrayBuffer, *memory_);
      Address mem_start = static_cast<Address>(memory_->backing_store());
      uint32_t mem_size =
          static_cast<uint32_t>(memory_->byte_length()->Number());
      LoadDataSegments(mem_start, mem_size);

      uint32_t old_mem_size = compiled_module_->has_heap()
                                  ? compiled_module_->mem_size()
                                  : compiled_module_->default_mem_size();
      Address old_mem_start =
          compiled_module_->has_heap()
              ? static_cast<Address>(compiled_module_->heap()->backing_store())
              : nullptr;
      RelocateInstanceCode(instance, old_mem_start, mem_start, old_mem_size,
                           mem_size);
      compiled_module_->set_heap(memory_);
    }

    //--------------------------------------------------------------------------
    // Set up the globals for the new instance.
    //--------------------------------------------------------------------------
    MaybeHandle<JSArrayBuffer> old_globals;
    MaybeHandle<JSArrayBuffer> globals;
    uint32_t globals_size = compiled_module_->globals_size();
    if (globals_size > 0) {
      Handle<JSArrayBuffer> global_buffer =
          NewArrayBuffer(isolate_, globals_size);
      globals = global_buffer;
      if (globals.is_null()) {
        thrower_->Error("Out of memory: wasm globals");
        return nothing;
      }
      Address old_address =
          owner.is_null() ? nullptr : GetGlobalStartAddressFromCodeTemplate(
                                          *factory->undefined_value(),
                                          JSObject::cast(*owner));
      RelocateGlobals(instance, old_address,
                      static_cast<Address>(global_buffer->backing_store()));
      instance->SetInternalField(kWasmGlobalsArrayBuffer, *global_buffer);
    }

    //--------------------------------------------------------------------------
    // Process the imports for the module.
    //--------------------------------------------------------------------------
    int num_imported_functions = ProcessImports(globals, code_table);
    if (num_imported_functions < 0) return nothing;

    //--------------------------------------------------------------------------
    // Process the initialization for the module's globals.
    //--------------------------------------------------------------------------
    ProcessInits(globals);

    //--------------------------------------------------------------------------
    // Set up the debug support for the new instance.
    //--------------------------------------------------------------------------
    // TODO(wasm): avoid referencing this stuff from the instance, use it off
    // the compiled module instead. See the following 3 assignments:
    if (compiled_module_->has_module_bytes()) {
      instance->SetInternalField(kWasmModuleBytesString,
                                 compiled_module_->ptr_to_module_bytes());
    }

    if (compiled_module_->has_function_names()) {
      instance->SetInternalField(kWasmFunctionNamesArray,
                                 compiled_module_->ptr_to_function_names());
    }

    {
      Handle<Object> handle = factory->NewNumber(num_imported_functions);
      instance->SetInternalField(kWasmNumImportedFunctions, *handle);
    }

    //--------------------------------------------------------------------------
    // Set up the runtime support for the new instance.
    //--------------------------------------------------------------------------
    Handle<WeakCell> weak_link = factory->NewWeakCell(instance);

    for (int i = num_imported_functions + FLAG_skip_compiling_wasm_funcs;
         i < code_table->length(); ++i) {
      Handle<Code> code = code_table->GetValueChecked<Code>(isolate_, i);
      if (code->kind() == Code::WASM_FUNCTION) {
        Handle<FixedArray> deopt_data = factory->NewFixedArray(2, TENURED);
        deopt_data->set(0, *weak_link);
        deopt_data->set(1, Smi::FromInt(static_cast<int>(i)));
        deopt_data->set_length(2);
        code->set_deoptimization_data(*deopt_data);
      }
    }

    //--------------------------------------------------------------------------
    // Set up the indirect function tables for the new instance.
    //--------------------------------------------------------------------------
    {
      std::vector<Handle<Code>> functions(
          static_cast<size_t>(code_table->length()));
      for (int i = 0; i < code_table->length(); ++i) {
        functions[i] = code_table->GetValueChecked<Code>(isolate_, i);
      }

      if (compiled_module_->has_indirect_function_tables()) {
        Handle<FixedArray> indirect_tables_template =
            compiled_module_->indirect_function_tables();
        Handle<FixedArray> to_replace =
            owner.is_null() ? indirect_tables_template
                            : handle(FixedArray::cast(owner->GetInternalField(
                                  kWasmModuleFunctionTable)));
        Handle<FixedArray> indirect_tables = SetupIndirectFunctionTable(
            isolate_, code_table, indirect_tables_template, to_replace);
        for (int i = 0; i < indirect_tables->length(); ++i) {
          Handle<FixedArray> metadata =
              indirect_tables->GetValueChecked<FixedArray>(isolate_, i);
          uint32_t size = Smi::cast(metadata->get(kSize))->value();
          Handle<FixedArray> table =
              metadata->GetValueChecked<FixedArray>(isolate_, kTable);
          PopulateFunctionTable(table, size, &functions);
        }
        instance->SetInternalField(kWasmModuleFunctionTable, *indirect_tables);
      }
    }

    //--------------------------------------------------------------------------
    // Set up the exports object for the new instance.
    //--------------------------------------------------------------------------
    ProcessExports(globals, code_table, instance);

    if (num_imported_functions > 0 || !owner.is_null()) {
      // If the code was cloned, or new imports were compiled, patch.
      PatchDirectCalls(old_code_table, code_table, num_imported_functions);
    }

    FlushICache(isolate_, code_table);

    //--------------------------------------------------------------------------
    // Run the start function if one was specified.
    //--------------------------------------------------------------------------
    if (compiled_module_->has_startup_function()) {
      Handle<FixedArray> startup_data = compiled_module_->startup_function();
      HandleScope scope(isolate_);
      int32_t start_index =
          startup_data->GetValueChecked<Smi>(isolate_, kExportIndex)->value();
      Handle<Code> startup_code =
          code_table->GetValueChecked<Code>(isolate_, start_index);
      int arity = Smi::cast(startup_data->get(kExportArity))->value();
      MaybeHandle<ByteArray> startup_signature =
          startup_data->GetValue<ByteArray>(isolate_, kExportedSignature);
      Handle<JSFunction> startup_fct = WrapExportCodeAsJSFunction(
          isolate_, startup_code, factory->InternalizeUtf8String("start"),
          arity, startup_signature, instance);
      RecordStats(isolate_, *startup_code);
      // Call the JS function.
      Handle<Object> undefined = factory->undefined_value();
      MaybeHandle<Object> retval =
          Execution::Call(isolate_, startup_fct, undefined, 0, nullptr);

      if (retval.is_null()) {
        thrower_->Error("WASM.instantiateModule(): start function failed");
        return nothing;
      }
    }

    DCHECK(wasm::IsWasmObject(*instance));

    {
      Handle<WeakCell> link_to_owner = factory->NewWeakCell(instance);

      Handle<Object> global_handle =
          isolate_->global_handles()->Create(*instance);
      Handle<WeakCell> link_to_clone = factory->NewWeakCell(compiled_module_);
      {
        DisallowHeapAllocation no_gc;
        compiled_module_->set_weak_owning_instance(link_to_owner);
        Handle<WeakCell> next;
        if (link_to_original.ToHandle(&next) && !next->cleared()) {
          WasmCompiledModule* original =
              WasmCompiledModule::cast(next->value());
          DCHECK(original->has_weak_owning_instance());
          DCHECK(!original->weak_owning_instance()->cleared());
          compiled_module_->set_weak_next_instance(next);
          original->set_weak_prev_instance(link_to_clone);
        }

        compiled_module_->set_weak_owning_instance(link_to_owner);
        instance->SetInternalField(kWasmCompiledModule, *compiled_module_);
        GlobalHandles::MakeWeak(global_handle.location(),
                                global_handle.location(), &InstanceFinalizer,
                                v8::WeakCallbackType::kFinalizer);
      }
    }
    TRACE("Finishing instance %d\n", compiled_module_->instance_id());
    TRACE_CHAIN(WasmCompiledModule::cast(module_object_->GetInternalField(0)));
    return instance;
  }

 private:
  Isolate* isolate_;
  ErrorThrower* thrower_;
  Handle<JSObject> module_object_;
  Handle<JSReceiver> ffi_;
  Handle<JSArrayBuffer> memory_;
  Handle<WasmCompiledModule> compiled_module_;

  // Helper routine to print out errors with imports (FFI).
  MaybeHandle<JSFunction> ReportFFIError(const char* error, uint32_t index,
                                         Handle<String> module_name,
                                         MaybeHandle<String> function_name) {
    Handle<String> function_name_handle;
    if (function_name.ToHandle(&function_name_handle)) {
      thrower_->Error("Import #%d module=\"%.*s\" function=\"%.*s\" error: %s",
                      index, module_name->length(),
                      module_name->ToCString().get(),
                      function_name_handle->length(),
                      function_name_handle->ToCString().get(), error);
    } else {
      thrower_->Error("Import #%d module=\"%.*s\" error: %s", index,
                      module_name->length(), module_name->ToCString().get(),
                      error);
    }
    thrower_->Error("Import ");
    return MaybeHandle<JSFunction>();
  }

  // Look up an import value in the {ffi_} object.
  MaybeHandle<Object> LookupImport(uint32_t index, Handle<String> module_name,
                                   MaybeHandle<String> import_name) {
    if (ffi_.is_null()) {
      return ReportFFIError("FFI is not an object", index, module_name,
                            import_name);
    }

    // Look up the module first.
    MaybeHandle<Object> result = Object::GetProperty(ffi_, module_name);
    if (result.is_null()) {
      return ReportFFIError("module not found", index, module_name,
                            import_name);
    }

    Handle<Object> module = result.ToHandleChecked();

    if (!import_name.is_null()) {
      // Look up the value in the module.
      if (!module->IsJSReceiver()) {
        return ReportFFIError("module is not an object or function", index,
                              module_name, import_name);
      }

      result = Object::GetProperty(module, import_name.ToHandleChecked());
      if (result.is_null()) {
        return ReportFFIError("import not found", index, module_name,
                              import_name);
      }
    } else {
      // No function specified. Use the "default export".
      result = module;
    }

    return result;
  }

  // Load data segments into the memory.
  void LoadDataSegments(Address mem_addr, size_t mem_size) {
    CHECK(compiled_module_->has_data_segments() ==
          compiled_module_->has_data_segments_info());

    // If we have neither, we're done.
    if (!compiled_module_->has_data_segments()) return;

    Handle<ByteArray> data = compiled_module_->data_segments();
    Handle<FixedArray> segments = compiled_module_->data_segments_info();

    uint32_t last_extraction_pos = 0;
    for (int i = 0; i < segments->length(); ++i) {
      Handle<ByteArray> segment =
          Handle<ByteArray>(ByteArray::cast(segments->get(i)));
      uint32_t dest_addr =
          static_cast<uint32_t>(segment->get_int(kDestAddrValue));
      uint32_t source_size =
          static_cast<uint32_t>(segment->get_int(kSourceSize));
      CHECK_LT(dest_addr, mem_size);
      CHECK_LE(source_size, mem_size);
      CHECK_LE(dest_addr, mem_size - source_size);
      byte* addr = mem_addr + dest_addr;
      data->copy_out(last_extraction_pos, addr, source_size);
      last_extraction_pos += source_size;
    }
  }

  Handle<Code> CompileImportWrapper(int index, Handle<FixedArray> data,
                                    Handle<JSReceiver> target,
                                    Handle<String> module_name,
                                    MaybeHandle<String> import_name) {
    // TODO(mtrofin): this is an uint32_t, actually. We should rationalize
    // it when we rationalize signed/unsigned stuff.
    int ret_count = Smi::cast(data->get(kOutputCount))->value();
    CHECK_GE(ret_count, 0);
    Handle<ByteArray> sig_data =
        data->GetValueChecked<ByteArray>(isolate_, kSignature);
    int sig_data_size = sig_data->length();
    int param_count = sig_data_size - ret_count;
    CHECK(param_count >= 0);

    Handle<Code> code;
    bool isMatch = false;
    Handle<Code> export_wrapper_code;
    if (target->IsJSFunction()) {
      Handle<JSFunction> func = Handle<JSFunction>::cast(target);
      export_wrapper_code = handle(func->code());
      if (export_wrapper_code->kind() == Code::JS_TO_WASM_FUNCTION) {
        int exported_param_count =
            Smi::cast(func->GetInternalField(kInternalArity))->value();
        Handle<ByteArray> exportedSig = Handle<ByteArray>(
            ByteArray::cast(func->GetInternalField(kInternalSignature)));
        if (exported_param_count == param_count &&
            exportedSig->length() == sig_data->length() &&
            memcmp(exportedSig->GetDataStartAddress(),
                   sig_data->GetDataStartAddress(),
                   exportedSig->length()) == 0) {
          isMatch = true;
        }
      }
    }
    if (isMatch) {
      int wasm_count = 0;
      int const mask = RelocInfo::ModeMask(RelocInfo::CODE_TARGET);
      for (RelocIterator it(*export_wrapper_code, mask); !it.done();
           it.next()) {
        RelocInfo* rinfo = it.rinfo();
        Address target_address = rinfo->target_address();
        Code* target = Code::GetCodeFromTargetAddress(target_address);
        if (target->kind() == Code::WASM_FUNCTION) {
          ++wasm_count;
          code = handle(target);
        }
      }
      DCHECK(wasm_count == 1);
      return code;
    } else {
      // Copy the signature to avoid a raw pointer into a heap object when
      // GC can happen.
      Zone zone(isolate_->allocator());
      MachineRepresentation* reps =
          zone.NewArray<MachineRepresentation>(sig_data_size);
      memcpy(reps, sig_data->GetDataStartAddress(),
             sizeof(MachineRepresentation) * sig_data_size);
      FunctionSig sig(ret_count, param_count, reps);

      return compiler::CompileWasmToJSWrapper(isolate_, target, &sig, index,
                                              module_name, import_name);
    }
  }

  void WriteGlobalValue(MaybeHandle<JSArrayBuffer> globals, uint32_t offset,
                        Handle<Object> value, int type) {
    double num = 0;
    if (value->IsSmi()) {
      num = Smi::cast(*value)->value();
    } else if (value->IsHeapNumber()) {
      num = HeapNumber::cast(*value)->value();
    } else {
      UNREACHABLE();
    }
    TRACE("init [globals+%u] = %lf, type = %d\n", offset, num, type);
    byte* ptr = raw_buffer_ptr(globals, offset);
    switch (type) {
      case kLocalI32:
        *reinterpret_cast<int32_t*>(ptr) = static_cast<int32_t>(num);
        break;
      case kLocalI64:
        // TODO(titzer): initialization of imported i64 globals.
        UNREACHABLE();
        break;
      case kLocalF32:
        *reinterpret_cast<float*>(ptr) = static_cast<float>(num);
        break;
      case kLocalF64:
        *reinterpret_cast<double*>(ptr) = num;
        break;
      default:
        UNREACHABLE();
    }
  }

  // Process the imports, including functions, tables, globals, and memory, in
  // order, loading them from the {ffi_} object. Returns the number of imported
  // functions.
  int ProcessImports(MaybeHandle<JSArrayBuffer> globals,
                     Handle<FixedArray> code_table) {
    int num_imported_functions = 0;
    if (!compiled_module_->has_imports()) return num_imported_functions;

    Handle<FixedArray> imports = compiled_module_->imports();
    for (int index = 0; index < imports->length(); ++index) {
      Handle<FixedArray> data =
          imports->GetValueChecked<FixedArray>(isolate_, index);

      Handle<String> module_name =
          data->GetValueChecked<String>(isolate_, kModuleName);
      MaybeHandle<String> function_name =
          data->GetValue<String>(isolate_, kFunctionName);

      MaybeHandle<Object> result =
          LookupImport(index, module_name, function_name);
      if (thrower_->error()) return -1;

      WasmExternalKind kind = static_cast<WasmExternalKind>(
          Smi::cast(data->get(kImportKind))->value());
      switch (kind) {
        case kExternalFunction: {
          // Function imports must be callable.
          Handle<Object> function = result.ToHandleChecked();
          if (!function->IsCallable()) {
            ReportFFIError("function import requires a callable", index,
                           module_name, function_name);
            return -1;
          }

          Handle<Code> import_wrapper = CompileImportWrapper(
              index, data, Handle<JSReceiver>::cast(function), module_name,
              function_name);
          int func_index = Smi::cast(data->get(kImportIndex))->value();
          code_table->set(func_index, *import_wrapper);
          RecordStats(isolate_, *import_wrapper);
          num_imported_functions++;
          break;
        }
        case kExternalTable:
          // TODO(titzer): Table imports must be a WebAssembly.Table.
          break;
        case kExternalMemory:
          // TODO(titzer): Memory imports must be a WebAssembly.Memory.
          break;
        case kExternalGlobal: {
          // Global imports are converted to numbers and written into the
          // {globals} array buffer.
          Handle<Object> object = result.ToHandleChecked();
          MaybeHandle<Object> number = Object::ToNumber(object);
          if (number.is_null()) {
            ReportFFIError("global import could not be converted to number",
                           index, module_name, function_name);
            return -1;
          }
          Handle<Object> val = number.ToHandleChecked();
          int offset = Smi::cast(data->get(kImportIndex))->value();
          int type = Smi::cast(data->get(kImportGlobalType))->value();
          WriteGlobalValue(globals, offset, val, type);
          break;
        }
        default:
          UNREACHABLE();
          break;
      }
    }
    return num_imported_functions;
  }

  // Process initialization of globals.
  void ProcessInits(MaybeHandle<JSArrayBuffer> globals) {
    if (!compiled_module_->has_inits()) return;

    Handle<FixedArray> inits = compiled_module_->inits();
    for (int index = 0; index < inits->length(); ++index) {
      Handle<FixedArray> data =
          inits->GetValueChecked<FixedArray>(isolate_, index);

      int offset = Smi::cast(data->get(kGlobalInitIndex))->value();
      Handle<Object> val(data->get(kGlobalInitValue), isolate_);
      int type = Smi::cast(data->get(kGlobalInitType))->value();
      if (Smi::cast(data->get(kGlobalInitKind))->value() == 0) {
        // Initialize with a constant.
        WriteGlobalValue(globals, offset, val, type);
      } else {
        // Initialize with another global.
        int old_offset = Smi::cast(*val)->value();
        TRACE("init [globals+%u] = [globals+%d]\n", offset, old_offset);
        int size = sizeof(int32_t);
        if (type == kLocalI64 || type == kLocalF64) size = sizeof(double);
        memcpy(raw_buffer_ptr(globals, offset),
               raw_buffer_ptr(globals, old_offset), size);
      }
    }
  }

  // Allocate memory for a module instance as a new JSArrayBuffer.
  Handle<JSArrayBuffer> AllocateMemory(uint32_t min_mem_pages) {
    if (min_mem_pages > WasmModule::kMaxMemPages) {
      thrower_->Error("Out of memory: wasm memory too large");
      return Handle<JSArrayBuffer>::null();
    }
    Handle<JSArrayBuffer> mem_buffer =
        NewArrayBuffer(isolate_, min_mem_pages * WasmModule::kPageSize);

    if (mem_buffer.is_null()) {
      thrower_->Error("Out of memory: wasm memory");
    }
    return mem_buffer;
  }

  // Process the exports, creating wrappers for functions, tables, memories,
  // and globals.
  void ProcessExports(MaybeHandle<JSArrayBuffer> globals,
                      Handle<FixedArray> code_table,
                      Handle<JSObject> instance) {
    if (!compiled_module_->has_exports()) return;

    Handle<JSObject> exports_object = instance;
    if (compiled_module_->origin() == kWasmOrigin) {
      // Create the "exports" object.
      Handle<JSFunction> object_function = Handle<JSFunction>(
          isolate_->native_context()->object_function(), isolate_);
      exports_object =
          isolate_->factory()->NewJSObject(object_function, TENURED);
      Handle<String> exports_name =
          isolate_->factory()->InternalizeUtf8String("exports");
      JSObject::AddProperty(instance, exports_name, exports_object, READ_ONLY);
    }

    PropertyDescriptor desc;
    desc.set_writable(false);

    Handle<FixedArray> exports = compiled_module_->exports();

    for (int i = 0; i < exports->length(); ++i) {
      Handle<FixedArray> export_data =
          exports->GetValueChecked<FixedArray>(isolate_, i);
      Handle<String> name =
          export_data->GetValueChecked<String>(isolate_, kExportName);
      WasmExternalKind kind = static_cast<WasmExternalKind>(
          Smi::cast(export_data->get(kExportKind))->value());
      switch (kind) {
        case kExternalFunction: {
          // Wrap and export the code as a JSFunction.
          int code_table_index =
              Smi::cast(export_data->get(kExportIndex))->value();
          Handle<Code> export_code =
              code_table->GetValueChecked<Code>(isolate_, code_table_index);
          int arity = Smi::cast(export_data->get(kExportArity))->value();
          MaybeHandle<ByteArray> signature =
              export_data->GetValue<ByteArray>(isolate_, kExportedSignature);
          desc.set_value(WrapExportCodeAsJSFunction(
              isolate_, export_code, name, arity, signature, instance));
          break;
        }
        case kExternalTable:
          // TODO(titzer): create a WebAssembly.Table instance.
          // TODO(titzer): should it have the same identity as an import?
          break;
        case kExternalMemory: {
          // TODO(titzer): should memory have the same identity as an
          // import?
          Handle<JSArrayBuffer> buffer =
              Handle<JSArrayBuffer>(JSArrayBuffer::cast(
                  instance->GetInternalField(kWasmMemArrayBuffer)));
          desc.set_value(
              WasmJs::CreateWasmMemoryObject(isolate_, buffer, false, 0));
          break;
        }
        case kExternalGlobal: {
          // Export the value of the global variable as a number.
          int offset = Smi::cast(export_data->get(kExportIndex))->value();
          byte* ptr = raw_buffer_ptr(globals, offset);
          double num = 0;
          switch (Smi::cast(export_data->get(kExportGlobalType))->value()) {
            case kLocalI32:
              num = *reinterpret_cast<int32_t*>(ptr);
              break;
            case kLocalF32:
              num = *reinterpret_cast<float*>(ptr);
              break;
            case kLocalF64:
              num = *reinterpret_cast<double*>(ptr);
              break;
            default:
              UNREACHABLE();
          }
          desc.set_value(isolate_->factory()->NewNumber(num));
          break;
        }
        default:
          UNREACHABLE();
          break;
      }

      Maybe<bool> status = JSReceiver::DefineOwnProperty(
          isolate_, exports_object, name, &desc, Object::THROW_ON_ERROR);
      if (!status.IsJust()) {
        thrower_->Error("export of %.*s failed.", name->length(),
                        name->ToCString().get());
        return;
      }
    }
  }
};

// Instantiates a WASM module, creating a WebAssembly.Instance from a
// WebAssembly.Module.
MaybeHandle<JSObject> WasmModule::Instantiate(Isolate* isolate,
                                              ErrorThrower* thrower,
                                              Handle<JSObject> module_object,
                                              Handle<JSReceiver> ffi,
                                              Handle<JSArrayBuffer> memory) {
  WasmInstanceBuilder builder(isolate, thrower, module_object, ffi, memory);
  return builder.Build();
}

Handle<WasmCompiledModule> WasmCompiledModule::New(Isolate* isolate,
                                                   uint32_t min_memory_pages,
                                                   uint32_t globals_size,
                                                   ModuleOrigin origin) {
  Handle<FixedArray> ret =
      isolate->factory()->NewFixedArray(PropertyIndices::Count, TENURED);
  // Globals size is expected to fit into an int without overflow. This is not
  // supported by the spec at the moment, however, we don't support array
  // buffer sizes over 1g, so, for now, we avoid alocating a HeapNumber for
  // the globals size. The CHECK guards this assumption.
  CHECK_GE(static_cast<int>(globals_size), 0);
  ret->set(kID_min_memory_pages,
           Smi::FromInt(static_cast<int>(min_memory_pages)));
  ret->set(kID_globals_size, Smi::FromInt(static_cast<int>(globals_size)));
  ret->set(kID_origin, Smi::FromInt(static_cast<int>(origin)));
  WasmCompiledModule::cast(*ret)->Init();
  return handle(WasmCompiledModule::cast(*ret));
}

void WasmCompiledModule::Init() {
#if DEBUG
  static uint32_t instance_id_counter = 0;
  set(kID_instance_id, Smi::FromInt(instance_id_counter++));
  TRACE("New compiled module id: %d\n", instance_id());
#endif
}

void WasmCompiledModule::PrintInstancesChain() {
#if DEBUG
  if (!FLAG_trace_wasm_instances) return;
  for (WasmCompiledModule* current = this; current != nullptr;) {
    PrintF("->%d", current->instance_id());
    if (current->ptr_to_weak_next_instance() == nullptr) break;
    CHECK(!current->ptr_to_weak_next_instance()->cleared());
    current =
        WasmCompiledModule::cast(current->ptr_to_weak_next_instance()->value());
  }
  PrintF("\n");
#endif
}

Handle<Object> GetWasmFunctionNameOrNull(Isolate* isolate, Handle<Object> wasm,
                                         uint32_t func_index) {
  if (!wasm->IsUndefined(isolate)) {
    Handle<ByteArray> func_names_arr_obj(
        ByteArray::cast(Handle<JSObject>::cast(wasm)->GetInternalField(
            kWasmFunctionNamesArray)),
        isolate);
    // TODO(clemens): Extract this from the module bytes; skip whole function
    // name table.
    Handle<Object> name;
    if (GetWasmFunctionNameFromTable(func_names_arr_obj, func_index)
            .ToHandle(&name)) {
      return name;
    }
  }
  return isolate->factory()->null_value();
}

Handle<String> GetWasmFunctionName(Isolate* isolate, Handle<Object> wasm,
                                   uint32_t func_index) {
  Handle<Object> name_or_null =
      GetWasmFunctionNameOrNull(isolate, wasm, func_index);
  if (!name_or_null->IsNull(isolate)) {
    return Handle<String>::cast(name_or_null);
  }
  return isolate->factory()->NewStringFromStaticChars("<WASM UNNAMED>");
}

bool IsWasmObject(Object* object) {
  if (!object->IsJSObject()) return false;

  JSObject* obj = JSObject::cast(object);
  Isolate* isolate = obj->GetIsolate();
  if (obj->GetInternalFieldCount() != kWasmModuleInternalFieldCount) {
    return false;
  }

  Object* mem = obj->GetInternalField(kWasmMemArrayBuffer);
  if (obj->GetInternalField(kWasmModuleCodeTable)->IsFixedArray() &&
      (mem->IsUndefined(isolate) || mem->IsJSArrayBuffer()) &&
      obj->GetInternalField(kWasmFunctionNamesArray)->IsByteArray()) {
    Object* debug_bytes = obj->GetInternalField(kWasmModuleBytesString);
    if (!debug_bytes->IsUndefined(isolate)) {
      if (!debug_bytes->IsSeqOneByteString()) {
        return false;
      }
      DisallowHeapAllocation no_gc;
      SeqOneByteString* bytes = SeqOneByteString::cast(debug_bytes);
      if (bytes->length() < 4) return false;
      if (memcmp(bytes->GetChars(), "\0asm", 4)) return false;
      // All checks passed.
    }
    return true;
  }
  return false;
}

SeqOneByteString* GetWasmBytes(JSObject* wasm) {
  return SeqOneByteString::cast(wasm->GetInternalField(kWasmModuleBytesString));
}

Handle<WasmDebugInfo> GetDebugInfo(Handle<JSObject> wasm) {
  Handle<Object> info(wasm->GetInternalField(kWasmDebugInfo),
                      wasm->GetIsolate());
  if (!info->IsUndefined(wasm->GetIsolate()))
    return Handle<WasmDebugInfo>::cast(info);
  Handle<WasmDebugInfo> new_info = WasmDebugInfo::New(wasm);
  wasm->SetInternalField(kWasmDebugInfo, *new_info);
  return new_info;
}

bool UpdateWasmModuleMemory(Handle<JSObject> object, Address old_start,
                            Address new_start, uint32_t old_size,
                            uint32_t new_size) {
  DisallowHeapAllocation no_allocation;
  if (!IsWasmObject(*object)) {
    return false;
  }

  // Get code table associated with the module js_object
  Object* obj = object->GetInternalField(kWasmModuleCodeTable);
  Handle<FixedArray> code_table(FixedArray::cast(obj));

  // Iterate through the code objects in the code table and update relocation
  // information
  for (int i = 0; i < code_table->length(); ++i) {
    obj = code_table->get(i);
    Handle<Code> code(Code::cast(obj));

    int mode_mask = RelocInfo::ModeMask(RelocInfo::WASM_MEMORY_REFERENCE) |
                    RelocInfo::ModeMask(RelocInfo::WASM_MEMORY_SIZE_REFERENCE);
    for (RelocIterator it(*code, mode_mask); !it.done(); it.next()) {
      RelocInfo::Mode mode = it.rinfo()->rmode();
      if (RelocInfo::IsWasmMemoryReference(mode) ||
          RelocInfo::IsWasmMemorySizeReference(mode)) {
        it.rinfo()->update_wasm_memory_reference(old_start, new_start, old_size,
                                                 new_size);
      }
    }
  }
  return true;
}

Handle<FixedArray> BuildFunctionTable(Isolate* isolate, uint32_t index,
                                      const WasmModule* module) {
  const WasmIndirectFunctionTable* table = &module->function_tables[index];
  DCHECK_EQ(table->size, table->values.size());
  DCHECK_GE(table->max_size, table->size);
  Handle<FixedArray> values =
      isolate->factory()->NewFixedArray(2 * table->max_size, TENURED);
  for (uint32_t i = 0; i < table->size; ++i) {
    const WasmFunction* function = &module->functions[table->values[i]];
    values->set(i, Smi::FromInt(function->sig_index));
    values->set(i + table->max_size, Smi::FromInt(table->values[i]));
  }
  // Set the remaining elements to -1 (instead of "undefined"). These
  // elements are accessed directly as SMIs (without a check). On 64-bit
  // platforms, it is possible to have the top bits of "undefined" take
  // small integer values (or zero), which are more likely to be equal to
  // the signature index we check against.
  for (uint32_t i = table->size; i < table->max_size; ++i) {
    values->set(i, Smi::FromInt(-1));
  }
  return values;
}

void PopulateFunctionTable(Handle<FixedArray> table, uint32_t table_size,
                           const std::vector<Handle<Code>>* code_table) {
  uint32_t max_size = table->length() / 2;
  for (uint32_t i = max_size; i < max_size + table_size; ++i) {
    int index = Smi::cast(table->get(static_cast<int>(i)))->value();
    DCHECK_GE(index, 0);
    DCHECK_LT(static_cast<size_t>(index), code_table->size());
    table->set(static_cast<int>(i), *(*code_table)[index]);
  }
}

int GetNumberOfFunctions(JSObject* wasm) {
  Object* func_names_obj = wasm->GetInternalField(kWasmFunctionNamesArray);
  // TODO(clemensh): this looks inside an array constructed elsewhere. Refactor.
  return ByteArray::cast(func_names_obj)->get_int(0);
}

Handle<JSObject> CreateCompiledModuleObject(Isolate* isolate,
                                            Handle<FixedArray> compiled_module,
                                            ModuleOrigin origin) {
  Handle<JSObject> module_obj;
  if (origin == ModuleOrigin::kWasmOrigin) {
    Handle<JSFunction> module_cons(
        isolate->native_context()->wasm_module_constructor());
    module_obj = isolate->factory()->NewJSObject(module_cons);
  } else {
    DCHECK(origin == ModuleOrigin::kAsmJsOrigin);
    Handle<Map> map = isolate->factory()->NewMap(
        JS_OBJECT_TYPE, JSObject::kHeaderSize + kPointerSize);
    module_obj = isolate->factory()->NewJSObjectFromMap(map, TENURED);
  }
  module_obj->SetInternalField(0, *compiled_module);
  if (origin == ModuleOrigin::kWasmOrigin) {
    Handle<Symbol> module_sym(isolate->native_context()->wasm_module_sym());
    Object::SetProperty(module_obj, module_sym, module_obj, STRICT).Check();
  }
  Handle<WeakCell> link_to_module = isolate->factory()->NewWeakCell(module_obj);
  WasmCompiledModule::cast(*compiled_module)
      ->set_weak_module_object(link_to_module);
  return module_obj;
}

MaybeHandle<JSObject> CreateModuleObjectFromBytes(Isolate* isolate,
                                                  const byte* start,
                                                  const byte* end,
                                                  ErrorThrower* thrower,
                                                  ModuleOrigin origin) {
  MaybeHandle<JSObject> nothing;
  Zone zone(isolate->allocator());
  ModuleResult result =
      DecodeWasmModule(isolate, &zone, start, end, false, origin);
  std::unique_ptr<const WasmModule> decoded_module(result.val);
  if (result.failed()) {
    thrower->Failed("Wasm decoding failed", result);
    return nothing;
  }
  MaybeHandle<FixedArray> compiled_module =
      decoded_module->CompileFunctions(isolate, thrower);
  if (compiled_module.is_null()) return nothing;

  return CreateCompiledModuleObject(isolate, compiled_module.ToHandleChecked(),
                                    origin);
}

bool ValidateModuleBytes(Isolate* isolate, const byte* start, const byte* end,
                         ErrorThrower* thrower, ModuleOrigin origin) {
  Zone zone(isolate->allocator());
  ModuleResult result =
      DecodeWasmModule(isolate, &zone, start, end, false, origin);
  if (result.ok()) {
    DCHECK_NOT_NULL(result.val);
    delete result.val;
    return true;
  }
  return false;
}

MaybeHandle<JSArrayBuffer> GetInstanceMemory(Isolate* isolate,
                                             Handle<JSObject> instance) {
  Object* mem = instance->GetInternalField(kWasmMemArrayBuffer);
  DCHECK(IsWasmObject(*instance));
  if (mem->IsUndefined(isolate)) return MaybeHandle<JSArrayBuffer>();
  return Handle<JSArrayBuffer>(JSArrayBuffer::cast(mem));
}

void SetInstanceMemory(Handle<JSObject> instance, JSArrayBuffer* buffer) {
  DisallowHeapAllocation no_gc;
  DCHECK(IsWasmObject(*instance));
  instance->SetInternalField(kWasmMemArrayBuffer, buffer);
  WasmCompiledModule* module =
      WasmCompiledModule::cast(instance->GetInternalField(kWasmCompiledModule));
  module->set_ptr_to_heap(buffer);
}

int32_t GetInstanceMemorySize(Isolate* isolate, Handle<JSObject> instance) {
  MaybeHandle<JSArrayBuffer> maybe_mem_buffer =
      GetInstanceMemory(isolate, instance);
  Handle<JSArrayBuffer> buffer;
  if (!maybe_mem_buffer.ToHandle(&buffer)) {
    return 0;
  } else {
    return buffer->byte_length()->Number() / WasmModule::kPageSize;
  }
}

int32_t GrowInstanceMemory(Isolate* isolate, Handle<JSObject> instance,
                           uint32_t pages) {
  if (pages == 0) {
    return GetInstanceMemorySize(isolate, instance);
  }
  Address old_mem_start = nullptr;
  uint32_t old_size = 0, new_size = 0;

  MaybeHandle<JSArrayBuffer> maybe_mem_buffer =
      GetInstanceMemory(isolate, instance);
  Handle<JSArrayBuffer> old_buffer;
  if (!maybe_mem_buffer.ToHandle(&old_buffer)) {
    // If module object does not have linear memory associated with it,
    // Allocate new array buffer of given size.
    // TODO(gdeepti): Fix bounds check to take into account size of memtype.
    new_size = pages * WasmModule::kPageSize;
    // The code generated in the wasm compiler guarantees this precondition.
    DCHECK(pages <= WasmModule::kMaxMemPages);
  } else {
    old_mem_start = static_cast<Address>(old_buffer->backing_store());
    old_size = old_buffer->byte_length()->Number();
    // If the old memory was zero-sized, we should have been in the
    // "undefined" case above.
    DCHECK_NOT_NULL(old_mem_start);
    DCHECK_NE(0, old_size);
    DCHECK(old_size + pages * WasmModule::kPageSize <=
           std::numeric_limits<uint32_t>::max());
    new_size = old_size + pages * WasmModule::kPageSize;
  }

  if (new_size <= old_size ||
      WasmModule::kMaxMemPages * WasmModule::kPageSize <= new_size) {
    return -1;
  }
  Handle<JSArrayBuffer> buffer = NewArrayBuffer(isolate, new_size);
  if (buffer.is_null()) return -1;
  Address new_mem_start = static_cast<Address>(buffer->backing_store());
  if (old_size != 0) {
    memcpy(new_mem_start, old_mem_start, old_size);
  }
  SetInstanceMemory(instance, *buffer);
  if (!UpdateWasmModuleMemory(instance, old_mem_start, new_mem_start, old_size,
                              new_size)) {
    return -1;
  }
  DCHECK(old_size % WasmModule::kPageSize == 0);
  return (old_size / WasmModule::kPageSize);
}

namespace testing {

void ValidateInstancesChain(Isolate* isolate, Handle<JSObject> module_obj,
                            int instance_count) {
  CHECK_GE(instance_count, 0);
  DisallowHeapAllocation no_gc;
  WasmCompiledModule* compiled_module =
      WasmCompiledModule::cast(module_obj->GetInternalField(0));
  CHECK_EQ(
      JSObject::cast(compiled_module->ptr_to_weak_module_object()->value()),
      *module_obj);
  Object* prev = nullptr;
  int found_instances = compiled_module->has_weak_owning_instance() ? 1 : 0;
  WasmCompiledModule* current_instance = compiled_module;
  while (current_instance->has_weak_next_instance()) {
    CHECK((prev == nullptr && !current_instance->has_weak_prev_instance()) ||
          current_instance->ptr_to_weak_prev_instance()->value() == prev);
    CHECK_EQ(current_instance->ptr_to_weak_module_object()->value(),
             *module_obj);
    CHECK(
        IsWasmObject(current_instance->ptr_to_weak_owning_instance()->value()));
    prev = current_instance;
    current_instance = WasmCompiledModule::cast(
        current_instance->ptr_to_weak_next_instance()->value());
    ++found_instances;
    CHECK_LE(found_instances, instance_count);
  }
  CHECK_EQ(found_instances, instance_count);
}

void ValidateModuleState(Isolate* isolate, Handle<JSObject> module_obj) {
  DisallowHeapAllocation no_gc;
  WasmCompiledModule* compiled_module =
      WasmCompiledModule::cast(module_obj->GetInternalField(0));
  CHECK(compiled_module->has_weak_module_object());
  CHECK_EQ(compiled_module->ptr_to_weak_module_object()->value(), *module_obj);
  CHECK(!compiled_module->has_weak_prev_instance());
  CHECK(!compiled_module->has_weak_next_instance());
  CHECK(!compiled_module->has_weak_owning_instance());
}

void ValidateOrphanedInstance(Isolate* isolate, Handle<JSObject> instance) {
  DisallowHeapAllocation no_gc;
  CHECK(IsWasmObject(*instance));
  WasmCompiledModule* compiled_module =
      WasmCompiledModule::cast(instance->GetInternalField(kWasmCompiledModule));
  CHECK(compiled_module->has_weak_module_object());
  CHECK(compiled_module->ptr_to_weak_module_object()->cleared());
}

}  // namespace testing
}  // namespace wasm
}  // namespace internal
}  // namespace v8
