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

using namespace v8::internal;
using namespace v8::internal::wasm;
namespace base = v8::base;

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
  kInternalFunctionIndex
};

// Internal constants for the layout of the module object.
enum WasmInstanceObjectFields {
  kWasmCompiledModule = 0,
  kWasmMemObject,
  kWasmMemArrayBuffer,
  kWasmGlobalsArrayBuffer,
  kWasmDebugInfo,
  kWasmInstanceInternalFieldCount
};

byte* raw_buffer_ptr(MaybeHandle<JSArrayBuffer> buffer, int offset) {
  return static_cast<byte*>(buffer.ToHandleChecked()->backing_store()) + offset;
}

uint32_t GetMinModuleMemSize(const WasmModule* module) {
  return WasmModule::kPageSize * module->min_mem_pages;
}

MaybeHandle<String> ExtractStringFromModuleBytes(
    Isolate* isolate, Handle<WasmCompiledModule> compiled_module,
    uint32_t offset, uint32_t size) {
  // TODO(wasm): cache strings from modules if it's a performance win.
  Handle<SeqOneByteString> module_bytes = compiled_module->module_bytes();
  Address raw = module_bytes->GetCharsAddress() + offset;
  if (!unibrow::Utf8::Validate(reinterpret_cast<const byte*>(raw), size))
    return {};  // UTF8 decoding error for name.
  return isolate->factory()->NewStringFromUtf8SubString(
      module_bytes, static_cast<int>(offset), static_cast<int>(size));
}

