// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_MODULE_COMPILER_H_
#define V8_WASM_MODULE_COMPILER_H_

#include <functional>

#include "src/base/atomic-utils.h"
#include "src/base/utils/random-number-generator.h"
#include "src/cancelable-task.h"
#include "src/compiler/wasm-compiler.h"
#include "src/isolate.h"

#include "src/wasm/wasm-code-specialization.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {
namespace wasm {

// A class compiling an entire module.
class ModuleCompiler {
 public:
  // The ModuleCompiler takes ownership of the {WasmModule}.
  // In {CompileToModuleObject}, it will transfer ownership to the generated
  // {WasmModuleWrapper}. If this method is not called, ownership may be
  // reclaimed by explicitely releasing the {module_} field.
  ModuleCompiler(Isolate* isolate, std::unique_ptr<WasmModule> module,
                 bool is_sync);

  // The actual runnable task that performs compilations in the background.
  class CompilationTask : public CancelableTask {
   public:
    ModuleCompiler* compiler_;
    explicit CompilationTask(ModuleCompiler* helper);

    void RunInternal() override;
  };

  class CodeGenerationSchedule {
   public:
    explicit CodeGenerationSchedule(
        base::RandomNumberGenerator* random_number_generator);

    void Schedule(std::unique_ptr<compiler::WasmCompilationUnit>&& item);

    bool IsEmpty() const { return schedule_.empty(); }

    std::unique_ptr<compiler::WasmCompilationUnit> GetNext();

   private:
    size_t GetRandomIndexInSchedule();

    base::RandomNumberGenerator* random_number_generator_ = nullptr;
    std::vector<std::unique_ptr<compiler::WasmCompilationUnit>> schedule_;
  };

  Isolate* isolate_;
  std::unique_ptr<WasmModule> module_;
  std::shared_ptr<Counters> counters_shared_;
  Counters* counters_;
  bool is_sync_;
  std::vector<std::unique_ptr<compiler::WasmCompilationUnit>>
      compilation_units_;
  CodeGenerationSchedule executed_units_;
  base::Mutex result_mutex_;
  base::AtomicNumber<size_t> next_unit_;
  size_t num_background_tasks_ = 0;
  // This flag should only be set while holding result_mutex_.
  bool finisher_is_running_ = false;

  // Run by each compilation task and by the main thread. The
  // no_finisher_callback is called within the result_mutex_ lock when no
  // finishing task is running, i.e. when the finisher_is_running_ flag is not
  // set.
  bool FetchAndExecuteCompilationUnit(
      std::function<void()> no_finisher_callback = [] {});

  size_t InitializeParallelCompilation(
      const std::vector<WasmFunction>& functions, ModuleBytesEnv& module_env);

  void InitializeHandles();

  uint32_t* StartCompilationTasks();

  void WaitForCompilationTasks(uint32_t* task_ids);

  void FinishCompilationUnits(std::vector<Handle<Code>>& results,
                              ErrorThrower* thrower);

  void SetFinisherIsRunning(bool value);

  Handle<Code> FinishCompilationUnit(ErrorThrower* thrower, int* func_index);

  void CompileInParallel(ModuleBytesEnv* module_env,
                         std::vector<Handle<Code>>& results,
                         ErrorThrower* thrower);

  void CompileSequentially(ModuleBytesEnv* module_env,
                           std::vector<Handle<Code>>& results,
                           ErrorThrower* thrower);

  void ValidateSequentially(ModuleBytesEnv* module_env, ErrorThrower* thrower);

  MaybeHandle<WasmModuleObject> CompileToModuleObject(
      ErrorThrower* thrower, const ModuleWireBytes& wire_bytes,
      Handle<Script> asm_js_script,
      Vector<const byte> asm_js_offset_table_bytes);

