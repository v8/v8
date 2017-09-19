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

#include "src/wasm/module-decoder.h"
#include "src/wasm/streaming-decoder.h"
#include "src/wasm/wasm-code-specialization.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {
namespace wasm {

// A class compiling an entire module.
class ModuleCompiler {
 public:
  ModuleCompiler(Isolate* isolate, WasmModule* module,
                 Handle<Code> centry_stub);

  // The actual runnable task that performs compilations in the background.
  class CompilationTask : public CancelableTask {
   public:
    ModuleCompiler* compiler_;
    explicit CompilationTask(ModuleCompiler*);

    void RunInternal() override;
  };

  // The CompilationUnitBuilder builds compilation units and stores them in an
  // internal buffer. The buffer is moved into the working queue of the
  // ModuleCompiler when {Commit} is called.
  class CompilationUnitBuilder {
   public:
    explicit CompilationUnitBuilder(ModuleCompiler* compiler)
        : compiler_(compiler) {}

    ~CompilationUnitBuilder() { DCHECK(units_.empty()); }

    void AddUnit(compiler::ModuleEnv* module_env, const WasmFunction* function,
                 uint32_t buffer_offset, Vector<const uint8_t> bytes,
                 WasmName name) {
      units_.emplace_back(new compiler::WasmCompilationUnit(
          compiler_->isolate_, module_env,
          wasm::FunctionBody{function->sig, buffer_offset, bytes.begin(),
                             bytes.end()},
          name, function->func_index, compiler_->centry_stub_,
          compiler_->counters()));
    }

    void Commit() {
      {
        base::LockGuard<base::Mutex> guard(
            &compiler_->compilation_units_mutex_);
        compiler_->compilation_units_.insert(
            compiler_->compilation_units_.end(),
            std::make_move_iterator(units_.begin()),
            std::make_move_iterator(units_.end()));
      }
      units_.clear();
    }

    void Clear() { units_.clear(); }

   private:
    ModuleCompiler* compiler_;
    std::vector<std::unique_ptr<compiler::WasmCompilationUnit>> units_;
  };

  class CodeGenerationSchedule {
   public:
    explicit CodeGenerationSchedule(
        base::RandomNumberGenerator* random_number_generator,
        size_t max_memory = 0);

    void Schedule(std::unique_ptr<compiler::WasmCompilationUnit>&& item);

    bool IsEmpty() const { return schedule_.empty(); }

    std::unique_ptr<compiler::WasmCompilationUnit> GetNext();

    bool CanAcceptWork() const;

    bool ShouldIncreaseWorkload() const;

    void EnableThrottling() { throttle_ = true; }

   private:
    size_t GetRandomIndexInSchedule();

    base::RandomNumberGenerator* random_number_generator_ = nullptr;
    std::vector<std::unique_ptr<compiler::WasmCompilationUnit>> schedule_;
    const size_t max_memory_;
    bool throttle_ = false;
    base::AtomicNumber<size_t> allocated_memory_{0};
  };

  Counters* counters() const { return async_counters_.get(); }

  // Run by each compilation task and by the main thread (i.e. in both
  // foreground and background threads). The no_finisher_callback is called
  // within the result_mutex_ lock when no finishing task is running, i.e. when
  // the finisher_is_running_ flag is not set.
  bool FetchAndExecuteCompilationUnit(
      std::function<void()> no_finisher_callback = nullptr);

  void OnBackgroundTaskStopped();

  void EnableThrottling() { executed_units_.EnableThrottling(); }

  bool CanAcceptWork() const { return executed_units_.CanAcceptWork(); }

  bool ShouldIncreaseWorkload() const {
    return executed_units_.ShouldIncreaseWorkload();
  }

  size_t InitializeCompilationUnits(const std::vector<WasmFunction>& functions,
                                    const ModuleWireBytes& wire_bytes,
                                    compiler::ModuleEnv* module_env);

  void RestartCompilationTasks();

  size_t FinishCompilationUnits(std::vector<Handle<Code>>& results,
                                ErrorThrower* thrower);

  bool IsFinisherRunning() const { return finisher_is_running_; }

  void SetFinisherIsRunning(bool value);

  MaybeHandle<Code> FinishCompilationUnit(ErrorThrower* thrower,
                                          int* func_index);

  void CompileInParallel(const ModuleWireBytes& wire_bytes,
                         compiler::ModuleEnv* module_env,
                         std::vector<Handle<Code>>& results,
                         ErrorThrower* thrower);

