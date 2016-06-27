// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/atomic-utils.h"
#include "src/macro-assembler.h"
#include "src/objects.h"
#include "src/property-descriptor.h"
#include "src/v8.h"

#include "src/simulator.h"

#include "src/wasm/ast-decoder.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-function-name-table.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-result.h"

#include "src/compiler/wasm-compiler.h"

namespace v8 {
namespace internal {
namespace wasm {

static const int kPlaceholderMarker = 1000000000;

static const char* wasmSections[] = {
#define F(enumerator, order, string) string,
    FOR_EACH_WASM_SECTION_TYPE(F)
#undef F
        "<unknown>"  // entry for "Max"
};

static uint8_t wasmSectionsLengths[]{
#define F(enumerator, order, string) sizeof(string) - 1,
    FOR_EACH_WASM_SECTION_TYPE(F)
#undef F
        9  // entry for "Max"
};

static uint8_t wasmSectionsOrders[]{
#define F(enumerator, order, string) order,
    FOR_EACH_WASM_SECTION_TYPE(F)
#undef F
        0  // entry for "Max"
};

static_assert(sizeof(wasmSections) / sizeof(wasmSections[0]) ==
                  (size_t)WasmSection::Code::Max + 1,
              "expected enum WasmSection::Code to be monotonic from 0");

WasmSection::Code WasmSection::begin() { return (WasmSection::Code)0; }
WasmSection::Code WasmSection::end() { return WasmSection::Code::Max; }
WasmSection::Code WasmSection::next(WasmSection::Code code) {
  return (WasmSection::Code)(1 + (uint32_t)code);
}

const char* WasmSection::getName(WasmSection::Code code) {
  return wasmSections[(size_t)code];
}

size_t WasmSection::getNameLength(WasmSection::Code code) {
  return wasmSectionsLengths[(size_t)code];
}

int WasmSection::getOrder(WasmSection::Code code) {
  return wasmSectionsOrders[(size_t)code];
}

WasmSection::Code WasmSection::lookup(const byte* string, uint32_t length) {
  // TODO(jfb) Linear search, it may be better to do a common-prefix search.
  for (Code i = begin(); i != end(); i = next(i)) {
    if (getNameLength(i) == length && 0 == memcmp(getName(i), string, length)) {
      return i;
    }
  }
  return Code::Max;
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

namespace {
// Internal constants for the layout of the module object.
const int kWasmModuleFunctionTable = 0;
const int kWasmModuleCodeTable = 1;
const int kWasmMemArrayBuffer = 2;
const int kWasmGlobalsArrayBuffer = 3;
// TODO(clemensh): Remove function name array, extract names from module bytes.
const int kWasmFunctionNamesArray = 4;
const int kWasmModuleBytesString = 5;
const int kWasmDebugInfo = 6;
const int kWasmModuleInternalFieldCount = 7;

uint32_t GetMinModuleMemSize(const WasmModule* module) {
  return WasmModule::kPageSize * module->min_mem_pages;
}

void LoadDataSegments(const WasmModule* module, byte* mem_addr,
                      size_t mem_size) {
  for (const WasmDataSegment& segment : module->data_segments) {
    if (!segment.init) continue;
    if (!segment.source_size) continue;
    CHECK_LT(segment.dest_addr, mem_size);
    CHECK_LE(segment.source_size, mem_size);
    CHECK_LE(segment.dest_addr + segment.source_size, mem_size);
    byte* addr = mem_addr + segment.dest_addr;
    memcpy(addr, module->module_start + segment.source_offset,
           segment.source_size);
  }
}

Handle<FixedArray> BuildFunctionTable(Isolate* isolate,
                                      const WasmModule* module) {
  // Compute the size of the indirect function table
  uint32_t table_size = module->FunctionTableSize();
  if (table_size == 0) {
    return Handle<FixedArray>::null();
  }

  Handle<FixedArray> fixed = isolate->factory()->NewFixedArray(2 * table_size);
  for (uint32_t i = 0;
       i < static_cast<uint32_t>(module->function_table.size());
       ++i) {
    const WasmFunction* function =
        &module->functions[module->function_table[i]];
    fixed->set(i, Smi::FromInt(function->sig_index));
  }
  return fixed;
}

Handle<JSArrayBuffer> NewArrayBuffer(Isolate* isolate, size_t size,
                                     byte** backing_store) {
  *backing_store = nullptr;
  if (size > (WasmModule::kMaxMemPages * WasmModule::kPageSize)) {
    // TODO(titzer): lift restriction on maximum memory allocated here.
    return Handle<JSArrayBuffer>::null();
  }
  void* memory = isolate->array_buffer_allocator()->Allocate(size);
  if (memory == nullptr) {
    return Handle<JSArrayBuffer>::null();
  }

  *backing_store = reinterpret_cast<byte*>(memory);

#if DEBUG
  // Double check the API allocator actually zero-initialized the memory.
  byte* bytes = reinterpret_cast<byte*>(*backing_store);
  for (size_t i = 0; i < size; ++i) {
    DCHECK_EQ(0, bytes[i]);
  }
#endif

  Handle<JSArrayBuffer> buffer = isolate->factory()->NewJSArrayBuffer();
  JSArrayBuffer::Setup(buffer, isolate, false, memory, static_cast<int>(size));
  buffer->set_is_neuterable(false);
  return buffer;
}

void RelocateInstanceCode(WasmModuleInstance* instance) {
  for (uint32_t i = 0; i < instance->function_code.size(); ++i) {
    Handle<Code> function = instance->function_code[i];
    AllowDeferredHandleDereference embedding_raw_address;
    int mask = (1 << RelocInfo::WASM_MEMORY_REFERENCE) |
               (1 << RelocInfo::WASM_MEMORY_SIZE_REFERENCE);
    for (RelocIterator it(*function, mask); !it.done(); it.next()) {
      it.rinfo()->update_wasm_memory_reference(
          nullptr, instance->mem_start, GetMinModuleMemSize(instance->module),
          static_cast<uint32_t>(instance->mem_size));
    }
  }
}

// Set the memory for a module instance to be the {memory} array buffer.
void SetMemory(WasmModuleInstance* instance, Handle<JSArrayBuffer> memory) {
  memory->set_is_neuterable(false);
  instance->mem_start = reinterpret_cast<byte*>(memory->backing_store());
  instance->mem_size = memory->byte_length()->Number();
  instance->mem_buffer = memory;
  RelocateInstanceCode(instance);
}

// Allocate memory for a module instance as a new JSArrayBuffer.
bool AllocateMemory(ErrorThrower* thrower, Isolate* isolate,
                    WasmModuleInstance* instance) {
  DCHECK(instance->module);
  DCHECK(instance->mem_buffer.is_null());

  if (instance->module->min_mem_pages > WasmModule::kMaxMemPages) {
    thrower->Error("Out of memory: wasm memory too large");
    return false;
  }
  instance->mem_size = GetMinModuleMemSize(instance->module);
  instance->mem_buffer =
      NewArrayBuffer(isolate, instance->mem_size, &instance->mem_start);
  if (instance->mem_start == nullptr) {
    thrower->Error("Out of memory: wasm memory");
    instance->mem_size = 0;
    return false;
  }
  RelocateInstanceCode(instance);
  return true;
}

bool AllocateGlobals(ErrorThrower* thrower, Isolate* isolate,
                     WasmModuleInstance* instance) {
  uint32_t globals_size = instance->module->globals_size;
  if (globals_size > 0) {
    instance->globals_buffer =
        NewArrayBuffer(isolate, globals_size, &instance->globals_start);
    if (!instance->globals_start) {
      // Not enough space for backing store of globals.
      thrower->Error("Out of memory: wasm globals");
      return false;
    }

    for (uint32_t i = 0; i < instance->function_code.size(); ++i) {
      Handle<Code> function = instance->function_code[i];
      AllowDeferredHandleDereference embedding_raw_address;
      int mask = 1 << RelocInfo::WASM_GLOBAL_REFERENCE;
      for (RelocIterator it(*function, mask); !it.done(); it.next()) {
        it.rinfo()->update_wasm_global_reference(nullptr,
                                                 instance->globals_start);
      }
    }
  }
  return true;
}

Handle<Code> CreatePlaceholder(Factory* factory, uint32_t index,
                               Code::Kind kind) {
  // Create a placeholder code object and encode the corresponding index in
  // the {constant_pool_offset} field of the code object.
  // TODO(titzer): placeholder code objects are somewhat dangerous.
  static byte buffer[] = {0, 0, 0, 0, 0, 0, 0, 0};  // fake instructions.
  static CodeDesc desc = {buffer, 8, 8, 0, 0, nullptr, 0, nullptr};
  Handle<Code> code = factory->NewCode(desc, Code::KindField::encode(kind),
                                       Handle<Object>::null());
  code->set_constant_pool_offset(static_cast<int>(index) + kPlaceholderMarker);
  return code;
}

// TODO(mtrofin): remove when we stop relying on placeholders.
void InitializePlaceholders(Factory* factory,
                            std::vector<Handle<Code>>* placeholders,
                            size_t size) {
  DCHECK(placeholders->empty());
  placeholders->reserve(size);

  for (uint32_t i = 0; i < size; ++i) {
    placeholders->push_back(CreatePlaceholder(factory, i, Code::WASM_FUNCTION));
  }
}

bool LinkFunction(Handle<Code> unlinked,
                  const std::vector<Handle<Code>>& code_targets,
                  Code::Kind kind) {
  bool modified = false;
  int mode_mask = RelocInfo::kCodeTargetMask;
  AllowDeferredHandleDereference embedding_raw_address;
  for (RelocIterator it(*unlinked, mode_mask); !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsCodeTarget(mode)) {
      Code* target =
          Code::GetCodeFromTargetAddress(it.rinfo()->target_address());
      if (target->kind() == kind &&
          target->constant_pool_offset() >= kPlaceholderMarker) {
        // Patch direct calls to placeholder code objects.
        uint32_t index = target->constant_pool_offset() - kPlaceholderMarker;
        CHECK(index < code_targets.size());
        Handle<Code> new_target = code_targets[index];
        if (target != *new_target) {
          it.rinfo()->set_target_address(new_target->instruction_start(),
                                         SKIP_WRITE_BARRIER, SKIP_ICACHE_FLUSH);
          modified = true;
        }
      }
    }
  }
  return modified;
}

void LinkModuleFunctions(Isolate* isolate,
                         std::vector<Handle<Code>>& functions) {
  for (size_t i = 0; i < functions.size(); ++i) {
    Handle<Code> code = functions[i];
    bool modified = LinkFunction(code, functions, Code::WASM_FUNCTION);
    if (modified) {
      Assembler::FlushICache(isolate, code->instruction_start(),
                             code->instruction_size());
    }
  }
}

void LinkImports(Isolate* isolate, std::vector<Handle<Code>>& functions,
                 const std::vector<Handle<Code>>& imports) {
  for (uint32_t i = 0; i < functions.size(); ++i) {
    Handle<Code> code = functions[i];
    bool modified = LinkFunction(code, imports, Code::WASM_TO_JS_FUNCTION);
    if (modified) {
      Assembler::FlushICache(isolate, code->instruction_start(),
                             code->instruction_size());
    }
  }
}

}  // namespace

WasmModule::WasmModule()
    : module_start(nullptr),
      module_end(nullptr),
      min_mem_pages(0),
      max_mem_pages(0),
      mem_export(false),
      mem_external(false),
      start_function_index(-1),
      origin(kWasmOrigin),
      globals_size(0),
      indirect_table_size(0),
      pending_tasks(new base::Semaphore(0)) {}

static MaybeHandle<JSFunction> ReportFFIError(ErrorThrower& thrower,
                                              const char* error, uint32_t index,
                                              wasm::WasmName module_name,
                                              wasm::WasmName function_name) {
  if (!function_name.is_empty()) {
    thrower.Error("Import #%d module=\"%.*s\" function=\"%.*s\" error: %s",
                  index, module_name.length(), module_name.start(),
                  function_name.length(), function_name.start(), error);
  } else {
    thrower.Error("Import #%d module=\"%.*s\" error: %s", index,
                  module_name.length(), module_name.start(), error);
  }
  thrower.Error("Import ");
  return MaybeHandle<JSFunction>();
}

static MaybeHandle<JSFunction> LookupFunction(
    ErrorThrower& thrower, Factory* factory, Handle<JSReceiver> ffi,
    uint32_t index, wasm::WasmName module_name, wasm::WasmName function_name) {
  if (ffi.is_null()) {
    return ReportFFIError(thrower, "FFI is not an object", index, module_name,
                          function_name);
  }

  // Look up the module first.
  Handle<String> name = factory->InternalizeUtf8String(module_name);
  MaybeHandle<Object> result = Object::GetProperty(ffi, name);
  if (result.is_null()) {
    return ReportFFIError(thrower, "module not found", index, module_name,
                          function_name);
  }

  Handle<Object> module = result.ToHandleChecked();

  if (!module->IsJSReceiver()) {
    return ReportFFIError(thrower, "module is not an object or function", index,
                          module_name, function_name);
  }

  Handle<Object> function;
  if (!function_name.is_empty()) {
    // Look up the function in the module.
    Handle<String> name = factory->InternalizeUtf8String(function_name);
    MaybeHandle<Object> result = Object::GetProperty(module, name);
    if (result.is_null()) {
      return ReportFFIError(thrower, "function not found", index, module_name,
                            function_name);
    }
    function = result.ToHandleChecked();
  } else {
    // No function specified. Use the "default export".
    function = module;
  }

  if (!function->IsJSFunction()) {
    return ReportFFIError(thrower, "not a function", index, module_name,
                          function_name);
  }

  return Handle<JSFunction>::cast(function);
}

namespace {
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