 private:
  MaybeHandle<WasmModuleObject> CompileToModuleObjectInternal(
      ErrorThrower* thrower, const ModuleWireBytes& wire_bytes,
      Handle<Script> asm_js_script,
      Vector<const byte> asm_js_offset_table_bytes, Factory* factory,
      WasmInstance* temp_instance, Handle<FixedArray>* function_tables,
      Handle<FixedArray>* signature_tables);
};

class JSToWasmWrapperCache {
 public:
  Handle<Code> CloneOrCompileJSToWasmWrapper(Isolate* isolate,
                                             const wasm::WasmModule* module,
                                             Handle<Code> wasm_code,
                                             uint32_t index);

 private:
  // sig_map_ maps signatures to an index in code_cache_.
  wasm::SignatureMap sig_map_;
  std::vector<Handle<Code>> code_cache_;
};

// A helper class to simplify instantiating a module from a compiled module.
// It closes over the {Isolate}, the {ErrorThrower}, the {WasmCompiledModule},
// etc.
class InstanceBuilder {
 public:
  InstanceBuilder(Isolate* isolate, ErrorThrower* thrower,
                  Handle<WasmModuleObject> module_object,
                  MaybeHandle<JSReceiver> ffi,
                  MaybeHandle<JSArrayBuffer> memory,
                  WeakCallbackInfo<void>::Callback instance_finalizer_callback);

  // Build an instance, in all of its glory.
  MaybeHandle<WasmInstanceObject> Build();

 private:
  // Represents the initialized state of a table.
  struct TableInstance {
    Handle<WasmTableObject> table_object;  // WebAssembly.Table instance
    Handle<FixedArray> js_wrappers;        // JSFunctions exported
    Handle<FixedArray> function_table;     // internal code array
    Handle<FixedArray> signature_table;    // internal sig array
  };

  Isolate* isolate_;
  WasmModule* const module_;
  std::shared_ptr<Counters> counters_shared_;
  Counters* counters_;
  ErrorThrower* thrower_;
  Handle<WasmModuleObject> module_object_;
  Handle<JSReceiver> ffi_;        // TODO(titzer): Use MaybeHandle
  Handle<JSArrayBuffer> memory_;  // TODO(titzer): Use MaybeHandle
  Handle<JSArrayBuffer> globals_;
  Handle<WasmCompiledModule> compiled_module_;
  std::vector<TableInstance> table_instances_;
  std::vector<Handle<JSFunction>> js_wrappers_;
  JSToWasmWrapperCache js_to_wasm_cache_;
  WeakCallbackInfo<void>::Callback instance_finalizer_callback_;

// Helper routines to print out errors with imports.
#define ERROR_THROWER_WITH_MESSAGE(TYPE)                                      \
  void Report##TYPE(const char* error, uint32_t index,                        \
                    Handle<String> module_name, Handle<String> import_name) { \
    thrower_->TYPE("Import #%d module=\"%.*s\" function=\"%.*s\" error: %s",  \
                   index, module_name->length(),                              \
                   module_name->ToCString().get(), import_name->length(),     \
                   import_name->ToCString().get(), error);                    \
  }                                                                           \
                                                                              \
  MaybeHandle<Object> Report##TYPE(const char* error, uint32_t index,         \
                                   Handle<String> module_name) {              \
    thrower_->TYPE("Import #%d module=\"%.*s\" error: %s", index,             \
                   module_name->length(), module_name->ToCString().get(),     \
                   error);                                                    \
    return MaybeHandle<Object>();                                             \
  }

  ERROR_THROWER_WITH_MESSAGE(LinkError)
  ERROR_THROWER_WITH_MESSAGE(TypeError)

  // Look up an import value in the {ffi_} object.
  MaybeHandle<Object> LookupImport(uint32_t index, Handle<String> module_name,
                                   Handle<String> import_name);

  // Look up an import value in the {ffi_} object specifically for linking an
  // asm.js module. This only performs non-observable lookups, which allows
  // falling back to JavaScript proper (and hence re-executing all lookups) if
  // module instantiation fails.
  MaybeHandle<Object> LookupImportAsm(uint32_t index,
                                      Handle<String> import_name);

  uint32_t EvalUint32InitExpr(const WasmInitExpr& expr);

  // Load data segments into the memory.
  void LoadDataSegments(Address mem_addr, size_t mem_size);

  void WriteGlobalValue(WasmGlobal& global, Handle<Object> value);

  // Process the imports, including functions, tables, globals, and memory, in
  // order, loading them from the {ffi_} object. Returns the number of imported
  // functions.
  int ProcessImports(Handle<FixedArray> code_table,
                     Handle<WasmInstanceObject> instance);

  template <typename T>
  T* GetRawGlobalPtr(WasmGlobal& global);

  // Process initialization of globals.
  void InitGlobals();

  // Allocate memory for a module instance as a new JSArrayBuffer.
  Handle<JSArrayBuffer> AllocateMemory(uint32_t min_mem_pages);

  bool NeedsWrappers();

  // Process the exports, creating wrappers for functions, tables, memories,
  // and globals.
  void ProcessExports(Handle<FixedArray> code_table,
                      Handle<WasmInstanceObject> instance,
                      Handle<WasmCompiledModule> compiled_module);

  void InitializeTables(Handle<WasmInstanceObject> instance,
                        CodeSpecialization* code_specialization);

  void LoadTableSegments(Handle<FixedArray> code_table,
                         Handle<WasmInstanceObject> instance);
};