  void CompileSequentially(const ModuleWireBytes& wire_bytes,
                           compiler::ModuleEnv* module_env,
                           std::vector<Handle<Code>>& results,
                           ErrorThrower* thrower);

  void ValidateSequentially(const ModuleWireBytes& wire_bytes,
                            compiler::ModuleEnv* module_env,
                            ErrorThrower* thrower);

  static MaybeHandle<WasmModuleObject> CompileToModuleObject(
      Isolate* isolate, ErrorThrower* thrower,
      std::unique_ptr<WasmModule> module, const ModuleWireBytes& wire_bytes,
      Handle<Script> asm_js_script,
      Vector<const byte> asm_js_offset_table_bytes);

 private:
  MaybeHandle<WasmModuleObject> CompileToModuleObjectInternal(
      ErrorThrower* thrower, std::unique_ptr<WasmModule> module,
      const ModuleWireBytes& wire_bytes, Handle<Script> asm_js_script,
      Vector<const byte> asm_js_offset_table_bytes);

  Isolate* isolate_;
  WasmModule* module_;
  const std::shared_ptr<Counters> async_counters_;
  std::vector<std::unique_ptr<compiler::WasmCompilationUnit>>
      compilation_units_;
  base::Mutex compilation_units_mutex_;
  CodeGenerationSchedule executed_units_;
  base::Mutex result_mutex_;
  const size_t num_background_tasks_;
  // This flag should only be set while holding result_mutex_.
  bool finisher_is_running_ = false;
  CancelableTaskManager background_task_manager_;
  size_t stopped_compilation_tasks_ = 0;
  base::Mutex tasks_mutex_;
  Handle<Code> centry_stub_;
};

class JSToWasmWrapperCache {
 public:
  Handle<Code> CloneOrCompileJSToWasmWrapper(Isolate* isolate,
                                             wasm::WasmModule* module,
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

  // A pre-evaluated value to use in import binding.
  struct SanitizedImport {
    Handle<String> module_name;
    Handle<String> import_name;
    Handle<Object> value;
  };

  Isolate* isolate_;
  WasmModule* const module_;
  const std::shared_ptr<Counters> async_counters_;
  ErrorThrower* thrower_;
  Handle<WasmModuleObject> module_object_;
  MaybeHandle<JSReceiver> ffi_;
  MaybeHandle<JSArrayBuffer> memory_;
  Handle<JSArrayBuffer> globals_;
  Handle<WasmCompiledModule> compiled_module_;
  std::vector<TableInstance> table_instances_;
  std::vector<Handle<JSFunction>> js_wrappers_;
  JSToWasmWrapperCache js_to_wasm_cache_;
  WeakCallbackInfo<void>::Callback instance_finalizer_callback_;
  std::vector<SanitizedImport> sanitized_imports_;

  const std::shared_ptr<Counters>& async_counters() const {
    return async_counters_;
  }
  Counters* counters() const { return async_counters().get(); }

// Helper routines to print out errors with imports.
#define ERROR_THROWER_WITH_MESSAGE(TYPE)                                      \
  void Report##TYPE(const char* error, uint32_t index,                        \
                    Handle<String> module_name, Handle<String> import_name) { \
    thrower_->TYPE("Import #%d module=\"%s\" function=\"%s\" error: %s",      \
                   index, module_name->ToCString().get(),                     \
                   import_name->ToCString().get(), error);                    \
  }                                                                           \
                                                                              \
  MaybeHandle<Object> Report##TYPE(const char* error, uint32_t index,         \
                                   Handle<String> module_name) {              \
    thrower_->TYPE("Import #%d module=\"%s\" error: %s", index,               \
                   module_name->ToCString().get(), error);                    \
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

  void SanitizeImports();
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
  Handle<JSArrayBuffer> AllocateMemory(uint32_t num_pages);

  bool NeedsWrappers() const;

  // Process the exports, creating wrappers for functions, tables, memories,
  // and globals.
  void ProcessExports(Handle<WasmInstanceObject> instance,
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
 public:
  explicit AsyncCompileJob(Isolate* isolate, std::unique_ptr<byte[]> bytes_copy,
                           size_t length, Handle<Context> context,
                           Handle<JSPromise> promise);

  void Start();

  std::shared_ptr<StreamingDecoder> CreateStreamingDecoder();

  void Abort();

  ~AsyncCompileJob();

 private:
  class CompileTask;
  class CompileStep;

  // States of the AsyncCompileJob.
  class DecodeModule;
  class DecodeFail;
  class PrepareAndStartCompile;
  class ExecuteAndFinishCompilationUnits;
  class WaitForBackgroundTasks;
  class FinishCompilationUnits;
  class FinishCompile;
  class CompileWrappers;
  class FinishModule;
  class AbortCompilation;

  const std::shared_ptr<Counters>& async_counters() const {
    return async_counters_;
  }
  Counters* counters() const { return async_counters().get(); }

  void AsyncCompileFailed(ErrorThrower& thrower);

  void AsyncCompileSucceeded(Handle<Object> result);

  void StartForegroundTask();

  void StartBackgroundTask();

  void RestartBackgroundTasks();

  // Switches to the compilation step {Step} and starts a foreground task to
  // execute it.
  template <typename Step, typename... Args>
  void DoSync(Args&&... args);

  // Switches to the compilation step {Step} and starts a background task to
  // execute it.
  template <typename Step, typename... Args>
  void DoAsync(Args&&... args);

  // Switches to the compilation step {Step} but does not start a task to
  // execute it.
  template <typename Step, typename... Args>
  void NextStep(Args&&... args);

  Isolate* isolate() { return isolate_; }

  friend class AsyncStreamingProcessor;

  Isolate* isolate_;
  const std::shared_ptr<Counters> async_counters_;
  std::unique_ptr<byte[]> bytes_copy_;
  ModuleWireBytes wire_bytes_;
  Handle<Context> context_;
  Handle<JSPromise> module_promise_;
  std::unique_ptr<ModuleCompiler> compiler_;
  std::unique_ptr<compiler::ModuleEnv> module_env_;
  std::unique_ptr<WasmModule> module_;

  std::vector<DeferredHandles*> deferred_handles_;
  Handle<WasmModuleObject> module_object_;
  Handle<WasmCompiledModule> compiled_module_;
  Handle<FixedArray> code_table_;
  Handle<FixedArray> export_wrappers_;
  size_t outstanding_units_ = 0;
  std::unique_ptr<CompileStep> step_;
  CancelableTaskManager background_task_manager_;
  // The number of background tasks which stopped executing within a step.
  base::AtomicNumber<size_t> stopped_tasks_{0};

  // For async compilation the AsyncCompileJob is the only finisher. For
  // streaming compilation also the AsyncStreamingProcessor has to finish before
  // compilation can be finished.
  base::AtomicNumber<int32_t> outstanding_finishers_{1};

  // Decrements the number of outstanding finishers. The last caller of this
  // function should finish the asynchronous compilation, see the comment on
  // {outstanding_finishers_}.
  V8_WARN_UNUSED_RESULT bool DecrementAndCheckFinisherCount() {
    return outstanding_finishers_.Decrement(1) == 0;
  }

  // Counts the number of pending foreground tasks.
  int32_t num_pending_foreground_tasks_ = 0;

  // The AsyncCompileJob owns the StreamingDecoder because the StreamingDecoder
  // contains data which is needed by the AsyncCompileJob for streaming
  // compilation. The AsyncCompileJob does not actively use the
  // StreamingDecoder.
  std::shared_ptr<StreamingDecoder> stream_;
};

class AsyncStreamingProcessor final : public StreamingProcessor {
 public:
  explicit AsyncStreamingProcessor(AsyncCompileJob* job);

  bool ProcessModuleHeader(Vector<const uint8_t> bytes,
                           uint32_t offset) override;

  bool ProcessSection(SectionCode section_code, Vector<const uint8_t> bytes,
                      uint32_t offset) override;

  bool ProcessCodeSectionHeader(size_t functions_count,
                                uint32_t offset) override;

  bool ProcessFunctionBody(Vector<const uint8_t> bytes,
                           uint32_t offset) override;

  void OnFinishedChunk() override;

  void OnFinishedStream(std::unique_ptr<uint8_t[]> bytes,
                        size_t length) override;

  void OnError(DecodeResult result) override;

  void OnAbort() override;

 private:
  // Finishes the AsyncCOmpileJob with an error.
  void FinishAsyncCompileJobWithError(ResultBase result);

  ModuleDecoder decoder_;
  AsyncCompileJob* job_;
  std::unique_ptr<ModuleCompiler::CompilationUnitBuilder>
      compilation_unit_builder_;
  uint32_t next_function_ = 0;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_MODULE_COMPILER_H_