void ReplaceReferenceInCode(Handle<Code> code, Handle<Object> old_ref,
                            Handle<Object> new_ref) {
  for (RelocIterator it(*code, 1 << RelocInfo::EMBEDDED_OBJECT); !it.done();
       it.next()) {
    if (it.rinfo()->target_object() == *old_ref) {
      it.rinfo()->set_target_object(*new_ref);
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

void RelocateMemoryReferencesInCode(Handle<FixedArray> code_table,
                                    Address old_start, Address start,
                                    uint32_t prev_size, uint32_t new_size) {
  for (int i = 0; i < code_table->length(); ++i) {
    DCHECK(code_table->get(i)->IsCode());
    Handle<Code> code = Handle<Code>(Code::cast(code_table->get(i)));
    AllowDeferredHandleDereference embedding_raw_address;
    int mask = (1 << RelocInfo::WASM_MEMORY_REFERENCE) |
               (1 << RelocInfo::WASM_MEMORY_SIZE_REFERENCE);
    for (RelocIterator it(*code, mask); !it.done(); it.next()) {
      it.rinfo()->update_wasm_memory_reference(old_start, start, prev_size,
                                               new_size);
    }
  }
}

void RelocateGlobals(Handle<FixedArray> code_table, Address old_start,
                     Address globals_start) {
  for (int i = 0; i < code_table->length(); ++i) {
    DCHECK(code_table->get(i)->IsCode());
    Handle<Code> code = Handle<Code>(Code::cast(code_table->get(i)));
    AllowDeferredHandleDereference embedding_raw_address;
    int mask = 1 << RelocInfo::WASM_GLOBAL_REFERENCE;
    for (RelocIterator it(*code, mask); !it.done(); it.next()) {
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

void FlushICache(Isolate* isolate, Handle<FixedArray> code_table) {
  for (int i = 0; i < code_table->length(); ++i) {
    Handle<Code> code = code_table->GetValueChecked<Code>(isolate, i);
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
      thrower->CompileError("Compilation of #%d:%.*s failed.", i, str.length(),
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
  uint32_t old_mem_size = compiled_module->mem_size();
  uint32_t default_mem_size = compiled_module->default_mem_size();
  Object* mem_start = compiled_module->ptr_to_memory();
  Address old_mem_address = nullptr;
  Address globals_start =
      GetGlobalStartAddressFromCodeTemplate(undefined, owner);

  // Reset function tables.
  FixedArray* function_tables = nullptr;
  FixedArray* empty_function_tables = nullptr;
  if (compiled_module->has_function_tables()) {
    function_tables = compiled_module->ptr_to_function_tables();
    empty_function_tables = compiled_module->ptr_to_empty_function_tables();
    compiled_module->set_ptr_to_function_tables(empty_function_tables);
  }

  if (old_mem_size > 0) {
    CHECK_NE(mem_start, undefined);
    old_mem_address =
        static_cast<Address>(JSArrayBuffer::cast(mem_start)->backing_store());
  }
  int mode_mask = RelocInfo::ModeMask(RelocInfo::WASM_MEMORY_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::WASM_MEMORY_SIZE_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::WASM_GLOBAL_REFERENCE) |
                  RelocInfo::ModeMask(RelocInfo::EMBEDDED_OBJECT);

  // Patch code to update memory references, global references, and function
  // table references.
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
        } else if (RelocInfo::IsWasmGlobalReference(mode)) {
          it.rinfo()->update_wasm_global_reference(globals_start, nullptr);
          changed = true;
        } else if (RelocInfo::IsEmbeddedObject(mode) && function_tables) {
          Object* old = it.rinfo()->target_object();
          for (int i = 0; i < function_tables->length(); ++i) {
            if (function_tables->get(i) == old) {
              it.rinfo()->set_target_object(empty_function_tables->get(i));
              changed = true;
            }
          }
        }
      }
      if (changed) {
        Assembler::FlushICache(isolate, code->instruction_start(),
                               code->instruction_size());
      }
    }
  }
  compiled_module->reset_memory();
}

static void InstanceFinalizer(const v8::WeakCallbackInfo<void>& data) {
  JSObject** p = reinterpret_cast<JSObject**>(data.GetParameter());
  JSObject* owner = *p;
  WasmCompiledModule* compiled_module = GetCompiledModule(owner);
  TRACE("Finalizing %d {\n", compiled_module->instance_id());
  Isolate* isolate = reinterpret_cast<Isolate*>(data.GetIsolate());
  DCHECK(compiled_module->has_weak_wasm_module());
  WeakCell* weak_wasm_module = compiled_module->ptr_to_weak_wasm_module();

  // weak_wasm_module may have been cleared, meaning the module object
  // was GC-ed. In that case, there won't be any new instances created,
  // and we don't need to maintain the links between instances.
  if (!weak_wasm_module->cleared()) {
    JSObject* wasm_module = JSObject::cast(weak_wasm_module->value());
    WasmCompiledModule* current_template =
        WasmCompiledModule::cast(wasm_module->GetInternalField(0));

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
        wasm_module->SetInternalField(0, next->value());
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
    TRACE_CHAIN(WasmCompiledModule::cast(wasm_module->GetInternalField(0)));
    TRACE("}\n");
  }
  compiled_module->reset_weak_owning_instance();
  GlobalHandles::Destroy(reinterpret_cast<Object**>(p));
  TRACE("}\n");
}

}  // namespace

const char* wasm::SectionName(WasmSectionCode code) {
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

std::ostream& wasm::operator<<(std::ostream& os, const WasmModule& module) {
  os << "WASM module with ";
  os << (module.min_mem_pages * module.kPageSize) << " min mem";
  os << (module.max_mem_pages * module.kPageSize) << " max mem";
  os << module.functions.size() << " functions";
  os << module.functions.size() << " globals";
  os << module.functions.size() << " data segments";
  return os;
}

std::ostream& wasm::operator<<(std::ostream& os, const WasmFunction& function) {
  os << "WASM function with signature " << *function.sig;

  os << " code bytes: "
     << (function.code_end_offset - function.code_start_offset);
  return os;
}

std::ostream& wasm::operator<<(std::ostream& os, const WasmFunctionName& pair) {
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

Handle<JSFunction> wasm::WrapExportCodeAsJSFunction(
    Isolate* isolate, Handle<Code> export_code, Handle<String> name,
    FunctionSig* sig, int func_index, Handle<JSObject> instance) {
  Handle<SharedFunctionInfo> shared =
      isolate->factory()->NewSharedFunctionInfo(name, export_code, false);
  int arity = static_cast<int>(sig->parameter_count());
  shared->set_length(arity);
  shared->set_internal_formal_parameter_count(arity);
  Handle<JSFunction> function = isolate->factory()->NewFunction(
      isolate->wasm_function_map(), name, export_code);
  function->set_shared(*shared);

  function->SetInternalField(kInternalModuleInstance, *instance);
  function->SetInternalField(kInternalFunctionIndex, Smi::FromInt(func_index));
  return function;
}

Object* wasm::GetOwningWasmInstance(Code* code) {
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

WasmModule* GetCppModule(Handle<JSObject> instance) {
  DCHECK(IsWasmInstance(*instance));
  return reinterpret_cast<WasmModuleWrapper*>(
             *GetCompiledModule(*instance)->module_wrapper())
      ->get();
}

int wasm::GetNumImportedFunctions(Handle<JSObject> instance) {
  return static_cast<int>(GetCppModule(instance)->num_imported_functions);
}

WasmModule::WasmModule(Zone* owned, const byte* module_start)
    : owned_zone(owned),
      module_start(module_start),
      pending_tasks(new base::Semaphore(0)) {}

MaybeHandle<WasmCompiledModule> WasmModule::CompileFunctions(
    Isolate* isolate, Handle<WasmModuleWrapper> module_wrapper,
    ErrorThrower* thrower) const {
  Factory* factory = isolate->factory();

  MaybeHandle<WasmCompiledModule> nothing;

  WasmInstance temp_instance(this);
  temp_instance.context = isolate->native_context();
  temp_instance.mem_size = GetMinModuleMemSize(this);
  temp_instance.mem_start = nullptr;
  temp_instance.globals_start = nullptr;

  // Initialize the indirect tables with placeholders.
  int function_table_count = static_cast<int>(this->function_tables.size());
  Handle<FixedArray> function_tables =
      factory->NewFixedArray(function_table_count);
  for (int i = 0; i < function_table_count; ++i) {
    temp_instance.function_tables[i] = factory->NewFixedArray(0);
    function_tables->set(i, *temp_instance.function_tables[i]);
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
      WasmCompiledModule::New(isolate, module_wrapper);
  ret->set_code_table(code_table);
  ret->set_min_mem_pages(min_mem_pages);
  if (function_table_count > 0) {
    ret->set_function_tables(function_tables);
    ret->set_empty_function_tables(function_tables);
  }

  // Compile JS->WASM wrappers for exported functions.
  int func_index = 0;
  for (auto exp : export_table) {
    if (exp.kind != kExternalFunction) continue;
    Handle<Code> wasm_code =
        code_table->GetValueChecked<Code>(isolate, exp.index);
    Handle<Code> wrapper_code = compiler::CompileJSToWasmWrapper(
        isolate, &module_env, wasm_code, exp.index);
    int export_index = static_cast<int>(functions.size() + func_index);
    code_table->set(export_index, *wrapper_code);
    func_index++;
  }

  {
    // TODO(wasm): only save the sections necessary to deserialize a
    // {WasmModule}. E.g. function bodies could be omitted.
    size_t module_bytes_len = module_end - module_start;
    DCHECK_LE(module_bytes_len, static_cast<size_t>(kMaxInt));
    Vector<const uint8_t> module_bytes_vec(module_start,
                                           static_cast<int>(module_bytes_len));
    Handle<String> module_bytes_string =
        factory->NewStringFromOneByte(module_bytes_vec, TENURED)
            .ToHandleChecked();
    DCHECK(module_bytes_string->IsSeqOneByteString());
    ret->set_module_bytes(Handle<SeqOneByteString>::cast(module_bytes_string));
  }

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
    MaybeHandle<JSObject> owner;

    TRACE("Starting new module instantiation\n");
    {
      // Root the owner, if any, before doing any allocations, which
      // may trigger GC.
      // Both owner and original template need to be in sync. Even
      // after we lose the original template handle, the code
      // objects we copied from it have data relative to the
      // instance - such as globals addresses.
      Handle<WasmCompiledModule> original;
      {
        DisallowHeapAllocation no_gc;
        original = handle(
            WasmCompiledModule::cast(module_object_->GetInternalField(0)));
        if (original->has_weak_owning_instance()) {
          owner =
              handle(JSObject::cast(original->weak_owning_instance()->value()));
        }
      }
      DCHECK(!original.is_null());
      // Always make a new copy of the code_table, since the old_code_table
      // may still have placeholders for imports.
      old_code_table = original->code_table();
      code_table = factory->CopyFixedArray(old_code_table);

      if (original->has_weak_owning_instance()) {
        // Clone, but don't insert yet the clone in the instances chain.
        // We do that last. Since we are holding on to the owner instance,
        // the owner + original state used for cloning and patching
        // won't be mutated by possible finalizer runs.
        DCHECK(!owner.is_null());
        TRACE("Cloning from %d\n", original->instance_id());
        compiled_module_ = WasmCompiledModule::Clone(isolate_, original);
        // Avoid creating too many handles in the outer scope.
        HandleScope scope(isolate_);

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
    module_ = reinterpret_cast<WasmModuleWrapper*>(
                  *compiled_module_->module_wrapper())
                  ->get();

    //--------------------------------------------------------------------------
    // Allocate the instance object.
    //--------------------------------------------------------------------------
    Handle<Map> map = factory->NewMap(
        JS_OBJECT_TYPE,
        JSObject::kHeaderSize + kWasmInstanceInternalFieldCount * kPointerSize);
    Handle<JSObject> instance = factory->NewJSObjectFromMap(map, TENURED);
    instance->SetInternalField(kWasmMemObject, *factory->undefined_value());

    //--------------------------------------------------------------------------
    // Set up the globals for the new instance.
    //--------------------------------------------------------------------------
    MaybeHandle<JSArrayBuffer> old_globals;
    MaybeHandle<JSArrayBuffer> globals;
    uint32_t globals_size = module_->globals_size;
    if (globals_size > 0) {
      Handle<JSArrayBuffer> global_buffer =
          NewArrayBuffer(isolate_, globals_size);
      globals = global_buffer;
      if (globals.is_null()) {
        thrower_->RangeError("Out of memory: wasm globals");
        return nothing;
      }
      Address old_address = owner.is_null()
                                ? nullptr
                                : GetGlobalStartAddressFromCodeTemplate(
                                      *factory->undefined_value(),
                                      JSObject::cast(*owner.ToHandleChecked()));
      RelocateGlobals(code_table, old_address,
                      static_cast<Address>(global_buffer->backing_store()));
      instance->SetInternalField(kWasmGlobalsArrayBuffer, *global_buffer);
    }

    //--------------------------------------------------------------------------
    // Process the imports for the module.
    //--------------------------------------------------------------------------
    int num_imported_functions = ProcessImports(globals, code_table, instance);
    if (num_imported_functions < 0) return nothing;

    //--------------------------------------------------------------------------
    // Process the initialization for the module's globals.
    //--------------------------------------------------------------------------
    InitGlobals(globals);

    //--------------------------------------------------------------------------
    // Set up the memory for the new instance.
    //--------------------------------------------------------------------------
    MaybeHandle<JSArrayBuffer> old_memory;

    uint32_t min_mem_pages = module_->min_mem_pages;
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
      LoadDataSegments(globals, mem_start, mem_size);

      uint32_t old_mem_size = compiled_module_->mem_size();
      Address old_mem_start =
          compiled_module_->has_memory()
              ? static_cast<Address>(
                    compiled_module_->memory()->backing_store())
              : nullptr;
      RelocateMemoryReferencesInCode(code_table, old_mem_start, mem_start,
                                     old_mem_size, mem_size);
      compiled_module_->set_memory(memory_);
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
    int function_table_count =
        static_cast<int>(module_->function_tables.size());
    if (function_table_count > 0) {
      Handle<FixedArray> old_function_tables =
          compiled_module_->function_tables();
      Handle<FixedArray> new_function_tables =
          factory->NewFixedArray(function_table_count);
      for (int index = 0; index < function_table_count; ++index) {
        WasmIndirectFunctionTable& table = module_->function_tables[index];
        uint32_t size = table.max_size;
        Handle<FixedArray> new_table = factory->NewFixedArray(size * 2);
        for (int i = 0; i < new_table->length(); ++i) {
          static const int kInvalidSigIndex = -1;
          // Fill the table with invalid signature indexes so that uninitialized
          // entries will always fail the signature check.
          new_table->set(i, Smi::FromInt(kInvalidSigIndex));
        }
        for (auto table_init : module_->table_inits) {
          uint32_t base = EvalUint32InitExpr(globals, table_init.offset);
          uint32_t table_size = static_cast<uint32_t>(new_table->length());
          if (base > table_size ||
              (base + table_init.entries.size() > table_size)) {
            thrower_->CompileError("table initializer is out of bounds");
            continue;
          }
          for (size_t i = 0; i < table_init.entries.size(); ++i) {
            FunctionSig* sig = module_->functions[table_init.entries[i]].sig;
            int32_t sig_index = table.map.Find(sig);
            new_table->set(static_cast<int>(i + base), Smi::FromInt(sig_index));
            new_table->set(static_cast<int>(i + base + size),
                           code_table->get(table_init.entries[i]));
          }
        }
        new_function_tables->set(static_cast<int>(index), *new_table);
      }
      // Patch all code that has references to the old indirect table.
      for (int i = 0; i < code_table->length(); ++i) {
        if (!code_table->get(i)->IsCode()) continue;
        Handle<Code> code(Code::cast(code_table->get(i)), isolate_);
        for (int j = 0; j < function_table_count; ++j) {
          ReplaceReferenceInCode(
              code, Handle<Object>(old_function_tables->get(j), isolate_),
              Handle<Object>(new_function_tables->get(j), isolate_));
        }
      }
      compiled_module_->set_function_tables(new_function_tables);
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
    // Set up and link the new instance.
    //--------------------------------------------------------------------------
    {
      Handle<Object> global_handle =
          isolate_->global_handles()->Create(*instance);
      Handle<WeakCell> link_to_clone = factory->NewWeakCell(compiled_module_);
      Handle<WeakCell> link_to_owning_instance = factory->NewWeakCell(instance);
      MaybeHandle<WeakCell> link_to_original;
      MaybeHandle<WasmCompiledModule> original;
      if (!owner.is_null()) {
        // prepare the data needed for publishing in a chain, but don't link
        // just yet, because
        // we want all the publishing to happen free from GC interruptions, and
        // so we do it in
        // one GC-free scope afterwards.
        original = handle(GetCompiledModule(*owner.ToHandleChecked()));
        link_to_original = factory->NewWeakCell(original.ToHandleChecked());
      }
      // Publish the new instance to the instances chain.
      {
        DisallowHeapAllocation no_gc;
        if (!link_to_original.is_null()) {
          compiled_module_->set_weak_next_instance(
              link_to_original.ToHandleChecked());
          original.ToHandleChecked()->set_weak_prev_instance(link_to_clone);
          compiled_module_->set_weak_wasm_module(
              original.ToHandleChecked()->weak_wasm_module());
        }
        module_object_->SetInternalField(0, *compiled_module_);
        instance->SetInternalField(kWasmCompiledModule, *compiled_module_);
        compiled_module_->set_weak_owning_instance(link_to_owning_instance);
        GlobalHandles::MakeWeak(global_handle.location(),
                                global_handle.location(), &InstanceFinalizer,
                                v8::WeakCallbackType::kFinalizer);
      }
    }

    DCHECK(wasm::IsWasmInstance(*instance));
    Handle<Object> memory_object(instance->GetInternalField(kWasmMemObject),
                                 isolate_);
    WasmJs::SetWasmMemoryInstance(isolate_, memory_object, instance);

    //--------------------------------------------------------------------------
    // Run the start function if one was specified.
    //--------------------------------------------------------------------------
    if (module_->start_function_index >= 0) {
      HandleScope scope(isolate_);
      int start_index = module_->start_function_index;
      Handle<Code> startup_code =
          code_table->GetValueChecked<Code>(isolate_, start_index);
      FunctionSig* sig = module_->functions[start_index].sig;
      Handle<JSFunction> startup_fct = WrapExportCodeAsJSFunction(
          isolate_, startup_code, factory->InternalizeUtf8String("start"), sig,
          start_index, instance);
      RecordStats(isolate_, *startup_code);
      // Call the JS function.
      Handle<Object> undefined = factory->undefined_value();
      MaybeHandle<Object> retval =
          Execution::Call(isolate_, startup_fct, undefined, 0, nullptr);

      if (retval.is_null()) {
        DCHECK(isolate_->has_pending_exception());
        isolate_->OptionalRescheduleException(false);
        // It's unfortunate that the new instance is already linked in the
        // chain. However, we need to set up everything before executing the
        // start function, such that stack trace information can be generated
        // correctly already in the start function.
        return nothing;
      }
    }

    DCHECK(!isolate_->has_pending_exception());
    TRACE("Finishing instance %d\n", compiled_module_->instance_id());
    TRACE_CHAIN(WasmCompiledModule::cast(module_object_->GetInternalField(0)));
    return instance;
  }

 private:
  Isolate* isolate_;
  WasmModule* module_;
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
      thrower_->TypeError(
          "Import #%d module=\"%.*s\" function=\"%.*s\" error: %s", index,
          module_name->length(), module_name->ToCString().get(),
          function_name_handle->length(),
          function_name_handle->ToCString().get(), error);
    } else {
      thrower_->TypeError("Import #%d module=\"%.*s\" error: %s", index,
                          module_name->length(), module_name->ToCString().get(),
                          error);
    }
    thrower_->TypeError("Import ");
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

  uint32_t EvalUint32InitExpr(MaybeHandle<JSArrayBuffer> globals,
                              WasmInitExpr& expr) {
    switch (expr.kind) {
      case WasmInitExpr::kI32Const:
        return expr.val.i32_const;
      case WasmInitExpr::kGlobalIndex: {
        uint32_t offset = module_->globals[expr.val.global_index].offset;
        return *reinterpret_cast<uint32_t*>(raw_buffer_ptr(globals, offset));
      }
      default:
        UNREACHABLE();
        return 0;
    }
  }

  // Load data segments into the memory.
  void LoadDataSegments(MaybeHandle<JSArrayBuffer> globals, Address mem_addr,
                        size_t mem_size) {
    Handle<SeqOneByteString> module_bytes = compiled_module_->module_bytes();
    for (auto segment : module_->data_segments) {
      uint32_t dest_offset = EvalUint32InitExpr(globals, segment.dest_addr);
      uint32_t source_size = segment.source_size;
      if (dest_offset >= mem_size || source_size >= mem_size ||
          dest_offset >= (mem_size - source_size)) {
        thrower_->RangeError("data segment does not fit into memory");
      }
      byte* dest = mem_addr + dest_offset;
      const byte* src = reinterpret_cast<const byte*>(
          module_bytes->GetCharsAddress() + segment.source_offset);
      memcpy(dest, src, source_size);
    }
  }

  Handle<Code> CompileImportWrapper(int index, const WasmImport& import,
                                    Handle<JSReceiver> target,
                                    Handle<String> module_name,
                                    MaybeHandle<String> import_name) {
    FunctionSig* sig = module_->functions[import.index].sig;
    Handle<Code> code;
    bool is_match = false;
    Handle<Code> export_wrapper_code;
    if (target->IsJSFunction()) {
      Handle<JSFunction> func = Handle<JSFunction>::cast(target);
      export_wrapper_code = handle(func->code());
      if (export_wrapper_code->kind() == Code::JS_TO_WASM_FUNCTION) {
        // Compare signature of other exported wasm function.
        Handle<JSObject> other_instance(
            JSObject::cast(func->GetInternalField(kInternalModuleInstance)),
            isolate_);
        int func_index =
            Smi::cast(func->GetInternalField(kInternalFunctionIndex))->value();
        FunctionSig* other_sig =
            GetCppModule(other_instance)->functions[func_index].sig;
        is_match = sig->Equals(other_sig);
      }
    }
    if (is_match) {
      // Signature matched. Unwrap the JS->WASM wrapper and return the naked
      // WASM function code.
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
      // Signature mismatch. Compile a new wrapper for the new signature.
      return compiler::CompileWasmToJSWrapper(isolate_, target, sig, index,
                                              module_name, import_name);
    }
  }

  void WriteGlobalValue(WasmGlobal& global, MaybeHandle<JSArrayBuffer> globals,
                        Handle<Object> value) {
    double num = 0;
    if (value->IsSmi()) {
      num = Smi::cast(*value)->value();
    } else if (value->IsHeapNumber()) {
      num = HeapNumber::cast(*value)->value();
    } else {
      UNREACHABLE();
    }
    TRACE("init [globals+%u] = %lf, type = %s\n", global.offset, num,
          WasmOpcodes::TypeName(global.type));
    switch (global.type) {
      case kAstI32:
        *GetRawGlobalPtr<int32_t>(global, globals) = static_cast<int32_t>(num);
        break;
      case kAstI64:
        // TODO(titzer): initialization of imported i64 globals.
        UNREACHABLE();
        break;
      case kAstF32:
        *GetRawGlobalPtr<float>(global, globals) = static_cast<float>(num);
        break;
      case kAstF64:
        *GetRawGlobalPtr<double>(global, globals) = static_cast<double>(num);
        break;
      default:
        UNREACHABLE();
    }
  }

  // Process the imports, including functions, tables, globals, and memory, in
  // order, loading them from the {ffi_} object. Returns the number of imported
  // functions.
  int ProcessImports(MaybeHandle<JSArrayBuffer> globals,
                     Handle<FixedArray> code_table, Handle<JSObject> instance) {
    int num_imported_functions = 0;
    for (int index = 0; index < static_cast<int>(module_->import_table.size());
         ++index) {
      WasmImport& import = module_->import_table[index];
      Handle<String> module_name =
          ExtractStringFromModuleBytes(isolate_, compiled_module_,
                                       import.module_name_offset,
                                       import.module_name_length)
              .ToHandleChecked();
      Handle<String> function_name = Handle<String>::null();
      if (import.field_name_length > 0) {
        function_name = ExtractStringFromModuleBytes(isolate_, compiled_module_,
                                                     import.field_name_offset,
                                                     import.field_name_length)
                            .ToHandleChecked();
      }

      MaybeHandle<Object> result =
          LookupImport(index, module_name, function_name);
      if (thrower_->error()) return -1;

      switch (import.kind) {
        case kExternalFunction: {
          // Function imports must be callable.
          Handle<Object> function = result.ToHandleChecked();
          if (!function->IsCallable()) {
            ReportFFIError("function import requires a callable", index,
                           module_name, function_name);
            return -1;
          }

          Handle<Code> import_wrapper = CompileImportWrapper(
              index, import, Handle<JSReceiver>::cast(function), module_name,
              function_name);
          code_table->set(num_imported_functions, *import_wrapper);
          RecordStats(isolate_, *import_wrapper);
          num_imported_functions++;
          break;
        }
        case kExternalTable:
          // TODO(titzer): Table imports must be a WebAssembly.Table.
          break;
        case kExternalMemory: {
          Handle<Object> object = result.ToHandleChecked();
          if (!WasmJs::IsWasmMemoryObject(isolate_, object)) {
            ReportFFIError("memory import must be a WebAssembly.Memory object",
                           index, module_name, function_name);
            return -1;
          }
          instance->SetInternalField(kWasmMemObject, *object);
          memory_ = WasmJs::GetWasmMemoryArrayBuffer(isolate_, object);
          break;
        }
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
          WriteGlobalValue(module_->globals[import.index], globals, val);
          break;
        }
        default:
          UNREACHABLE();
          break;
      }
    }
    return num_imported_functions;
  }

  template <typename T>
  T* GetRawGlobalPtr(WasmGlobal& global, MaybeHandle<JSArrayBuffer> globals) {
    return reinterpret_cast<T*>(raw_buffer_ptr(globals, global.offset));
  }

  // Process initialization of globals.
  void InitGlobals(MaybeHandle<JSArrayBuffer> globals) {
    for (auto global : module_->globals) {
      switch (global.init.kind) {
        case WasmInitExpr::kI32Const:
          *GetRawGlobalPtr<int32_t>(global, globals) =
              global.init.val.i32_const;
          break;
        case WasmInitExpr::kI64Const:
          *GetRawGlobalPtr<int64_t>(global, globals) =
              global.init.val.i64_const;
          break;
        case WasmInitExpr::kF32Const:
          *GetRawGlobalPtr<float>(global, globals) = global.init.val.f32_const;
          break;
        case WasmInitExpr::kF64Const:
          *GetRawGlobalPtr<double>(global, globals) = global.init.val.f64_const;
          break;
        case WasmInitExpr::kGlobalIndex: {
          // Initialize with another global.
          uint32_t new_offset = global.offset;
          uint32_t old_offset =
              module_->globals[global.init.val.global_index].offset;
          TRACE("init [globals+%u] = [globals+%d]\n", global.offset,
                old_offset);
          size_t size = (global.type == kAstI64 || global.type == kAstF64)
                            ? size = sizeof(double)
                            : sizeof(int32_t);
          memcpy(raw_buffer_ptr(globals, new_offset),
                 raw_buffer_ptr(globals, old_offset), size);
          break;
        }
        case WasmInitExpr::kNone:
          // Happens with imported globals.
          break;
        default:
          UNREACHABLE();
          break;
      }
    }
  }

  // Allocate memory for a module instance as a new JSArrayBuffer.
  Handle<JSArrayBuffer> AllocateMemory(uint32_t min_mem_pages) {
    if (min_mem_pages > WasmModule::kMaxMemPages) {
      thrower_->RangeError("Out of memory: wasm memory too large");
      return Handle<JSArrayBuffer>::null();
    }
    Handle<JSArrayBuffer> mem_buffer =
        NewArrayBuffer(isolate_, min_mem_pages * WasmModule::kPageSize);

    if (mem_buffer.is_null()) {
      thrower_->RangeError("Out of memory: wasm memory");
    }
    return mem_buffer;
  }

  // Process the exports, creating wrappers for functions, tables, memories,
  // and globals.
  void ProcessExports(MaybeHandle<JSArrayBuffer> globals,
                      Handle<FixedArray> code_table,
                      Handle<JSObject> instance) {
    if (module_->export_table.size() == 0) return;

    Handle<JSObject> exports_object = instance;
    if (module_->origin == kWasmOrigin) {
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

    int func_index = 0;
    for (auto exp : module_->export_table) {
      Handle<String> name =
          ExtractStringFromModuleBytes(isolate_, compiled_module_,
                                       exp.name_offset, exp.name_length)
              .ToHandleChecked();
      switch (exp.kind) {
        case kExternalFunction: {
          // Wrap and export the code as a JSFunction.
          WasmFunction& function = module_->functions[exp.index];
          int export_index =
              static_cast<int>(module_->functions.size() + func_index);
          Handle<Code> export_code =
              code_table->GetValueChecked<Code>(isolate_, export_index);
          desc.set_value(WrapExportCodeAsJSFunction(
              isolate_, export_code, name, function.sig, func_index, instance));
          func_index++;
          break;
        }
        case kExternalTable:
          // TODO(titzer): create a WebAssembly.Table instance.
          // TODO(titzer): should it have the same identity as an import?
          break;
        case kExternalMemory: {
          // Export the memory as a WebAssembly.Memory object.
          Handle<Object> memory_object(
              instance->GetInternalField(kWasmMemObject), isolate_);
          if (memory_object->IsUndefined(isolate_)) {
            // If there was no imported WebAssembly.Memory object, create one.
            Handle<JSArrayBuffer> buffer(
                JSArrayBuffer::cast(
                    instance->GetInternalField(kWasmMemArrayBuffer)),
                isolate_);
            memory_object =
                WasmJs::CreateWasmMemoryObject(isolate_, buffer, false, 0);
            instance->SetInternalField(kWasmMemObject, *memory_object);
          }

          desc.set_value(memory_object);
          break;
        }
        case kExternalGlobal: {
          // Export the value of the global variable as a number.
          WasmGlobal& global = module_->globals[exp.index];
          double num = 0;
          switch (global.type) {
            case kAstI32:
              num = *GetRawGlobalPtr<int32_t>(global, globals);
              break;
            case kAstF32:
              num = *GetRawGlobalPtr<float>(global, globals);
              break;
            case kAstF64:
              num = *GetRawGlobalPtr<double>(global, globals);
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

      v8::Maybe<bool> status = JSReceiver::DefineOwnProperty(
          isolate_, exports_object, name, &desc, Object::THROW_ON_ERROR);
      if (!status.IsJust()) {
        thrower_->TypeError("export of %.*s failed.", name->length(),
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
                                              Handle<JSObject> wasm_module,
                                              Handle<JSReceiver> ffi,
                                              Handle<JSArrayBuffer> memory) {
  WasmInstanceBuilder builder(isolate, thrower, wasm_module, ffi, memory);
  return builder.Build();
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
    if (current->ptr_to_weak_next_instance() == nullptr) break;
    CHECK(!current->ptr_to_weak_next_instance()->cleared());
    current =
        WasmCompiledModule::cast(current->ptr_to_weak_next_instance()->value());
  }
  PrintF("\n");
#endif
}

Handle<Object> wasm::GetWasmFunctionNameOrNull(Isolate* isolate,
                                               Handle<Object> instance,
                                               uint32_t func_index) {
  if (!instance->IsUndefined(isolate)) {
    DCHECK(IsWasmInstance(*instance));
    WasmModule* module = GetCppModule(Handle<JSObject>::cast(instance));
    WasmFunction& function = module->functions[func_index];
    Handle<WasmCompiledModule> compiled_module(GetCompiledModule(*instance),
                                               isolate);
    MaybeHandle<String> string = ExtractStringFromModuleBytes(
        isolate, compiled_module, function.name_offset, function.name_length);
    if (!string.is_null()) return string.ToHandleChecked();
  }
  return isolate->factory()->null_value();
}

Handle<String> wasm::GetWasmFunctionName(Isolate* isolate,
                                         Handle<Object> instance,
                                         uint32_t func_index) {
  Handle<Object> name_or_null =
      GetWasmFunctionNameOrNull(isolate, instance, func_index);
  if (!name_or_null->IsNull(isolate)) {
    return Handle<String>::cast(name_or_null);
  }
  return isolate->factory()->NewStringFromStaticChars("<WASM UNNAMED>");
}

bool wasm::IsWasmInstance(Object* object) {
  if (!object->IsJSObject()) return false;

  JSObject* obj = JSObject::cast(object);
  Isolate* isolate = obj->GetIsolate();
  if (obj->GetInternalFieldCount() != kWasmInstanceInternalFieldCount) {
    return false;
  }

  Object* mem = obj->GetInternalField(kWasmMemArrayBuffer);
  if (!(mem->IsUndefined(isolate) || mem->IsJSArrayBuffer()) ||
      !WasmCompiledModule::IsWasmCompiledModule(
          obj->GetInternalField(kWasmCompiledModule))) {
    return false;
  }

  // All checks passed.
  return true;
}

WasmCompiledModule* wasm::GetCompiledModule(Object* instance) {
  DCHECK(IsWasmInstance(instance));
  return WasmCompiledModule::cast(
      JSObject::cast(instance)->GetInternalField(kWasmCompiledModule));
}

bool wasm::WasmIsAsmJs(Object* instance, Isolate* isolate) {
  return IsWasmInstance(instance) &&
         GetCompiledModule(JSObject::cast(instance))->has_asm_js_script();
}

Handle<Script> wasm::GetAsmWasmScript(Handle<JSObject> instance) {
  DCHECK(IsWasmInstance(*instance));
  WasmCompiledModule* compiled_module = GetCompiledModule(*instance);
  return compiled_module->asm_js_script();
}

int wasm::GetAsmWasmSourcePosition(Handle<JSObject> instance, int func_index,
                                   int byte_offset) {
  return WasmDebugInfo::GetAsmJsSourcePosition(GetDebugInfo(instance),
                                               func_index, byte_offset);
}

Handle<SeqOneByteString> wasm::GetWasmBytes(Handle<JSObject> instance) {
  DCHECK(IsWasmInstance(*instance));
  WasmCompiledModule* compiled_module = GetCompiledModule(*instance);
  return compiled_module->module_bytes();
}

Handle<WasmDebugInfo> wasm::GetDebugInfo(Handle<JSObject> instance) {
  Handle<Object> info(instance->GetInternalField(kWasmDebugInfo),
                      instance->GetIsolate());
  if (!info->IsUndefined(instance->GetIsolate()))
    return Handle<WasmDebugInfo>::cast(info);
  Handle<WasmDebugInfo> new_info = WasmDebugInfo::New(instance);
  instance->SetInternalField(kWasmDebugInfo, *new_info);
  return new_info;
}

int wasm::GetNumberOfFunctions(Handle<JSObject> instance) {
  return static_cast<int>(GetCppModule(instance)->functions.size());
}

Handle<JSObject> wasm::CreateWasmModuleObject(
    Isolate* isolate, Handle<WasmCompiledModule> compiled_module,
    ModuleOrigin origin) {
  Handle<JSObject> wasm_module;
  if (origin == ModuleOrigin::kWasmOrigin) {
    Handle<JSFunction> module_cons(
        isolate->native_context()->wasm_module_constructor());
    wasm_module = isolate->factory()->NewJSObject(module_cons);
  } else {
    DCHECK(origin == ModuleOrigin::kAsmJsOrigin);
    Handle<Map> map = isolate->factory()->NewMap(
        JS_OBJECT_TYPE, JSObject::kHeaderSize + kPointerSize);
    wasm_module = isolate->factory()->NewJSObjectFromMap(map, TENURED);
  }
  wasm_module->SetInternalField(0, *compiled_module);
  if (origin == ModuleOrigin::kWasmOrigin) {
    Handle<Symbol> module_sym(isolate->native_context()->wasm_module_sym());
    Object::SetProperty(wasm_module, module_sym, wasm_module, STRICT).Check();
  }
  Handle<WeakCell> link_to_module =
      isolate->factory()->NewWeakCell(wasm_module);
  compiled_module->set_weak_wasm_module(link_to_module);
  return wasm_module;
}

// TODO(clemensh): origin can be inferred from asm_js_script; remove it.
MaybeHandle<JSObject> wasm::CreateModuleObjectFromBytes(
    Isolate* isolate, const byte* start, const byte* end, ErrorThrower* thrower,
    ModuleOrigin origin, Handle<Script> asm_js_script,
    const byte* asm_js_offset_tables_start,
    const byte* asm_js_offset_tables_end) {
  MaybeHandle<JSObject> nothing;
  ModuleResult result = DecodeWasmModule(isolate, start, end, false, origin);
  if (result.failed()) {
    if (result.val) delete result.val;
    thrower->CompileFailed("Wasm decoding failed", result);
    return nothing;
  }
  // The {module_wrapper} will take ownership of the {WasmModule} object,
  // and it will be destroyed when the GC reclaims the wrapper object.
  Handle<WasmModuleWrapper> module_wrapper =
      WasmModuleWrapper::New(isolate, const_cast<WasmModule*>(result.val));

  // Compile the functions of the module, producing a compiled module.
  MaybeHandle<WasmCompiledModule> maybe_compiled_module =
      result.val->CompileFunctions(isolate, module_wrapper, thrower);

  if (maybe_compiled_module.is_null()) return nothing;

  Handle<WasmCompiledModule> compiled_module =
      maybe_compiled_module.ToHandleChecked();

  DCHECK_EQ(origin == kAsmJsOrigin, !asm_js_script.is_null());
  DCHECK(!compiled_module->has_asm_js_script());
  DCHECK(!compiled_module->has_asm_js_offset_tables());
  if (origin == kAsmJsOrigin) {
    compiled_module->set_asm_js_script(asm_js_script);
    size_t offset_tables_len =
        asm_js_offset_tables_end - asm_js_offset_tables_start;
    DCHECK_GE(static_cast<size_t>(kMaxInt), offset_tables_len);
    Handle<ByteArray> offset_tables =
        isolate->factory()->NewByteArray(static_cast<int>(offset_tables_len));
    memcpy(offset_tables->GetDataStartAddress(), asm_js_offset_tables_start,
           offset_tables_len);
    compiled_module->set_asm_js_offset_tables(offset_tables);
  }

  return CreateWasmModuleObject(isolate, compiled_module, origin);
}

bool wasm::ValidateModuleBytes(Isolate* isolate, const byte* start,
                               const byte* end, ErrorThrower* thrower,
                               ModuleOrigin origin) {
  ModuleResult result = DecodeWasmModule(isolate, start, end, false, origin);
  if (result.ok()) {
    DCHECK_NOT_NULL(result.val);
    delete result.val;
    return true;
  }
  return false;
}

MaybeHandle<JSArrayBuffer> wasm::GetInstanceMemory(Isolate* isolate,
                                                   Handle<JSObject> instance) {
  Object* mem = instance->GetInternalField(kWasmMemArrayBuffer);
  DCHECK(IsWasmInstance(*instance));
  if (mem->IsUndefined(isolate)) return MaybeHandle<JSArrayBuffer>();
  return Handle<JSArrayBuffer>(JSArrayBuffer::cast(mem));
}

void SetInstanceMemory(Handle<JSObject> instance, JSArrayBuffer* buffer) {
  DisallowHeapAllocation no_gc;
  DCHECK(IsWasmInstance(*instance));
  instance->SetInternalField(kWasmMemArrayBuffer, buffer);
  WasmCompiledModule* compiled_module = GetCompiledModule(*instance);
  compiled_module->set_ptr_to_memory(buffer);
}

int32_t wasm::GetInstanceMemorySize(Isolate* isolate,
                                    Handle<JSObject> instance) {
  MaybeHandle<JSArrayBuffer> maybe_mem_buffer =
      GetInstanceMemory(isolate, instance);
  Handle<JSArrayBuffer> buffer;
  if (!maybe_mem_buffer.ToHandle(&buffer)) {
    return 0;
  } else {
    return buffer->byte_length()->Number() / WasmModule::kPageSize;
  }
}

uint32_t GetMaxInstanceMemorySize(Isolate* isolate, Handle<JSObject> instance) {
  uint32_t max_pages = WasmModule::kMaxMemPages;
  Handle<Object> memory_object(instance->GetInternalField(kWasmMemObject),
                               isolate);
  if (memory_object->IsUndefined(isolate)) return max_pages;
  return WasmJs::GetWasmMemoryMaximumSize(isolate, memory_object);
}

int32_t wasm::GrowInstanceMemory(Isolate* isolate, Handle<JSObject> instance,
                                 uint32_t pages) {
  if (!IsWasmInstance(*instance)) return -1;
  if (pages == 0) return GetInstanceMemorySize(isolate, instance);
  uint32_t max_pages = GetMaxInstanceMemorySize(isolate, instance);
  if (WasmModule::kMaxMemPages < max_pages) return -1;

  Address old_mem_start = nullptr;
  uint32_t old_size = 0, new_size = 0;

  MaybeHandle<JSArrayBuffer> maybe_mem_buffer =
      GetInstanceMemory(isolate, instance);
  Handle<JSArrayBuffer> old_buffer;
  if (!maybe_mem_buffer.ToHandle(&old_buffer) ||
      old_buffer->backing_store() == nullptr) {
    // If module object does not have linear memory associated with it,
    // Allocate new array buffer of given size.
    new_size = pages * WasmModule::kPageSize;
    if (max_pages < pages) return -1;
  } else {
    old_mem_start = static_cast<Address>(old_buffer->backing_store());
    old_size = old_buffer->byte_length()->Number();
    // If the old memory was zero-sized, we should have been in the
    // "undefined" case above.
    DCHECK_NOT_NULL(old_mem_start);
    DCHECK(old_size + pages * WasmModule::kPageSize <=
           std::numeric_limits<uint32_t>::max());
    new_size = old_size + pages * WasmModule::kPageSize;
  }

  if (new_size <= old_size || max_pages * WasmModule::kPageSize < new_size) {
    return -1;
  }
  Handle<JSArrayBuffer> buffer = NewArrayBuffer(isolate, new_size);
  if (buffer.is_null()) return -1;
  Address new_mem_start = static_cast<Address>(buffer->backing_store());
  if (old_size != 0) {
    memcpy(new_mem_start, old_mem_start, old_size);
  }
  SetInstanceMemory(instance, *buffer);
  Handle<FixedArray> code_table = GetCompiledModule(*instance)->code_table();
  RelocateMemoryReferencesInCode(code_table, old_mem_start, new_mem_start,
                                 old_size, new_size);
  DCHECK(old_size % WasmModule::kPageSize == 0);
  return (old_size / WasmModule::kPageSize);
}

void testing::ValidateInstancesChain(Isolate* isolate,
                                     Handle<JSObject> wasm_module,
                                     int instance_count) {
  CHECK_GE(instance_count, 0);
  DisallowHeapAllocation no_gc;
  WasmCompiledModule* compiled_module =
      WasmCompiledModule::cast(wasm_module->GetInternalField(0));
  CHECK_EQ(JSObject::cast(compiled_module->ptr_to_weak_wasm_module()->value()),
           *wasm_module);
  Object* prev = nullptr;
  int found_instances = compiled_module->has_weak_owning_instance() ? 1 : 0;
  WasmCompiledModule* current_instance = compiled_module;
  while (current_instance->has_weak_next_instance()) {
    CHECK((prev == nullptr && !current_instance->has_weak_prev_instance()) ||
          current_instance->ptr_to_weak_prev_instance()->value() == prev);
    CHECK_EQ(current_instance->ptr_to_weak_wasm_module()->value(),
             *wasm_module);
    CHECK(IsWasmInstance(
        current_instance->ptr_to_weak_owning_instance()->value()));
    prev = current_instance;
    current_instance = WasmCompiledModule::cast(
        current_instance->ptr_to_weak_next_instance()->value());
    ++found_instances;
    CHECK_LE(found_instances, instance_count);
  }
  CHECK_EQ(found_instances, instance_count);
}

void testing::ValidateModuleState(Isolate* isolate,
                                  Handle<JSObject> wasm_module) {
  DisallowHeapAllocation no_gc;
  WasmCompiledModule* compiled_module =
      WasmCompiledModule::cast(wasm_module->GetInternalField(0));
  CHECK(compiled_module->has_weak_wasm_module());
  CHECK_EQ(compiled_module->ptr_to_weak_wasm_module()->value(), *wasm_module);
  CHECK(!compiled_module->has_weak_prev_instance());
  CHECK(!compiled_module->has_weak_next_instance());
  CHECK(!compiled_module->has_weak_owning_instance());
}

void testing::ValidateOrphanedInstance(Isolate* isolate,
                                       Handle<JSObject> wasm_module) {
  DisallowHeapAllocation no_gc;
  CHECK(IsWasmInstance(*wasm_module));
  WasmCompiledModule* compiled_module = GetCompiledModule(*wasm_module);
  CHECK(compiled_module->has_weak_wasm_module());
  CHECK(compiled_module->ptr_to_weak_wasm_module()->cleared());
}

void WasmCompiledModule::RecreateModuleWrapper(Isolate* isolate,
                                               Handle<FixedArray> array) {
  Handle<WasmCompiledModule> compiled_module(
      reinterpret_cast<WasmCompiledModule*>(*array), isolate);

  WasmModule* module = nullptr;
  {
    Handle<SeqOneByteString> module_bytes = compiled_module->module_bytes();
    // We parse the module again directly from the module bytes, so
    // the underlying storage must not be moved meanwhile.
    DisallowHeapAllocation no_allocation;
    const byte* start =
        reinterpret_cast<const byte*>(module_bytes->GetCharsAddress());
    const byte* end = start + module_bytes->length();
    // TODO(titzer): remember the module origin in the compiled_module
    // For now, we assume serialized modules did not originate from asm.js.
    ModuleResult result =
        DecodeWasmModule(isolate, start, end, false, kWasmOrigin);
    CHECK(result.ok());
    CHECK_NOT_NULL(result.val);
    module = const_cast<WasmModule*>(result.val);
  }

  Handle<WasmModuleWrapper> module_wrapper =
      WasmModuleWrapper::New(isolate, module);

  compiled_module->set_module_wrapper(module_wrapper);
  DCHECK(WasmCompiledModule::IsWasmCompiledModule(*compiled_module));
}