// Encapsulates all the state and steps of an asynchronous compilation.
// An asynchronous compile job consists of a number of tasks that are executed
// as foreground and background tasks. Any phase that touches the V8 heap or
// allocates on the V8 heap (e.g. creating the module object) must be a
// foreground task. All other tasks (e.g. decoding and validating, the majority
// of the work of compilation) can be background tasks.
// TODO(wasm): factor out common parts of this with the synchronous pipeline.
class AsyncCompileJob {
  // TODO(ahaas): Fix https://bugs.chromium.org/p/v8/issues/detail?id=6263 to
  // make sure that d8 does not shut down before the AsyncCompileJob is
  // finished.
 public:
  explicit AsyncCompileJob(Isolate* isolate, std::unique_ptr<byte[]> bytes_copy,
                           size_t length, Handle<Context> context,
                           Handle<JSPromise> promise);

  void Start();

  ~AsyncCompileJob();

 private:
  Isolate* isolate_;
  std::shared_ptr<Counters> counters_shared_;
  Counters* counters_;
  std::unique_ptr<byte[]> bytes_copy_;
  ModuleWireBytes wire_bytes_;
  Handle<Context> context_;
  Handle<JSPromise> module_promise_;
  std::unique_ptr<ModuleCompiler> compiler_;
  std::unique_ptr<ModuleBytesEnv> module_bytes_env_;

  bool failed_ = false;
  std::vector<DeferredHandles*> deferred_handles_;
  Handle<WasmModuleObject> module_object_;
  Handle<FixedArray> function_tables_;
  Handle<FixedArray> signature_tables_;
  Handle<WasmCompiledModule> compiled_module_;
  Handle<FixedArray> code_table_;
  std::unique_ptr<WasmInstance> temp_instance_ = nullptr;
  size_t outstanding_units_ = 0;
  size_t num_background_tasks_ = 0;

  void ReopenHandlesInDeferredScope();

  void AsyncCompileFailed(ErrorThrower& thrower);

  void AsyncCompileSucceeded(Handle<Object> result);

  template <typename Task, typename... Args>
  void DoSync(Args&&... args);

  template <typename Task, typename... Args>
  void DoAsync(Args&&... args);

  class CompileTask;
  class AsyncCompileTask;
  class SyncCompileTask;
  class DecodeModule;
  class DecodeFail;
  class PrepareAndStartCompile;
  class ExecuteCompilationUnits;
  class WaitForBackgroundTasks;
  class FinishCompilationUnits;
  class FailCompile;
  class FinishCompile;
  class CompileWrappers;
  class FinishModule;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_MODULE_COMPILER_H_