  // - 1 because AtomicIntrement returns the value after the atomic increment.
  size_t index = next_unit->Increment(1) - 1;
  if (index >= compilation_units->size()) {
    return false;
  }

  compiler::WasmCompilationUnit* unit = compilation_units->at(index);
  if (unit != nullptr) {
    unit->ExecuteCompilation();
    {
      base::LockGuard<base::Mutex> guard(result_mutex);
      executed_units->push(unit);
    }
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

// Records statistics on the code generated by compiling WASM functions.
struct CodeStats {
  size_t code_size;
  size_t reloc_size;

  inline CodeStats() : code_size(0), reloc_size(0) {}

  inline void Record(Code* code) {
    code_size += code->body_size();
    reloc_size += code->relocation_info()->length();
  }

  inline void Report() {
    PrintF("Total generated wasm code: %zu bytes\n", code_size);
    PrintF("Total generated wasm reloc: %zu bytes\n", reloc_size);
  }
};

bool CompileWrappersToImportedFunctions(
    Isolate* isolate, const WasmModule* module, const Handle<JSReceiver> ffi,
    WasmModuleInstance* instance, ErrorThrower* thrower, Factory* factory) {
  if (module->import_table.size() > 0) {
    instance->import_code.reserve(module->import_table.size());
    for (uint32_t index = 0; index < module->import_table.size(); ++index) {
      const WasmImport& import = module->import_table[index];
      WasmName module_name = module->GetNameOrNull(import.module_name_offset,
                                                   import.module_name_length);
      WasmName function_name = module->GetNameOrNull(
          import.function_name_offset, import.function_name_length);
      MaybeHandle<JSFunction> function = LookupFunction(
          *thrower, factory, ffi, index, module_name, function_name);
      if (function.is_null()) return false;

      Handle<Code> code = compiler::CompileWasmToJSWrapper(
          isolate, function.ToHandleChecked(), import.sig, module_name,
          function_name);
      instance->import_code[index] = code;
    }
  }
  return true;
}

void InitializeParallelCompilation(
    Isolate* isolate, const std::vector<WasmFunction>& functions,
    std::vector<compiler::WasmCompilationUnit*>& compilation_units,
    ModuleEnv& module_env, ErrorThrower& thrower) {
  for (uint32_t i = FLAG_skip_compiling_wasm_funcs; i < functions.size(); ++i) {
    compilation_units[i] = new compiler::WasmCompilationUnit(
        &thrower, isolate, &module_env, &functions[i], i);
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
                                *module_env, *thrower);

  // Objects for the synchronization with the background threads.
  base::Mutex result_mutex;
  base::AtomicNumber<size_t> next_unit(
      static_cast<size_t>(FLAG_skip_compiling_wasm_funcs));

  // 2) The main thread spawns {WasmCompilationTask} instances which run on
  //    the background threads.
  base::SmartArrayPointer<uint32_t> task_ids(StartCompilationTasks(
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

    DCHECK_EQ(i, func.func_index);
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

void PopulateFunctionTable(WasmModuleInstance* instance) {
  if (!instance->function_table.is_null()) {
    uint32_t table_size = instance->module->FunctionTableSize();
    DCHECK_EQ(table_size * 2, instance->function_table->length());
    uint32_t populated_table_size =
        static_cast<uint32_t>(instance->module->function_table.size());
    for (uint32_t i = 0; i < populated_table_size; ++i) {
    instance->function_table->set(
        i + table_size,
        *instance->function_code[instance->module->function_table[i]]);
    }
  }
}
}  // namespace

void SetDeoptimizationData(Factory* factory, Handle<JSObject> js_object,
                           std::vector<Handle<Code>>& functions) {
  for (size_t i = FLAG_skip_compiling_wasm_funcs; i < functions.size(); ++i) {
    Handle<Code> code = functions[i];
    DCHECK(code->deoptimization_data() == nullptr ||
           code->deoptimization_data()->length() == 0);
    Handle<FixedArray> deopt_data = factory->NewFixedArray(2, TENURED);
    if (!js_object.is_null()) {
      deopt_data->set(0, *js_object);
    }
    deopt_data->set(1, Smi::FromInt(static_cast<int>(i)));
    deopt_data->set_length(2);
    code->set_deoptimization_data(*deopt_data);
  }
}

Handle<FixedArray> WasmModule::CompileFunctions(Isolate* isolate) const {
  Factory* factory = isolate->factory();
  ErrorThrower thrower(isolate, "WasmModule::CompileFunctions()");

  WasmModuleInstance temp_instance_for_compilation(this);
  temp_instance_for_compilation.function_table =
      BuildFunctionTable(isolate, this);
  temp_instance_for_compilation.context = isolate->native_context();
  temp_instance_for_compilation.mem_size = GetMinModuleMemSize(this);
  temp_instance_for_compilation.mem_start = nullptr;
  temp_instance_for_compilation.globals_start = nullptr;

  ModuleEnv module_env;
  module_env.module = this;
  module_env.instance = &temp_instance_for_compilation;
  module_env.origin = origin;
  InitializePlaceholders(factory, &module_env.placeholders, functions.size());

  Handle<FixedArray> ret =
      factory->NewFixedArray(static_cast<int>(functions.size()), TENURED);

  temp_instance_for_compilation.import_code.resize(import_table.size());
  for (uint32_t i = 0; i < import_table.size(); ++i) {
    temp_instance_for_compilation.import_code[i] =
        CreatePlaceholder(factory, i, Code::WASM_TO_JS_FUNCTION);
  }
  isolate->counters()->wasm_functions_per_module()->AddSample(
      static_cast<int>(functions.size()));
  if (FLAG_wasm_num_compilation_tasks != 0) {
    CompileInParallel(isolate, this,
                      temp_instance_for_compilation.function_code, &thrower,
                      &module_env);
  } else {
    CompileSequentially(isolate, this,
                        temp_instance_for_compilation.function_code, &thrower,
                        &module_env);
  }
  if (thrower.error()) {
    return Handle<FixedArray>::null();
  }

  LinkModuleFunctions(isolate, temp_instance_for_compilation.function_code);

  // At this point, compilation has completed. Update the code table
  // and record sizes.
  for (size_t i = FLAG_skip_compiling_wasm_funcs;
       i < temp_instance_for_compilation.function_code.size(); ++i) {
    Code* code = *temp_instance_for_compilation.function_code[i];
    ret->set(static_cast<int>(i), code);
  }

  PopulateFunctionTable(&temp_instance_for_compilation);

  return ret;
}

// Instantiates a wasm module as a JSObject.
//  * allocates a backing store of {mem_size} bytes.
//  * installs a named property "memory" for that buffer if exported
//  * installs named properties on the object for exported functions
//  * compiles wasm code to machine code
MaybeHandle<JSObject> WasmModule::Instantiate(
    Isolate* isolate, Handle<JSReceiver> ffi,
    Handle<JSArrayBuffer> memory) const {
  HistogramTimerScope wasm_instantiate_module_time_scope(
      isolate->counters()->wasm_instantiate_module_time());
  ErrorThrower thrower(isolate, "WasmModule::Instantiate()");
  Factory* factory = isolate->factory();

  //-------------------------------------------------------------------------
  // Allocate the instance and its JS counterpart.
  //-------------------------------------------------------------------------
  Handle<Map> map = factory->NewMap(
      JS_OBJECT_TYPE,
      JSObject::kHeaderSize + kWasmModuleInternalFieldCount * kPointerSize);
  WasmModuleInstance instance(this);
  instance.context = isolate->native_context();
  instance.js_object = factory->NewJSObjectFromMap(map, TENURED);

  Handle<FixedArray> code_table = CompileFunctions(isolate);
  if (code_table.is_null()) return Handle<JSObject>::null();

  instance.js_object->SetInternalField(kWasmModuleCodeTable, *code_table);
  size_t module_bytes_len =
      instance.module->module_end - instance.module->module_start;
  DCHECK_LE(module_bytes_len, static_cast<size_t>(kMaxInt));
  Vector<const uint8_t> module_bytes_vec(instance.module->module_start,
                                         static_cast<int>(module_bytes_len));
  Handle<String> module_bytes_string =
      factory->NewStringFromOneByte(module_bytes_vec, TENURED)
          .ToHandleChecked();
  instance.js_object->SetInternalField(kWasmModuleBytesString,
                                       *module_bytes_string);

  for (uint32_t i = 0; i < functions.size(); ++i) {
    Handle<Code> code = Handle<Code>(Code::cast(code_table->get(i)));
    instance.function_code[i] = code;
  }

  //-------------------------------------------------------------------------
  // Allocate and initialize the linear memory.
  //-------------------------------------------------------------------------
  isolate->counters()->wasm_min_mem_pages_count()->AddSample(
      instance.module->min_mem_pages);
  isolate->counters()->wasm_max_mem_pages_count()->AddSample(
      instance.module->max_mem_pages);
  if (memory.is_null()) {
    if (!AllocateMemory(&thrower, isolate, &instance)) {
      return MaybeHandle<JSObject>();
    }
  } else {
    SetMemory(&instance, memory);
  }
  instance.js_object->SetInternalField(kWasmMemArrayBuffer,
                                       *instance.mem_buffer);
  LoadDataSegments(this, instance.mem_start, instance.mem_size);

  //-------------------------------------------------------------------------
  // Allocate the globals area if necessary.
  //-------------------------------------------------------------------------
  if (!AllocateGlobals(&thrower, isolate, &instance)) {
    return MaybeHandle<JSObject>();
  }
  if (!instance.globals_buffer.is_null()) {
    instance.js_object->SetInternalField(kWasmGlobalsArrayBuffer,
                                         *instance.globals_buffer);
  }

  HistogramTimerScope wasm_compile_module_time_scope(
      isolate->counters()->wasm_compile_module_time());

  ModuleEnv module_env;
  module_env.module = this;
  module_env.instance = &instance;
  module_env.origin = origin;

  //-------------------------------------------------------------------------
  // Compile wrappers to imported functions.
  //-------------------------------------------------------------------------
  if (!CompileWrappersToImportedFunctions(isolate, this, ffi, &instance,
                                          &thrower, factory)) {
    return MaybeHandle<JSObject>();
  }

  // If FLAG_print_wasm_code_size is set, this aggregates the sum of all code
  // objects created for this module.
  // TODO(titzer): switch this to TRACE_EVENT
  CodeStats code_stats;
  if (FLAG_print_wasm_code_size) {
    for (Handle<Code> c : instance.function_code) code_stats.Record(*c);
    for (Handle<Code> c : instance.import_code) code_stats.Record(*c);
  }

  {
    instance.js_object->SetInternalField(kWasmModuleFunctionTable,
                                         Smi::FromInt(0));
    LinkImports(isolate, instance.function_code, instance.import_code);

    SetDeoptimizationData(factory, instance.js_object, instance.function_code);

    //-------------------------------------------------------------------------
    // Create and populate the exports object.
    //-------------------------------------------------------------------------
    if (export_table.size() > 0 || mem_export) {
      Handle<JSObject> exports_object;
      if (origin == kWasmOrigin) {
        // Create the "exports" object.
        Handle<JSFunction> object_function = Handle<JSFunction>(
            isolate->native_context()->object_function(), isolate);
        exports_object = factory->NewJSObject(object_function, TENURED);
        Handle<String> exports_name = factory->InternalizeUtf8String("exports");
        JSObject::AddProperty(instance.js_object, exports_name, exports_object,
                              READ_ONLY);
      } else {
        // Just export the functions directly on the object returned.
        exports_object = instance.js_object;
      }

      PropertyDescriptor desc;
      desc.set_writable(false);

      // Compile wrappers and add them to the exports object.
      for (const WasmExport& exp : export_table) {
        if (thrower.error()) break;
        WasmName str = GetName(exp.name_offset, exp.name_length);
        Handle<String> name = factory->InternalizeUtf8String(str);
        Handle<Code> code = instance.function_code[exp.func_index];
        Handle<JSFunction> function = compiler::CompileJSToWasmWrapper(
            isolate, &module_env, name, code, instance.js_object,
            exp.func_index);
        if (FLAG_print_wasm_code_size) {
          code_stats.Record(function->code());
        }
        desc.set_value(function);
        Maybe<bool> status = JSReceiver::DefineOwnProperty(
            isolate, exports_object, name, &desc, Object::THROW_ON_ERROR);
        if (!status.IsJust()) {
          thrower.Error("export of %.*s failed.", str.length(), str.start());
          break;
        }
      }

      if (mem_export) {
        // Export the memory as a named property.
        Handle<String> name = factory->InternalizeUtf8String("memory");
        JSObject::AddProperty(exports_object, name, instance.mem_buffer,
                              READ_ONLY);
      }
    }
  }

  if (FLAG_print_wasm_code_size) {
    code_stats.Report();
  }
  //-------------------------------------------------------------------------
  // Attach the function name table.
  //-------------------------------------------------------------------------
  Handle<ByteArray> function_name_table =
      BuildFunctionNamesTable(isolate, module_env.module);
  instance.js_object->SetInternalField(kWasmFunctionNamesArray,
                                       *function_name_table);

  // Run the start function if one was specified.
  if (this->start_function_index >= 0) {
    HandleScope scope(isolate);
    uint32_t index = static_cast<uint32_t>(this->start_function_index);
    Handle<String> name = isolate->factory()->NewStringFromStaticChars("start");
    Handle<Code> code = instance.function_code[index];
    Handle<JSFunction> jsfunc = compiler::CompileJSToWasmWrapper(
        isolate, &module_env, name, code, instance.js_object, index);

    // Call the JS function.
    Handle<Object> undefined = isolate->factory()->undefined_value();
    MaybeHandle<Object> retval =
        Execution::Call(isolate, jsfunc, undefined, 0, nullptr);

    if (retval.is_null()) {
      thrower.Error("WASM.instantiateModule(): start function failed");
    }
  }
  return instance.js_object;
}

// TODO(mtrofin): remove this once we move to WASM_DIRECT_CALL
Handle<Code> ModuleEnv::GetCodeOrPlaceholder(uint32_t index) const {
  DCHECK(IsValidFunction(index));
  if (!placeholders.empty()) return placeholders[index];
  DCHECK_NOT_NULL(instance);
  return instance->function_code[index];
}

Handle<Code> ModuleEnv::GetImportCode(uint32_t index) {
  DCHECK(IsValidImport(index));
  return instance ? instance->import_code[index] : Handle<Code>::null();
}

compiler::CallDescriptor* ModuleEnv::GetCallDescriptor(Zone* zone,
                                                       uint32_t index) {
  DCHECK(IsValidFunction(index));
  // Always make a direct call to whatever is in the table at that location.
  // A wrapper will be generated for FFI calls.
  const WasmFunction* function = &module->functions[index];
  return GetWasmCallDescriptor(zone, function->sig);
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
  if (obj->GetInternalFieldCount() != kWasmModuleInternalFieldCount ||
      !obj->GetInternalField(kWasmModuleCodeTable)->IsFixedArray() ||
      !obj->GetInternalField(kWasmMemArrayBuffer)->IsJSArrayBuffer() ||
      !obj->GetInternalField(kWasmFunctionNamesArray)->IsByteArray() ||
      !obj->GetInternalField(kWasmModuleBytesString)->IsSeqOneByteString()) {
    return false;
  }
  DisallowHeapAllocation no_gc;
  SeqOneByteString* bytes =
      SeqOneByteString::cast(obj->GetInternalField(kWasmModuleBytesString));
  if (bytes->length() < 4) return false;
  if (memcmp(bytes->GetChars(), "\0asm", 4)) return false;

  // All checks passed.
  return true;
}

SeqOneByteString* GetWasmBytes(JSObject* wasm) {
  return SeqOneByteString::cast(wasm->GetInternalField(kWasmModuleBytesString));
}

WasmDebugInfo* GetDebugInfo(JSObject* wasm) {
  Object* info = wasm->GetInternalField(kWasmDebugInfo);
  if (!info->IsUndefined(wasm->GetIsolate())) return WasmDebugInfo::cast(info);
  Handle<WasmDebugInfo> new_info = WasmDebugInfo::New(handle(wasm));
  wasm->SetInternalField(kWasmDebugInfo, *new_info);
  return *new_info;
}

namespace testing {

int32_t CompileAndRunWasmModule(Isolate* isolate, const byte* module_start,
                                const byte* module_end, bool asm_js) {
  HandleScope scope(isolate);
  Zone zone(isolate->allocator());
  ErrorThrower thrower(isolate, "CompileAndRunWasmModule");

  // Decode the module, but don't verify function bodies, since we'll
  // be compiling them anyway.
  ModuleResult decoding_result =
      DecodeWasmModule(isolate, &zone, module_start, module_end, false,
                       asm_js ? kAsmJsOrigin : kWasmOrigin);

  std::unique_ptr<const WasmModule> module(decoding_result.val);
  if (decoding_result.failed()) {
    // Module verification failed. throw.
    thrower.Error("WASM.compileRun() failed: %s",
                  decoding_result.error_msg.get());
    return -1;
  }

  if (module->import_table.size() > 0) {
    thrower.Error("Not supported: module has imports.");
  }
  if (module->export_table.size() == 0) {
    thrower.Error("Not supported: module has no exports.");
  }

  if (thrower.error()) return -1;

  Handle<JSObject> instance =
      module
          ->Instantiate(isolate, Handle<JSReceiver>::null(),
                        Handle<JSArrayBuffer>::null())
          .ToHandleChecked();

  Handle<Name> exports = isolate->factory()->InternalizeUtf8String("exports");
  Handle<JSObject> exports_object = Handle<JSObject>::cast(
      JSObject::GetProperty(instance, exports).ToHandleChecked());
  Handle<Name> main_name = isolate->factory()->NewStringFromStaticChars("main");
  PropertyDescriptor desc;
  Maybe<bool> property_found = JSReceiver::GetOwnPropertyDescriptor(
      isolate, exports_object, main_name, &desc);
  if (!property_found.FromMaybe(false)) return -1;

  Handle<JSFunction> main_export = Handle<JSFunction>::cast(desc.value());

  // Call the JS function.
  Handle<Object> undefined = isolate->factory()->undefined_value();
  MaybeHandle<Object> retval =
      Execution::Call(isolate, main_export, undefined, 0, nullptr);

  // The result should be a number.
  if (retval.is_null()) {
    thrower.Error("WASM.compileRun() failed: Invocation was null");
    return -1;
  }
  Handle<Object> result = retval.ToHandleChecked();
  if (result->IsSmi()) {
    return Smi::cast(*result)->value();
  }
  if (result->IsHeapNumber()) {
    return static_cast<int32_t>(HeapNumber::cast(*result)->value());
  }
  thrower.Error("WASM.compileRun() failed: Return value should be number");
  return -1;
}

}  // namespace testing
}  // namespace wasm
}  // namespace internal
}  // namespace v8
